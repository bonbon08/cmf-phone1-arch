#!/usr/bin/env bash
# Turn an installed phone's shell into the Nothing shell (nothing-wm + nothing-shell, phone layout)
# instead of Phosh. Run on the PC, phone booted into Arch and reachable over USB or WLAN.
# The phone builds nothing-de from a local checkout of Nothing_Wayland (aarch64 package), installs
# it and writes /etc/tetris-shell = nothing. Phosh stays installed; back to it with
# `sudo rm /etc/tetris-shell && sudo reboot` on the phone.
#
# Needs our wlroots0.20 (hwcomposer backend) already on the phone: scripts/setup-phone.sh or
# pkgbuilds/build-wlroots-phoc.sh. Otherwise pacman would pull Arch's stock wlroots, which has no
# GPU path.
#
#   setup-nothing.sh [/path/to/Nothing_Wayland]
#     default: ~/Nothing_Wayland if it exists, else a fresh clone of $NOTHING_REPO
#     ($NOTHING_REF) in out/Nothing_Wayland
#   SUDO_PASS=...  if alarm's password is no longer "alarm"
set -euo pipefail
P=$(realpath "$(dirname "$0")/..")
NOTHING_REPO=${NOTHING_REPO:-https://github.com/bonbon08/Nothing_Wayland.git}
NOTHING_REF=${NOTHING_REF:-main}
SRC=${1:-}
if [ -z "$SRC" ]; then
    if [ -f "$HOME/Nothing_Wayland/PKGBUILD" ]; then
        SRC=$HOME/Nothing_Wayland
    else
        rm -rf "$P/out/Nothing_Wayland"
        git clone --depth 1 -b "$NOTHING_REF" "$NOTHING_REPO" "$P/out/Nothing_Wayland"
        SRC=$P/out/Nothing_Wayland
    fi
fi
SRC=$(realpath "$SRC")
SSH="$P/scripts/phone-ssh.sh"
PASS=${SUDO_PASS:-alarm}
sudo_() { "$SSH" "echo '$PASS' | sudo -S -p '' sh -c '$1'"; }

[ -f "$SRC/PKGBUILD" ] || { echo "!! no PKGBUILD in $SRC (give the Nothing_Wayland checkout)" >&2; exit 1; }

echo "== wlroots (ours)"
"$SSH" 'pacman -Q wlroots0.20' >/dev/null 2>&1 || {
    echo "!! wlroots0.20 is not installed on the phone; build it first (pkgbuilds/build-wlroots-phoc.sh)" >&2
    exit 1
}

echo "== packages"
sudo_ 'pacman -Sy --noconfirm --needed base-devel meson ninja wayland-protocols gtk4 gtk4-layer-shell libadwaita python-gobject python-cairo python-numpy gnome-online-accounts accountsservice polkit-gnome xdg-desktop-portal-gtk xdg-desktop-portal-wlr xorg-xwayland squeekboard grim'

echo "== source"
tar -C "$SRC" --exclude=./.git --exclude=./src --exclude=./pkg --exclude=./build \
    --exclude=./.pytest_cache --exclude='./*.pkg.tar.*' -cf - . \
    | "$SSH" 'rm -rf ~/build/nothing-de && mkdir -p ~/build/nothing-de && tar -C ~/build/nothing-de -xf -'

echo "== build nothing-de (on the phone)"
"$SSH" 'cd ~/build/nothing-de && makepkg -f --noconfirm --nocheck'

echo "== install"
sudo_ 'pacman -U --noconfirm /home/alarm/build/nothing-de/nothing-de-*.pkg.tar.*'

echo "== session files"
chmod +x "$P/rootfs/overlay/usr/local/bin/tetris-nothing-session"
tar -C "$P/rootfs/overlay" --owner=0 --group=0 \
    -cf - usr/local/bin/tetris-nothing-session usr/local/bin/tetris-phosh-session \
    | "$SSH" 'cat > /tmp/overlay-nothing.tar'
sudo_ 'tar -C / -xf /tmp/overlay-nothing.tar && rm /tmp/overlay-nothing.tar'
sudo_ 'echo nothing > /etc/tetris-shell'

echo "== done"
echo "Reboot the phone to start the Nothing shell: sudo reboot"
echo "Back to Phosh: sudo rm /etc/tetris-shell && sudo reboot"
