#!/usr/bin/env bash
# Builds the RT6010 haptic module for the phone's current kernel, copies it and the FF test
# to the phone, swaps the vendor haptic driver for it and vibrates the motor.
#
#   mainline/test/deploy-and-test.sh          build, install, test (phone on USB, booted into Arch)
#   mainline/test/deploy-and-test.sh restore  unload it and bring the vendor driver back
#
#   SUDO_PASS=...  if alarm's password is no longer "alarm"
set -euo pipefail
R=$(realpath "$(dirname "$0")/../..")
SSH="$R/scripts/phone-ssh.sh"
PASS=${SUDO_PASS:-alarm}
KO=$R/out/rt6010-ko/rt6010-haptic.ko
PHONE_KO=/tmp/rt6010-haptic.ko
PHONE_TEST=/tmp/haptic-ff-test.py
sudo_() { "$SSH" "echo '$PASS' | sudo -S -p '' sh -c '$1'"; }

if [[ ${1:-} == restore ]]; then
    sudo_ 'rmmod rt6010-haptic 2>/dev/null; modprobe haptic && systemctl start tetris-vibra; echo restored; lsmod | grep -E "^haptic" '
    exit 0
fi

"$R/mainline/test/build-for-phone.sh"

scp_() { scp -i "$R/out/ssh/id_ed25519" -o UserKnownHostsFile="$R/out/known_hosts" "$@"; }
scp_ "$KO" "$R/mainline/test/haptic-ff-test.py" "alarm@172.16.42.1:/tmp/"

sudo_ "
    systemctl stop tetris-vibra 2>/dev/null || true
    rmmod rt6010-haptic 2>/dev/null || true
    rmmod haptic_sfdc 2>/dev/null || true
    rmmod haptic 2>/dev/null || true
    insmod $PHONE_KO
    sleep 4
    dmesg | grep -i 'rt6010\|haptic' | tail -20
"
echo "== vibrating"
sudo_ "python3 $PHONE_TEST 3 300"
echo "Done. To go back to the vendor driver: mainline/test/deploy-and-test.sh restore"
