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

#include <assert.h>
#include <libpayload.h>
#include <vboot_api.h>

#include <cbfs.h>

#include "drivers/ec/cros/ec.h"
#include "drivers/flash/flash.h"
#include "image/fmap.h"
#include "image/index.h"
#include "vboot/util/flag.h"

/* SHA-256 from vboot cryptolib (linked via vboot_fw.a) */
#define SHA256_DIGEST_SIZE 32
uint8_t *internal_SHA256(const uint8_t *data, uint64_t len, uint8_t *digest);

int VbExTrustEC(int devidx)
{
	int val;

	if (devidx != 0)
		return 0;

	val = flag_fetch(FLAG_ECINRW);
	if (val < 0) {
		printf("Couldn't tell if the EC is running RW firmware.\n");
		return 0;
	}
	// Trust the EC if it's NOT in its RW firmware.
	return !val;
}

VbError_t VbExEcRunningRW(int devidx, int *in_rw)
{
	enum ec_current_image image;

	if (cros_ec_read_current_image(devidx, &image) < 0) {
		printf("Failed to read current EC image.\n");
		return VBERROR_UNKNOWN;
	}
	switch (image) {
	case EC_IMAGE_RO:
		*in_rw = 0;
		break;
	case EC_IMAGE_RW:
		*in_rw = 1;
		break;
	default:
		printf("Unrecognized EC image type %d.\n", image);
		return VBERROR_UNKNOWN;
	}

	return VBERROR_SUCCESS;
}

VbError_t VbExEcJumpToRW(int devidx)
{
	if (cros_ec_reboot(devidx, EC_REBOOT_JUMP_RW, 0) < 0) {
		printf("Failed to make the EC jump to RW.\n");
		return VBERROR_UNKNOWN;
	}

	return VBERROR_SUCCESS;
}

VbError_t VbExEcDisableJump(int devidx)
{
	if (cros_ec_reboot(devidx, EC_REBOOT_DISABLE_JUMP, 0) < 0) {
		printf("Failed to make the EC disable jumping.\n");
		return VBERROR_UNKNOWN;
	}

	return VBERROR_SUCCESS;
}

VbError_t VbExEcHashRW(int devidx, const uint8_t **hash, int *hash_size)
{
	static struct ec_response_vboot_hash resp;

	if (cros_ec_read_hash(devidx, &resp) < 0) {
		printf("Failed to read EC hash.\n");
		return VBERROR_UNKNOWN;
	}

	/*
	 * TODO (rspangler@chromium.org): the code below isn't very tolerant
	 * of errors.
	 *
	 * If the EC is busy calculating a hash, we should wait and retry
	 * reading the hash status.
	 *
	 * If the hash is unavailable, the wrong type, or covers the wrong
	 * offset/size (which we need to get from the FDT, since it's
	 * board-specific), we should request a new hash and wait for it to
	 * finish.  Also need a flag to force it to rehash, which we'll use
	 * after doing a firmware update.
	 */
	if (resp.status != EC_VBOOT_HASH_STATUS_DONE) {
		printf("EC hash wasn't finished.\n");
		return VBERROR_UNKNOWN;
	}
	if (resp.hash_type != EC_VBOOT_HASH_TYPE_SHA256) {
		printf("EC hash was the wrong type.\n");
		return VBERROR_UNKNOWN;
	}

	*hash = resp.hash_digest;
	*hash_size = resp.digest_size;

	return VBERROR_SUCCESS;
}

VbError_t VbExEcGetExpectedRW(int devidx, enum VbSelectFirmware_t select,
			      const uint8_t **image, int *image_size)
{
	const char *name;

	switch (select) {
	case VB_SELECT_FIRMWARE_A:
		name = (devidx == 0 ? "EC_MAIN_A" : "PD_MAIN_A");
		break;
	case VB_SELECT_FIRMWARE_B:
		name = (devidx == 0 ? "EC_MAIN_A" : "PD_MAIN_A");
		break;
	default:
		printf("Unrecognized EC firmware requested.\n");
		return VBERROR_UNKNOWN;
	}

	struct cbfs_file *file = cbfs_get_file(CBFS_DEFAULT_MEDIA, name);
	if (file == NULL) {
		printf("Didn't find EC firmware '%s' in CBFS.\n", name);
		return VBERROR_UNKNOWN;
	}

	*image_size = ntohl(file->len);
	*image = cbfs_get_file_content(CBFS_DEFAULT_MEDIA, name,
				       CBFS_TYPE_RAW);
	if (!*image) {
		printf("Failed to load EC firmware '%s' from CBFS.\n", name);
		return VBERROR_UNKNOWN;
	}

	printf("EC-RW firmware address, size are %p, %d.\n",
		*image, *image_size);

	return VBERROR_SUCCESS;
}

