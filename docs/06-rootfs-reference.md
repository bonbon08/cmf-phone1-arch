# 6. Rootfs reference: files and services on the phone

**🇬🇧 English** · [🇩🇪 Deutsch](de/06-rootfs-referenz.md)

The **rootfs** is the file system of the Arch Linux system on the phone. This chapter explains what our
project adds to it or changes. The base is an official Arch Linux ARM image. Everything we add lives in the
directory `rootfs/`.

## 6.1 What is an overlay?

An **overlay** is a directory tree whose contents are copied into the rootfs
(`cp -a rootfs/overlay/. <rootfs>/`). Files with the same name overwrite the original. The project has three
overlays that are combined depending on the profile:

| Directory | When copied | Content (files) |
|---|---|---|
| `rootfs/overlay/` | always | phone profile, interface, services, hardware adjustments |
| `rootfs/overlay-server/` | profile `server` and `desktop` | Docker, Ollama, SSH, zram, `tetris-mode` |
| `rootfs/overlay-desktop/` | profile `desktop` only | GDM, RDP, GNOME start, Mesa list |

Together they make about 108 files. `build-rootfs.sh` sets the owners and permissions (everything
`root:root`, except the files for the user `alarm`).

The directory structure inside an overlay matches the phone's file system. The file
`rootfs/overlay/usr/local/bin/tetris-mode` therefore ends up on the phone at `/usr/local/bin/tetris-mode`.

On a running phone a single overlay can be applied like this (as the scripts `setup-server.sh` and
`setup-desktop.sh` do):

```sh
tar -C rootfs/overlay-server --owner=0 --group=0 -cf - . | scripts/phone-ssh.sh 'cat > /tmp/o.tar'
scripts/phone-ssh.sh "echo alarm | sudo -S -p '' sh -c 'tar -C / --no-overwrite-dir -xf /tmp/o.tar && systemctl daemon-reload'"
```

---

## 6.2 Structure of the overlay

### `etc/`: configuration

| Path | Purpose |
|---|---|
| `etc/hostname`, `etc/issue` | name of the phone and login text |
| `etc/sudoers.d/10-wheel` | members of the group `wheel` may use `sudo` |
| `etc/modprobe.d/tetris-blocklist.conf` | blocks `apusys` and related modules (hangs when the AI unit powers on) |
| `etc/tetris-android/hybris.env` | environment for programs that access the Mali GPU through libhybris |
| `etc/tetris-android/vendor-init.allow` | list of vendor init files the container may run |
| `etc/tetris-android/init/tetris-fp.rc`, `tetris-gnss.rc` | our own init files for fingerprint and GPS in the container |
| `etc/udev/rules.d/78-tetris-modem.rules` | marks `ttyMD0` as the AT port for ModemManager, `ccmni1` as the data network |
| `etc/udev/rules.d/78-tetris-camera.rules` | hands the camera nodes to the Android camera service in the container |
| `etc/udev/rules.d/78-tetris-gnss.rules` | hands the GPS nodes to the Android user `gps` (UID 1021) |
| `etc/udev/rules.d/78-tetris-usb-role.rules` | USB role follows the cable |
| `etc/udev/rules.d/79-tetris-vibra.rules` | the vibration device belongs to `feedbackd` |
| `etc/NetworkManager/conf.d/10-unmanaged-usb0.conf` | NetworkManager leaves the USB network (`usb0`) alone. `systemd-networkd` manages it |
| `etc/NetworkManager/system-connections/mobile-data.nmconnection` | mobile data for any SIM, **without** an APN. The modem uses the access point the network offers |
| `etc/systemd/network/50-usb0.network` | address `172.16.42.1` for the USB network |
| `etc/systemd/logind.conf.d/50-tetris.conf` | the power key and idle do not trigger sleep |
| `etc/systemd/sleep.conf.d/50-tetris-nosuspend.conf` | **sleep is forbidden**: suspend freezes the system because it does not wake up |
| `etc/systemd/system-generators/tetris-mode-once` | generator for the one-boot start in the server or desktop profile (see [08](08-profiles.md)) |
| `etc/polkit-1/rules.d/50-tetris-boot-once.rules` | allows the app Startmodus to reboot into another profile |
| `etc/polkit-1/rules.d/50-tetris-camera.rules` | allows the camera app to start and stop the camera stack |
| `etc/geoclue/conf.d/90-tetris-gnss.conf` | GPS source for geoclue |
| `etc/geoclue/conf.d/91-tetris-beacondb.conf` | location via Wi-Fi access points (BeaconDB) |
| `etc/UPower/UPower.conf.d/50-tetris.conf` | battery display: the battery algorithm runs in the kernel |
| `etc/skel/.config/gtk-3.0/gtk.css` | marks the position of the fingerprint sensor on the lock screen (also for new users) |
| `etc/systemd/system/ModemManager.service.d/tetris-shutdown.conf` | at shutdown the container stops first, then ModemManager |
| `etc/systemd/user/sm.puri.Chatty-daemon.service.d/tetris-software-render.conf` | Chatty renders on the processor (otherwise it crashes after an incoming SMS) |
| `etc/systemd/user/calls-daemon.service.d/tetris-software-render.conf` | same reason for `calls` |

