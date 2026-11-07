# 8. Profiles and operating modes

**🇬🇧 English** · [🇩🇪 Deutsch](de/08-profile.md)

The phone can be four things, depending on the setting:

1. **Phone with the Nothing shell** (default), or with **Phosh** as the alternative;
2. **Server**: no screen, reachable via SSH and Docker, with local AI models;
3. **Desktop**: like the server, plus a complete GNOME desktop over the network (RDP).

This chapter explains how to switch between these modes, what each one does exactly and what to keep
in mind.

## 8.1 The terms

| Term | Meaning |
|---|---|
| **Profile** | a collection of services and settings that together make up an operating mode: `phone`, `server` or `desktop` |
| **Interface (shell)** | the program that draws the screen: Nothing shell or Phosh. Belongs to the profile `phone` |
| **One-boot start** (`once`) | a start in another profile; afterwards the phone automatically returns to the phone profile |
| **permanent** | the profile stays across reboots until you change it |

The difference between **permanent** and **one-boot start**: with a one-boot start the system writes nothing
lasting. The settings live under `/run`, the volatile memory that is empty at the next start. The file
`/var/lib/tetris-mode/once` is the only marker and is deleted at boot.

## 8.2 The switches

| Means | Effect | Permanent? | Where to run |
|---|---|---|---|
| `sudo tetris-mode phone` | phone with interface | yes | on the phone |
| `sudo tetris-mode server` | server | yes | on the phone |
| `sudo tetris-mode desktop` | desktop over RDP | yes | on the phone |
| `tetris-mode` (no argument) | shows the current profile | – | on the phone |
| app **Startmodus** | one-boot start as server or desktop; also choice of the interface | no (server/desktop); yes (interface) | on the phone, in the interface |
| `sudo systemctl start tetris-shell@nothing` (or `@phosh`) | sets the interface for the next start | yes | on the phone |
| `sudo rm /etc/tetris-shell` | Phosh as the interface | yes | on the phone |

After every permanent change a reboot is needed:

```sh
sudo tetris-mode server && sudo reboot
```

The switch `--with-modem` keeps the mobile connection in the `server` and `desktop` profiles. For this the
Halium container, which serves the modem, must run:

```sh
sudo tetris-mode server --with-modem && sudo reboot
```

## 8.3 What a profile switches on

`tetris-mode` only switches **services**. The files stay the same. That is why you can always switch back.
The mapping is in the script `rootfs/overlay-server/usr/local/bin/tetris-mode`:

| Service | `phone` | `server` | `desktop` |
|---|---|---|---|
| `tetris-phosh` (interface) | on | off | off |
| `tetris-sensors`, `tetris-vibra`, `tetris-torch`, `tetris-gnss`, `tetris-fp` | on | off | off |
| `bluetooth`, `tetris-bt-bridge` | on | off | off |
| `ModemManager`, `tetris-modem` | on | off (on with `--with-modem`) | off (on with `--with-modem`) |
| `tetris-display-off` | off | on | on |
| `docker`, `avahi-daemon` (`tetris.local`), `systemd-oomd`, `tetris-ai` | off | on | on |
| `gdm`, `gnome-headless-session@alarm` | off | off | on |
| `tetris-android` (container) | on (via Phosh) | off, unless something needs it | on (for the GPU) |
| default target (`default.target`) | `graphical.target` | `multi-user.target` | `graphical.target` (GDM) |
| Wi-Fi power saving | on | off (otherwise power saving delays incoming connections) | off |
| seat marker (`/etc/tetris-headless-seat`) | no | no | yes: the panel does not belong to seat0 |

The script lives in `rootfs/overlay-server/usr/local/bin/tetris-mode`. Its header comment lists all commands.

## 8.4 The interface: Nothing shell or Phosh

The `phone` profile uses one of two interfaces. The choice is stored in the file `/etc/tetris-shell`:

| File content | Interface |
|---|---|
| `nothing` | Nothing shell (default after `setup-nothing.sh`) |
| (file missing) | Phosh |

Both run through the same service (`tetris-phosh.service`). The script `tetris-phosh-session` reads
`/etc/tetris-shell` and then starts the Nothing session (`tetris-nothing-session`) or Phosh. Both use the
same GPU or software decision (`tetris-phosh-select`).

### Operating the Nothing shell

