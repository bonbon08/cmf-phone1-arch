#!/usr/bin/env bash
# Builds rt6010-haptic.ko against the kernel the phone runs (out/kernel, 6.1 vendor tree),
# with the same compiler and flags as scripts/build-kernel.sh. The module needs the kernel's
# Module.symvers (MODVERSIONS is on), so it only loads on that exact kernel release.
set -euo pipefail
R=$(realpath "$(dirname "$0")/../..")
OUT=$R/out/kernel
EXT=$R/out/rt6010-ko
SRC=$R/mainline/drivers/input/misc/rt6010-haptic.c
export PATH=$R/toolchain/clang-r487747c/bin:$PATH

[[ -f $OUT/Module.symvers ]] || { echo "no built kernel in out/kernel (run scripts/build-kernel.sh)" >&2; exit 1; }
rm -rf "$EXT" && mkdir -p "$EXT"
cp "$SRC" "$EXT/rt6010-haptic.c"
printf 'obj-m := rt6010-haptic.o\n' > "$EXT/Makefile"

make -C "$R/src/kernel-6.1" O="$OUT" M="$EXT" ARCH=arm64 LLVM=1 LLVM_IAS=1 \
    CROSS_COMPILE=aarch64-linux-gnu- -j"$(nproc)" modules
echo "built: $EXT/rt6010-haptic.ko"
modinfo "$EXT/rt6010-haptic.ko" | grep -E '^(vermagic|license|description|depends):'
