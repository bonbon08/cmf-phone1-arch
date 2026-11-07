# 5. Script reference

**🇬🇧 English** · [🇩🇪 Deutsch](de/05-skript-referenz.md)

Every script in the project is described here: purpose, invocation, inputs, outputs and side
effects. The scripts fall into three groups:

- **`scripts/`:** runs on the **PC**. Builds, flashes and sets up the phone over USB.
- **`pkgbuilds/`:** runs on the **phone** (as user `alarm`). Builds software that has to be compiled
  on the phone. The PC copies the files there.
- **`initramfs/`:** the mini system that starts before Arch (not a script in the classic sense).

All scripts are Bash scripts (`#!/usr/bin/env bash`) with `set -euo pipefail`, so they abort at the
first error. Most of them locate their project directory themselves, so you can call them from
anywhere.

## 5.1 Overview

| Script | Location | Runs on | Purpose | Writes to the device? |
|---|---|---|---|---|
| `fetch-sources.sh` | `scripts/` | PC | fetch kernel sources and apply patches | no |
| `build-kernel.sh` | `scripts/` | PC | build the kernel and all modules | no |
| `build-debug-boot.sh` | `scripts/` | PC | create boot, vendor_boot and init_boot images | no |
| `build-rootfs.sh` | `scripts/` | PC (root) | build the Arch Linux image for `userdata` | no |
| `extract-vendor.sh` | `scripts/` | PC | extract firmware from the original `vendor` image | no |
| `flash-stock.sh` | `scripts/` | PC | write original Nothing OS to both slots | **yes, all partitions, erases data** |
| `flash-debug-b.sh` | `scripts/` | PC | write kernel and initramfs to slot b | **yes, slot b** |
| `push-modules.sh` | `scripts/` | PC → phone | copy kernel modules onto the running phone | yes, `/usr/lib/modules` |
| `setup-phone.sh` | `scripts/` | PC → phone | second stage: Halium, libhybris, wlroots, shell | yes |
| `setup-nothing.sh` | `scripts/` | PC → phone | install the Nothing shell | yes |
| `setup-server.sh` | `scripts/` | PC → phone | set up the server profile | yes, reboot |
| `setup-desktop.sh` | `scripts/` | PC → phone | set up the desktop profile | yes, reboot |
| `phone-ssh.sh` | `scripts/` | PC → phone | SSH connection with the project key | no |
| `phone-sh.sh` | `scripts/` | PC → initramfs | run a command in the emergency Telnet shell | only what the command does |
| `host-nat.sh` | `scripts/` | PC (root) | share the PC's internet with the phone | the PC's firewall |
| `desktop-rdp.sh` | `scripts/` | PC | RDP connection to the desktop profile | no |
| `read-flashlog.sh` | `scripts/` | PC (adb, root) | read the initramfs log from `nt_reserve2` | no |
| `build-libhybris.sh` | `pkgbuilds/` | phone | build libhybris | yes, `/opt` |
| `build-wlroots-phoc.sh` | `pkgbuilds/` | phone | build wlroots 0.20 with our backend | yes, package |
| `build-mm-plugin-tetris.sh` | `pkgbuilds/` | phone | build the ModemManager plugin | yes, `/usr/lib/ModemManager` |
| `build-llama-vulkan.sh` | `pkgbuilds/` | phone | build llama.cpp with Vulkan on the GPU | yes, `/opt` |

---

## 5.2 Scripts on the PC (`scripts/`)

### `fetch-sources.sh`

**Purpose:** fetches the sources of the Nothing kernel and applies our patches.

**Invocation:** `scripts/fetch-sources.sh` (no arguments)

**Inputs:** internet access to `github.com/NothingOSS`.

**Outputs:** directories in `src/`:

| Directory in `src/` | Repository | Content |
|---|---|---|
| `kernel-6.1` | `android_kernel_6.1_nothing_mt6878` | kernel sources |
| `kernel_device_modules-6.1` | `android_kernel_device_modules_6.1_nothing_mt6878` | device drivers (MediaTek, display, battery, touch) |
| `vendor/mediatek/kernel_modules` | `android_kernel_modules_nothing_mt6878` | Wi-Fi, Bluetooth, GPS, GPU, camera |
| `build` | `android_kernel_build_nothing_mt6878` | build configuration |

