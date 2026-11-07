# 7. Kernel, drivers and patches

**🇬🇧 English** · [🇩🇪 Deutsch](de/07-kernel-treiber-patches.md)

This chapter explains what the phone's kernel is made of, which changes we make and why. It applies to
all parts that change foreign software: the kernel, the device drivers, libhybris, wlroots and
ModemManager.

## 7.1 What is a patch?

A **patch** is a text file that describes which lines of a foreign source file are changed. It has the
format of `git diff`. The file begins with a description (`Subject:` and the text below it), followed by
the changes.

Patches are the heart of the adaptation: we do **not** modify foreign sources directly. Instead every
change lives as its own file in the repository. That stays reviewable, and when a new version of the
foreign software arrives, the patches can be re-applied in a targeted way.

The script `scripts/fetch-sources.sh` applies the patches to the kernel sources. The packages libhybris,
wlroots and ModemManager are patched by their build scripts on the phone.

## 7.2 Overview of all patches

| File | Target | What it changes | Required? |
|---|---|---|---|
| `patches/kernel/0001-net-backport-SO_PEERPIDFD.patch` | kernel (networking) | backports `SO_PEERPIDFD` from Linux 6.5 | required for the desktop profile |
| `patches/kernel_device_modules/0001-mtk-battery-kernel-gauge-default.patch` | battery driver | initialises the wait queue of the battery interrupt before requesting it | required (prevents a NULL pointer crash) |
| `patches/kernel_device_modules/0002-mt6369-readable-pmic-reg-clear.patch` | audio codec MT6369 | adds a read routine for a mixer switch | required for audio detection (PipeWire) |
| `patches/kernel_device_modules/0003-s6e8fc3x02-hbm-mode-notifies-fingerprint.patch` | display driver | tells the fingerprint driver when high-brightness mode (HBM) is on | required for the fingerprint |
| `patches/kernel_device_modules/0004-ft3519-resume-like-suspended.patch` | touch driver | restores touch correctly after wake-up | required (otherwise touch stays dead) |
| `patches/kernel_modules/0001-bt-mt66xx-optional-linux-hci.patch` | Bluetooth (`mt66xx`) | optional variant: Bluetooth as a Linux HCI device | no, only with `TETRIS_LINUX_HCI=1` (experimental) |
| `patches/libhybris/0001-egl-stub-EGL_EXT_device_query-for-GTK4.patch` | libhybris | advertises EGL extensions that GTK 4 requires | required for GTK 4 apps |
| `patches/libhybris/0002-egl-surfaceless-mesa-platform-for-mutter.patch` | libhybris | supports the "surfaceless" EGL platform for the GNOME shell (Mutter) | required for the desktop profile |
| `patches/libhybris/0003-wayland-server-texture-format-for-rgbx-buffers.patch` | libhybris | provides a format for RGBX buffers that compositors accept | required for games (SDL) and GNOME |
| `patches/wlroots/0002-hwcomposer-backend-hybris-allocator.patch` | wlroots 0.20 | new backend for the Android composer and a memory allocator via libhybris | required for the GPU interface |
| `patches/modemmanager/tetris/…` (5 files) | ModemManager | plugin for the MediaTek modem | required for mobile networking |

The wlroots patch `0001` ("Revert layer-shell") comes from phoc 0.57 and is not stored in the project.
The script `build-wlroots-phoc.sh` downloads it directly from GitLab.

### 7.2.1 Kernel: `SO_PEERPIDFD` (`kernel/0001`)

