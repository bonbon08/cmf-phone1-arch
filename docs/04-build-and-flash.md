# 4. Building and flashing: step by step

**🇬🇧 English** · [🇩🇪 Deutsch](de/04-bauen-und-flashen.md)

This chapter leads from an empty PC to a running phone. It explains every step, its inputs and
outputs, and what can go wrong. The scripts themselves are described in detail in
[05-script-reference.md](05-script-reference.md).

> **Read the [risks](#42-risks) first.** Flashing `userdata` erases all data on the phone. A wrong
> image on the wrong slot can make the system unusable.

## 4.1 Overview

```
Phase 0  Get the inputs (firmware, toolchain, Arch Linux ARM image, busybox)
Phase 1  Prepare the PC (packages, permissions)
Phase 2  Build         fetch-sources → build-kernel → build-debug-boot → build-rootfs
Phase 3  Flash         [optional: flash-stock] → userdata → flash-debug-b
Phase 4  First boot    A black screen is normal. Reachable over USB (172.16.42.1)
Phase 5  Second stage  setup-phone.sh: Halium container, libhybris, wlroots, Nothing shell
Phase 6  Optional      Server or desktop profile (setup-server.sh, setup-desktop.sh)
```

Time required: the kernel build and the rootfs build take many minutes to hours on an ordinary PC.
The second stage compiles libhybris and wlroots **on the phone**, which takes correspondingly long.
Plan for an evening or more.

## 4.2 Risks

| Risk | Consequence | Protection |
|---|---|---|
| Flashing `userdata` | **All data is gone** (photos, apps, accounts) | Back up everything you need beforehand |
| Bootloader is locked again | The phone only starts signed software again. A modified system stays unusable | **Never** lock the bootloader again while your kernel or AVB is off |
| Modem partitions are overwritten | IMEI and radio calibration are gone. The mobile modem then no longer works | `scripts/` never writes these partitions. Back up `nvdata`, `nvcfg`, `nvram`, `protect1`, `protect2`, `persist` before experimenting (see [02](02-hardware-and-partitions.md#26-what-you-should-never-do)) |
| Wrong slot or wrong image | A boot fails | The script `flash-debug-b.sh` only writes slot b. Slot a stays Nothing OS |
| No suspend | The battery only lasts about a day if the screen is mostly off | Do not leave the phone in standby; use the charger |
| No warranty | Hobby project, responsibility lies with you | |

## 4.3 Getting the inputs yourself

These files do **not** belong in the repository. They are proprietary or too large. Put them in the
locations named below. The scripts find them through fixed paths.

| Path | Content | Source |
|---|---|---|
| `firmware/Tetris_B4.1-260928-1140/images/` | Original Nothing OS **B4.1** images for Tetris: `vendor_boot.img`, `vendor.img`, `boot.img`, `init_boot.img`, `vbmeta.img`, `preloader_raw.img` and all partitions from the lists in `flash-stock.sh` | Your own phone firmware, e.g. a firmware archive for Tetris (the partition list is in `scripts/flash-stock.sh`) |
| `toolchain/clang-r487747c/` | Android clang **r487747c** from AOSP `prebuilts/clang/host/linux-x86`, the same version Nothing uses | AOSP prebuilts (repository `platform/prebuilts/clang/host/linux-x86`) |
| `downloads/alarm/ArchLinuxARM-aarch64-latest.tar.gz` | Arch Linux ARM base system for aarch64 | [archlinuxarm.org](https://archlinuxarm.org) |
| `downloads/alarm/usr/bin/busybox` | statically linked busybox for the initramfs | `busybox` package from Arch Linux ARM, extracted so that this path exists |
| Halium `system.img` (only phase 5) | LuneOS Halium 16 GSI `halium-luneos-16.0-20261017-1-halium_arm64`, unpacked | [webOS-ports/halium-images](https://github.com/webOS-ports/halium-images) |
| `src/` | kernel sources | fetched by `scripts/fetch-sources.sh` from the internet |

You may only use the phone firmware (`firmware/`) and the partition data (`stock-info/`) for your own
device.

### Checking the inputs

A short check before you start:

```sh
cd cmfphone1archLinux
ls firmware/Tetris_B4.1-260928-1140/images/vendor_boot.img   # firmware there?
ls toolchain/clang-r487747c/bin/clang                          # compiler there?
ls downloads/alarm/ArchLinuxARM-aarch64-latest.tar.gz          # base system there?
ls downloads/alarm/usr/bin/busybox                             # busybox there?
```

All four commands must list a file without an error message.

## 4.4 Host packages

Install on the PC (Arch Linux):

```sh
sudo pacman -S --needed android-tools arch-install-scripts qemu-user-static-binfmt \
    erofs-utils lz4 cpio e2fsprogs git curl
```

These are the packages listed in the main README. The scripts also need these tools, which most
systems already have: `make`, `gcc`, `bsdtar`, `depmod`, `ssh`, `scp`, `nc`, `pkg-config`, `ld.lld`
and `llvm-strip` (the last one comes with the Android clang in `toolchain/`). According to the main
README, `mkbootimg` and `unpack_bootimg` come from the package `android-tools`. If that is different
on your distribution, install them from the AOSP repository `system/tools/mkbootimg`. For the RDP test
of the desktop you need `xfreerdp3`.

Check for missing commands with:

```sh
for t in git make gcc bsdtar mkbootimg unpack_bootimg fsck.erofs e2fsck resize2fs \
         mkfs.ext4 img2simg lz4 cpio depmod arch-chroot ssh scp nc fastboot adb pkg-config; do
    command -v "$t" >/dev/null || echo "missing: $t"
done
```

For the extra commands your user must be in the group `wheel` or be allowed to run `sudo`
(`build-rootfs.sh` runs as root).

## 4.5 Phase 2: building

All build results end up in `out/`. A new run overwrites them.

### 2a: fetching the sources (`fetch-sources.sh`)

```sh
scripts/fetch-sources.sh
```

- Shallow-clones (`--depth 1`) Nothing's repositories for kernel 6.1, the device modules, the kernel
  modules and the build system (`NothingOSS`, branch `mt6878/Tetris/16b`) into `src/`.
- Applies our patches from `patches/kernel*`. Patches that are already applied are recognised and
  skipped. The script is therefore **safe to run again**.
- Duration: depends on the network, several gigabytes.

### 2b: building the kernel (`build-kernel.sh`)

```sh
scripts/build-kernel.sh            # everything: config, kernel, modules, ext
```

The phases individually, in case one step fails:

| Phase | Command | What happens |
|---|---|---|
| configuration | `build-kernel.sh config` | merges `gki_defconfig`, the device configuration and `kernel/linux.config` |
| kernel | `build-kernel.sh kernel` | `Image.gz` and the kernel's modules |
| device modules | `build-kernel.sh modules` | device modules (`kernel_device_modules-6.1`) |
| external modules | `build-kernel.sh ext [module]` | Wi-Fi, Bluetooth, GPU, camera, GPS (list in `EXT_MODULES`), then our modules `tetris_mdtty` and `uleds`. With a module name it starts at that module, e.g. `ext connectivity/bt` |

Output: `out/kernel/` (build directory), `out/kernel/arch/arm64/boot/Image`, and
`out/kernel/include/config/kernel.release` (the kernel version that all later scripts read).

The warning "not applied" from `config` means: the build did not take an option from
`kernel/linux.config`. Check that line, since it can affect features such as Docker.

### 2c: building the boot images (`build-debug-boot.sh`)

```sh
scripts/build-debug-boot.sh
```

Creates three images for slot b from the kernel and the modules, plus a variant with an emergency exit:

| File | Partition | Content |
|---|---|---|
| `out/debug-boot.img` | `boot_b` | kernel (`Image.lz4`) |
| `out/debug-vendor_boot.img` | `vendor_boot_b` | stock DTB, command line, our initramfs |
| `out/debug-vendor_boot-shell.img` | (alternative to `vendor_boot_b`) | as above, with `tetris.debug=1`: always stays in the initramfs |
| `out/debug-init_boot.img` | `init_boot_b` | **empty**, so that Android's `/init` cannot interfere |

During the run the script checks the module lists (`initramfs/modules.load.3`,
`initramfs/modules.blocklist`) and reports missing modules as "not built". That is normal for modules
from the original system's list that the device does not need. A missing module from `modules.load.3`
is a real error.

On the first run the script also extracts the firmware from `vendor.img` into `out/vendor-extract/`,
because the kernel needs it when loading modules.

### 2d: building the rootfs (`build-rootfs.sh`)

```sh
sudo scripts/build-rootfs.sh               # phone profile
sudo PROFILE=server scripts/build-rootfs.sh
sudo PROFILE=desktop scripts/build-rootfs.sh
```

The script (runs as root) builds the file system for the `userdata` partition. The steps are
numbered so that you can find them when something fails:

1. Creates an empty ext4 image `out/rootfs.img` (size `SIZE`, default `10G`).
2. Unpacks the Arch Linux ARM base system and copies the kernel modules into it.
3. Copies the overlays (`rootfs/overlay`, plus `-server` or `-desktop` depending on the profile) and
   the firmware from `out/vendor-extract/`.
4. Creates an SSH key `out/ssh/id_ed25519` (if missing). Your own public keys from `~/.ssh/*.pub` are
   also put onto the phone.
5. Installs the packages in an **aarch64 chroot** via QEMU (this takes the longest).
6. Cross-compiles our C programs with the Android clang against the libraries in the rootfs.
7. Enables the services and writes `out/rootfs.simg`, the sparse version for fastboot.

Output: `out/rootfs.img` and `out/rootfs.simg`.

The option `--finish` works on an existing `out/rootfs.img`. It does not unpack anything again and does
not install packages. It applies the overlay and the services again and shrinks the image. This is
fast if you have only changed files in `rootfs/overlay`.

More on the packages and profiles: [05-script-reference.md](05-script-reference.md#build-rootfssh).

## 4.6 Phase 3: flashing

The phone must be in **fastboot mode**. Switch it off, then hold **Power + Volume down** until the
fastboot screen appears. Connect it to the PC via USB.

Check the connection:

```sh
fastboot devices                    # shows a serial number
fastboot getvar product             # expected: product: k6878v1_64
```

### 3a: optional: rewriting the stock system (`flash-stock.sh`)

Only needed if you assume an unknown or damaged state. The script writes Nothing OS B4.1 to **both**
slots, disables AVB (needed for our kernel) and erases `userdata` and `metadata`:

```sh
scripts/flash-stock.sh firmware/Tetris_B4.1-260928-1140/images
```

The script aborts if an image is missing or the product identifier is not `k6878v1_64`. It switches
into fastbootd mode (`fastboot reboot fastboot`), recreates the logical partitions and writes them.
This takes several minutes and is normal.

With `--keep-avb` the original `vbmeta` is kept. Then our kernel will not start.

### 3b: data partition (`userdata`)

```sh
fastboot flash userdata out/rootfs.simg     # ERASES ALL DATA
```

### 3c: kernel and initramfs on slot b (`flash-debug-b.sh`)

```sh
scripts/flash-debug-b.sh                    # normal: emergency exit disabled
scripts/flash-debug-b.sh --shell            # variant: always stays in the initramfs
```

The script:

1. Waits until a device appears in fastboot mode (and restarts a running Android with
   `adb reboot bootloader`).
2. Writes `boot_b`, `init_boot_b` and `vendor_boot_b`.
3. Sets slot b active (`fastboot --set-active=b`).
4. Starts the phone (`fastboot reboot`).

Slot a stays unchanged. The way back is `fastboot --set-active=a && fastboot reboot`.

## 4.7 Phase 4: first boot

After the first boot the **screen stays black**. This is expected. The phone is already on the network,
though:

- **Initramfs:** the phone registers over USB as a network device (`172.16.42.1`), Telnet runs on port 23.
- **Arch Linux:** when the system starts, the screen stays black. Arch's standard wlroots lacks a patch
  for `phoc` (layer shell), so Phosh exits. The second stage builds the right version
  (`build-wlroots-phoc.sh`).

### Setting up USB networking (PC)

The PC needs a fixed address on the same network:

```sh
sudo nmcli c add type ethernet con-name tetris-usb match.driver cdc_ncm \
    ipv4.method manual ipv4.addresses 172.16.42.2/24 ipv6.method ignore
```

Give the phone internet via the PC (only needed for phases 5 and 6):

```sh
sudo scripts/host-nat.sh up         # NAT for 172.16.42.0/24 via the PC's default network
```

Test:

```sh
scripts/phone-ssh.sh                # ssh alarm@172.16.42.1 with the project key
```

The password of the user `alarm` is `alarm`. **Change it immediately** with `passwd`, because SSH is on.

Emergency shell in the initramfs (without Arch): `scripts/phone-sh.sh 'dmesg | tail'` runs a
command over Telnet.

## 4.8 Phase 5: second stage (`setup-phone.sh`)

The second stage installs everything that has to be **built on the phone itself**, plus the GPU path:

```sh
scripts/setup-phone.sh /path/to/halium/system.img
```

What the script does:

1. Copies the Halium `system.img` to `/var/lib/lxc/android/`.
2. Copies the sources and patches to the phone.
3. Builds on the phone: **libhybris** (`build-libhybris.sh`), **wlroots 0.20** with our backend
   (`build-wlroots-phoc.sh`), the **ModemManager plugin** (`build-mm-plugin-tetris.sh`).
4. Builds and installs the camera app, the GPS client and the fingerprint client.
5. Installs the **Nothing shell** (`setup-nothing.sh`), unless `PHOSH=1` is set.
6. Switches the GPU mode on (`/etc/tetris-android/gpu-shell`) and reboots.

```sh
PHOSH=1 scripts/setup-phone.sh /path/to/system.img    # Phosh instead of the Nothing shell
```

After the reboot the interface starts on the GPU. If that fails, the system falls back to software mode
automatically. Check it with `cat /run/tetris-phosh/mode` (on the phone). The value is `gpu` or `sw`.

The Nothing shell can also be installed on its own:

```sh
scripts/setup-nothing.sh [~/Nothing_Wayland]
```

This needs the source directory `Nothing_Wayland` (with a `PKGBUILD`) or clones it. The Nothing shell
needs our wlroots version, otherwise pacman pulls in Arch's standard wlroots.

## 4.9 Phase 6: server and desktop profiles

With the phone running (phase 4 done, this branch's kernel on slot b):

```sh
scripts/setup-server.sh [--with-modem]     # Docker, Avahi, zram, server profile
scripts/setup-desktop.sh [--with-modem]    # like server, plus GNOME over RDP
```

These scripts need the kernel with the Docker options (`kernel/linux.config`). The desktop script also
needs the `SO_PEERPIDFD` patch (`patches/kernel/0001`). Both check this at the start and abort with a
clear message if the kernel does not match.

Details: [08-profiles.md](08-profiles.md).

## 4.10 Way back

| Goal | Command |
|---|---|
| Back to Nothing OS (slot a) | `fastboot --set-active=a && fastboot reboot` |
| Reinstall Nothing OS completely (**erases data**) | `scripts/flash-stock.sh firmware/…/images` |
| From server/desktop back to the phone | `sudo tetris-mode phone && sudo reboot` on the phone |
| Only one boot as server/desktop | app **Startmodus** (see [08](08-profiles.md)) |
| Back to Phosh | `sudo rm /etc/tetris-shell && sudo reboot` |
| In an emergency, into the initramfs | hold a volume key at boot (see [03](03-architecture.md#step-3-the-initramfs-initramfsinit)) |

## 4.11 Updating the image

After changes to the kernel or the modules:

- **Only modules changed** (kernel unchanged): `scripts/push-modules.sh` sends the new modules to the
  running phone over SSH.
- **Kernel changed:** `build-kernel.sh`, `build-debug-boot.sh`, `flash-debug-b.sh`, then
  `push-modules.sh` (the modules must match the kernel version, otherwise the kernel rejects them).
- **Rootfs changed** (files from `rootfs/overlay`): simplest is to copy the single file onto the phone.
  A new image is more work, but clean.

## 4.12 Common build errors

| Symptom | Cause | Remedy |
|---|---|---|
| `missing …/ArchLinuxARM-aarch64-latest.tar.gz` | input missing | section 4.3 |
| `run as root` from `build-rootfs.sh` | wrong invocation | `sudo scripts/build-rootfs.sh` |
| `!! device is not k6878v1_64` | wrong device or fastboot not active | restart the device into fastboot, check `fastboot getvar product` |
| `!! not in fastbootd` | switching into fastbootd failed | run the script again. The device must be **in fastboot mode** for this |
| The phone rejects modules (`disagrees about version of symbol`, `Invalid module format`) | modules come from a different kernel build | `scripts/push-modules.sh` after the kernel build, or `build-kernel.sh all` |
| `pacman` fails in the chroot | QEMU binfmt not active | install `qemu-user-static-binfmt` and restart the `systemd-binfmt` service |
| Phone does not reach `172.16.42.1` | the PC's USB network is not set up | run the `nmcli` command above, check the cable, `ip addr` on the PC |

More errors and logs: [09-troubleshooting.md](09-troubleshooting.md).