**Procedure:**

1. Four repositories in parallel with `git clone --depth 1` (branch `mt6878/Tetris/16b`). If a
   directory already exists, it is skipped.
2. The function `apply` applies all patches in `patches/kernel`, `patches/kernel_device_modules` and
   `patches/kernel_modules`. With `git apply --check -R` it detects whether a patch is already in, and
   skips it then.

**Side effects:** none on the device. Changes in `src/` stay local.

**Errors:** a patch that no longer fits (for example after a new Nothing release) aborts with the
message from `git apply`. The patch then has to be re-rolled, see
[07-kernel-drivers-patches.md](07-kernel-drivers-patches.md#75-refreshing-patches).

---

### `build-kernel.sh`

**Purpose:** builds the kernel and all modules with plain `make` and the Android clang. The script
follows the flow of Nothing's build (`build.config.mtk_kernel_device_modules`), without Bazel/Kleaf.

**Invocation:**

```sh
scripts/build-kernel.sh [config|kernel|modules|ext [first-module]|all]
```

Without an argument `all` is chosen. `ext` optionally accepts a module name or path fragment to
resume from.

**Inputs:** `src/` (from `fetch-sources.sh`), `toolchain/clang-r487747c/`, `kernel/linux.config`.

**Outputs:** `out/kernel/` with `Image`, `Image.gz`, modules (`*.ko`), `System.map`,
`include/config/kernel.release`.

**Environment variables:**

| Variable | Default | Effect |
|---|---|---|
| `JOBS` | number of CPU cores | parallel jobs for `make` |
| `TETRIS_LINUX_HCI` | `0` | `1` builds Bluetooth as a Linux HCI device instead of via `/dev/stpbt`. This option is experimental and has once prevented the boot |

**Procedure in detail:**

1. `config`: `gki_defconfig` as the base, then `merge_config.sh` lays the device configuration
   `mgk_64_k61_defconfig` and our `kernel/linux.config` on top, then `olddefconfig`. Options from our
   file that are missing afterwards are reported with `!! not applied`.
2. `kernel`: `make Image.gz modules`.
3. `modules`: the device modules from `kernel_device_modules-6.1`.
4. `ext`: all modules from `EXT_MODULES` (Wi-Fi `gen4m`, Bluetooth `mt66xx`, GPU, camera, GPS,
   conninfra ...) in dependency order. Afterwards it builds our modules `rootfs/src/tetris-mdtty`
   (AT tty pair) and `rootfs/src/tetris-torch` (uleds).

**Warning:** the modules are bound to exactly this kernel (version check, modversions). Every kernel
build changes the module signatures. Afterwards all modules must be rebuilt and copied to the phone
(`push-modules.sh` or a new rootfs).

---

### `build-debug-boot.sh`

**Purpose:** creates the boot images for slot b and the initramfs.

**Invocation:** `scripts/build-debug-boot.sh` (no arguments)

**Inputs:**

| Path | From | Use |
|---|---|---|
| `out/kernel/` | `build-kernel.sh` | kernel, modules, `kernel.release` |
| `stock-info/unpacked/vendor_boot_vendor_ramdisk00.d/lib/modules/modules.load` | original system | list of stage 1 modules |
| `stock-info/vendor_dlkm/modules.load` | original system | list of stage 2 modules |
| `initramfs/modules.load.3` | project | list of stage 3 modules |
| `initramfs/modules.blocklist` | project | modules that are not loaded |
| `initramfs/init`, `initramfs/tools/*.c` | project | start script, helper programs |
| `downloads/alarm/usr/bin/busybox` | Arch Linux ARM | toolbox in the initramfs |
| `firmware/…/images/vendor_boot.img` | original firmware | DTB, command line, memory layout |
| `src/kernel-6.1/drivers/video/logo/logo_linux_clut224.ppm` | kernel sources | Tux logo for the splash |

**Outputs:** `out/debug-boot.img`, `out/debug-vendor_boot.img`, `out/debug-vendor_boot-shell.img`,
`out/debug-init_boot.img`, `out/debug-initramfs.cpio.lz4`, `out/Image.lz4`, `out/empty.cpio.lz4`,
`out/stock-vendor_boot/`.

**Procedure:**

1. Builds the initramfs directory `out/debug-initramfs/`: busybox, `init`, firmware (from
   `out/vendor-extract/`, created by `extract-vendor.sh` when needed), two helper programs
   (`reboot-mode` and `tetris-splash`, built statically against no libc).
2. Copies the modules from the three lists and strips debug symbols (`llvm-strip`). Modules from
   `modules.blocklist` are skipped.
3. `depmod` creates `modules.dep` for the initramfs. The message `unresolved symbols` is a count:
   vendor modules refer to symbols that were not built. This is not an error.
4. Packs the initramfs as `cpio` (newc) and `lz4`.
5. Creates the boot images with `mkbootimg` (header version 4) and the memory layout of the original
   `vendor_boot`.

**Side effects:** none on the device. The script deletes and recreates `out/debug-initramfs/` and
`out/stock-vendor_boot/`.

---

### `build-rootfs.sh`

**Purpose:** builds the complete Arch Linux ARM file system for the phone.

**Invocation:**

```sh
sudo scripts/build-rootfs.sh                    # phone profile
sudo scripts/build-rootfs.sh --finish           # on an existing image: overlay, services, shrink
sudo PROFILE=server scripts/build-rootfs.sh     # server profile
sudo PROFILE=desktop scripts/build-rootfs.sh    # desktop profile
sudo SIZE=12G scripts/build-rootfs.sh           # different upper size of the image
```

| Variable | Default | Effect |
|---|---|---|
| `PROFILE` | `phone` | `phone`, `server` or `desktop`. Determines packages and overlays |
| `SIZE` | `10G` | size of the image before it is shrunk |

**Inputs:** `downloads/alarm/ArchLinuxARM-aarch64-latest.tar.gz`, `out/kernel/`,
`out/vendor-extract/`, `out/kernel_device_modules-6.1/` (or `out/vendor/`), `rootfs/…`, `pkgbuilds/`,
`patches/`, your `~/.ssh/*.pub`.

**Package list** (in the script as the arrays `PACKAGES`, `SERVER_PACKAGES`, `DESKTOP_PACKAGES`):

| Group | Packages |
|---|---|
| Interface | `phosh`, `phoc`, `squeekboard`, `phosh-mobile-settings`, `gnome-console`, `gnome-control-center`, `gnome-text-editor`, `nautilus`, fonts, `zenity` |
| System | `networkmanager`, `modemmanager`, `chatty`, `gnome-calls`, `gnome-maps`, `wpa_supplicant`, `feedbackd`, `gnome-keyring`, `bluez` |
| Tools | `openssh`, `sudo`, `vim`, `htop`, `base-devel`, `git`, `meson` |
| Container | `lxc` |
| Server (profile `server`/`desktop`) | `docker`, `docker-compose`, `docker-buildx`, `avahi`, `nss-mdns`, `zram-generator` |
| Desktop (profile `desktop`) | `gdm`, `gnome-session`, `gnome-remote-desktop`, `xdg-desktop-portal-gnome`, `gnome-backgrounds`, `gnome-system-monitor` |

**Procedure:**

1. **Create the image** (without `--finish`): an empty image with `truncate`, `mkfs.ext4 -L tetris-root`,
   mounted through a loop device at `out/rootfs-mnt/`.
2. **Base system:** `bsdtar` unpacks the Arch Linux ARM archive.
3. **Modules:** all `.ko` files from the kernel, the device modules and vendor are copied to
   `/usr/lib/modules/<version>/` and shrunk with `llvm-strip`. Plus `tetris_mdtty.ko` and `uleds.ko` in
   `extra/`. Then `depmod`.
4. **Overlay:** copies `rootfs/overlay`, and depending on the profile `overlay-server` and
   `overlay-desktop` on top. It also corrects owners and permissions (files in `/etc/sudoers.d`, keys
   `0600` and so on).
5. **Keys:** creates `out/ssh/id_ed25519` (if missing) and puts the public keys for `alarm` into
   `~/.ssh/authorized_keys`.
6. **Packages** in an aarch64 chroot (`arch-chroot`, QEMU emulation). The option `--disable-sandbox`
   is needed because pacman's download sandbox does not run under QEMU. `linux-aarch64` is removed,
   because the kernel comes from `boot_b`.
7. **Own programs** are cross-compiled with the Android clang against the rootfs (`--sysroot`):
   `tetris-bt-bridge`, `tetris-mdtty-bridge`, `tetris-vibra`, `tetris-torch`, `tetris-display-off`,
   `tetris-sensors` (with `gio` from the rootfs). The list is described in
   [06-rootfs-reference.md](06-rootfs-reference.md#65-own-programs-rootfssrc).
8. **Services** are enabled in the chroot (`systemctl enable`), for example `tetris-phosh`,
   `tetris-modem`, `tetris-sensors`. In the server and desktop profiles `tetris-mode <profile>` also runs.
9. **Finish:** shrinks the image (`resize2fs -M`) and creates the sparse image (`img2simg`).

**Not built in the rootfs:** `libmm-plugin-tetris.so`, the camera app, `tetris-gnss`, `tetris-fp` and
libhybris. The second stage builds those on the phone.

**Outputs:** `out/rootfs.img` (ext4), `out/rootfs.simg` (sparse, for `fastboot flash userdata`).

---

### `extract-vendor.sh`

**Purpose:** extracts the original `vendor` image (erofs) to `out/vendor-extract/vendor`. That is where
the firmware the drivers need at load time lives (`conninfra.cfg`, `connfem.cfg`, `wifi.cfg`,
`mali_csffw.bin`, the SCP firmware and others).

**Invocation:** `scripts/extract-vendor.sh [path/to/vendor.img]`

**Inputs:** `firmware/…/images/vendor.img` (default).

**Tool:** `fsck.erofs --extract` (package `erofs-utils`).

**Output:** `out/vendor-extract/vendor/firmware/` and the number of files.

**Called automatically** by `build-debug-boot.sh` and `build-rootfs.sh` when the directory is missing.

---

### `flash-stock.sh`

**Purpose:** writes the **original Nothing OS B4.1** to both slots. This is the safe way out if something
has gone badly wrong.

**Invocation:**

```sh
scripts/flash-stock.sh firmware/Tetris_B4.1-260928-1140/images [--keep-avb]
```

| Argument | Effect |
|---|---|
| directory | folder with the unpacked `.img` files |
| `--keep-avb` | the `vbmeta` images stay unchanged. Our kernel will then **not** start |

**Procedure in detail:**

1. **Check:** all expected images must be present. The product identifier must be `k6878v1_64`.
   Otherwise the script aborts before writing anything.
2. **Disable AVB** (without `--keep-avb`): the flags in the header of the `vbmeta` images are set with
   `dd` to `HASHTREE_DISABLED | VERIFICATION_DISABLED` (byte 120). Fastboot cannot do this itself; the
   error message is documented in the script.
3. `fastboot --set-active=a`, then `erase userdata` and `erase metadata`.
4. **Write, slots a and b:** `boot`, `dtbo`, `init_boot`, `vendor_boot`, the firmware partitions
   (`apusys`, `gpueb`, `lk`, `modem`, `scp`, `tee` ...), `preloader`, `vbmeta*`.
5. `fastboot reboot fastboot`, wait, check that fastbootd is active.
6. **Logical partitions** (`system`, `vendor`, `product`, `odm` ...): recreate them (or use
   `super_empty.img` with `wipe-super` if the file exists) and write them to slot a.
7. `fastboot --set-active=a`. The closing hint is `fastboot reboot`.

**Side effects:** **erases `userdata`, `metadata` and the logical partitions.** Writes all partitions of
both slots. The modem partitions (`nvdata`, `nvcfg`, `protect`, `persist`) are **not** touched.

---

### `flash-debug-b.sh`

**Purpose:** writes our kernel and initramfs to **slot b** and starts the phone. Slot a stays untouched.

**Invocation:**

```sh
scripts/flash-debug-b.sh            # normal
scripts/flash-debug-b.sh --shell    # always stays in the initramfs (tetris.debug=1)
```

**Procedure:**

1. If Android runs with `adb`, the script restarts it with `adb reboot bootloader`.
2. Waits up to 60 seconds until `fastboot devices` finds a device.
3. Writes `boot_b`, `init_boot_b` (empty) and `vendor_boot_b` (`debug-vendor_boot.img` or `-shell`).
4. `fastboot --set-active=b`, then `fastboot reboot`.

**Way back:** `fastboot --set-active=a && fastboot reboot`.

---

### `push-modules.sh`

**Purpose:** replaces the kernel modules in the **installed** rootfs with those from `out/`. Needed when
the kernel has changed. The kernel rejects outdated modules (version check).

**Invocation:** `scripts/push-modules.sh`, with `SUDO_PASS=…` if the password of `alarm` is no longer
`alarm`.

**Requirement:** the phone runs Arch and is reachable over USB (`phone-ssh.sh`).

**Procedure:**

1. Creates a module directory for the current kernel version from `out/`, plus `tetris_mdtty`, `uleds`,
   `tetris_novtswitch` in `extra/`. Then `depmod`.
2. Copies the archive to the phone over SSH as `/tmp/modules.tgz`.
3. On the phone: the old directory is moved to `<version>.old`, and the new modules are unpacked.

**After the run:** restart the phone so the modules are loaded.

---

### `setup-phone.sh`

**Purpose:** the **second stage**. Everything that has to be built on the phone itself, and the GPU path.

**Invocation:**

```sh
scripts/setup-phone.sh /path/to/halium/system.img
PHOSH=1 scripts/setup-phone.sh /path/to/system.img    # Phosh instead of the Nothing shell
```

**Inputs:** Halium `system.img` (LuneOS Halium 16 GSI), a connection over USB or Wi-Fi with internet on
the phone.

**Procedure:**

1. `system.img` via `scp` to the phone and to `/var/lib/lxc/android/system.img`.
2. Copies sources to the phone: `pkgbuilds/*.sh`, the wlroots patch file, the libhybris patches,
   `patches/modemmanager/tetris`, `rootfs/src/tetris-camera-app`, `tetris-gnss`, `tetris-fp`.
3. Runs on the phone: `build-libhybris.sh`, `build-wlroots-phoc.sh`, `build-mm-plugin-tetris.sh`.
4. Builds and installs the camera app, the GPS client and the fingerprint client with `make`.
5. `setup-nothing.sh` (unless `PHOSH=1`).
6. `touch /etc/tetris-android/gpu-shell` and reboot.

**Warning:** the script takes a long time (libhybris and wlroots compile on the phone). The connection
must not drop.

---

### `setup-nothing.sh`

**Purpose:** installs the **Nothing shell** (`nothing-wm` and `nothing-shell` in phone layout) and
switches the interface to it. Phosh stays installed.

**Invocation:**

```sh
scripts/setup-nothing.sh [/path/to/Nothing_Wayland]
```

| Argument / variable | Default | Effect |
|---|---|---|
| path | `~/Nothing_Wayland` if it has a `PKGBUILD`; otherwise a clone in `out/Nothing_Wayland` | source directory |
| `NOTHING_REPO` | `https://github.com/bonbon08/Nothing_Wayland.git` | clone source |
| `NOTHING_REF` | `main` | branch or tag |
| `SUDO_PASS` | `alarm` | password for `sudo` on the phone |

**Procedure:**

1. Checks that `wlroots0.20` is installed on the phone. Otherwise it aborts, so that pacman does not
   install Arch's standard wlroots without the GPU path.
2. Installs build and runtime packages (`gtk4`, `libadwaita`, `squeekboard`, `xwayland`, `grim` ...).
3. Copies the source directory to the phone and builds it there with `makepkg`.
4. `pacman -U` installs the package. After that `tetris-nothing-session` and `tetris-phosh-session` from
   the overlay are added.
5. Writes `nothing` to `/etc/tetris-shell`.

**Way back:** `sudo rm /etc/tetris-shell && sudo reboot` on the phone.

---

### `setup-server.sh`

**Purpose:** turns a **running** phone with its data kept into a server (Docker, Avahi, zram, overlay,
`tetris-mode server`).

**Invocation:** `scripts/setup-server.sh [--with-modem]`

| Argument | Effect |
|---|---|
| `--with-modem` | mobile data stays active as a connection (needs the Halium container) |

**Requirement:** a kernel with Docker options (`zcat /proc/config.gz` contains `CONFIG_NF_TABLES=y`). If
not, the script aborts with a message.

**Procedure:** installs packages, transfers `rootfs/overlay-server` via `tar`, compiles the service
`tetris-display-off` from source, runs `tetris-mode server`, reboots.

---

### `setup-desktop.sh`

**Purpose:** turns a running phone into the **desktop** profile (GNOME over RDP).

**Invocation:** `scripts/setup-desktop.sh [--with-modem]`

**Requirements:**

- A kernel with the `SO_PEERPIDFD` patch (`patches/kernel/0001`). The script checks this with a socket
  call on the phone.
- For the GPU: the Halium container (`setup-phone.sh`). Otherwise GNOME renders on the processor.

**Procedure:**

1. Calls `setup-server.sh` (server profile, reboot).
2. Waits until the phone is back.
3. Installs GDM, `gnome-remote-desktop` and the GNOME stack.
4. Transfers `rootfs/overlay-desktop`.
5. Builds libhybris with the glvnd variant on the phone, if the container is installed.
6. `tetris-mode desktop`, reboot. Then it waits for the file `~/.config/tetris-rdp-password` and prints
   the password.

---

### `phone-ssh.sh`

**Purpose:** opens an SSH session to the phone with the project key. The arguments are passed to `ssh`.

```sh
scripts/phone-ssh.sh                          # shell
scripts/phone-ssh.sh 'uptime'                 # single command
```

Connects to `alarm@172.16.42.1`, uses `out/ssh/id_ed25519` and `out/known_hosts`.
`StrictHostKeyChecking=accept-new` accepts the phone's key the first time.

---

### `phone-sh.sh`

**Purpose:** runs a command in the **initramfs** (Telnet, port 23). This also works when the Arch system
does not start.

```sh
scripts/phone-sh.sh 'dmesg | tail -20' [seconds]
```

The script sends the command via `nc` to `172.16.42.1:23`, waits for the given time (default 3 s), and
filters out control characters.

---

### `host-nat.sh`

**Purpose:** shares the PC's internet connection with the phone over the USB network. Needed when the
phone has to download packages or compile.

```sh
sudo scripts/host-nat.sh up     # default
sudo scripts/host-nat.sh down
```

**Effect:** sets `net.ipv4.ip_forward=1` and adds rules in `iptables` (NAT via the default network,
forwarding for `172.16.42.0/24`). The rules stay until the PC restarts. Docker sets forwarding to `DROP`,
which is why there is a dedicated allow rule.

---

### `desktop-rdp.sh`

**Purpose:** connects to the phone's GNOME desktop in full HD.

```sh
scripts/desktop-rdp.sh                       # 172.16.42.1 over USB
scripts/desktop-rdp.sh tetris.local /f       # Wi-Fi, full screen
```

Reads the RDP password from the phone (`~/.config/tetris-rdp-password`) and starts `xfreerdp3` with
1920 × 1080, H.264 (`AVC444`) and a TOFU certificate (`/cert:tofu`).

---

### `read-flashlog.sh`

**Purpose:** reads the initramfs log from `nt_reserve2`. Useful when the phone no longer answers after a
boot, but the original system still starts.

```sh
scripts/read-flashlog.sh out/flashlog1
```

Needs `adb` and root (Magisk) on Nothing OS. Writes `init.log` (first 16 MB) and `dmesg.txt` (16 to
18 MB) into the target directory. Details in
[09-troubleshooting.md](09-troubleshooting.md#93-finding-logs).

---

## 5.3 Scripts on the phone (`pkgbuilds/`)

`setup-phone.sh` (or `setup-desktop.sh`) copies these scripts to the phone and runs them there. You can
also run them directly on the phone if you have put the sources and patches into the home directory of
`alarm`.

All four use `SUDOPW` (default `alarm`) for `sudo`.

### `build-libhybris.sh`

**Purpose:** builds **libhybris** for the Halium 16 GSI and installs it to `/opt/libhybris`. A second build
goes to `/opt/libhybris-glvnd` (for GNOME in the desktop profile).

**Sources (pinned):**

| Repository | Branch | Commit |
|---|---|---|
| `Halium/android-headers` | `herrie/halium-16.0` | `ad5912e` |
| `webOS-ports/libhybris` | `herrie/android16-tls` | `92b4e61` |

**Patches:** all `~/libhybris-patches/*.patch` (`patches/libhybris/`).

**Special case:** the Vulkan wrapper (`libvulkan.so`) is moved to `/opt/tetris-hybris-vulkan`. It must not
sit next to `libEGL`, otherwise GTK apps crash.

---

### `build-wlroots-phoc.sh`

**Purpose:** rebuilds **wlroots 0.20** from the Arch package and adds two patches:

1. the patch from phoc 0.57 ("Revert layer-shell: error on 0 dimension without anchors"). Without it Phosh
   exits, because the home bar has height 0;
2. `patches/wlroots/0002-hwcomposer-backend-hybris-allocator.patch`, our backend for the Android composer.
   It is enabled with `-Dhwcomposer=enabled`. The libhybris library is only loaded when
   `WLR_BACKENDS=hwcomposer` is set. The normal DRM path stays unchanged.

**Procedure:** clones the Arch packaging repository, changes the `PKGBUILD` with a Python script, installs
the dependencies and builds with `makepkg -A --skippgpcheck --nocheck`. The new package
`wlroots0.20-*.pkg.tar.*` is installed with `pacman -U` (the package gets the old `pkgrel` with the suffix
`.2`).

---

### `build-mm-plugin-tetris.sh`

**Purpose:** builds only the **plugin** `libmm-plugin-tetris.so` for ModemManager and installs it to
`/usr/lib/ModemManager/`. The ModemManager sources are fetched in the version installed on the phone.

**Procedure:** reads the version from `pacman -Q modemmanager`, clones the tag, copies `~/mm-plugin-tetris`
to `src/plugins/tetris`, adds a block to `src/plugins/meson.build` (Python script, with `assert`), builds only
the target `libmm-plugin-tetris.so` and installs it.

**Rebuild after every ModemManager update**, otherwise the plugin interface does not match.

---

### `build-llama-vulkan.sh`

**Purpose:** builds **llama.cpp** with the Vulkan backend on the phone, into `/opt/llama.cpp`
(experimental). The revision is pinned (`5851a8e`, 4 November 2026).

**Runtime:** `tetris-llama` (server overlay) starts a model with libhybris' Vulkan driver. The Halium
container must be running for this, because it provides the Mali driver under `/vendor`. The GPU has only
two cores at 1 GHz, so the gain is limited.

---

## 5.4 The initramfs files (`initramfs/`)

| File | Type | Purpose |
|---|---|---|
| `init` | shell script | start script, process 1 in the initramfs. See [03-architecture.md](03-architecture.md#step-3-the-initramfs-initramfsinit) |
| `modules.load.3` | list | modules of the third stage. Own list, because it does not come from the original |
| `modules.blocklist` | list | modules that are not loaded. Currently `apu_top.ko`, `apusys.ko`, `sapu.ko` (hang when the AI unit is powered on) |
| `tools/reboot-mode.c` | C | restarts the phone into a mode (`reboot-mode bootloader`), because busybox cannot do that |
| `tools/tetris-splash.c` | C | shows the Tux splash on the display until the compositor or composer takes over. It is ended by `tetris-splash-stop.service` |

The helper programs are **freestanding** (no libc) and are built with the Android clang, so that they run
in the initramfs without libraries.

---

## 5.5 Configuration files for the scripts

| File | Read by | Effect |
|---|---|---|
| `kernel/linux.config` | `build-kernel.sh config` | our kernel options (phone, Docker section "Server profile") |
| `.gitignore` | git | whitelist: only certain directories may enter the repository |
| `initramfs/modules.blocklist` | `build-debug-boot.sh` | see above |
| `initramfs/modules.load.3` | `build-debug-boot.sh` | see above |
| `rootfs/overlay/var/lib/lxc/android/config` | LXC | configuration of the Halium container, see [03-architecture.md](03-architecture.md#33-the-halium-container) |
| `rootfs/overlay/etc/tetris-android/vendor-init.allow` | `tetris-android-prepare` | which vendor init files the container may run |
| `rootfs/overlay-desktop/etc/tetris-software-gl.list` | `tetris-software-gl-desktop-files` | programs that run on Mesa instead of libhybris |

An overview of all overlay files is in [06-rootfs-reference.md](06-rootfs-reference.md).