| Gesture or key | Effect |
|---|---|
| swipe down from the top edge | control centre (Wi-Fi, Bluetooth, rotation, volume, brightness) |
| swipe up from the home bar | short: home screen; long: open apps (swipe a card sideways to close it) |
| power key, short press | screen off and lock. When the screen is off, turns it back on |
| power key, long press | power menu (also on the lock screen) |
| volume keys | volume with an on-screen display, also when locked and with the screen off |
| both volume keys at the same time | screenshot (`tetris-screenshot`, saved to `~/Pictures/Screenshots`) |
| rotating the phone | the screen rotates with it (sensor `tetris-sensors`); when locked it stays upright |

The on-screen keyboard is **squeekboard**. It appears in text fields. The key on the right of the home bar
hides it until you tap a field again.

The Settings app covers Wi-Fi, LAN, Bluetooth (including pairing) and mobile network (SIM PIN, data
connection, access point, roaming). You choose the home screen wallpaper under **Settings → Display**.

### Startmodus (app)

The app **Startmodus** (`tetris-modes`, "start mode") is available in both interfaces. It has two jobs:

1. **One-boot start** as server or desktop. The app shows the address under which the phone will be
   reachable afterwards (SSH, RDP with password). Then it reboots the phone.
2. **Choosing the interface:** Nothing shell or Phosh (`tetris-shell@`), effective from the next start.

In the background the app writes `server` or `desktop` into `/var/lib/tetris-mode/once` and reboots. Then
the following happens:

```
Startmodus ──► tetris-boot-as ──► tetris-boot-once (as root) ──► writes "once"
                                                                       │
                                                          reboot       ▼
   generator tetris-mode-once (before systemd, reads "once", builds the profile under /run)
                                                                       │
                                                                       ▼
   tetris-mode-once-done.service deletes "once" ──► next start: back to the phone
```

The **generator** (`/etc/systemd/system-generators/tetris-mode-once`) is a small program that systemd runs
at startup. From the file `once` it creates the symbolic links for this one profile. Because everything
lives under `/run`, it disappears by itself at the next reboot.

**Short press during the one-boot start:** in the server and desktop one-boot profile, a short press of
the power key triggers a reboot. This takes you straight back to the phone.

## 8.5 Server profile

The phone as a small server that stays switched on. The screen stays off.

### What it contains

| Area | Implementation |
|---|---|
| **Kernel** | Docker options in `kernel/linux.config` (nftables, network namespaces, CFS bandwidth limits) |
| **Screen** | `tetris-display-off` switches the panel off via DPMS and holds the DRM master so that no other process takes over the display |
| **USB network** | `172.16.42.1`, as in the phone profile |
| **Wi-Fi** | NetworkManager, power saving **off** (otherwise it delays incoming connections by a beacon interval) |
| **Name on the network** | `tetris.local` via Avahi (mDNS). Needs `nss-mdns` on the PC |
| **SSH** | keys only over Wi-Fi. Password only over the USB cable (`50-tetris-server.conf`) |
| **Docker** | `overlay2` storage on the ext4 rootfs, logs limited to 3 × 10 MB per container, `live-restore`. The user `alarm` is in the group `docker` |
| **AI** | Ollama as a container (`/opt/tetris-server/ai/compose.yaml`, service `tetris-ai`), API on port 11434, CPU only |
| **Memory** | zram swap (half the RAM, zstd), `systemd-oomd`. The Halium container does not run, except with `--with-modem` |
| **Off** | interface, sensors, vibration, torch, GPS, fingerprint, Bluetooth, modem (with `--with-modem` the modem stays on) |

### Setting up

