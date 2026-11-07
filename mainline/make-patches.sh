#!/usr/bin/env bash
# Regenerates patches/mainline/ from a clean mainline tree: first the device-tree binding,
# then the driver with its Kconfig, Makefile and MAINTAINERS entries.
#   mainline/make-patches.sh <clean-linux-tree>
set -euo pipefail
R=$(realpath "$(dirname "$0")/..")
T=$(realpath "${1:?usage: make-patches.sh <clean-linux-tree>}")
OUT=$R/patches/mainline
BINDING=Documentation/devicetree/bindings/input/richtek,rt6010-haptic.yaml

"$R/mainline/apply-to-tree.sh" "$T"
cd "$T"
git add -N "$BINDING" drivers/input/misc/rt6010-haptic.c
mkdir -p "$OUT"
rm -f "$OUT"/*.patch
git diff -- "$BINDING" > "$OUT/0001-dt-bindings-input-add-richtek-rt6010-haptic.patch"
git diff -- drivers/input/misc/rt6010-haptic.c drivers/input/misc/Kconfig \
    drivers/input/misc/Makefile MAINTAINERS > "$OUT/0002-Input-add-RT6010-haptic-driver.patch"
ls -l "$OUT"
