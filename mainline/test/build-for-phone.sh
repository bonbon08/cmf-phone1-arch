#!/usr/bin/env bash
# Builds rt6010-haptic.ko against the kernel the phone runs (out/kernel, 6.1 vendor tree),
# with the same compiler and flags as scripts/build-kernel.sh. The module needs the kernel's
# Module.symvers (MODVERSIONS is on), so it only loads on that exact kernel release.
set -euo pipefail
R=$(realpath "$(dirname "$0")/../..")
OUT=$R/out/kernel
EXT=$R/out/rt6010-ko
MOD_SRC=(
    "$R/mainline/drivers/input/misc/rt6010-haptic.c"
    "$R/mainline/drivers/input/touchscreen/focaltech-ft3519.c"
)
export PATH=$R/toolchain/clang-r487747c/bin:$PATH

[[ -f $OUT/Module.symvers ]] || { echo "no built kernel in out/kernel (run scripts/build-kernel.sh)" >&2; exit 1; }
rm -rf "$EXT" && mkdir -p "$EXT"
mods=""
for src in "${MOD_SRC[@]}"; do
    cp "$src" "$EXT/"
    mods+="$(basename "$src" .c).o "
done
printf 'obj-m := %s\n' "$mods" > "$EXT/Makefile"

make -C "$R/src/kernel-6.1" O="$OUT" M="$EXT" ARCH=arm64 LLVM=1 LLVM_IAS=1 \
    CROSS_COMPILE=aarch64-linux-gnu- -j"$(nproc)" modules
for ko in "$EXT"/*.ko; do
    echo "built: $ko"
    modinfo "$ko" | grep -E '^(vermagic|description):'
done
