# Information

## Warning (Read This First)

Flashing a custom bootloader is a **high-risk** operation. It has not been fully validated and **may permanently brick your device**. Please make sure you fully understand the risks and have the ability to recover/unbrick the device before flashing and testing this bootloader. **This project is not responsible** for any devices that become bricked after flashing.

## Background Notes

The Pixel C was originally a ChromeOS device based on the Tegra K1 (Denver). Later it was transitioned to a Tegra X1 device running Android. Even though it runs Android, it retains coreboot as its bootloader (from its ChromeOS heritage). As a result, the Pixel C bootloader has some ChromeOS-specific characteristics, and it even includes CCD hardware/features that are present but not fully enabled.

## ⚠️ Warning (SPI Flash Region)

Never attempt to modify the SPI flash region at `0x0-0x19000`. Code in this range is verified using a Google PKC public key signature！

Never attempt to modify the SPI flash region at `0x0-0x19000`. Code in this range is verified using a Google PKC public key signature！

Never attempt to modify the SPI flash region at `0x0-0x19000`. Code in this range is verified using a Google PKC public key signature！

## How to Debug

- [SuzyQ documentation](https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/main/docs/ccd.md#suzyq-suzyqable)

~~At the moment, you must **disassemble the device** to access the debug interface because **CCD is not enabled yet**.~~

After investigation, **CCD(Suzy-Q) is not usable during the EC_RO stage** (From factory firmware, EC-RO never updates, at least on my device; version: `RO, ryu_v1.8.205-f72390a 2015-10-07 09:31:14 @build169-m2`). This is a potential risk/unknown. The good news is that the **EC does not enforce firmware signature verification**, so it is possible to run a **custom EC firmware** ~~(tested; support will be added later)~~ (Now EC-RO firmware can be updated through the fastboot menu).

In the **EC_RW stage**, CCD(Suzy-Q) can enumerate a device over USB (tested using an EC_RW image extracted from `bootloader-dragon-google_smaug.7900.139.0`).

Also, the **WP_L pin is pulled high by default in hardware**, which means the EC is **unlocked by default**.

Read firmware from spiflash.

    $ sudo flashrom -p raiden_debug_spi -r backupfirmware.rom

The debug interface is a **50-pin header** located next to the mainboard **USB Type-C** connector. For connector/pinout background, see:

- [Servo Micro (uServo) documentation](https://chromium.googlesource.com/chromiumos/third_party/hdctools/+/main/docs/servo_micro.md)

### SPI flash (servo header)

- **1**: GND
- **2**: CLK
- **3**: CS
- **4**: MOSI
- **5**: MISO
- **VCC**: connect to the SPI flash chip’s own VCC

### AP UART  (servo header)

- **RX**: pin **17**
- **TX**: pin **16**

# HOW TO MAKE
## Source URL
    coreboot -> https://chromium.googlesource.com/chromiumos/third_party/coreboot/+/refs/heads/firmware-smaug-7900.B

    depthcharge -> https://chromium.googlesource.com/chromiumos/platform/depthcharge/+/refs/heads/firmware-smaug-7900.B

    arm-trusted-firmware -> https://chromium.googlesource.com/chromiumos/third_party/arm-trusted-firmware/+/refs/heads/firmware-smaug-7900.B

    vboot_reference -> https://chromium.googlesource.com/chromiumos/platform/vboot_reference/+/refs/heads/firmware-smaug-7900.B

    ec -> https://chromium.googlesource.com/chromiumos/platform/ec/+/refs/heads/firmware-smaug-7900.B


## install Tegra BCT and bootable flash image generator/compiler
    $ sudo apt install cbootimage

## download toolchain
    $ cd coreboot/util/cbfstool/
    $ make 

## build libpayload
    $ cd coreboot/payloads/libpayload/
    $ make defconfig
    $ make

## build cbfstool
    $ cd coreboot/util/cbfstool/
    $ make cbfstool

## build payload depthcharge
    $ cd coreboot/payloads/external/depthcharge
    $ export BOARD=smaug
    $ make defconfig
    $ make depthcharge_unified

## build ec (Allow enable CCD.) 
    $ cd ec/
    $ export CROSS_COMPILE=~/gcc-arm-none-eabi-6-2017-q2-update/bin/arm-none-eabi-
    $ export BOARD=ryu
    $ make

## build coreboot
    $ cd coreboot/
    $ make smaug_defconfig
    $ make

- output -> only-ro_test_smaug.7132.295.0.bin