static VbError_t ec_protect_rw(int devidx, int protect)
{
	struct ec_response_flash_protect resp;
	uint32_t mask = EC_FLASH_PROTECT_ALL_NOW | EC_FLASH_PROTECT_ALL_AT_BOOT;

	/* Update protection */
	if (cros_ec_flash_protect(devidx, mask,
				  protect ? mask : 0, &resp) < 0) {
		printf("Failed to update EC flash protection.\n");
		return VBERROR_UNKNOWN;
	}

	if (!protect) {
		/* If protection is still enabled, need reboot */
		if (resp.flags & EC_FLASH_PROTECT_ALL_NOW)
			return VBERROR_EC_REBOOT_TO_RO_REQUIRED;

		return VBERROR_SUCCESS;
	}

	/*
	 * If write protect and ro-at-boot aren't both asserted, don't expect
	 * protection enabled.
	 */
	if ((~resp.flags) & (EC_FLASH_PROTECT_GPIO_ASSERTED |
			     EC_FLASH_PROTECT_RO_AT_BOOT))
		return VBERROR_SUCCESS;

	/* If flash is protected now, success */
	if (resp.flags & EC_FLASH_PROTECT_ALL_NOW)
		return VBERROR_SUCCESS;

	/* If RW will be protected at boot but not now, need a reboot */
	if (resp.flags & EC_FLASH_PROTECT_ALL_AT_BOOT)
		return VBERROR_EC_REBOOT_TO_RO_REQUIRED;

	/* Otherwise, it's an error */
	return VBERROR_UNKNOWN;
}

VbError_t VbExEcGetExpectedRWHash(int devidx, enum VbSelectFirmware_t select,
				  const uint8_t **hash, int *hash_size)
{
	const char *name;

	switch (select) {
	case VB_SELECT_FIRMWARE_A:
		name = "EC_MAIN_A_HASH";
		break;
	case VB_SELECT_FIRMWARE_B:
		name = "EC_MAIN_A_HASH";
		break;
	default:
		printf("Unrecognized EC hash requested.\n");
		return VBERROR_UNKNOWN;
	}

	struct cbfs_file *file = cbfs_get_file(CBFS_DEFAULT_MEDIA, name);
	if (file == NULL) {
		printf("Didn't find EC hash '%s' in CBFS.\n", name);
		return VBERROR_UNKNOWN;
	}

	*hash_size = ntohl(file->len);
	*hash = cbfs_get_file_content(CBFS_DEFAULT_MEDIA, name,
				      CBFS_TYPE_RAW);
	if (!*hash) {
		printf("Failed to load EC hash '%s' from CBFS.\n", name);
		return VBERROR_UNKNOWN;
	}

	printf("EC-RW hash address, size are %p, %d.\n",
		*hash, *hash_size);

	printf("Hash = ");
	for (int i = 0; i < *hash_size; i++)
		printf("%02x", (*hash)[i]);
	printf("\n");

	return VBERROR_SUCCESS;
}

VbError_t VbExEcUpdateRW(int devidx, const uint8_t *image, int image_size)
{
	int rv;

	rv = ec_protect_rw(devidx, 0);
	if (rv == VBERROR_EC_REBOOT_TO_RO_REQUIRED || rv != VBERROR_SUCCESS)
		return rv;

	if (cros_ec_flash_update_rw(devidx, image, image_size)) {
		printf("Failed to update EC RW flash.\n");
		return VBERROR_UNKNOWN;
	}

	return VBERROR_SUCCESS;
}

VbError_t VbExEcProtectRW(int devidx)
{
	return ec_protect_rw(devidx, 1);
}


/*
 * VbExEcUpdateRO() - Update EC RO firmware from CBFS.
 *
 * EC may not have CONFIG_CMD_HASH enabled, so we cannot rely on
 * cros_ec_read_hash(). Instead we read back the RO flash region
 * via cros_ec_flash_read() and compute SHA-256 on the AP side.
 *
 * Flow:
 *   1. Load expected RO image & hash from CBFS.
 *   2. Verify CBFS image integrity (compute SHA-256, compare with stored hash).
 *   3. Jump EC to RW; read back current RO flash; compute SHA-256.
 *      Skip update if hash matches (already up-to-date).
 *   4. Check flash write-protection status; abort if RO is protected.
 *   5. Check image size against EC RO region capacity.
 *   6. Erase + write the new RO image.
 *   7. Read back EC RO flash again; compute SHA-256 and verify write.
 */
