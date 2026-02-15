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

At the moment, you must **disassemble the device** to access the debug interface because **CCD is not enabled yet**.

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