**Problem:** from GNOME 49 on, the desktop obtains a **pidfd** of the caller over the D-Bus message bus (a
safe process reference). The bus `dbus-broker` only hands them out if the kernel knows the socket option
`SO_PEERPIDFD`. Linux 6.1 does not know it yet. Without it, the remote login fails (message "D-Bus broker
doesn't support pidfd").

**Solution:** the patch carries the feature from Linux 6.5 (commit `7b26952a91cf`) into our kernel.
`pidfd_prepare()` is already present in the kernel.

**Where it has an effect:** only in the desktop profile, but the kernel is built with this patch for all
profiles.

### 7.2.2 Battery: wait queue before requesting (`kernel_device_modules/0001`)

**Problem:** the driver of the battery gauge (`mtk-battery`) requests an interrupt before its wait queue
(`wait_que`) is initialised. In the failure case the kernel dereferences a NULL pointer.

**Solution:** the initialisation of the wait queue is moved in front of the interrupt request. The patch
also contains a comment that explains the cause.

### 7.2.3 Audio: readable mixer switch (`kernel_device_modules/0002`)

**Problem:** a switch in the audio codec `MT6369` has no read function. Reading it fails with `-EPERM`.
Then `snd_mixer_load()` (used by PipeWire to detect a headphone jack) cannot load for the whole sound card.

**Solution:** a read function that always returns 0.

### 7.2.4 Display: fingerprint and high brightness (`kernel_device_modules/0003`)

**Problem:** the optical fingerprint sensor only captures an image while high-brightness mode (HBM) is on.
On Android the composer switches it. Under Linux there is no composer in this role, so the panel driver now
reports the switch directly to the fingerprint driver (`mtk_panel_proc_hbm(1)`).

### 7.2.5 Touch: waking up after sleep (`kernel_device_modules/0004`)

**Problem:** on wake-up the touch driver `ft3519` decided, based on flags, whether to restore gesture mode
or normal mode. If the fingerprint mode had been set while the display was off, it took the wrong path. The
interrupt then stayed off, and touch only worked again after a reboot.

**Solution:** the driver remembers in which mode it was when it went to sleep
(`fts_suspended_in_gesture`).

### 7.2.6 Bluetooth: Linux HCI (`kernel_modules/0001`, optional)

By default Bluetooth talks through the Android device path `/dev/stpbt`, and the bridge `tetris-bt-bridge`
translates to `/dev/vhci`. The patch allows creating a normal HCI device directly instead
(`USE_DEVICE_NODE=0`). The option `TETRIS_LINUX_HCI=1` at the kernel build switches that on. The comment in
`build-kernel.sh` says: this variant has prevented the boot once. That is why it is off by default.

### 7.2.7 libhybris: three adjustments

| Patch | Problem | Solution |
|---|---|---|
| `0001` EGL extension | GTK 4 requires the extension `EGL_EXT_device_query`, which libhybris does not know | the extension is advertised; the functions return only what is needed |
| `0002` surfaceless | in headless mode Mutter (the GNOME shell) asks for the platform `EGL_PLATFORM_SURFACELESS_MESA`, which libhybris does not know | mapped to the off-screen mode of the Android driver (without a window protocol) |
| `0003` RGBX | for EGL Wayland buffers compositors only accept `RGB` or `RGBA`. Games with an RGBX configuration produced "Unsupported buffer format" | `RGBX_8888` and `RGB_888` are reported as `EGL_TEXTURE_RGB` |

The patches are tied to the pinned commits in `pkgbuilds/build-libhybris.sh`: changing the commit requires
new patches.

### 7.2.8 wlroots: the hwcomposer backend (`wlroots/0002`)

This patch is the largest (about 1,500 lines). It adds to wlroots 0.20 a **backend** that uses the Android
composer instead of DRM, and a **memory allocator** that requests buffers through gralloc. The backend is
built with `-Dhwcomposer=enabled`. It is only active when `WLR_BACKENDS=hwcomposer` is set; otherwise the
normal path stays unchanged.

The patch also contains the screen rotation in the backend (`WLR_OUTPUT_STATE_TRANSFORM`), as mentioned in
the main README.

### 7.2.9 ModemManager plugin (`patches/modemmanager/tetris/`)

| File | Purpose |
|---|---|
| `mm-plugin-tetris.c` | entry point: recognises the device (MediaTek, `tetris`) and registers the plugin |
| `mm-broadband-modem-tetris.c` | the modem: AT commands, waiting for the SMS storage (`+CME ERROR: 100` is retried until the storage is ready) |
| `mm-broadband-modem-tetris.h` | interface of the modem |
| `mm-broadband-bearer-tetris.c` | the data connection: activation via `AT+EAPNACT`, IP addresses via `AT+EPDN`, DNS via `AT+CGCONTRDP` |
| `mm-broadband-bearer-tetris.h` | interface of the connection |

A note from the header comment of the file: `AT+CGDATA="M-CCMNI"` must **not** be sent. It blocks the AT
channel until the next reboot.

The plugin is **not** built with the rootfs. It is built on the phone with
`pkgbuilds/build-mm-plugin-tetris.sh` and needs the sources of the installed ModemManager version. After an
update of ModemManager it has to be rebuilt.

---

## 7.3 The kernel configuration (`kernel/linux.config`)

The file is a **configuration fragment**: it is laid over the standard configuration (`gki_defconfig` and
`mgk_64_k61_defconfig`), and its values win. The file contains about 70 settings, grouped into sections:

| Section | Content |
|---|---|
| `systemd / udev` | `devtmpfs` for `/dev`, `autofs`, POSIX ACLs and `xattr` for tmpfs, `cgroup` BPF |
| `console / VT` | text consoles, because GNOME and logind expect a VT |
| `LXC container for the Android vendor side` | namespaces (IPC, PID, user, UTS, network), control groups for devices and processes, `veth`, bridge, MACVLAN |
| `debugging over USB` | NCM USB gadget and Telnet in the initramfs |
| `we rebuild every module ourselves` | no signature check, no KMI trimming, because we build all modules ourselves |
| `host has no pahole` | BTF is only debug information and is off |
| `Bluetooth for BlueZ` | `/dev/vhci`, BNEP for the personal area network (PAN) |
| `Server profile` | Docker and Podman (according to the `check-config.sh` of the Docker project): `nftables`, `iptables`, `IPVS`, `VXLAN`, `IPVLAN`, CFS bandwidth limits (`--cpus`) |

Important individual values you will find in the file:

| Option | Meaning |
|---|---|
| `CONFIG_NF_TABLES=y` | basis of the nftables firewall (Docker needs it) |
| `CONFIG_CGROUP_BPF=y`, `CONFIG_VETH=y`, `CONFIG_BRIDGE=y` | networking for containers |
| `CONFIG_CFS_BANDWIDTH=y` | `docker run --cpus` |
| `CONFIG_IP_VS=y` | load balancing (IPVS) for Docker Swarm |
| `# CONFIG_MODULE_SIG_FORCE is not set` | modules do not have to be signed |
| `# CONFIG_TRIM_UNUSED_KSYMS is not set` | all symbols are kept so that the modules fit |

### Checking whether the configuration was applied

`build-kernel.sh config` reports every line of the file that was not applied after `olddefconfig`, with
`!! not applied`. This is a **warning** you should take seriously, especially for Docker options. On the
running phone you can check the actual configuration:

```sh
scripts/phone-ssh.sh 'zcat /proc/config.gz | grep -E "CONFIG_NF_TABLES|CONFIG_CFS_BANDWIDTH"'
```

`setup-server.sh` uses the same check to reject a wrong kernel.

---

## 7.4 The module lists

Kernel modules are loaded in three stages (see [03-architecture.md](03-architecture.md#step-3-the-initramfs-initramfsinit)):

| List | Source | Content |
|---|---|---|
| stage 1 | original system `vendor_boot`, `modules.load` | UFS storage and all drivers that Android loads early |
| stage 2 | original system `vendor_dlkm`, `modules.load` | further hardware modules |
| stage 3 | `initramfs/modules.load.3` (our list) | modules that Android loads from `.rc` files, e.g. touch (`tui-common.ko`, `focaltech_tp.ko`, `ft3683g.ko`, `eph861.ko`) |
| blocklist | `initramfs/modules.blocklist` | `apu_top.ko`, `apusys.ko`, `sapu.ko`: the AI unit hangs the boot |

`depmod` creates the dependencies for the initramfs. In the rootfs the same applies, with the note from
[01-basics.md](01-basics.md): **never** run `depmod` over the whole module set when duplicate exports
exist. The kernel then picks the wrong modules. Outside the initramfs, modules are loaded with `insmod`
(for example `tetris-torch`).

---

## 7.5 Refreshing patches

If a patch no longer fits after an update of the foreign sources, proceed as follows:

1. **Read the error.** `fetch-sources.sh` reports which patch fails (`git apply`).
2. **Re-apply the patch in the source tree**, but with a three-way merge:
   ```sh
   git -C src/kernel-6.1 apply --3way patches/kernel/0001-net-backport-SO_PEERPIDFD.patch
   ```
   With a shallow clone (`--depth 1`) `--3way` sometimes fails because the old objects are missing. Then
   apply the patch by hand.
   If a conflict occurred, git marks the places in the source. Resolve them, then `git add` and
   `git diff --cached > patches/…/new.patch`.
3. **Check the change** before you replace the patch in the repository. The changes in the patch must have
   the same effect as before.
4. **Rebuild the kernel** and test the function.
5. **Keep the patch file name**, so that references in scripts and documentation stay correct.

A patch directory contains **only** our own changes. We do not copy whole foreign files into the repository.

### Creating a new patch of our own

```sh
cd src/kernel-6.1
# … make the changes in the source …
git diff > ../../patches/kernel/0002-short-description.patch
```

The patch file should have a description at the top (`Subject:` and text), so that others understand why the
change is needed. The existing files show the pattern.

The file names begin with a four-digit number (`0001`, `0002` …) and a short descriptive word. The order
matters, because `git apply` applies them in that order.

---

## 7.6 Kernel and module errors

| Symptom | Cause | Remedy |
|---|---|---|
| `disagrees about version of symbol` when loading a module | module and kernel come from different builds | `scripts/push-modules.sh` or `build-kernel.sh all` |
| `Invalid module format` | module for another architecture or another kernel | rebuild the module |
| `Unknown symbol in module` | a module it depends on is missing | check the module order (`modules.dep`) |
| boot hangs after `apusys` | the AI unit (APU) hangs the boot as soon as it is powered on | `apusys` is in `modules.blocklist` and in `rootfs/overlay/etc/modprobe.d/tetris-blocklist.conf` |
| touch does not respond after sleep | the bug that patch `0004` fixes | check the patch (`git -C src/kernel_device_modules-6.1 apply --check -R …`) |
| `!! not applied` during configuration | an option from `linux.config` was not applied | check the option's dependencies (`make menuconfig`), then `build-kernel.sh config` |

More errors and logs are in [09-troubleshooting.md](09-troubleshooting.md).