VbError_t VbExEcUpdateRO(int devidx)
{
	enum ec_current_image cur_image;
	struct ec_response_flash_protect prot_resp;
	const uint8_t *cbfs_image = NULL;
	const uint8_t *cbfs_hash = NULL;
	int image_size = 0;
	int hash_size = 0;
	uint32_t ro_offset, ro_size;
	int i;

	printf("=== EC RO Update Start ===\n");

	/* Step 1: Load expected RO image and hash from CBFS */
	{
		struct cbfs_file *file;

		file = cbfs_get_file(CBFS_DEFAULT_MEDIA, "EC_RO");
		if (!file) {
			printf("EC RO Update: EC_RO not found in CBFS.\n");
			return VBERROR_UNKNOWN;
		}
		image_size = ntohl(file->len);
		cbfs_image = cbfs_get_file_content(CBFS_DEFAULT_MEDIA, "EC_RO",
						   CBFS_TYPE_RAW);
		if (!cbfs_image) {
			printf("EC RO Update: Failed to load EC_RO from CBFS.\n");
			return VBERROR_UNKNOWN;
		}
		printf("EC RO Update: EC_RO loaded from CBFS, size = %d\n",
		       image_size);

		file = cbfs_get_file(CBFS_DEFAULT_MEDIA, "EC_RO_HASH");
		if (!file) {
			printf("EC RO Update: EC_RO_HASH not found in CBFS.\n");
			return VBERROR_UNKNOWN;
		}
		hash_size = ntohl(file->len);
		cbfs_hash = cbfs_get_file_content(CBFS_DEFAULT_MEDIA,
						  "EC_RO_HASH",
						  CBFS_TYPE_RAW);
		if (!cbfs_hash) {
			printf("EC RO Update: Failed to load EC_RO_HASH from CBFS.\n");
			return VBERROR_UNKNOWN;
		}
		if (hash_size < SHA256_DIGEST_SIZE) {
			printf("EC RO Update: EC_RO_HASH too small (%d < %d).\n",
			       hash_size, SHA256_DIGEST_SIZE);
			return VBERROR_UNKNOWN;
		}
		printf("EC RO Update: Expected RO hash = ");
		for (i = 0; i < SHA256_DIGEST_SIZE; i++)
			printf("%02x", cbfs_hash[i]);
		printf("\n");
	}

	/* Step 2: Verify CBFS image integrity (SHA-256) */
	{
		uint8_t digest[SHA256_DIGEST_SIZE];

		internal_SHA256(cbfs_image, (uint64_t)image_size, digest);

		printf("EC RO Update: CBFS image SHA-256 = ");
		for (i = 0; i < SHA256_DIGEST_SIZE; i++)
			printf("%02x", digest[i]);
		printf("\n");

		if (memcmp(digest, cbfs_hash, SHA256_DIGEST_SIZE) != 0) {
			printf("EC RO Update: CBFS image hash mismatch! "
			       "Image may be corrupted. Aborting.\n");
			return VBERROR_UNKNOWN;
		}
		printf("EC RO Update: CBFS image integrity verified.\n");
	}

	/*
	 * Step 3: Ensure EC is running RW, then read back RO flash to
	 * check if update is needed.
	 *
	 * EC may not have CONFIG_CMD_HASH enabled, so we cannot rely on
	 * cros_ec_read_hash(). Instead, jump to RW (needed anyway for
	 * flash erase/write), read back the RO region, compute SHA-256,
	 * and compare with the CBFS hash.
	 */
	if (cros_ec_read_current_image(devidx, &cur_image) < 0) {
		printf("EC RO Update: Failed to read current EC image.\n");
		return VBERROR_UNKNOWN;
	}
	if (cur_image != EC_IMAGE_RW) {
		printf("EC RO Update: Jumping EC to RW...\n");
		if (cros_ec_reboot(devidx, EC_REBOOT_JUMP_RW, 0) < 0) {
			printf("EC RO Update: Failed to jump EC to RW.\n");
			return VBERROR_UNKNOWN;
		}
		mdelay(500);
		if (cros_ec_read_current_image(devidx, &cur_image) < 0 ||
		    cur_image != EC_IMAGE_RW) {
			printf("EC RO Update: EC still not in RW after jump.\n");
			return VBERROR_UNKNOWN;
		}
	}
	printf("EC RO Update: EC now running RW.\n");

	/* Get RO region info (needed for read-back and size check) */
	if (cros_ec_flash_offset(devidx, EC_FLASH_REGION_RO,
				 &ro_offset, &ro_size)) {
		printf("EC RO Update: Failed to get EC RO region info.\n");
		return VBERROR_UNKNOWN;
	}
	printf("EC RO Update: EC RO region offset=0x%x size=%u\n",
	       ro_offset, ro_size);

	/* Read back current RO flash and compute SHA-256 */
	{
		uint8_t *readback = malloc(image_size);
		if (!readback) {
			printf("EC RO Update: malloc(%d) failed.\n",
			       image_size);
			return VBERROR_UNKNOWN;
		}

		if (cros_ec_flash_read(devidx, readback,
				       ro_offset, image_size)) {
			printf("EC RO Update: Failed to read EC RO flash.\n");
			free(readback);
			return VBERROR_UNKNOWN;
		}

		uint8_t cur_digest[SHA256_DIGEST_SIZE];
		internal_SHA256(readback, (uint64_t)image_size, cur_digest);
		free(readback);

		printf("EC RO Update: Current EC RO SHA-256 = ");
		for (i = 0; i < SHA256_DIGEST_SIZE; i++)
			printf("%02x", cur_digest[i]);
		printf("\n");

		if (memcmp(cur_digest, cbfs_hash, SHA256_DIGEST_SIZE) == 0) {
			printf("EC RO Update: EC RO is already up-to-date. "
			       "Skipping update.\n");
			return VBERROR_SUCCESS;
		}
		printf("EC RO Update: Hash mismatch, update needed.\n");
	}

	/* Step 4: Check flash write-protection status */
	if (cros_ec_flash_protect(devidx, 0, 0, &prot_resp) < 0) {
		printf("EC RO Update: Failed to read flash protection.\n");
		return VBERROR_UNKNOWN;
	}
	printf("EC RO Update: Flash protect flags = 0x%08x\n", prot_resp.flags);
	if (prot_resp.flags & EC_FLASH_PROTECT_RO_NOW) {
		printf("EC RO Update: RO is write-protected! Aborting.\n");
		return VBERROR_UNKNOWN;
	}

	/* Step 5: Check image size against EC RO region */
	printf("EC RO Update: image size=%d, RO region size=%u\n",
	       image_size, ro_size);
	if ((uint32_t)image_size > ro_size) {
		printf("EC RO Update: Image too large for EC RO region!\n");
		return VBERROR_UNKNOWN;
	}

	/* Step 6: Erase and write */
	printf("EC RO Update: Erasing and writing EC RO flash...\n");
	if (cros_ec_flash_update_ro(devidx, cbfs_image, image_size)) {
		printf("EC RO Update: Flash write FAILED!\n");
		return VBERROR_UNKNOWN;
	}
	printf("EC RO Update: Flash write completed.\n");

	/*
	 * Step 7: Read back the written RO region and verify SHA-256.
	 *
	 * We can read the RO flash area from RW mode via
	 * cros_ec_flash_read(), then compute SHA-256 of the read-back
	 * data (only image_size bytes) and compare with the CBFS hash.
	 */
	printf("EC RO Update: Reading back EC RO flash for verification...\n");
	{
		uint8_t *readback = malloc(image_size);
		if (!readback) {
			printf("EC RO Update: WARNING - malloc(%d) failed, "
			       "cannot verify.\n", image_size);
			goto done;
		}

		if (cros_ec_flash_read(devidx, readback,
				       ro_offset, image_size)) {
			printf("EC RO Update: WARNING - Failed to read back "
			       "EC RO flash.\n");
			free(readback);
			goto done;
		}

		uint8_t verify_digest[SHA256_DIGEST_SIZE];
		internal_SHA256(readback, (uint64_t)image_size, verify_digest);
		free(readback);

		printf("EC RO Update: Read-back SHA-256 = ");
		for (i = 0; i < SHA256_DIGEST_SIZE; i++)
			printf("%02x", verify_digest[i]);
		printf("\n");

		if (memcmp(verify_digest, cbfs_hash,
			   SHA256_DIGEST_SIZE) == 0) {
			printf("EC RO Update: Verification PASSED.\n");
		} else {
			printf("EC RO Update: Verification FAILED! "
			       "Read-back hash mismatch.\n");
			return VBERROR_UNKNOWN;
		}
	}

done:
	printf("=== EC RO Update Complete ===\n");
	return VBERROR_SUCCESS;
}

VbError_t VbExEcEnteringMode(int devidx, enum VbEcBootMode_t mode)
{
	switch(mode) {
	case VB_EC_RECOVERY:
		return cros_ec_entering_mode(devidx, VBOOT_MODE_RECOVERY);
	case VB_EC_DEVELOPER:
		return cros_ec_entering_mode(devidx, VBOOT_MODE_DEVELOPER);
	case VB_EC_NORMAL:
	default :
		return cros_ec_entering_mode(devidx, VBOOT_MODE_NORMAL);
	}
}
