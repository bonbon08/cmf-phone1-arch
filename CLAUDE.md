# Working on this repo

Arch Linux ARM on the CMF Phone 1 (Tetris, MT6878). Start with `TODO.md` (open work, what waits
for a test) and the README (status, roadmap to mainline 7.x). Detailed docs: `docs/` (English) and
`docs/de/` (German); keep both in sync when you change one.

## Conventions

- The owner writes German; answer in German. Code, comments, commit messages and docs are English
  (docs also German in `docs/de/`).
- Work on a branch (`driver/...`, `dts/...`, `rootfs/...`), merge into `main` with `--no-ff`
  ("Merge branch 'x' into main"), push `main` and the branch when the owner asks for it.
  Nothing_Wayland (the shell, `../Nothing_Wayland`) goes through pull requests instead.
- Update the README status tables and `TODO.md` with every change; say what was tested and what
  was not.
- Vendor kernel changes go into `patches/kernel_device_modules/NNNN-*.patch` (generated from the
  tree in `src/`, which is only a checkout with the patches applied); mainline ports go into
  `mainline/` and must build for 6.1 (phone) and 7.3 with `W=1` without warnings.

## The phone

- On USB: `scripts/phone-ssh.sh` (alarm@172.16.42.1, sudo password `alarm`). It may be in use by
  the owner; avoid UI actions (nothing-msg) without need.
- Vendor modules and the ports load from the initramfs in `vendor_boot_b`, not from
  `/usr/lib/modules`: rebuild modules, `scripts/build-debug-boot.sh`, reboot to fastboot with the
  reboot syscall (see `mainline/test/deploy-and-test.sh`), `scripts/flash-debug-b.sh`. Slot a
  (Nothing OS) stays untouched.
- The kernel has no dynamic debug (dev_dbg is invisible) and no i2c-dev; for read-only register
  checks build `drivers/i2c/i2c-dev.c` as an external module and insmod it temporarily.
- Remote screen control: `NOTHING_WM_SOCK=/run/user/1000/nothing-wm.wayland-0.sock nothing-msg ...`;
  screenshots with `grim` (WAYLAND_DISPLAY=wayland-0, XDG_RUNTIME_DIR=/run/user/1000).
