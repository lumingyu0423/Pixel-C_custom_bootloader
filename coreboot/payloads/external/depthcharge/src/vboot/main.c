
/*
 * Copyright 2012 Google Inc.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <libpayload.h>
#include <vboot_struct.h>
#include <vboot_api.h>
#include <vboot_nvstorage.h>

#include "arch/sign_of_life.h"
#include "base/init_funcs.h"
#include "base/timestamp.h"
#include "boot/bcb.h"
#include "config.h"
#include "debug/cli/common.h"
#include "drivers/input/input.h"
#include "vboot/fastboot.h"
#include "vboot/stages.h"
#include "vboot/util/commonparams.h"
#include "vboot/util/flag.h"
#include "vboot/util/vboot_handoff.h"
#include "vboot/vbnv.h"

static int vboot_init_handoff()
{
	struct vboot_handoff *vboot_handoff;

	// Set up the common param structure, not clearing shared data.
	if (common_params_init(0))
		return 1;

	if (lib_sysinfo.vboot_handoff == NULL) {
		printf("vboot handoff pointer is NULL\n");
		return 1;
	}

	if (lib_sysinfo.vboot_handoff_size != sizeof(struct vboot_handoff)) {
		printf("Unexpected vboot handoff size: %d\n",
		       lib_sysinfo.vboot_handoff_size);
		return 1;
	}

	vboot_handoff = lib_sysinfo.vboot_handoff;

	/* If the lid is closed, don't count down the boot
	 * tries for updates, since the OS will shut down
	 * before it can register success.
	 *
	 * VbInit was already called in coreboot, so we need
	 * to update the vboot internal flags ourself.
	 */
	int lid_switch = flag_fetch(FLAG_LIDSW);
	if (!lid_switch) {
		VbSharedDataHeader *vdat;
		int vdat_size;

		if (find_common_params((void **)&vdat, &vdat_size) != 0)
			vdat = NULL;

		/* We need something to work with */
		if (vdat != NULL)
			/* Tell kernel selection to not count down */
			vdat->flags |= VBSD_NOFAIL_BOOT;
	}

	uint32_t out_flags = vboot_handoff->init_params.out_flags;

	printf("Bootblock out_flags = 0x%08X\n", out_flags);

	/*
	 * The bootblock (verstage) is signature-verified and cannot be
	 * modified.  It always sets ENABLE_RECOVERY because our custom
	 * RW firmware fails Google's key verification (recovery_reason
	 * = VBNV_RECOVERY_RO_INVALID_RW = 0x03).
	 *
	 * Only override the recovery flag when the reason is
	 * RO_INVALID_RW (0x03) — that is the expected "false alarm"
	 * from the unmodifiable bootblock.  All other recovery reasons
	 * (e.g. user-requested fastboot 0xC3, FW fastboot 0x5E,
	 * manual recovery, etc.) must be honoured so that fastboot
	 * and recovery remain accessible.
	 */
	if (out_flags & VB_INIT_OUT_ENABLE_RECOVERY) {
		VbSharedDataHeader *vdat_tmp;
		int vdat_tmp_size;
		uint8_t reason = 0xFF;

		if (find_common_params((void **)&vdat_tmp,
				       &vdat_tmp_size) == 0 && vdat_tmp)
			reason = vdat_tmp->recovery_reason;

		printf("Recovery in out_flags, reason = 0x%02X\n", reason);

		if (reason == VBNV_RECOVERY_RO_INVALID_RW) {
			printf("Overriding: RO_INVALID_RW is expected on "
			       "custom FW, clearing ENABLE_RECOVERY\n");
			out_flags &= ~VB_INIT_OUT_ENABLE_RECOVERY;
			out_flags |= VB_INIT_OUT_ENABLE_DEVELOPER |
				     VB_INIT_OUT_ENABLE_DISPLAY;
		}
	}

	return vboot_do_init_out_flags(out_flags);
}

int main(void)
{
	// Let the world know we're alive.
	sign_of_life(0xaa);

	// Initialize some consoles.
	serial_console_init();
	cbmem_console_init();
	input_init();

	printf("\n\nStarting depthcharge on " CONFIG_BOARD "...\n");

	// Set up time keeping.
	timestamp_init();

	// Run any generic initialization functions that are compiled in.
	if (run_init_funcs())
		halt();

	/*
	 * Ensure fastboot capabilities are always enabled.
	 * The bootblock (verstage) may have cleared these flags if the
	 * device was not in developer mode. Since bootblock code cannot
	 * be modified (signature-verified), we re-enable them here.
	 */
	if (!vbnv_read(VBNV_DEV_BOOT_FASTBOOT_FULL_CAP)) {
		printf("Enabling VBNV_DEV_BOOT_FASTBOOT_FULL_CAP\n");
		vbnv_write(VBNV_DEV_BOOT_FASTBOOT_FULL_CAP, 1);
	}
	if (!vbnv_read(VBNV_FASTBOOT_UNLOCK_IN_FW)) {
		printf("Enabling VBNV_FASTBOOT_UNLOCK_IN_FW\n");
		vbnv_write(VBNV_FASTBOOT_UNLOCK_IN_FW, 1);
	}

	timestamp_add_now(TS_RO_VB_INIT);

	if (CONFIG_CLI)
		console_loop();

	// Set up the common param structure, not clearing shared data.
	if (vboot_init_handoff())
		halt();

	VbSharedDataHeader *vdat;
	int vdat_size;

	if (find_common_params((void **)&vdat, &vdat_size) != 0)
		vdat = NULL;

	if (vdat != NULL) {
		printf("recovery_reason = 0x%X (%u)\n",
		       vdat->recovery_reason, vdat->recovery_reason);
		printf("vboot_in_recovery() = 0x%X, vboot_in_developer() = 0x%X\n",
		       vboot_in_recovery(), vboot_in_developer());
		/*
		 * Only clear recovery_reason when it is the expected
		 * RO_INVALID_RW (0x03) from bootblock signature check.
		 * Other reasons (fastboot, manual recovery, etc.) must
		 * be preserved so vboot_try_fastboot() /
		 * is_fastboot_mode_requested() can read them.
		 */
		if (vdat->recovery_reason == VBNV_RECOVERY_RO_INVALID_RW) {
			printf("Clearing RO_INVALID_RW recovery_reason\n");
			vdat->recovery_reason = VBNV_RECOVERY_NOT_REQUESTED;
		}
	}

	/* Fastboot is only entered in recovery path */
	if (vboot_in_recovery())
		vboot_try_fastboot();

	/* Handle BCB command, if supported. */
	if (CONFIG_BCB_SUPPORT)
		bcb_handle_command();

	/*
	 * Show splash screen in both normal and developer modes.
	 * Previously the device was always in recovery (due to
	 * RO_INVALID_RW), so the fastboot menu would initialize the
	 * display.  Now that we override recovery → developer, we
	 * need to explicitly show the splash here so the screen is
	 * not black during EC sync and kernel loading.
	 */
	if (vboot_in_normal() || vboot_in_developer())
		if (vboot_draw_screen(VB_SCREEN_SPLASH, 0, 1))
			printf("Failed to draw splash screen\n");

	timestamp_add_now(TS_VB_SELECT_AND_LOAD_KERNEL);

	// Select a kernel and boot it.
	if (vboot_select_and_load_kernel())
		halt();

	// We should never get here.
	printf("Got to the end!\n");
	halt();
	return 0;
}
