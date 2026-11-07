# 3. Architecture

**🇬🇧 English** · [🇩🇪 Deutsch](de/03-architektur.md)

This chapter shows how the parts of the system work together. It describes the path from
power-on to the user interface, the role of the Android container and the two critical paths:
graphics and mobile network. Terms such as kernel or initramfs are explained in
[01-basics.md](01-basics.md).

## 3.1 Overview

```
             PC (x86-64, Arch Linux)
  scripts/ ──► out/ (kernel, modules, images, rootfs.simg)
                  │  fastboot
                  ▼
 ┌───────────────────────── Phone ───────────────────────────┐
 │ Preloader ─► LK (bootloader)                              │
 │               │ reads boot_b, vendor_boot_b, init_boot_b  │
 │               ▼                                           │
 │  kernel 6.1 (ours) + initramfs (busybox, modules, NCM)    │
 │               │ switch_root                               │
 │               ▼                                           │
 │  systemd (Arch Linux ARM on userdata)                     │
 │    ├─ Halium container "android" (lxc)                    │
 │    │    └─ Android HALs: gralloc, composer, modem,        │
 │    │       camera, GNSS, fingerprint, power               │
 │    ├─ Interface: Nothing shell or Phosh (compositor)      │
 │    ├─ Services: modem, Wi-Fi, Bluetooth, sensors, ...     │
 │    └─ Profile: server (Docker, Ollama), desktop (GNOME)   │
 └───────────────────────────────────────────────────────────┘
```

The most important idea: **the kernel and the system software are ours**. The hardware drivers,
which only exist in Android form, run in a **container**, and Linux talks to them through a bridge.

## 3.2 The boot chain in detail

### Step 1: preloader and LK

