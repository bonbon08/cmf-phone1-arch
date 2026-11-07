#!/usr/bin/env bash
# Adds the RT6010 haptic driver to a mainline kernel tree: the driver, its binding and the
# Kconfig/Makefile entries. Run it on a clean tree; it refuses to run twice.
#   mainline/apply-to-tree.sh <linux-tree>
set -euo pipefail
M=$(realpath "$(dirname "$0")")
T=${1:?usage: apply-to-tree.sh <linux-tree>}
[[ -f $T/drivers/input/misc/Kconfig ]] || { echo "not a kernel tree: $T" >&2; exit 1; }
grep -q INPUT_RT6010_HAPTIC "$T/drivers/input/misc/Kconfig" && { echo "already applied" >&2; exit 1; }

install -m644 "$M/drivers/input/misc/rt6010-haptic.c" "$T/drivers/input/misc/"
install -Dm644 "$M/Documentation/devicetree/bindings/input/richtek,rt6010-haptic.yaml" \
    "$T/Documentation/devicetree/bindings/input/richtek,rt6010-haptic.yaml"

python3 - "$T" <<'PY'
import sys, pathlib
t = pathlib.Path(sys.argv[1]) / "drivers/input/misc"
kc = t / "Kconfig"
s = kc.read_text()
anchor = "config INPUT_HISI_POWERKEY"
block = '''config INPUT_RT6010_HAPTIC
\ttristate "RT6010 haptic driver (LRA actuator)"
\tdepends on I2C
\tselect INPUT_FF_MEMLESS
\thelp
\t  Say Y or M if your device has an RT6010 haptic driver with an
\t  LRA vibration motor, such as the CMF Phone 1. The motor is
\t  exposed as a force-feedback device (FF_RUMBLE). The chip
\t  configuration is read from the firmware file haptic_config.bin.

\t  To compile this driver as a module, choose M here: the module
\t  will be called rt6010-haptic.

'''
assert s.count(anchor) == 1
kc.write_text(s.replace(anchor, block + anchor))
ma = pathlib.Path(sys.argv[1]) / "MAINTAINERS"
s = ma.read_text()
anchor = "RICOH R5C592 MEMORYSTICK DRIVER\n"
entry = ("RICHTEK RT6010 HAPTIC DRIVER\n"
         "S:\tMaintained\n"
         "F:\tDocumentation/devicetree/bindings/input/richtek,rt6010-haptic.yaml\n"
         "F:\tdrivers/input/misc/rt6010-haptic.c\n\n")
assert s.count(anchor) == 1
ma.write_text(s.replace(anchor, entry + anchor))
mk = t / "Makefile"
s = mk.read_text()
a = "obj-$(CONFIG_INPUT_REGULATOR_HAPTIC)\t+= regulator-haptic.o\n"
assert s.count(a) == 1
mk.write_text(s.replace(a, a + "obj-$(CONFIG_INPUT_RT6010_HAPTIC)\t+= rt6010-haptic.o\n"))
PY
echo "applied to $T"