### `usr/`: programs, libraries, defaults

| Path | Purpose |
|---|---|
| `usr/local/bin/…` | all `tetris-*` commands, see 6.4 |
| `usr/lib/firefox/defaults/pref/tetris-phone.js` | Firefox: no hardware video decoding, so it does not probe ~170 devices (start from 15 s to 2 s) |
| `usr/lib/NetworkManager/conf.d/50-tetris-wifi-powersave.conf` | Wi-Fi power saving on. Saves about a third of the current in standby |
| `usr/share/alsa/ucm2/conf.d/mt6878-mt6369/` | audio profile for the MT6369 codec and the aw882xx speaker amplifier |
| `usr/share/applications/eu.tetris.Camera.desktop` | launcher for the camera app |
| `usr/share/applications/eu.tetris.Modes.desktop` | launcher for the app Startmodus |
| `usr/local/share/dbus-1/services/org.gnome.Maps.service` | overrides the D-Bus starter of GNOME Maps (reason is in the header comment of the file) |

### `var/`: runtime data and container

| Path | Purpose |
|---|---|
| `var/lib/lxc/android/config` | configuration of the Halium container (see [03](03-architecture.md#33-the-halium-container)) |

---

## 6.3 systemd services

The services live in `etc/systemd/system/`, `etc/systemd/user/` and in the profile overlays. The columns
"Starts after" and "Starts before" show the most important ordering relationships. All services with the
prefix `tetris-` belong to this project.

### System-wide services (phone profile)

| Service | Description | Starts after | Activated by |
|---|---|---|---|
| `tetris-growfs` | enlarges the file system to the full `userdata` on first boot | `local-fs.target`, before `sysinit.target` | `sysinit.target` |
| `tetris-cpuidle` | disables two deep CPU sleep states (`mcusysoff*`, `system-vcore`) | `sysinit.target` | `basic.target` |
| `tetris-splash-stop` | ends the Tux splash from the initramfs | `sysinit.target` | `basic.target` |
| `tetris-android` | Halium container: `prepare`, `lxc-start`, HALs | `local-fs.target` | `multi-user.target` |
| `tetris-phosh` | interface (`tetris-phosh-session`), autologin `alarm` on tty7 | D-Bus, ModemManager, `tetris-android` | `graphical.target` |
| `tetris-boot-success` | reports a successful start to the bootloader (retry counter in `misc`) | `tetris-phosh` | `graphical.target` |
| `tetris-gpu-clean-shutdown` | clears the GPU crash marker on a clean shutdown | `tetris-phosh` | `multi-user.target` |
| `tetris-wlan` | switches on the Wi-Fi chip after `conninfra` has calibrated | `tetris-phosh` | `multi-user.target` |
| `tetris-bt-bridge` | connects `/dev/stpbt` to `/dev/vhci` for BlueZ | `tetris-wlan`, before `bluetooth` | `multi-user.target` |
| `tetris-modem` | starts the AT bridge `tetris-mdtty-bridge` after the container | `tetris-android` | `multi-user.target` |
| `tetris-sensors` | D-Bus service `net.hadess.SensorProxy` (SCP sensor hub) | – | `multi-user.target` |
| `tetris-vibra` | vibration motor as a force-feedback device | – | `multi-user.target` |
| `tetris-torch` | torch as an LED class (`white:torch`), loaded via `insmod` | – | `multi-user.target` |
| `tetris-gnss` | GPS NMEA from the container for geoclue | `tetris-android`, before `geoclue` | `multi-user.target` |
| `tetris-fp` | fingerprint HAL in the container (TEE, Goodix) | `tetris-android` | `multi-user.target` |
| `tetris-fpd` | unlocks the lock screen with the fingerprint | `tetris-fp` | `multi-user.target` |
| `tetris-camera` | camera stack in the container (`start`/`stop`, on demand) | `tetris-android` | – (started by the app) |
| `tetris-usb-role` | USB data role after the cable | `systemd-udev-trigger` | `multi-user.target` |
| `tetris-thermal` | thermal protection: throttles CPU/GPU and powers off | `sysinit.target` | `multi-user.target` |
| `tetris-shell@` | template: `tetris-shell@nothing` or `@phosh` sets the interface for the next start | – | by hand or the app |
| `tetris-boot-once@` | template: start once in the profile `server` or `desktop` | – | app Startmodus |
| `tetris-mode-once-done` | deletes the file for the one-boot start after the boot | `local-fs.target` | `multi-user.target` (via the server profile) |

### User services (per user)

| Service | Description | Activated by |
|---|---|---|
| `tetris-fp-overlay` | light spot for the fingerprint sensor on the lock screen (started by `tetris-fpd`) | `tetris-fpd` |
| `tetris-screenshot` | screenshot when both volume keys are pressed | `graphical-session.target` |

### Services of the server profile (`overlay-server`)

| Service | Description |
|---|---|
| `tetris-ai` | starts the Ollama containers from `/opt/tetris-server/ai/compose.yaml` (waits for Docker and the network) |
| `tetris-display-off` | switches the panel off (DPMS) and holds the DRM master while no compositor runs |

Services such as `docker`, `avahi-daemon` and `systemd-oomd` are enabled by `tetris-mode server` itself.

### Services of the desktop profile (`overlay-desktop`)

| File | Description |
|---|---|
| `etc/systemd/system/gnome-headless-session@.service.d/tetris-restart.conf` | restarts the GNOME session when it ends. The desktop is the only interface |
| `etc/systemd/user/org.gnome.Shell@user.service.d/tetris-gpu.conf` | GNOME Shell on the Mali GPU (via `tetris-gnome-shell`) |
| `etc/systemd/user/gnome-remote-desktop-headless.service.d/tetris.conf` | RDP only in the desktop profile |
| `etc/systemd/user/tetris-rdp-setup.service` | first login: creates the certificate and password for RDP |
| `etc/systemd/user-environment-generators/60-tetris-hybris-gl` | puts the glvnd EGL variant of libhybris in front of Mesa |

### Order of services at startup (simplified)

```
local-fs ─► tetris-growfs ─► sysinit ─► tetris-cpuidle ─► basic ─► tetris-splash-stop
                                                                       │
                       tetris-android (container + HALs) ◄─────────────┤
                         │                    │                        │
          tetris-modem ◄─┘                    ▼                        ▼
          tetris-gnss, tetris-fp ──► tetris-fpd            tetris-phosh (interface)
                                                                       │
                                               tetris-wlan ─► tetris-bt-bridge ─► bluetooth
                                                                       │
                                                       tetris-boot-success (graphical.target)
```

The order is derived from the unit files (`After=`, `Before=`, `Requires=`). The exact sequence of a
start can be shown on the phone with `systemd-analyze critical-chain`.

---

## 6.4 Own commands (`usr/local/bin/`)

### Start, profiles and system management

| Command | Language | Description |
|---|---|---|
| `tetris-phosh-session` | sh | starts the interface that `tetris-phosh-select` chose (Phosh or Nothing) |
| `tetris-phosh-select` | sh | decides GPU or software and runs the crash guard (see [03](03-architecture.md#decision-at-startup-gpu-or-software)) |
| `tetris-phosh-session-hwc` | sh | Phosh variant with the Android composer and the Mali GPU |
| `tetris-nothing-session` | sh | session of the Nothing shell; started by `tetris-phosh.service` when `/etc/tetris-shell` says `nothing` |
| `tetris-modes` | Python (GTK) | app **Startmodus**: start once as server or desktop, choose the interface |
| `tetris-boot-as` | sh | called from the apps: one-boot start in the server or desktop profile, then reboot |
| `tetris-boot-once` | sh | part of `tetris-boot-as`, as root: writes `/var/lib/tetris-mode/once` and reboots |
| `tetris-boot-success` | Python | sets the A/B state in `misc` to "successful" (CRC-protected block at offset `0x800`) |
| `tetris-thermal` | sh | thermal protection: three throttling steps, power off at 100 °C SoC, 58 °C board or 60 °C battery |
| `tetris-usb-role` | sh | sets the USB data role (device when a cable is plugged in) |
| `tetris-map-super` | Python | creates the Android partitions from `super` as mapper devices (`dm-linear`) |
| `tetris-mount-apexes` | Python | binds the APEX modules of the Halium system into the container |
| `tetris-android-prepare` | sh | preparation of the container (see [03](03-architecture.md#33-the-halium-container)) |
| `tetris-android-start-hals` | sh | starts the HALs in the container and waits for them |
| `tetris-android-start-modem` | sh | starts the modem CPU via `ccci_mdinit` |
| `tetris-wlan-on` | sh | switches Wi-Fi on once the calibration is finished |
| `tetris-bt-start` | sh | starts the Bluetooth bridge. The device address comes from `nvdata` (`APCFG/APRDEB/BT_Addr`) |

### Hardware helpers

| Command | Language | Description |
|---|---|---|
| `tetris-camera` | sh | starts or stops the camera stack in the container when the app needs it |
| `tetris-camera-app` | sh | starts the camera app on libhybris and stops the stack when it quits |
| `tetris-gnss-hal` | sh | starts the GPS services in the container and waits for the GNSS HAL |
| `tetris-fp-hal` | sh | starts the fingerprint stack in the container (TEE, then the Goodix HAL) |
| `tetris-fpd` | sh | unlocks with the fingerprint. Only runs when a finger is enrolled |
| `tetris-fp-overlay` | Python (GTK 3) | shows a white circle over the sensor as a layer-shell window above the lock screen. The bright spot lights the finger for the optical sensor |
| `tetris-screenshot` | Python | screenshot when both volume keys are pressed. Saves to `~/Pictures/Screenshots` |
| `prismlauncher` | sh | starts Prism Launcher with a clean graphics environment (SDL on Wayland, Mesa software OpenGL) |

### Desktop profile (`overlay-desktop/usr/local/bin/`)

| Command | Description |
|---|---|
| `tetris-gnome-shell` | starts GNOME Shell on the Mali GPU. Falls back to software if it crashes within 60 s |
| `tetris-software-gl` | runs a program through Mesa instead of libhybris. For apps that crash on libhybris |
| `tetris-software-gl-desktop-files` | rewrites the launchers of the apps in `tetris-software-gl.list` so that they run through `tetris-software-gl`. The pacman hook `tetris-software-gl.hook` calls it after every change to `/usr/share/applications/*.desktop` |
| `firefox` | starts Firefox through `tetris-software-gl` (Mesa), because WebRender crashes on libhybris |
| `tetris-rdp-setup` | on first login creates the TLS certificate and the RDP password in `~/.config/tetris-rdp-password` |

### Server profile (`overlay-server/usr/local/bin/`)

| Command | Description |
|---|---|
| `tetris-mode` | switches the profile permanently: `server`, `desktop` or `phone` (`--with-modem` keeps the mobile connection). Without an argument it shows the current profile |
| `tetris-llama` | starts llama.cpp on the GPU via libhybris (Vulkan). Needs the container for `/vendor` |
| `tetris-ollama-pull` | pulls an Ollama model and sets it to 4 threads (about 20 % faster than the default of 8 threads) |

---

## 6.5 Own programs (`rootfs/src/`)

These programs are either installed **in the rootfs** or built **on the phone**.

| Directory or file | Language | Built by | Purpose |
|---|---|---|---|
| `tetris-bt-bridge.c` | C | `build-rootfs.sh` | `/dev/stpbt` ↔ `/dev/vhci` for BlueZ |
| `tetris-mdtty-bridge.c` | C | `build-rootfs.sh` | copies between the modem's AT channel (`/dev/ttyC0`) and `ttyMD1`, so that ModemManager sees the modem on `ttyMD0` |
| `tetris-mdtty/tetris_mdtty.c` | C (kernel module) | `build-kernel.sh ext` | creates the tty pair `ttyMD0`/`ttyMD1` |
| `tetris-vibra.c` | C | `build-rootfs.sh` | vibration motor as a force-feedback device (uinput) |
| `tetris-torch/tetris-torch.c` | C | `build-rootfs.sh` | controls the torch |
| `tetris-torch/uleds.c` | C (kernel module) | `build-kernel.sh ext` | userspace LEDs (`uleds`), because `CONFIG_LEDS_USER` is off |
| `tetris-display-off.c` | C | `build-rootfs.sh` | switches the panel off (DRM DPMS) and holds the DRM master |
| `tetris-sensors/tetris-sensors.c`, `hf_abi.h` | C (GIO) | `build-rootfs.sh` | sensor hub, D-Bus service `net.hadess.SensorProxy` |
| `tetris-fp/tetris-fp.c` | C | `setup-phone.sh` on the phone | fingerprint client (enrolment, unlocking) |
| `tetris-gnss/tetris-gnss.c` | C | `setup-phone.sh` on the phone | GPS client: NMEA for geoclue |
| `tetris-camera-app/` | C with NDK | `setup-phone.sh` on the phone | camera app (`app.c`), NDK access (`ndkcam.c`), test tool (`probe.c`), Makefile |
| `tetris-novtswitch/tetris_novtswitch.c` | C (kernel module) | only `push-modules.sh`, if built | prevents the switch to a text console on sleep. Otherwise the display would turn on at every wake-up |
| `tetris-kmsgnet/tetris_kmsgnet.c` | C (kernel module) | not in the build | debug aid: sends the kernel log over USB to the PC via UDP, even when journald is frozen by a suspend hang |
| `tetris-audiohal-shim/tetris_audiohal_shim.cpp` | C++ | not in the build | experiment: `LD_PRELOAD` shim for the audio service in the container. It keeps the binder device paths separate |
| `tetris-voicecall/tetris-voicecall.c` | C | not in the build | experiment: loads MediaTek's audio HAL via libhybris and switches on call mode |
| `tetris-mipc/` | C | not in the build | experiment for incoming calls. `tetris-mipc-calld.c` approves them over the MIPC channel. `mipc-ids.c` reads the message IDs from the vendor library |
| `hwc-buffer-probe.c` | C | not in the build | prototype of the composer path: render gralloc buffers and hand them to the Android composer |
| `hybris-gles-probe.c` | C | not in the build | offscreen test: draws a triangle on the Mali GPU via libhybris without using the display |

The experiments without a build are kept in the repository on purpose, so the state of the work stays
traceable. Files in `rootfs/src/` with the endings `.ko`, `.o` or `Module.symvers` are build results. They
are listed in `.gitignore`.

### Build rules

- **Cross-compilation:** the Android clang (`toolchain/clang-r487747c/bin/clang`) with
  `--target=aarch64-linux-gnu --sysroot=<rootfs>` and `-fuse-ld=lld`. The result runs on the phone with its
  own glibc.
- **Kernel modules:** against the kernel in `out/kernel` with `make M=… modules`.

---

## 6.6 Configuration files in detail

### Halium container (`var/lib/lxc/android/config`)

The full text is in the file. The sections:

| Section | Purpose |
|---|---|
| `lxc.rootfs.path` | directory of the container (`/var/lib/lxc/android/rootfs`, the overlay) |
| `lxc.cap.drop` | removes rights: `mac_admin`, `mac_override`, `sys_boot` |
| `lxc.net.0.type = empty` | own network namespace without a connection |
| `lxc.autodev = 0` | no automatic device list. The nodes are bound one by one |
| `lxc.mount.entry` | bind mounts: `/dev/binderfs`, `/dev/__properties__`, `/dev/socket`, `/dev/kmsg`, `/apex`, `/vendor`, `/odm`, `/vendor_dlkm`, and `/sys/power` (read-only) |

### Vendor init list (`etc/tetris-android/vendor-init.allow`)

Each line names a file from `/vendor/etc/init`. The file is copied into the container if it is listed.
The benefit: Android only gets the services we need.

Why this is necessary: the original's vendor init files would reconfigure USB, Wi-Fi and Bluetooth, load
modules and start the audio service. Our host system does those things itself.

### Mesa list (`overlay-desktop/etc/tetris-software-gl.list`)

One line per program that should run on Mesa. The file is read when the desktop session starts and by the
pacman hook (`tetris-software-gl.hook`).

---

## 6.7 What you may change and what you should not

| Change | Safe? | Note |
|---|---|---|
| add new `tetris-*` scripts to the overlay | yes | do not forget `chmod 755`, set the owner to `root:root` |
| disable a service: `sudo systemctl disable --now <service>` | yes | the change lasts until the next `build-rootfs.sh` or `tetris-mode` |
| change `tetris-mode` or `tetris-phosh-select` | with care | these files control the start. A mistake can keep the phone in the initramfs. Test with `tetris.debug=1` |
| extend `vendor-init.allow` | with care | Android services can switch USB or the battery back on |
| update `ModemManager` or `wlroots` | only with a rebuild | the plugin and `wlroots0.20` must be rebuilt (see [05](05-script-reference.md#53-scripts-on-the-phone-pkgbuilds)) |
| `/etc/tetris-shell`, `/etc/tetris-android/gpu-shell` | safe | they are the switches for interface and GPU |