The chip starts a built-in **preloader** (`preloader_a/b`). It loads the bootloader **LK** from
`lk_a` or `lk_b`. LK selects the active A/B slot (its state is kept in the `misc` area, see
[02-hardware-and-partitions.md](02-hardware-and-partitions.md#23-the-ab-slots)) and loads the kernel
and ramdisk.

Peculiarities of this phone, documented in `initramfs/` and `scripts/build-debug-boot.sh`:

- LK ignores the ramdisk in the boot image. It **concatenates the ramdisks of `vendor_boot` and
  `init_boot`**.
- `init_boot` is only 8 MB. That is why our initramfs (busybox and all modules) lives in
  `vendor_boot` (64 MB). `init_boot_b` is flashed **empty** so that Android's own `/init` cannot
  interfere.
- LK has no `fastboot boot`. Every test therefore means flashing slot b and booting it.

### Step 2: the kernel

The kernel (`out/Image.lz4`, inside the boot image `debug-boot.img`) is Nothing's source with our
patches and the configuration from `kernel/linux.config` (see
[07-kernel-drivers-patches.md](07-kernel-drivers-patches.md)). It starts, recognises the hardware
through the **device tree** (the hardware description taken from `vendor_boot`) and hands control to
the initramfs.

### Step 3: the initramfs (`initramfs/init`)

The initramfs is a small file system in memory, made of busybox and our helpers. `initramfs/init`
is a shell script and runs as process 1. It does the following in order:

1. **Basic environment:** mounts `/proc`, `/sys`, `/dev`, `/tmp` and `configfs`.
2. **Log:** all messages go to `/tmp/init.log`. In addition they are stored via `pstore`
   (`/dev/pmsg0`, survives a warm reset) and, once the UFS storage is up, in the partition
   `nt_reserve2`. This is the basis of troubleshooting, see
   [09-troubleshooting.md](09-troubleshooting.md#93-finding-logs).
3. **Driver stage 1:** loads the modules listed in `modules.load` (the order of the original system,
   including the UFS driver).
4. **Driver stages 2 and 3:** loads `modules.load.2` (from the original system's list,
   `vendor_dlkm`) and `modules.load.3` (our own list in `initramfs/modules.load.3`). Modules listed in
   `initramfs/modules.blocklist` are skipped. One example is `apusys`, which hung the boot when the AI
   unit (APU) was powered on.
5. **Wi-Fi calibration:** the Wi-Fi driver `gen4m` only registers after it has received the NVRAM data
   from the partition `nvdata`. The script reads the file `APCFG/APRDEB/WIFI` and writes it in one go
   to `/dev/wmtWifi`. This has to happen before the screen is first unblanked.
6. **USB networking:** the initramfs sets up a USB gadget service (NCM). The phone is then reachable
   at `172.16.42.1`. A DHCP server (`udhcpd`) for the PC and a Telnet server (`telnetd` on port 23)
   run as an emergency shell.
7. **Screen:** the splash screen `tetris-splash` shows the Linux penguin Tux until the compositor or
   composer takes over the display.
8. **Emergency exit:** hold a **volume key** during the first 3 seconds and the phone stays in the
   initramfs (debug mode). The boot image `debug-vendor_boot-shell.img` sets the kernel option
   `tetris.debug=1`, so the phone always stays in the initramfs.
9. **Switch:** if there is an Arch Linux rootfs on `userdata` (ext4, containing
   `/usr/lib/systemd/systemd`) and no lock file `/.tetris-debug`, it switches to the rootfs with
   `switch_root`. There `systemd` starts as process 1.

The initramfs never stops, because process 1 must not exit (the kernel would panic). At the end it
runs an endless loop that writes a heartbeat to the log.

### Step 4: systemd in the rootfs

After `switch_root` the real system starts. The order of the most important services is defined in
the unit files (`rootfs/overlay/etc/systemd/system/`, details in
[06-rootfs-reference.md](06-rootfs-reference.md#63-systemd-services)):

- `tetris-growfs`: on first boot enlarges the file system to the full `userdata`.
- `tetris-cpuidle`: disables two deep CPU sleep states (`mcusysoff*` and `system-vcore`). Without
  this the phone would reset when there is neither USB nor display.
- `tetris-splash-stop`: ends the splash screen before the compositor starts.
- `tetris-android`: starts the Halium container (see below).
- `tetris-phosh` or `tetris-phosh.service` with `tetris-phosh-session`: starts the interface
  (Nothing shell via `/etc/tetris-shell`, otherwise Phosh).
- `tetris-boot-success`: reports a successful start to the bootloader.

## 3.3 The Halium container

### What it is

The container `android` is started with **LXC** (Linux Containers). It contains a minimal
**LuneOS Halium 16 Android** (GSI, "Generic System Image"), which lies inside the container as
`/system`. In addition it takes the hardware-specific partitions `vendor`, `odm` and `vendor_dlkm`
from the original system.

The configuration file is `rootfs/overlay/var/lib/lxc/android/config`. The most important points:

- **Own network namespace** (`lxc.net.0.type = empty`). Android sees neither the Wi-Fi nor the
  host's routes. It cannot take the network away from the host.
- **Restricted rights:** `lxc.cap.drop = mac_admin mac_override sys_boot`. Android may not reboot the
  host. An earlier failure had done exactly that when `vold` crashed.
- **No suspend:** `/sys/power` is mounted read-only. Android therefore cannot put the system to
  sleep, which does not work here.
- **Restricted init:** the file `/etc/tetris-android/vendor-init.allow` names the only vendor init
  files Android may run. The others would reconfigure USB, Wi-Fi and Bluetooth, and we do that
  ourselves.

### Startup in three steps

`tetris-android.service` runs the following in sequence:

1. **`ExecStartPre=/usr/local/bin/tetris-android-prepare`** prepares everything:
   - mounts the GSI image `system.img` read-only and places a writable **overlay** on top of it
     (file-system layering), because Android's init must create directories in `/`;
   - uses `tetris-map-super` to open `vendor`, `odm` and `vendor_dlkm` from `super` (slot a) as
     mapper devices and mounts them read-only. This is the Android method that Android's own first
     start uses;
   - removes a manifest file of the software codec and adds a header line to `netbpfload.rc` so that
     the library `libmeminfo` (API 36) starts;
   - sets up `binderfs` and creates the classic paths `/dev/binder`, `/dev/hwbinder` and
     `/dev/vndbinder` that libhybris expects;
   - binds the APEX modules (Android packages) in via `tetris-mount-apexes`.
2. **`ExecStart=lxc-start -F -n android`** starts the container in the foreground. Its log is in
   `/var/log/lxc-android.log`.
3. **`ExecStartPost=-/usr/local/bin/tetris-android-start-hals`** starts the HALs (Hardware
   Abstraction Layers, the Android interfaces to the hardware). The script waits until the service
   manager is ready, then starts `vendor.gralloc-v2` (memory for images), `mtkpower` (for the camera)
   and `pq` (picture quality). It only starts the composer (`vendor.hwcomposer-3-2`) when the GPU
   interface takes over the display. The reason: if the composer runs while another process holds the
   display, it spins in a loop at real-time priority. That has already triggered a watchdog reset.

### Who uses the container?

| Service | What it uses in the container |
|---|---|
| `tetris-modem` | `ccci_mdinit`, which starts the modem firmware (see 3.5) |
| `tetris-gnss` | GNSS HAL for GPS |
| `tetris-fp` | Goodix fingerprint HAL and TEE services |
| `tetris-camera` | camera HAL, Codec2 HAL, `mtkpower` (on demand, costs power) |
| `tetris-phosh` / Nothing shell (GPU mode) | gralloc and composer via libhybris |
| apps using the GPU (`/opt/libhybris`) | Mali driver via libhybris |

The container has its own processes, files and services. On the host only the results are visible:
device nodes, sockets and memory areas that were shared through bind mounts.

## 3.4 The GPU path (graphics)

The Mali GPU can only be operated with the Android driver. The path of an image:

```
App (GTK/Qt/SDL) ──EGL──► libhybris ──► Mali driver (/vendor, in the container)
                                              │
Compositor (nothing-wm or phoc, wlroots       ▼
  with our hwcomposer backend) ◄── gralloc (image memory)
        │                                     │
        └──────► composer HAL (Android) ──────► display (DRM)
```

- **libhybris** (`/opt/libhybris`) loads Android libraries so that normal programs can use them. The
  building blocks are in `patches/libhybris/`.
- **wlroots 0.20** with our **hwcomposer backend** (`patches/wlroots/0002-…`). It hands images to the
  Android composer instead of scanning out through DRM itself.
- **Configuration** for apps: `/etc/tetris-android/hybris.env` and `/etc/tetris-android/gpu-shell`.

### Decision at startup: GPU or software?

`tetris-phosh-select` runs before every start of the interface (`ExecStartPre`):

| Condition | Result |
|---|---|
| file `/run/tetris-phosh/force-sw` exists | software (fixed for this start) |
| crash marker `/var/lib/tetris-android/gpu-attempt` exists | software. The last GPU start did not run well for 90 seconds |
| `/etc/tetris-android/gpu-shell` exists and the composer runs | **GPU** |
| otherwise | software |

In **GPU mode** the script sets the marker and starts a 90-second timer. Only when the timer has
removed the marker does the start count as successful. If the GPU session crashes before that, the
marker stays and the next start falls back to software.

In **software mode** the script stops the composer service so that the compositor can open the
display itself through DRM.

The crash guard logic is described in [09-troubleshooting.md](09-troubleshooting.md#99-crash-guard).

### The desktop variant

In the desktop profile GNOME runs without a display (`gnome-headless-session@alarm`). The GNOME
shell uses the GPU through a second build variant of libhybris as a **glvnd EGL provider**
(`/opt/libhybris-glvnd`). The file `/usr/local/bin/tetris-gnome-shell` and the environment variables in
`user-environment-generators/60-tetris-hybris-gl` control this. Apps that crash with libhybris are
listed in `/etc/tetris-software-gl.list` and run on the processor through Mesa.

## 3.5 The modem path (mobile network)

```
ModemManager (host) ──AT commands──► /dev/ttyMD0 (pair from tetris_mdtty)
                                            │
          tetris-mdtty-bridge ◄── tetris_mdtty.ko (kernel module, creates the tty pair
          ttyMD0 + ttyMD1; the bridge owns ttyMD1)
                                            │
          ccci_mdinit (in the container) ──► modem CPU (firmware from modem_a/b)
                                            │
          plugin libmm-plugin-tetris.so ◄── ModemManager loads it (patches/modemmanager/tetris)
```

- **`tetris-modem.service`** starts the bridge `tetris-mdtty-bridge` after the container. The modem
  needs about 35 seconds before it answers. That is why the order in the unit file is deliberate
  (`After=` without `Before=ModemManager`).
- **`tetris-android-start-modem`** waits until `ccci_mdinit` has started the modem CPU (recognisable
  by the value `md1:4` in `/sys/kernel/ccci/boot`). The firmware has already been loaded by the
  bootloader. `ccci_mdinit` provides the NVRAM file system that the modem CPU needs. The partitions
  holding IMEI and radio calibration are only mounted **inside** the container, and only by name, so
  that the host system does not touch them.
- **Network detection** runs through udev rules (`78-tetris-modem.rules`): `ttyMD0` is marked as the
  AT port for ModemManager, `ttyMD1` is reserved for the bridge, and the data network `ccmni1` is only
  released once `ttyMD0` exists.
- **The plugin** `patches/modemmanager/tetris` makes the modem a device for ModemManager. It uses the
  AT commands and the data connection of the MediaTek modem. The connection works with IPv4 and IPv6,
  without configuring an access point name (APN).
- **Calls are missing.** Outgoing calls connect, but without sound. Incoming calls run over a
  MediaTek-specific channel (MIPC) that ModemManager does not know. The experiments are in
  `rootfs/src/tetris-mipc` and `rootfs/src/tetris-voicecall`; they are not part of the build.

## 3.6 Other subsystems

| Part | How it works | Where |
|---|---|---|
| **Wi-Fi** | driver `gen4m`, registers with `conninfra` (combo chip). NetworkManager manages the connection. Power saving off in the server profile | `tetris-wlan`, `tetris-wlan-on`, `overlay/usr/lib/NetworkManager/conf.d/` |
| **Bluetooth** | the MediaTek device `/dev/stpbt` is attached to `/dev/vhci` by `tetris-bt-bridge`. BlueZ then talks to the chip | `tetris-bt-start`, `rootfs/src/tetris-bt-bridge.c` |
| **Sensors** | `tetris-sensors` reads the SCP sensor hub and provides the D-Bus service `net.hadess.SensorProxy`. The Nothing shell rotates the screen with it | `rootfs/src/tetris-sensors/` |
| **Audio** | ALSA with a UCM profile for MT6878 and MT6369. Speaker and microphone work | `overlay/usr/share/alsa/ucm2/conf.d/mt6878-mt6369/` |
| **Vibration** | `tetris-vibra` turns the motor into a force-feedback device for feedbackd | `rootfs/src/tetris-vibra.c` |
| **Torch** | `tetris-torch` drives the LED as a flash class device (`white:torch`) via the `uleds` module | `rootfs/src/tetris-torch/` |
| **Camera** | camera HAL in the container. The app `Kamera` (`tetris-camera-app`) uses the Camera2 NDK via libhybris | `rootfs/src/tetris-camera-app/` |
| **Fingerprint** | `tetris-fp` (enrolment, sensor queries) and `tetris-fpd` (unlocking). The sensor sits under the glass; the area is marked by `tetris-fp-overlay` | `rootfs/src/tetris-fp/`, `rootfs/overlay/usr/local/bin/tetris-fp*` |
| **GPS** | `tetris-gnss` reads NMEA from the GNSS HAL and feeds geoclue. The receiver is only on while an app asks | `rootfs/src/tetris-gnss/` |
| **Thermal** | `tetris-thermal` throttles in three steps (70/80/90 °C SoC) and powers off at 100 °C | `overlay/usr/local/bin/tetris-thermal` |
| **Display and touch** | patch `0003` (high-brightness mode of the panel) tells the fingerprint service when a scan runs. Patch `0004` makes the touch controller respond again after sleep | `patches/kernel_device_modules/` |

## 3.7 Profiles in the architecture

A profile is a collection of services, overlay files and start targets. Switching only changes the
start order and enables or disables services. The files stay the same. That is why you can switch
between profiles at any time.

| Profile | Overlay | Start target | Display | Notes |
|---|---|---|---|---|
| Phone | `overlay` | `graphical.target` | on, interface | default |
| Server | `overlay` + `overlay-server` | `multi-user.target` | off (`tetris-display-off`) | Docker, Ollama, Avahi (`tetris.local`), zram |
| Desktop | + `overlay-desktop` | `graphical.target` (GDM) | off, GNOME over RDP | GNOME session without display |

The switching in detail is described in [08-profiles.md](08-profiles.md).

## 3.8 Where files end up

| Location on the phone | Origin | Purpose |
|---|---|---|
| `/` (ext4 on `userdata`) | `out/rootfs.simg`, then updates | Arch Linux system |
| `/usr/lib/modules/<version>/` | `scripts/build-rootfs.sh` or `push-modules.sh` | kernel modules |
| `/var/lib/lxc/android/` | `setup-phone.sh` (`system.img`) | container configuration and GSI |
| `/var/lib/tetris-android/` | `tetris-android-prepare` | overlay, `vendor` mount, crash marker |
| `/etc/tetris-android/` | overlay | container init list, GPU switch, environment variables |
| `/opt/libhybris/`, `/opt/libhybris-glvnd/` | `pkgbuilds/build-libhybris.sh` | Android bridge for apps |
| `/opt/llama.cpp/` | `pkgbuilds/build-llama-vulkan.sh` | AI runtime on the GPU (experimental) |
| `/var/lib/tetris-mode/` | `tetris-boot-as`, `tetris-mode` | one-boot profile (`once`) |
| `/run/…` | systemd and generators | everything that only applies to this boot |
