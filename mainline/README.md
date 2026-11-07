# Mainline drivers for the CMF Phone 1

This directory holds drivers that are being ported from Nothing's vendor kernel (6.1, with Android
hooks) to mainline Linux, so that the phone can run a current mainline kernel (7.x) later on.

## Contents

| Path | What |
|---|---|
| `drivers/input/misc/rt6010-haptic.c` | RT6010 haptic driver (LRA vibration motor), mainline port |
| `Documentation/devicetree/bindings/input/richtek,rt6010-haptic.yaml` | device-tree binding |
| `dts/tetris-rt6010-haptic.dtsi` | the node for the CMF Phone 1 in a mainline device tree |
| `apply-to-tree.sh` | adds the driver to a kernel tree (sources, binding, Kconfig, Makefile) |
| `tests/check_haptic_config.py` | checks the firmware file `haptic_config.bin` against the driver's rules |

The patch that is applied to a mainline tree is in `patches/mainline/`. It is generated with
`apply-to-tree.sh`, so the two stay in sync.

## What the RT6010 driver does

The driver is a reduced port of the vendor code `drivers/misc/haptic/haptic_drv.c` and `rt6010.c`:

- probe: reset through `enable-gpios`, read the chip id
- EFUSE trim: the same register sequence as the vendor code
- configuration from `haptic_config.bin`: RAM layout, gain, boost voltage, pre-f0, waveform
- vibration: writes a playlist for waveform 0 at a fixed amplitude, starts RAM playback and
  stops it again. Effects arrive through the input core as force-feedback `FF_RUMBLE` effects.
  The input core handles the replay length.

The vendor driver's "activate = 101" interface (LED class `vibrator_nt`, used by `tetris-vibra`)
is replaced by the input force-feedback device. `feedbackd` can use it directly, so `tetris-vibra`
is not needed on a mainline system.

Not ported, because vibration does not need them: debugfs and procfs, BEMF measurements and F0
tracking, the richtap stream interface, the stream and track play modes, the ADC offset readout,
the Android hardware-info registration and the framebuffer notifier.

## Build and check

```sh
# 1. a mainline tree (here v7.3)
git clone --depth 1 -b v7.3 https://github.com/torvalds/linux.git out/mainline-7.3
# 2. add the driver
mainline/apply-to-tree.sh out/mainline-7.3
# 3. build it (arm64, LLVM)
cd out/mainline-7.3
make ARCH=arm64 LLVM=1 defconfig
scripts/config --module INPUT_RT6010_HAPTIC
make ARCH=arm64 LLVM=1 olddefconfig modules_prepare
make ARCH=arm64 LLVM=1 W=1 drivers/input/misc/rt6010-haptic.o
# 4. check the firmware file
python3 mainline/tests/check_haptic_config.py out/vendor-extract/vendor/firmware/haptic_config.bin
```

## Test on the phone (current kernel)

The port runs on the phone's kernel 6.1 (`out/kernel`). It is part of the boot image: `init`
loads `rt6010-haptic.ko` right before the vendor `haptic.ko`, so the port claims the chip at boot
and the vendor driver finds it taken. The motor then appears as a force-feedback input device.

```sh
mainline/test/deploy-and-test.sh --flash  # build the module and boot images, flash slot b, test
mainline/test/deploy-and-test.sh          # test only, on the phone as it is
```

`--flash` rebuilds the module (`build-for-phone.sh`), the boot images (`scripts/build-debug-boot.sh`),
moves the phone into fastboot with the reboot argument `bootloader`, flashes slot b with
`scripts/flash-debug-b.sh` and then runs the test. Slot a (Nothing OS) is not touched. The test
uploads one force-feedback effect to `rt6010-haptic` and plays it three times (300 ms each).

### Why the port is loaded at boot, not swapped in later

Unbinding the vendor driver from `0-005f` at run time resets the phone. This was reproduced with the
vendor driver alone (unbind + bind of `haptic_rt`, no port involved), so the cause is the unbind, not
the port. The boot-time claim avoids the unbind entirely.

### Back to the vendor driver

Remove the module and rebuild the boot images. Without `out/rt6010-ko/rt6010-haptic.ko` the init script
falls through to the vendor `haptic.ko`:

```sh
rm -rf out/rt6010-ko && scripts/build-debug-boot.sh && scripts/flash-debug-b.sh
```

## Status

- Builds for mainline 7.3 (arm64, `W=1`) and for the phone's kernel 6.1.162 without warnings in the
  driver file. Both builds use the same source.
- The `gpio-en` name and `ics,haptic_rt` compatible of the vendor device tree are accepted by kernels
  before 6.3 only, because `devm_gpiod_get_from_of_node()` is gone from 6.3 on.
- On the phone: `rt6010-haptic 0-005f: RT6010 chip 0x6b ready`, the input device is created, the test
  effect is accepted and played. **Whether the motor physically vibrates has to be felt on the phone.**
- Known deviations from the vendor code: the IRQ pin is not driven during reset, and the ADC offset
  readout is left out.
- Not verified: the phone stays stable. A separate battery-gauge oops (`mt6375_battery`, NULL pointer in
  `__wake_up_common` from `coulomb_irq`) is in the pstore log of an earlier boot. It is not in the
  haptic code path, but it can reset the phone at any time and is not fixed here.
- The directory is for this repository only. Nothing here is meant for the mainline kernel.
