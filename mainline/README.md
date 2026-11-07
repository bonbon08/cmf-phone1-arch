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

## Status

- The driver compiles against mainline 7.3 for arm64 with `W=1` without warnings in its own file.
- `tests/check_haptic_config.py` accepts the firmware file of the B4.1 build.
- **Not tested on hardware.** The register sequence was taken from the vendor code. Whether the
  phone vibrates with the mainline driver has to be checked on the device (`fftest`, or `evtest`
  on the new input device).
- Known deviations from the vendor code: the IRQ pin is not driven during reset, and the ADC offset
  readout is left out.