Set-up from the repository, with a running phone (see
[04-build-and-flash.md](04-build-and-flash.md#49-phase-6-server-and-desktop-profiles)):

```sh
scripts/setup-server.sh            # or: scripts/setup-server.sh --with-modem
```

Or build a new phone image with the profile:

```sh
sudo PROFILE=server scripts/build-rootfs.sh
```

### Using it

```sh
ssh alarm@tetris.local                                # over Wi-Fi
ssh alarm@172.16.42.1                                 # over USB
docker context create tetris --docker host=ssh://alarm@tetris.local
docker --context tetris run --rm hello-world
docker --context tetris exec -it ollama ollama pull qwen3:1.7b
curl http://tetris.local:11434/api/generate -d '{"model":"qwen3:1.7b","prompt":"Hello"}'
```

- Images must be built for **`linux/arm64`**. For x86-only images, build arm64 images yourself with
  `docker buildx build --platform linux/arm64`.
- The first start pulls the Ollama image (about 3 GB) and needs time for that. Follow it with
  `journalctl -u tetris-ai`.
- **Models:** `tetris-ollama-pull <model>` sets the thread count to 4. The reason is in the script: the four
  slow cores (Cortex-A55) hold back the four fast ones (A78). With four threads generation is about 20 %
  faster than with the default of eight. As a reference point the project names `qwen3:1.7b` at about
  12 tokens per second.
- For 1 to 4 billion parameters in Q4 format models are practical. The working memory sets the upper limit
  (figure from the main README).
- **GPU (experimental):** `tetris-llama` uses Vulkan via libhybris on the Mali GPU. The GPU has two cores
  at 1 GHz. Do not expect miracles. The AI unit (APU) only works with models compiled for NeuroPilot under
  Android. Its kernel drivers hang the boot on our system, which is why it is blocked.
- The phone is mostly on the charger. Keep it cool and out of direct sunlight.

Back to the phone: `sudo tetris-mode phone && sudo reboot`.

## 8.6 Desktop profile

The phone is a server with a **GNOME desktop** that you operate over RDP from a PC, a tablet or another
phone. The screen stays off.

### What it contains

| Area | Implementation |
|---|---|
| **Session** | `gnome-headless-session@alarm`: GNOME without a display. GDM starts it at boot |
| **Seat** | `72-tetris-headless-seat.rules` takes the panel out of seat0, so that GDM shows no login screen on it |
| **RDP** | `gnome-remote-desktop` in headless mode, port **3389**. The certificate and a random password are created on the first start (`tetris-rdp-setup`) and stored in `~/.config/tetris-rdp-password` |
| **GPU** | GNOME Shell uses the Mali GPU through a second libhybris variant as a **glvnd EGL provider** (`/opt/libhybris-glvnd`). Xwayland uses Mesa |
| **Exceptions** | programs from `/etc/tetris-software-gl.list` run on Mesa (Firefox). `tetris-software-gl <program>` runs any program on Mesa |
| **Kernel** | the `SO_PEERPIDFD` patch. Without it the remote login fails (see [07](07-kernel-drivers-patches.md#721-kernel-so_peerpidfd-kernel0001)) |

To connect:

```sh
scripts/desktop-rdp.sh                       # USB, 1920×1080, H.264
scripts/desktop-rdp.sh tetris.local /f       # Wi-Fi, full screen
```

With other clients (Remmina, Windows Remote Desktop, Microsoft RDP apps) set 1920 × 1080 or full screen.
The desktop keeps running when you disconnect.

**New RDP password:** `tetris-rdp-setup` on the phone.

### Setting up

```sh
scripts/setup-desktop.sh [--with-modem]
```

The script first checks whether the kernel has `SO_PEERPIDFD`. Then it sets up the server profile, installs
GNOME and builds libhybris with the glvnd variant (if the Halium container is installed). At the end it
prints the RDP password.

Without the Halium container GNOME renders on the processor (Mesa llvmpipe). That works, but is slower.

### Memory

Idle (GNOME, Docker and Ollama, no load) about 850 MB are used (figure from the main README).

## 8.7 Problems with profiles

| Problem | Cause | Remedy |
|---|---|---|
| after `tetris-mode server` the phone does not come onto the network | Wi-Fi power saving or network configuration | connect over USB (`172.16.42.1`), check `nmcli` |
| `tetris-mode: gdm and gnome-remote-desktop are not installed` | desktop packages missing | run `scripts/setup-desktop.sh` |
| desktop shows the GDM login screen on the panel | the seat switch is missing | run `sudo tetris-mode desktop` again, reboot |
| GDM tries three times at boot to show a local login screen | known, see main README | `tetris-display-off` holds the panel, so nothing is visible |
| one-boot start does not start in the server profile | file `once` is missing or has other content | check `cat /var/lib/tetris-mode/once`; only `server` or `desktop` is valid |
| phone stays in the server profile after the one-boot start | `tetris-mode-once-done` did not run | `sudo rm -f /var/lib/tetris-mode/once && sudo reboot` |

More causes of problems: [09-troubleshooting.md](09-troubleshooting.md).
