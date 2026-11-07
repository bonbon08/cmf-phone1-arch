#!/usr/bin/env bash
# Tests the RT6010 haptic port on the phone.
#
#   mainline/test/deploy-and-test.sh          test on the running phone (already flashed)
#   mainline/test/deploy-and-test.sh --flash  build the module and boot images, flash slot b, then test
#
# The port is in the initramfs and is loaded before the vendor haptic.ko, so it claims the chip at
# boot. Unbinding the vendor driver at run time resets the phone (reproduced), so this script never does it.
# Slot a (Nothing OS) is not touched.
#
#   SUDO_PASS=...  if alarm's password is no longer "alarm"
set -euo pipefail
R=$(realpath "$(dirname "$0")/../..")
T=$R/mainline/test
SSH="$R/scripts/phone-ssh.sh"
PASS=${SUDO_PASS:-alarm}
scp_() { scp -i "$R/out/ssh/id_ed25519" -o UserKnownHostsFile="$R/out/known_hosts" "$@"; }
phone_sudo() { "$SSH" "echo '$PASS' | sudo -S -p '' $1"; }

wait_for_phone() {
    for _ in $(seq 60); do
        timeout 6 "$SSH" true 2>/dev/null && return 0
        sleep 4
    done
    echo "phone not reachable over USB" >&2
    return 1
}

if [[ ${1:-} == --flash ]]; then
    "$T/build-for-phone.sh"
    "$R/scripts/build-debug-boot.sh" > "$R/out/build-debug-boot.log" 2>&1
    echo "== boot images built (out/debug-*.img)"
    echo "== rebooting the phone into fastboot"
    # reboot(2) with the argument "bootloader", as the initramfs reboot-mode tool does
    timeout 15 "$SSH" "echo '$PASS' | sudo -S -p '' sync; echo '$PASS' | sudo -S -p '' python3 -c 'import ctypes; ctypes.CDLL(None).syscall(142,0xfee1dead,672274793,0xA1B2C3D4,b\"bootloader\")'" >/dev/null 2>&1 || true
    for _ in $(seq 30); do fastboot devices 2>/dev/null | grep -q . && break; sleep 3; done
    fastboot devices | grep -q . || { echo "no device in fastboot" >&2; exit 1; }
    echo "== flashing slot b"
    "$R/scripts/flash-debug-b.sh"
    echo "== waiting for the phone to boot"
    sleep 20
    wait_for_phone
fi

echo "== checking the chip owners"
check_owner() { # check_owner <i2c device> <expected driver>
    local owner
    owner=$("$SSH" "readlink /sys/bus/i2c/devices/$1/driver | xargs -r basename" 2>/dev/null || true)
    if [[ $owner != "$2" ]]; then
        echo "$1 is owned by '${owner:-nothing}', not $2" >&2
        return 1
    fi
    echo "$1 owner: $2"
}
check_owner 0-005f rt6010-haptic
check_owner 2-0038 focaltech-ft3519

scp_ "$T/haptic-ff-test.py" "$T/touch-test.py" "alarm@172.16.42.1:/tmp/"
echo "== vibrating"
"$SSH" "echo '$PASS' | sudo -S -p '' python3 /tmp/haptic-ff-test.py 3 300"
echo "== touch: tap the screen now (15 s)"
"$SSH" "echo '$PASS' | sudo -S -p '' python3 /tmp/touch-test.py 15" || true
echo "Done."
