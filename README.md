# Arch Linux ARM + Phosh on the CMF Phone 1

Arch Linux ARM with the Nothing shell (or Phosh) on the **CMF Phone 1** by Nothing
(codename **Tetris**, MediaTek Dimensity 7300 / MT6878). It runs on Nothing's own
Android kernel (6.1) with the MediaTek vendor drivers. The Android hardware
abstraction layers for GPU, modem and camera run in a small Halium container and are
used through [libhybris](https://github.com/libhybris/libhybris).

> **Status: early preview, for people who know fastboot and Linux.** Installing wipes
> the phone's data. Standby does not work yet, so battery life is short. Read
> [Risks](#risks) before you start.

> **Documentation:** a detailed technical description for beginners and developers is in
> [docs/](docs/README.md): basics, architecture, every script, troubleshooting and glossary.
> **🇬🇧 English** (default) · [🇩🇪 Deutsch](docs/de/README.md)

## What works

| Area | Status | Notes |
|---|---|---|
| Boot, display, touch | ✅ | Nothing kernel 6.1 + vendor modules, Arch on `userdata`, Android stays on slot a |
| Phosh on the GPU | ✅ | Mali-G615 through our wlroots **hwcomposer backend** (libhybris); software fallback built in |
| GPU apps | ✅ | GTK apps render on the Mali GPU via libhybris EGL (`android_wlegl`) |
| Auto-rotation | ✅ | Touch follows the rotation |
| Sensors | ✅ | Accelerometer, light, proximity: read straight from the SCP sensor hub (`tetris-sensors`, drop-in for iio-sensor-proxy) |
| WLAN | ✅ | MediaTek gen4m driver |
| Bluetooth | ✅ | |
| Battery / charging | ✅ | |
| Audio (speaker, mic) | ✅ | UCM config. The phone has no 3.5 mm jack; analog headsets via a passive USB-C adapter should be detected (codec "Headset Jack"), untested |
| Vibration | ✅ | RT6010 haptic driver, a mainline-style port in `mainline/`. It claims the chip at boot and exposes the motor as a force-feedback input device, which feedbackd uses directly. The old `tetris-vibra` bridge only runs if the port is not built |
| Torch | ✅ | Phosh quick setting (`tetris-torch`, flash LED via uleds) |
| Screenshots | ✅ | Hold both volume keys (`tetris-screenshot`, saved to `~/Pictures/Screenshots`) |
| Cameras | ✅ | Main (50 MP), front, and the rear IR depth sensor; own app **Kamera** with an IR false-colour mode |
| Mobile data (LTE) | ✅ | ModemManager with our plugin for the MediaTek modem, IPv4 + IPv6; works without configuring an APN |
| SIM PIN, SMS receive | ✅ | Chats (Chatty) |
| SMS send | ✅ | Chats (Chatty) |
| Calls | ❌ | Outgoing calls connect, but without audio; incoming calls exist only on MediaTek's MIPC channel, not on AT, so ModemManager never sees them. Details in `rootfs/src/tetris-mipc`. Help welcome |
| Standby (suspend) | ❌ | The SoC hangs in deep sleep. The screen can still turn off, but the CPU stays awake |
| GPS | 🟡 | MediaTek GNSS HAL + `tetris-gnss` feed geoclue (receiver on only while an app asks). Satellites are found, a first fix not confirmed yet |
| Fingerprint | ✅ | Under-display sensor unlocks the Phosh lock screen (`tetris-fpd`), tested with the vendor touch driver; with the mainline touch port see the roadmap. Enroll with the app "Fingerabdruck einrichten" (hints, touches left) or `sudo tetris-fp enroll`. Goodix HAL + Trustonic TEE, two kernel patches. The whole screen brightens while scanning. The phone has no NFC |

## How it is put together

- **Kernel:** Nothing's open source kernel and MediaTek vendor modules
  ([NothingOSS](https://github.com/NothingOSS)), built with Android's clang and plain
  `make`. There are a few small patches in `patches/kernel*`.
- **Boot:** The kernel goes to `boot_b`. A small busybox initramfs goes into
  `vendor_boot_b`; it loads the vendor modules in stock order and mounts the Arch rootfs
  from `userdata`. Slot a keeps Nothing OS. The bootloader has no `fastboot boot`, so
  every test means flashing slot b.
- **Halium container (LXC):** a LuneOS Halium 16 GSI runs the vendor HALs: gralloc,
  composer, modem (`ccci_mdinit`), camera, power, codec2 and GNSS. It runs in its own network
  namespace and cannot suspend the host.
- **GPU:** a wlroots 0.20 **hwcomposer backend**, a gralloc allocator and
  `android_wlegl` client buffers (`patches/wlroots/`). phoc composites on the Mali GPU
  and hands frames to Android's composer HAL.
- **Modem:** a tty bridge kernel module (`tetris_mdtty`) and a ModemManager plugin for
  MediaTek's AT and data interface (`patches/modemmanager/tetris`).
- **Own services** (`rootfs/overlay`, `rootfs/src`): Phosh session with GPU/software
  selection and crash guard, `tetris-sensors`, `tetris-vibra`, `tetris-torch`, the
  camera stack (`tetris-camera`), the **Kamera** app (Camera2 NDK via libhybris) and
  `tetris-gnss` (GPS NMEA for geoclue).

## Building and installing

You need an x86_64 Linux PC (developed on Arch Linux) and the phone with an
**unlocked bootloader**.

### Graphical installer (recommended)

`installer/tetris_installer.py` runs the scripts below in the right order and shows
progress and the log in a small window. It checks the tools and input files first,
asks for confirmation before anything that deletes data (type `LOESCHEN`) and keeps the
sudo password in memory only.

```sh
sudo pacman -S tk                          # Tkinter, once
python3 installer/tetris_installer.py      # from anywhere in the repository
```

Options: `--dry-run` (window, runs nothing), `--print-plan --mode full --profile phone
--halium <system.img>` (prints the steps). Details: [installer/README.md](installer/README.md).

The steps by hand are below.

### Inputs you have to get yourself

These are not in the repository: they are proprietary, or too large to include.

| Path | What |
|---|---|
| `firmware/Tetris_B4.1-260928-1140/images/` | Stock Nothing OS **B4.1** fastboot images (`vendor_boot.img`, `vendor.img`, ...) for Tetris |
| `toolchain/clang-r487747c/` | Android clang **r487747c** (AOSP `prebuilts/clang/host/linux-x86`), the same version Nothing uses |
| `downloads/alarm/` | `ArchLinuxARM-aarch64-latest.tar.gz` and the static `busybox` from Arch Linux ARM's `busybox` package (unpack it so that `downloads/alarm/usr/bin/busybox` exists) |
| Halium image | LuneOS `halium-luneos-16.0-20261017-1-halium_arm64` from [webOS-ports/halium-images](https://github.com/webOS-ports/halium-images); you need the unpacked `system.img` |

Host packages: `android-tools` (fastboot, mkbootimg), `arch-install-scripts`,
`qemu-user-static-binfmt`, `erofs-utils`, `lz4`, `cpio`, `e2fsprogs`, `git`, `curl`.

### Steps

```sh
scripts/fetch-sources.sh          # Nothing kernel sources + our patches into src/
scripts/build-kernel.sh           # kernel, vendor modules, out-of-tree modules
scripts/build-debug-boot.sh       # boot_b / vendor_boot_b / init_boot_b images
sudo scripts/build-rootfs.sh      # out/rootfs.simg (Arch Linux ARM + overlay)
```

Then flash, with the phone in fastboot (Power + Volume down):

```sh
# Optional, from a clean state: stock B4.1 on both slots, AVB off (needed for our kernel)
scripts/flash-stock.sh firmware/Tetris_B4.1-260928-1140/images
fastboot flash userdata out/rootfs.simg     # wipes all Android data
scripts/flash-debug-b.sh                    # our kernel + initramfs to slot b, boot it
```

After the first boot the **screen stays black**. That is expected: Arch's stock
wlroots lacks phoc's layer-shell patch, and Phosh exits. The second stage fixes this.
The phone is reachable over USB at `172.16.42.1`. Give the PC side a static
`172.16.42.2/24`, e.g. `nmcli c add type ethernet con-name tetris-usb match.driver cdc_ncm
ipv4.method manual ipv4.addresses 172.16.42.2/24 ipv6.method ignore`. Give it internet (`sudo
scripts/host-nat.sh up` shares the PC's connection over USB) and run the second stage.
It builds libhybris, wlroots and the ModemManager plugin on the phone and installs the
Halium image:

```sh
scripts/setup-phone.sh /path/to/halium/system.img
```

The user is `alarm` with password `alarm`. **Change it** (`passwd`), because SSH is on.

## Server and desktop profiles

Besides the phone (Phosh) the phone can boot as a **server** (headless: Docker, local
AI models, SSH) or as a **desktop** (GNOME over RDP, on the GPU). Two ways to switch:

- **One boot only:** the app **Startmodus** (`tetris-modes`, in the Nothing shell and in
  Phosh) shows where the phone will be reachable (SSH, RDP with password) and reboots into
  that profile; it also picks the phone's shell (Nothing shell or Phosh, `tetris-shell@`). The boot after that is the phone again: a short press on the
  power key reboots back. Under the hood `/var/lib/tetris-mode/once` is read by a systemd
  generator (`system-generators/tetris-mode-once`) that sets up the profile under `/run`
  only (runtime masks of the phone services, default target, WLAN power save off, power
  key = reboot); `tetris-mode-once-done.service` deletes the file during that boot.
- **For good:** `sudo tetris-mode server|desktop|phone` (`--with-modem` keeps mobile
  data), then reboot.

Both need the profile installed once (`scripts/setup-server.sh`, `scripts/setup-desktop.sh`,
see below); the apps say so otherwise.

### Server profile

The phone as a small always-on server: no Phosh, panel off, reachable over USB and WLAN,
runs Docker containers and local AI models (Ollama).

| | |
|---|---|
| Kernel | Docker options on top of the phone config (`kernel/linux.config`, "Server profile"): nftables + iptables compat, `br_netfilter`, `xt_addrtype`, IPv6 NAT, CFS bandwidth (`--cpus`), IPVS, VXLAN/IPVLAN |
| Display | `tetris-display-off` switches the panel off (DRM DPMS) and holds DRM master |
| Network | USB: `172.16.42.1` (as before). WLAN: NetworkManager, power save **off** (it delays incoming connections by a beacon interval; the phone's default "on" lives in `/usr/lib/NetworkManager/conf.d` so `/run` and `/etc` can override it). mDNS name `tetris.local` (Avahi) |
| SSH | Keys only over WLAN, password too over the USB cable (`etc/ssh/sshd_config.d/50-tetris-server.conf`). The project key and the PC user's keys are installed |
| Docker | `overlay2` on the ext4 rootfs, logs capped at 3 × 10 MB per container, `live-restore`. `alarm` is in the `docker` group |
| AI | Ollama as a container (`/opt/tetris-server/ai/compose.yaml`, `tetris-ai.service`), API on port 11434, CPU only. Optional web chat (Open WebUI, port 3000) |
| Memory | zram swap (half the RAM, zstd), `systemd-oomd`. The Halium container stays off (frees its RAM) unless you keep the modem |
| Off | Phosh, sensors, vibration, torch, GPS, fingerprint, Bluetooth, modem (keep it with `--with-modem`) |

#### Setting it up

The kernel of this tree must be on slot b first (all modules are rebuilt, `vendor_boot_b` too):

```sh
scripts/build-kernel.sh && scripts/build-debug-boot.sh
scripts/flash-debug-b.sh            # phone in fastboot
```

Then either convert the installed phone (keeps `userdata`; needs internet on the phone).
The new kernel refuses the old modules in the rootfs, so push the new ones first:

```sh
scripts/push-modules.sh             # modules matching the flashed kernel
scripts/setup-server.sh             # or --with-modem for mobile data as uplink
```

or build a fresh server image: `sudo PROFILE=server scripts/build-rootfs.sh` and flash
it to `userdata` as described above (the second stage, `setup-phone.sh`, is only needed
for the phone profile and for the modem).

#### Using it

```sh
ssh alarm@tetris.local                               # or alarm@172.16.42.1 over USB
docker context create tetris --docker host=ssh://alarm@tetris.local
docker --context tetris run --rm hello-world         # Docker from the PC, runs on the phone
docker --context tetris exec -it ollama ollama pull qwen3:1.7b
curl http://tetris.local:11434/api/generate -d '{"model":"qwen3:1.7b","prompt":"Hallo"}'
```

- `tetris.local` needs mDNS on the PC (`nss-mdns`, `mdns_minimal` in `/etc/nsswitch.conf`);
  without it use the WLAN address.
- The first boot pulls the Ollama image (~3 GB, takes a while); `journalctl -u tetris-ai`.
- Images must be `linux/arm64`. For x86-only images, build arm64 ones yourself
  (`docker buildx build --platform linux/arm64`).
- AI models run on the CPU (4 × Cortex-A78 + 4 × Cortex-A55). Models of 1–4 B
  parameters in Q4 are practical; RAM decides the upper limit. Pull models with
  `tetris-ollama-pull <model>`: it pins them to 4 threads, ~20 % faster than Ollama's
  default of 8 (the A55 cores hold the A78 ones back). qwen3:1.7b: ~12 tokens/s.
- **GPU (experimental):** the vendor Mali driver works through libhybris for OpenCL 3.0
  and Vulkan 1.3. llama.cpp's OpenCL backend only accepts Adreno/Intel, its Vulkan
  backend is generic: `pkgbuilds/build-llama-vulkan.sh` (on the phone) builds it,
  `tetris-llama llama-bench -m model.gguf -ngl 99` runs it (container must be up for
  `/vendor`). The GPU is small (2 cores, 1 GHz), so don't expect miracles; token
  generation is limited by memory bandwidth on both. The MediaTek APU (NPU) only works
  with NeuroPilot-compiled models under Android, and its kernel drivers hang here.
- The phone runs on the charger all the time: keep it cool and out of direct sun.
- Back to the phone: `sudo tetris-mode phone && sudo reboot`.

### Desktop profile

The server profile plus a full GNOME desktop that you use over **RDP** from a PC, tablet
or another phone (Remmina, FreeRDP, Windows' Remote Desktop, Microsoft's RDP apps). The
panel stays off; GNOME runs as a headless session of `alarm`, **renders on the Mali GPU**
(libhybris), and RDP streams it with H.264/RemoteFX.

| | |
|---|---|
| Kernel | Everything from the server profile, plus a backport of `SO_PEERPIDFD` from Linux 6.5 (`patches/kernel/0001`). GNOME 49+ needs peer pidfds from dbus-broker: without them the GDM polkit rule for headless sessions and the remote-login handover fail ("D-Bus broker doesn't support pidfd") |
| Session | `gnome-headless-session@alarm` (GDM starts it at boot, no display needed); `72-tetris-headless-seat.rules` takes the panel out of seat0 so GDM opens no login screen on it |
| RDP | `gnome-remote-desktop` in headless mode in alarm's session, port 3389. Certificate and a random password are made on the first boot (`tetris-rdp-setup`, user unit); the password is in `~/.config/tetris-rdp-password` |
| GPU | libhybris is built a second time as a **glvnd EGL vendor** (`/opt/libhybris-glvnd`) and listed in front of Mesa for the session (`user-environment-generators/60-tetris-hybris-gl`). glvnd picks per EGL platform: mutter's surfaceless display and the apps' Wayland EGL go to the Mali driver (mutter then offers `android_wlegl` to clients), Xwayland's GBM goes to Mesa. Needs `patches/libhybris/0002` (surfaceless platform) and the TLS fix in `0001`, the Halium container (gralloc) and a larger glibc static TLS reserve (`GLIBC_TUNABLES`). Verified: GNOME Shell, GTK 4 apps and X11 GL apps (glxgears) on the Mali-G615 |
| Exceptions | Apps that crash on libhybris EGL run on Mesa: listed in `/etc/tetris-software-gl.list` (Firefox: WebRender's renderer thread dies in `libGLES_mali`), a pacman hook rewrites their desktop entries to start through `tetris-software-gl`. Any other program: `tetris-software-gl <program>` |
| Fallback | Without the container, glvnd uses Mesa. A GPU shell that dies within 60 s switches the session to software for that boot (`tetris-gnome-shell`). Off for good: `touch ~/.config/tetris-no-gpu` |
| Memory | ~850 MB used after boot with GNOME, Docker and Ollama idle (CPU rendering; the container adds its share with the GPU) |

#### Setting it up

```sh
scripts/build-kernel.sh && scripts/build-debug-boot.sh
scripts/flash-debug-b.sh            # phone in fastboot
scripts/push-modules.sh             # modules for the new kernel (after a kernel change)
scripts/setup-desktop.sh            # server + desktop profile, prints the RDP password
```

For the GPU the Halium image must be installed (`scripts/setup-phone.sh`, as for GPU
Phosh); `setup-desktop.sh` then rebuilds libhybris with its glvnd variant. A fresh image
with the desktop profile: `sudo PROFILE=desktop scripts/build-rootfs.sh`, then
`setup-phone.sh` for the GPU.

Then connect to `tetris.local:3389` (or `172.16.42.1` over USB, or the WLAN address)
with user `alarm` and the RDP password. GNOME takes the resolution the client asks for:
`scripts/desktop-rdp.sh` connects in 1920×1080 (H.264), `scripts/desktop-rdp.sh
tetris.local /f` full screen over WLAN; with other clients set 1920×1080 (or full
screen) in the connection settings. The session is the logged-in GNOME of `alarm`; the desktop keeps
running when you disconnect. New RDP password: `tetris-rdp-setup` on the phone.
Back: `sudo tetris-mode server` or `sudo tetris-mode phone`, then reboot (after a one-boot
desktop: just reboot, or press the power key). Known: with the panel out of seat0, logind
can still report seat0 as graphical, and GDM tries (and gives up on) a local login screen
three times at boot; `tetris-display-off` holds the panel, so nothing shows.

## Nothing shell (default)

The phone runs the Nothing desktop ([Nothing_Wayland](https://github.com/bonbon08/Nothing_Wayland))
in its phone layout: status bar on top, app grid with search as home
screen, home bar at the bottom. It uses the same stack as Phosh (nothing-wm in place of phoc,
the same GPU or software choice and crash guard). `scripts/setup-phone.sh` builds and switches
to it at the end (`PHOSH=1` keeps Phosh); on an installed phone:

```sh
scripts/setup-nothing.sh [~/Nothing_Wayland]   # without a path: ~/Nothing_Wayland or a fresh clone
sudo reboot                                    # on the phone
```

Back to Phosh: `sudo rm /etc/tetris-shell && sudo reboot`. Phosh stays installed.

| Gesture / key | Does |
|---|---|
| Swipe down from the status bar | Control centre (WLAN, Bluetooth, rotation lock, volume, brightness) |
| Short / long swipe up from the home bar | Home screen / open apps (swipe a card sideways to close it) |
| Power key short / held | Screen off and lock, screen on / power menu (also on the lock screen) |
| Volume keys | Volume with an on-screen display, also locked and with the screen off |
| Turning the phone | The screen turns with it (sensor: `tetris-sensors`); locked it stays upright |

The on-screen keyboard is squeekboard (input-method protocol), it shows up for text fields;
the keyboard button at the right of the home bar closes it until the next tap into a field.
The home screen wallpaper is set in Einstellungen → Darstellung (own photos first). The
settings app also covers WLAN, LAN, Bluetooth (pairing included) and Mobilfunk (SIM PIN,
mobile data, APN, roaming).

**Thermal guard** (`tetris-thermal.service`): without Android's thermal HAL the kernel only
acts at 119 °C and the CPUs have no cooling device. The service caps the CPU clusters, the
GPU and finally the backlight in three steps from 70/80/90 °C SoC, 42/46/50 °C board NTC or
45/50/55 °C battery, and powers off at 100 °C SoC, 58 °C board or 60 °C battery
(`journalctl -u tetris-thermal`). **Firefox** gets `/usr/lib/firefox/defaults/pref/tetris-phone.js`:
without hardware video decoding it no longer probes the ~170 `/dev/video*` nodes at every
start (15 s → 2 s).
**Minecraft** (Prism Launcher, e.g. the demo): `/usr/local/bin/prismlauncher` starts Prism
without the libhybris libraries and with SDL on native Wayland, so the game renders with
Mesa's llvmpipe (OpenGL 4.6, software). The Mali GPU can't do it: its EGL is GLES-only, its
Vulkan lacks `VK_KHR_push_descriptor`/`VK_EXT_vertex_attribute_divisor` that Minecraft's
Vulkan backend needs, and Zink on it fails to create a screen.
The fingerprint unlock works with the Nothing lock screen (LockedHint, logind Unlock; the
sensor spot is drawn above the lock screen). The screen goes off after 60 s without touch
(Einstellungen → Telefon). Apps get `GLIBC_TUNABLES=glibc.rtld.optional_static_tls=16384`
in GPU mode (`tetris-nothing-session`), else PyGObject apps such as the settings app abort
with "static TLS block". Rotation needs `WLR_OUTPUT_STATE_TRANSFORM` in our wlroots
hwcomposer backend (patches/wlroots/0002).

## Roadmap: open items and the move to Linux 7.x

Goal: the phone should run a mainline 7.x kernel instead of Nothing's 6.1 kernel. The table tracks what
is still tied to the Nothing kernel. "Blocker" says what has to change first. Status values: **done**,
**in progress**, **open**, **blocked** (needs a proprietary blob or a driver nobody has written yet).

### Open features

| Item | Status | What is missing |
|---|---|---|
| Calls | open | Incoming calls: MediaTek's MIPC channel approves calls (`rootfs/src/tetris-mipc`, not built). Call audio: the audio HAL switches the speech path on (`rootfs/src/tetris-voicecall`, work in progress) |
| Standby (suspend) | open | The SoC hangs in deep sleep. Suspend is disabled (`sleep.conf.d/50-tetris-nosuspend.conf`) |
| GPS first fix | open | Satellites are found; a fix is not confirmed. Needs a test outdoors and a check of `tetris-gnss` |
| Headset jack | open | Analog headsets via a USB-C adapter, untested |
| Fingerprint with the mainline touch port | in progress | The trusted app only captures after the touch controller reports a finger on the spot (FOD info register `0xE1`, Goodix `FINGER_DOWN` via the vendor notifier `touchpanel_event_call_notifier`) followed by `UI_READY` (`hbm_mode` and `ui_status` set by `tetris-fp` on each touch). The port now reads the FOD info and calls the notifier (taken with `symbol_get`, no hard dependency). Not tested on the phone yet. Lessons from the vendor driver: set the FOD mode again a few times early in a scan (the controller drops it right after the display comes on), light more than the sensor radius (images otherwise stay just below the quality threshold), allow 10 min for enrollment, and run only one HAL client at a time (`tetris-fp list` during an enrollment cancels it) |
| Battery gauge oops | open | `mt6375_battery` can NULL-dereference in `__wake_up_common` from `coulomb_irq` and reset the phone. Root cause not found; the probe path is correct. A pstore log of this is kept in `out/` |
| Touch on mainline | in progress | `mainline/drivers/input/touchscreen/focaltech-ft3519.c` claims the FT3519 at boot (chip id 0x54, 1080x2400, 10 slots) and reports touches. Missing: gestures, double-tap wake, and the FOD/fingerprint mode of the vendor driver (the fingerprint unlock may depend on it, to be checked) |
| Vibration on mainline | in progress | The port builds for 7.3 and runs on 6.1. Mainline-only check still open |

### What a mainline 7.x kernel needs

| Component | Today (Nothing 6.1 tree) | For 7.x | Blocker / next step | Status |
|---|---|---|---|---|
| Kernel config | `kernel/linux.config` on top of Nothing's defconfig | a mainline defconfig plus our fragment | list the options we need, check which exist upstream | open |
| SO_PEERPIDFD | backported (`patches/kernel/0001`) | upstream since 6.5 | drop the patch | done on paper, check on 7.x |
| Haptics (RT6010) | vendor `haptic` driver, ~4400 lines | port in `mainline/drivers/input/misc/rt6010-haptic.c` | mainline 7.3 builds; runs on 6.1 | in progress |
| Battery / charger | vendor `mt6375` gauge and charger, patched (`patches/kernel_device_modules/0001`) | check whether 7.x has MT6375 support | the gauge crash above | open |
| Display panels | vendor `s6e8fc3x02` and others, HBM patch `0003` | upstream DRM panel drivers, plus the HBM notification | the notification to the fingerprint driver is ours | open |
| Touch | vendor `focaltech` ft3519 with resume patch `0004`; basic port in `mainline/` runs on 6.1 | the basic read path works on the phone (chip 0x54). Gestures and FOD are missing; the resume fix `0004` has to be carried over | gestures and FOD (fingerprint mode) | in progress |
| Sensors | SCP sensor hub `/dev/hf_manager` (vendor) | an upstream sensor-hub driver, or keep the vendor one | `tetris-sensors` talks to `/dev/hf_manager` | blocked |
| Audio codec | MT6369 plus aw882xx, UCM profile | ALSA codec drivers for MT6369 and aw882xx | patch `0002` (mixer switch read) | open |
| GPU (Mali-G615) | vendor Mali kernel module, used through libhybris | a mainline DRM driver for this GPU | check whether a mainline driver supports the G615. Until then the Halium container and libhybris stay | blocked |
| Display path | vendor composer HAL over DRM, wlroots hwcomposer backend | mainline DRM and KMS on the MediaTek display | depends on the GPU row above | blocked |
| Modem (CCCI) | vendor `ccci` kernel modules, `ccci_mdinit` in the container | a mainline modem driver | proprietary protocol and firmware; `tetris_mdtty` is ours | blocked |
| Wi-Fi and Bluetooth (connac2, conninfra) | vendor `connectivity/*` modules, `/dev/wmtWifi`, `/dev/stpbt` | mainline support for this combo chip | out-of-tree vendor code; the NVRAM handoff in `initramfs/init` is specific to it | blocked |
| GNSS | vendor `gps` modules, Android `mnld` | mainline GNSS support | blob-based | blocked |
| Fingerprint | Goodix HAL with TEE (Trustonic) | mainline driver | TEE dependency | blocked |
| Camera | vendor `mtkcam` and camera HAL | mainline MediaTek ISP support | large, proprietary 3A algorithms | blocked |
| Device tree | `k6878v1_64_tetris.dts` (vendor) | a mainline DTS for Tetris | copy the hardware description, drop vendor properties | open |
| Boot and partitions | vendor `boot`, `vendor_boot`, LK, A/B slots | the same boot chain on 7.x | the initramfs and NVRAM steps stay | open |

**Bottom line:** a mainline 7.x kernel can boot the phone only when the blocked rows are solved or still
handled by the Halium container. Today the modem, Wi-Fi, Bluetooth, camera, GNSS and GPU depend on vendor
kernel code. The container and the vendor modules are therefore still part of the plan.

## Risks

- **Never relock the bootloader** while a custom kernel is on the phone or AVB is off.
  A relocked bootloader with modified partitions bricks the phone.
- Flashing `userdata` deletes all Android data. Nothing OS on slot a cannot read the ext4
  `userdata` either. To go back, reflash stock with `scripts/flash-stock.sh`.
- Back up the modem partitions (`nvdata`, `nvcfg`, `protect1`/`protect2`, `persist`)
  before experimenting. They hold the IMEI and RF calibration.
- No suspend: expect roughly a day with the screen mostly off.
- No warranty. This is a hobby project.

## Gotchas

- **Crash guard.** If the GPU session dies within 90 s of starting, the next start
  falls back to software Phosh for that boot. If you restart `tetris-phosh` within
  90 s, you trigger it too.
- **GTK4 apps on libhybris EGL.** They can crash when Android threads exit. Chats, Calls
  and Kamera therefore render in software.
- **Don't `depmod` over the whole vendor module set.** Duplicate exports make it pick
  the wrong modules. Out-of-tree modules are loaded with `insmod`.

## Credits

[Nothing](https://github.com/NothingOSS) for the kernel sources;
[libhybris](https://github.com/libhybris/libhybris),
[Halium](https://halium.org) and the [LuneOS / webOS-ports](https://github.com/webOS-ports)
team for the Android 16 libhybris work and GSI; [Droidian](https://droidian.org) for
the original wlroots hwcomposer backend idea; [Phosh](https://phosh.mobi);
[spike0en/nothing_fastboot_flasher](https://github.com/spike0en/nothing_fastboot_flasher)
for the stock partition lists.

## License

Our own code (scripts, services, patches, apps) is **GPL-2.0-or-later** (see
`LICENSE`). Patches to other projects keep those projects' licenses: Linux and
ModemManager (GPL-2.0), wlroots (MIT), libhybris (Apache-2.0).
