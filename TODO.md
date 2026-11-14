# Open work

Handoff list for the next session (state of 2026-11-14). The README's roadmap tables say *what*
works; this file says *what to do next* and where to start. Keep it current: strike what is done,
add what you find.

## Waiting for a test by the owner (needs hands on the phone)

| What | How to test | Code |
|---|---|---|
| USB keyboard over an OTG adapter | plug it in; `cat /sys/class/usb_role/*/role` should say `host`, `journalctl -b \| grep tetris-usb-role` shows "host (Type-C stack)" | `rootfs/overlay/usr/local/bin/tetris-usb-role` |
| Double-tap to wake (FT3519 port) | `sudo python3 mainline/test/gesture-test.py` on the phone, then double-tap the dark screen; touch must work after waking | `mainline/drivers/input/touchscreen/focaltech-ft3519.c` |
| Torch tile in the control centre | Nothing shell → swipe down → "Taschenlampe" | Nothing_Wayland `shell/nothing_de/control.py` |
| Clock in the status bar | correct (CET) after the next reboot; `/etc/localtime` was linked by hand on 14 Nov | `scripts/build-rootfs.sh` (TIMEZONE) |

## Move to mainline 7.x: drivers

Status values as in the README. All ports live in `mainline/`, build for the phone kernel with
`mainline/test/build-for-phone.sh` (6.1) and for 7.3 as external modules in `out/mainline-7.3`
(`make ARCH=arm64 LLVM=1 W=1 M=<dir> modules`; modpost errors there only mean no Module.symvers).

### Next: MT6375 fuel gauge and ADC

Core (`mainline/drivers/mfd/mt6375.c`), Type-C (`tcpci_mt6375.c`) and charger
(`mt6375-charger.c`) are written and build; none has run (see "Not run yet" below).

1. **ADC first** (smaller, the charger and gauge want it): vendor
   `src/kernel_device_modules-6.1/drivers/iio/adc/mt6375-adc.c` (check the path with
   `grep -rl mediatek,mt6375-adc src/kernel_device_modules-6.1`). Mainline template:
   `drivers/iio/adc/mt6370-adc.c`. Channels used by the vendor charger (DT `io-channels`):
   CHGVINDIV5, VSYS, VBAT, IBUS, IBAT, TEMP_JC, USBDP, USBDM. ADC done IRQ is 44 (`adc_donei`).
   Add an MFD cell (`mediatek,mt6375-adc` is already a cell) and `#io-channel-cells` to the binding.
2. **Gauge**: vendor `drivers/power/nt_supply/mt6375-gauge.c` (hardware) plus
   `mtk_battery*.c` (algorithm, ~1800 lines in `mtk_battery_algo.c`). Mainline has nothing alike
   for MediaTek. Plan: a plain power_supply "battery" with voltage/current/temperature from the
   gauge registers and coulomb counting for the level; decide with the owner whether to port the
   vendor algorithm or write a simple one. Lessons from the vendor kernel (patch 0005): the level
   must re-sync at end of charge (VBAT >= 4.40 V, |I| < 150 mA for 3 min), or an error in the
   saved level stays forever. Gauge IRQs: 104 `gm30_evt` (the vendor handles it in the hard IRQ),
   names in the vendor DT node `mtk-gauge`.
3. Charger follow-ups: ADC readings (VBUS, IBUS) in the power supply once the ADC exists; the
   SC8541 charge pump (I2C11 0x6a, vendor `sc8541_charger.c`) for fast charging and PD above 5 V
   (then add higher sink PDOs in `mainline/dts/tetris-mt6375.dtsi`).

### Not run yet (built only)

These need a 7.x kernel on the phone; on the vendor kernel the MT6375 core would take the chip
from the vendor charger and gauge (charging and battery level would stop).

- `mainline/drivers/mfd/mt6375.c`, `mainline/drivers/usb/typec/tcpm/tcpci_mt6375.c`,
  `mainline/drivers/power/supply/mt6375-charger.c` (checked read-only on the phone through
  i2c-dev: ids, RT2 bank, IRQ masks, every charger field decodes to the vendor driver's values)
- HL5280 audio switch = mainline `fsa4480` (`mainline/dts/tetris-hl5280-audioswitch.dtsi`)
- AW88261 speaker amp = mainline `aw88261` with the vendor `aw882xx_acf.bin`
  (`mainline/dts/tetris-aw88261-speaker.dtsi`, checker `mainline/tests/check_aw88261_acf.py`)

### Done on the vendor kernel, built for 7.3

RT6010 haptics, FT3519 touch (incl. FOD, screen off, double-tap), LM3644 flash LED. Each is loaded
by `initramfs/init` right before the vendor module it replaces.

### Other rows of the README's 7.x table

- **Kernel config / device tree for 7.x**: nothing started. A mainline DTS for Tetris needs the
  MT6878 SoC basics first (no upstream support for MT6878 yet: check `arch/arm64/boot/dts/mediatek`
  in the 7.3 tree for the closest SoC, e.g. mt8186/mt8188/mt6991). The `mainline/dts/*.dtsi`
  snippets assume labels `&i2c3 &i2c5 &i2c6 &pio`.
- **Display panel** (s6e8fc3x02, AMOLED): vendor `drivers/gpu/drm/panel/panel-samsung-s6e8fc3x02.c`
  plus HBM patch 0003; a mainline DRM panel driver is a candidate once there is a display path.
- Blocked (need blobs or a whole subsystem): GPU, display path, modem, Wi-Fi/BT, GNSS, camera,
  sensors hub, fingerprint TEE.
- Chips in the vendor DT that are **not fitted** on this phone (no nodes needed): rt5133, both
  rt6160, rt4831, rt5512, upm6720.

## Open features on the current (vendor) system

- **Calls**: no audio; incoming calls only on MIPC (`rootfs/src/tetris-mipc`, `tetris-voicecall`).
- **Standby**: the SoC hangs in deep sleep; suspend is disabled.
- **GPS first fix**: needs a test outdoors.
- **Headset over USB-C adapter**: untested.
- **Unexplained reset** on 14 Nov 2026: first boot after an empty battery, journal ends at 480 s
  without an oops (`journalctl -b <n>`; the pstore console record on the phone is a stale one
  from 1 Nov, kernel build #1). Watch whether it happens again.
- **Charger asleep on a PC port at end of charge** (vendor kernel): power ready 0, IBUS 0, the
  battery carries the load (-30 to -70 mA). Seen with the vendor driver; unclear whether it is
  intended. Check with a wall charger.
- Android `charger` service in the Halium container restarts every 5 s ("cannot set
  capabilities for charger"): harmless, but noise and wakeups; could be disabled in the container.
- Harmless kernel warnings at boot: `enable_irq` unbalanced in `mt6375_battery` (via
  `fg_sw_bat_cycle_accu`), `scs_patch` on module load.
