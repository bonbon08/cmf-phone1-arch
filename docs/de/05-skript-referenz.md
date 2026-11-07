# 5. Skript-Referenz

**🇩🇪 Deutsch** · [🇬🇧 English](../05-script-reference.md)

Jedes Skript im Projekt wird hier beschrieben: Zweck, Aufruf, Eingaben, Ausgaben und
Nebenwirkungen. Die Skripte sind in drei Gruppen eingeteilt:

- **`scripts/`:** läuft auf dem **PC**. Baut, flasht und richtet das Telefon über USB ein.
- **`pkgbuilds/`:** läuft auf dem **Telefon** (als Benutzer `alarm`). Baut Software, die auf
  dem Telefon kompiliert werden muss. Der PC kopiert die Dateien dorthin.
- **`initramfs/`:** das Mini-System, das vor Arch startet (kein Skript im klassischen Sinn).

Alle Skripte sind Bash-Skripte (`#!/usr/bin/env bash`) mit `set -euo pipefail`. Sie brechen
also beim ersten Fehler ab. Die meisten bestimmen ihr Projektverzeichnis selbst, Sie können
sie von überall aus aufrufen.

## 5.1 Übersicht

| Skript | Ort | Läuft | Zweck | Schreibt auf das Gerät? |
|---|---|---|---|---|
| `fetch-sources.sh` | `scripts/` | PC | Kernel-Quellen holen und Patches anwenden | nein |
| `build-kernel.sh` | `scripts/` | PC | Kernel und alle Module bauen | nein |
| `build-debug-boot.sh` | `scripts/` | PC | Boot-, vendor_boot- und init_boot-Images erzeugen | nein |
| `build-rootfs.sh` | `scripts/` | PC (root) | Arch-Linux-Abbild für `userdata` bauen | nein |
| `extract-vendor.sh` | `scripts/` | PC | Firmware aus dem Original-`vendor`-Image entpacken | nein |
| `flash-stock.sh` | `scripts/` | PC | Original-Nothing-OS auf beide Slots schreiben | **ja, alle Partitionen, löscht Daten** |
| `flash-debug-b.sh` | `scripts/` | PC | Kernel und Initramfs auf Slot b schreiben | **ja, Slot b** |
| `push-modules.sh` | `scripts/` | PC → Telefon | Kernel-Module auf das laufende Telefon kopieren | ja, `/usr/lib/modules` |
| `setup-phone.sh` | `scripts/` | PC → Telefon | zweite Stufe: Halium, libhybris, wlroots, Shell | ja |
| `setup-nothing.sh` | `scripts/` | PC → Telefon | Nothing-Shell installieren | ja |
| `setup-server.sh` | `scripts/` | PC → Telefon | Server-Profil einrichten | ja, reboot |
| `setup-desktop.sh` | `scripts/` | PC → Telefon | Desktop-Profil einrichten | ja, reboot |
| `phone-ssh.sh` | `scripts/` | PC → Telefon | SSH-Verbindung mit dem Projektschlüssel | nein |
| `phone-sh.sh` | `scripts/` | PC → Initramfs | Befehl im Notfall-Telnet ausführen | nur was der Befehl tut |
| `host-nat.sh` | `scripts/` | PC (root) | PC-Internet mit dem Telefon teilen | Firewall des PC |
| `desktop-rdp.sh` | `scripts/` | PC | RDP-Verbindung zum Desktop-Profil | nein |
| `read-flashlog.sh` | `scripts/` | PC (adb, Root) | Initramfs-Protokoll aus `nt_reserve2` lesen | nein |
| `build-libhybris.sh` | `pkgbuilds/` | Telefon | libhybris bauen | ja, `/opt` |
| `build-wlroots-phoc.sh` | `pkgbuilds/` | Telefon | wlroots 0.20 mit unserem Backend bauen | ja, Paket |
| `build-mm-plugin-tetris.sh` | `pkgbuilds/` | Telefon | ModemManager-Plugin bauen | ja, `/usr/lib/ModemManager` |
| `build-llama-vulkan.sh` | `pkgbuilds/` | Telefon | llama.cpp mit Vulkan auf der GPU bauen | ja, `/opt` |

---

## 5.2 Skripte auf dem PC (`scripts/`)

### `fetch-sources.sh`

**Zweck:** Holt die Quellen des Nothing-Kernels und wendet unsere Patches an.

**Aufruf:** `scripts/fetch-sources.sh` (keine Argumente)

**Eingaben:** Internetzugang zu `github.com/NothingOSS`.

**Ausgaben:** Verzeichnisse in `src/`:

| Verzeichnis in `src/` | Repository | Inhalt |
|---|---|---|
| `kernel-6.1` | `android_kernel_6.1_nothing_mt6878` | Kernel-Quellen |
| `kernel_device_modules-6.1` | `android_kernel_device_modules_6.1_nothing_mt6878` | Gerätetreiber (MediaTek, Display, Akku, Touch) |
| `vendor/mediatek/kernel_modules` | `android_kernel_modules_nothing_mt6878` | WLAN, Bluetooth, GPS, GPU, Kamera |
| `build` | `android_kernel_build_nothing_mt6878` | Build-Konfiguration |

**Ablauf:**

1. Vier Repositorien parallel mit `git clone --depth 1` (Branch `mt6878/Tetris/16b`). Ist ein
   Verzeichnis schon da, wird es übersprungen.
2. Die Funktion `apply` wendet alle Patches in `patches/kernel`, `patches/kernel_device_modules`
   und `patches/kernel_modules` an. Mit `git apply --check -R` erkennt sie, ob ein Patch schon
   drin ist, und überspringt ihn dann.

**Nebenwirkungen:** Keine auf dem Gerät. Änderungen an `src/` bleiben lokal.

**Fehler:** Ein Patch, der nicht mehr passt (zum Beispiel nach einem neuen Nothing-Release),
bricht mit der Meldung von `git apply` ab. Dann muss der Patch neu gerollt werden, siehe
[07-kernel-treiber-patches.md](07-kernel-treiber-patches.md#75-patches-erneuern).

---

### `build-kernel.sh`

**Zweck:** Baut den Kernel und alle Module mit einfachem `make` und dem Android-Clang.
Das Skript folgt dem Ablauf des Nothing-Builds (`build.config.mtk_kernel_device_modules`),
ohne Bazel/Kleaf.

**Aufruf:**

```sh
scripts/build-kernel.sh [config|kernel|modules|ext [erstes-modul]|all]
```

Ohne Argument wird `all` gewählt. `ext` akzeptiert optional einen Modulnamen oder Pfadteil, um
dort weiterzumachen.

**Eingaben:** `src/` (von `fetch-sources.sh`), `toolchain/clang-r487747c/`, `kernel/linux.config`.

**Ausgaben:** `out/kernel/` mit `Image`, `Image.gz`, Modulen (`*.ko`), `System.map`,
`include/config/kernel.release`.

**Umgebungsvariablen:**

| Variable | Standard | Wirkung |
|---|---|---|
| `JOBS` | Anzahl der CPU-Kerne | parallele Jobs für `make` |
| `TETRIS_LINUX_HCI` | `0` | `1` baut Bluetooth als Linux-HCI-Gerät statt über `/dev/stpbt`. Diese Option ist experimentell und hat den Start schon einmal gehindert |

**Ablauf im Detail:**

1. `config`: `gki_defconfig` als Grundlage, dann mit `merge_config.sh` die Device-Konfiguration
   `mgk_64_k61_defconfig` und unsere `kernel/linux.config` übereinanderlegen, dann `olddefconfig`.
   Optionen aus unserer Datei, die danach fehlen, werden mit `!! not applied` gemeldet.
2. `kernel`: `make Image.gz modules`.
3. `modules`: die Gerätemodule aus `kernel_device_modules-6.1`.
4. `ext`: alle Module aus `EXT_MODULES` (WLAN `gen4m`, Bluetooth `mt66xx`, GPU, Kamera, GPS,
   Conninfra ...) in der Abhängigkeitsreihenfolge. Danach baut es unsere Module
   `rootfs/src/tetris-mdtty` (AT-Tty-Paar) und `rootfs/src/tetris-torch` (uleds).

**Warnung:** Die Module sind an genau diesen Kernel gebunden (Versionsprüfung, modversions).
Jeder Kernel-Build verändert die Module-Signaturen. Danach müssen alle Module neu gebaut und
auf das Telefon kopiert werden (`push-modules.sh` oder ein neues Rootfs).

---

### `build-debug-boot.sh`

**Zweck:** Erzeugt die Boot-Images für Slot b und das Initramfs.

**Aufruf:** `scripts/build-debug-boot.sh` (keine Argumente)

**Eingaben:**

| Pfad | Woher | Verwendung |
|---|---|---|
| `out/kernel/` | `build-kernel.sh` | Kernel, Module, `kernel.release` |
| `stock-info/unpacked/vendor_boot_vendor_ramdisk00.d/lib/modules/modules.load` | Original-System | Liste der Stufe-1-Module |
| `stock-info/vendor_dlkm/modules.load` | Original-System | Liste der Stufe-2-Module |
| `initramfs/modules.load.3` | Projekt | Liste der Stufe-3-Module |
| `initramfs/modules.blocklist` | Projekt | Module, die nicht geladen werden |
| `initramfs/init`, `initramfs/tools/*.c` | Projekt | Startskript, Hilfsprogramme |
| `downloads/alarm/usr/bin/busybox` | Arch Linux ARM | Werkzeugkasten im Initramfs |
| `firmware/…/images/vendor_boot.img` | Original-Firmware | DTB, Kommandozeile, Speicherlayout |
| `src/kernel-6.1/drivers/video/logo/logo_linux_clut224.ppm` | Kernel-Quellen | Tux-Logo für den Splash |

**Ausgaben:** `out/debug-boot.img`, `out/debug-vendor_boot.img`,
`out/debug-vendor_boot-shell.img`, `out/debug-init_boot.img`, `out/debug-initramfs.cpio.lz4`,
`out/Image.lz4`, `out/empty.cpio.lz4`, `out/stock-vendor_boot/`.

**Ablauf:**

1. Baut das Initramfs-Verzeichnis `out/debug-initramfs/`: busybox, `init`, Firmware
   (aus `out/vendor-extract/`, wird bei Bedarf von `extract-vendor.sh` erzeugt), zwei
   Hilfsprogramme (`reboot-mode` und `tetris-splash`, statisch gegen Null-libc gebaut).
2. Kopiert die Module aus den drei Listen und entfernt die Debug-Symbole (`llvm-strip`).
   Module aus `modules.blocklist` werden übersprungen.
3. `depmod` erzeugt `modules.dep` für das Initramfs. Die Meldung `unresolved symbols` ist eine
   Zählung: Vendor-Module verweisen auf Symbole, die nicht gebaut wurden. Das ist kein Fehler.
4. Packt das Initramfs als `cpio` (newc) und `lz4`.
5. Erzeugt die Boot-Images mit `mkbootimg` (Header-Version 4) und dem Speicherlayout des
   Original-`vendor_boot`.

**Nebenwirkungen:** Keine auf dem Gerät. Das Skript löscht `out/debug-initramfs/` und
`out/stock-vendor_boot/` und erzeugt sie neu.

---

### `build-rootfs.sh`

**Zweck:** Baut das komplette Arch-Linux-ARM-Dateisystem für das Telefon.

**Aufruf:**

```sh
sudo scripts/build-rootfs.sh                    # Telefon-Profil
sudo scripts/build-rootfs.sh --finish           # auf bestehendem Abbild: Overlay, Dienste, Verkleinern
sudo PROFILE=server scripts/build-rootfs.sh     # Server-Profil
sudo PROFILE=desktop scripts/build-rootfs.sh    # Desktop-Profil
sudo SIZE=12G scripts/build-rootfs.sh           # andere Obergröße des Abbilds
```

| Variable | Standard | Wirkung |
|---|---|---|
| `PROFILE` | `phone` | `phone`, `server` oder `desktop`. Bestimmt Pakete und Overlays |
| `SIZE` | `10G` | Größe des Abbilds, bevor es verkleinert wird |

**Eingaben:** `downloads/alarm/ArchLinuxARM-aarch64-latest.tar.gz`, `out/kernel/`,
`out/vendor-extract/`, `out/kernel_device_modules-6.1/` (bzw. `out/vendor/`), `rootfs/…`,
`pkgbuilds/`, `patches/`, Ihre `~/.ssh/*.pub`.

**Paketliste** (im Skript als Arrays `PACKAGES`, `SERVER_PACKAGES`, `DESKTOP_PACKAGES`):

| Gruppe | Pakete |
|---|---|
| Oberfläche | `phosh`, `phoc`, `squeekboard`, `phosh-mobile-settings`, `gnome-console`, `gnome-control-center`, `gnome-text-editor`, `nautilus`, Schriften, `zenity` |
| System | `networkmanager`, `modemmanager`, `chatty`, `gnome-calls`, `gnome-maps`, `wpa_supplicant`, `feedbackd`, `gnome-keyring`, `bluez` |
| Werkzeuge | `openssh`, `sudo`, `vim`, `htop`, `base-devel`, `git`, `meson` |
| Container | `lxc` |
| Server (Profil `server`/`desktop`) | `docker`, `docker-compose`, `docker-buildx`, `avahi`, `nss-mdns`, `zram-generator` |
| Desktop (Profil `desktop`) | `gdm`, `gnome-session`, `gnome-remote-desktop`, `xdg-desktop-portal-gnome`, `gnome-backgrounds`, `gnome-system-monitor` |

**Ablauf:**

1. **Abbild anlegen** (ohne `--finish`): leeres Abbild mit `truncate`, `mkfs.ext4 -L tetris-root`,
   Einhängen über Loop-Device in `out/rootfs-mnt/`.
2. **Basissystem:** `bsdtar` entpackt das Arch-Linux-ARM-Archiv.
3. **Module:** alle `.ko` aus Kernel, Gerätemodulen und Vendor werden nach
   `/usr/lib/modules/<Version>/` kopiert und per `llvm-strip` verkleinert. Dazu kommen
   `tetris_mdtty.ko` und `uleds.ko` in `extra/`. Danach `depmod`.
4. **Overlay:** `rootfs/overlay` kopieren, je nach Profil `overlay-server` und
   `overlay-desktop` dazu. Dazu Besitzer und Rechte korrigieren (Dateien in `/etc/sudoers.d`,
   Schlüssel `0600` usw.).
5. **Schlüssel:** Erzeugt `out/ssh/id_ed25519` (falls fehlt) und legt die öffentlichen
   Schlüssel für `alarm` in `~/.ssh/authorized_keys` ab.
6. **Pakete** in einer aarch64-chroot (`arch-chroot`, QEMU-Emulation). Die Option
   `--disable-sandbox` ist nötig, weil pacmans Download-Sandbox unter QEMU nicht läuft.
   `linux-aarch64` wird entfernt, weil der Kernel von `boot_b` kommt.
7. **Eigene Programme** werden mit dem Android-Clang gegen das Rootfs kreuzkompiliert
   (`--sysroot`): `tetris-bt-bridge`, `tetris-mdtty-bridge`, `tetris-vibra`, `tetris-torch`,
   `tetris-display-off`, `tetris-sensors` (mit `gio` aus dem Rootfs). Die Liste ist in
   [06-rootfs-referenz.md](06-rootfs-referenz.md#65-eigene-programme-rootfssrc) beschrieben.
8. **Dienste** werden in der chroot aktiviert (`systemctl enable`), zum Beispiel
   `tetris-phosh`, `tetris-modem`, `tetris-sensors`. Im Server- und Desktop-Profil läuft
   zusätzlich `tetris-mode <profil>`.
9. **Abschluss:** Abbild verkleinern (`resize2fs -M`), sparse-Abbild erzeugen (`img2simg`).

**Nicht im Rootfs gebaut:** `libmm-plugin-tetris.so`, die Kamera-App, `tetris-gnss`, `tetris-fp`
und libhybris. Die baut die zweite Stufe auf dem Telefon.

**Ausgaben:** `out/rootfs.img` (ext4), `out/rootfs.simg` (sparse, für `fastboot flash userdata`).

---

### `extract-vendor.sh`

**Zweck:** Entpackt das Original-`vendor`-Image (erofs) nach `out/vendor-extract/vendor`.
Dort liegt die Firmware, die die Treiber beim Laden brauchen (`conninfra.cfg`,
`connfem.cfg`, `wifi.cfg`, `mali_csffw.bin`, die SCP-Firmware und andere).

**Aufruf:** `scripts/extract-vendor.sh [pfad/zu/vendor.img]`

**Eingaben:** `firmware/…/images/vendor.img` (Standard).

**Werkzeug:** `fsck.erofs --extract` (Paket `erofs-utils`).

**Ausgabe:** `out/vendor-extract/vendor/firmware/` und die Anzahl der Dateien.

**Wird automatisch aufgerufen** von `build-debug-boot.sh` und `build-rootfs.sh`, wenn der
Ordner fehlt.

---

### `flash-stock.sh`

**Zweck:** Schreibt das **originale Nothing OS B4.1** auf beide Slots. Das ist der sichere
Ausweg, wenn etwas gründlich schiefgegangen ist.

**Aufruf:**

```sh
scripts/flash-stock.sh firmware/Tetris_B4.1-260928-1140/images [--keep-avb]
```

| Argument | Wirkung |
|---|---|
| Verzeichnis | Ordner mit den entpackten `.img`-Dateien |
| `--keep-avb` | Die `vbmeta`-Images bleiben unverändert. Unser Kernel startet dann **nicht** |

**Ablauf im Detail:**

1. **Prüfung:** Alle erwarteten Images müssen vorhanden sein. Die Produkt-Kennung muss
   `k6878v1_64` sein. Sonst bricht das Skript ab, bevor es etwas schreibt.
2. **AVB außer Kraft setzen** (ohne `--keep-avb`): Die Flags im Header der `vbmeta`-Images
   werden mit `dd` auf `HASHTREE_DISABLED | VERIFICATION_DISABLED` gesetzt (Byte 120).
   Fastboot selbst kann das nicht, die Fehlermeldung ist im Skript dokumentiert.
3. `fastboot --set-active=a`, dann `erase userdata` und `erase metadata`.
4. **Schreiben, Slot a und b:** `boot`, `dtbo`, `init_boot`, `vendor_boot`, die Firmware-
   Partitionen (`apusys`, `gpueb`, `lk`, `modem`, `scp`, `tee` ...), `preloader`, `vbmeta*`.
5. `fastboot reboot fastboot`, warten, prüfen, dass fastbootd aktiv ist.
6. **Logische Partitionen** (`system`, `vendor`, `product`, `odm` ...) neu anlegen (oder
   `super_empty.img` mit `wipe-super` verwenden, wenn die Datei vorhanden ist) und in Slot a
   schreiben.
7. `fastboot --set-active=a`. Der Abschluss-Hinweis lautet `fastboot reboot`.

**Nebenwirkungen:** **Löscht `userdata`, `metadata` und die logischen Partitionen.** Schreibt
alle Partitionen beider Slots. Die Modem-Partitionen (`nvdata`, `nvcfg`, `protect`, `persist`)
werden **nicht** angefasst.

---

### `flash-debug-b.sh`

**Zweck:** Schreibt unseren Kernel und das Initramfs auf **Slot b** und startet das Telefon.
Slot a bleibt unberührt.

**Aufruf:**

```sh
scripts/flash-debug-b.sh            # Normal
scripts/flash-debug-b.sh --shell    # bleibt immer im Initramfs (tetris.debug=1)
```

**Ablauf:**

1. Läuft ein Android mit `adb`, startet das Skript es per `adb reboot bootloader` neu.
2. Wartet bis zu 60 Sekunden, bis `fastboot devices` ein Gerät findet.
3. Schreibt `boot_b`, `init_boot_b` (leer) und `vendor_boot_b` (`debug-vendor_boot.img` oder
   `-shell`).
4. `fastboot --set-active=b`, dann `fastboot reboot`.

**Rückweg:** `fastboot --set-active=a && fastboot reboot`.

---

### `push-modules.sh`

**Zweck:** Ersetzt die Kernel-Module im **installierten** Rootfs durch die aus `out/`. Nötig,
wenn sich der Kernel geändert hat. Der Kernel lehnt veraltete Module ab (Versionsprüfung).

**Aufruf:** `scripts/push-modules.sh`, mit `SUDO_PASS=…`, wenn das Passwort von `alarm` nicht
mehr `alarm` ist.

**Voraussetzung:** Das Telefon läuft mit Arch und ist über USB erreichbar (`phone-ssh.sh`).

**Ablauf:**

1. Erzeugt ein Modul-Verzeichnis für die aktuelle Kernel-Version aus `out/`, plus
   `tetris_mdtty`, `uleds`, `tetris_novtswitch` in `extra/`. Danach `depmod`.
2. Kopiert das Archiv per SSH nach `/tmp/modules.tgz` auf das Telefon.
3. Auf dem Telefon: Das alte Verzeichnis wird nach `<Version>.old` verschoben. Die neuen
   Module werden entpackt.

**Nach dem Lauf:** Das Telefon neu starten, damit die Module geladen werden.

---

### `setup-phone.sh`

**Zweck:** Die **zweite Stufe**. Alles, was auf dem Telefon selbst gebaut werden muss, und
der GPU-Pfad.

**Aufruf:**

```sh
scripts/setup-phone.sh /pfad/zu/halium/system.img
PHOSH=1 scripts/setup-phone.sh /pfad/zu/system.img    # Phosh statt Nothing-Shell
```

**Eingaben:** Halium-`system.img` (LuneOS Halium 16 GSI), eine Verbindung über USB oder
WLAN mit Internet auf dem Telefon.

**Ablauf:**

1. `system.img` per `scp` auf das Telefon und nach `/var/lib/lxc/android/system.img`.
2. Quellen auf das Telefon kopieren: `pkgbuilds/*.sh`, die wlroots-Patch-Datei, Patches für
   libhybris, `patches/modemmanager/tetris`, `rootfs/src/tetris-camera-app`, `tetris-gnss`,
   `tetris-fp`.
3. Auf dem Telefon ausführen: `build-libhybris.sh`, `build-wlroots-phoc.sh`,
   `build-mm-plugin-tetris.sh`.
4. Kamera-App, GPS-Client und Fingerabdruck-Client mit `make` bauen und installieren.
5. `setup-nothing.sh` (wenn nicht `PHOSH=1`).
6. `touch /etc/tetris-android/gpu-shell` und Neustart.

**Warnung:** Das Skript dauert lange (libhybris und wlroots kompilieren auf dem Telefon). Die
Verbindung darf nicht abreißen.

---

### `setup-nothing.sh`

**Zweck:** Installiert die **Nothing-Shell** (`nothing-wm` und `nothing-shell` im Telefon-Layout)
und schaltet die Oberfläche darauf um. Phosh bleibt installiert.

**Aufruf:**

```sh
scripts/setup-nothing.sh [/pfad/zu/Nothing_Wayland]
```

| Argument / Variable | Standard | Wirkung |
|---|---|---|
| Pfad | `~/Nothing_Wayland`, falls mit `PKGBUILD`; sonst eine Klon-Kopie in `out/Nothing_Wayland` | Quellverzeichnis |
| `NOTHING_REPO` | `https://github.com/bonbon08/Nothing_Wayland.git` | Klon-Quelle |
| `NOTHING_REF` | `main` | Branch oder Tag |
| `SUDO_PASS` | `alarm` | Passwort für `sudo` auf dem Telefon |

**Ablauf:**

1. Prüft, dass `wlroots0.20` auf dem Telefon installiert ist. Sonst bricht es ab, damit pacman
   nicht Arch' Standard-wlroots ohne GPU-Pfad installiert.
2. Installiert Build- und Laufzeitpakete (`gtk4`, `libadwaita`, `squeekboard`, `xwayland`,
   `grim` ...).
3. Kopiert das Quellverzeichnis auf das Telefon und baut dort mit `makepkg`.
4. `pacman -U` installiert das Paket. Danach kommen `tetris-nothing-session` und
   `tetris-phosh-session` aus dem Overlay dazu.
5. Schreibt `nothing` in `/etc/tetris-shell`.

**Zurück:** `sudo rm /etc/tetris-shell && sudo reboot` auf dem Telefon.

---

### `setup-server.sh`

**Zweck:** Macht ein **laufendes** Telefon mit erhaltenen Daten zum Server (Docker, Avahi,
zram, Overlay, `tetris-mode server`).

**Aufruf:** `scripts/setup-server.sh [--with-modem]`

| Argument | Wirkung |
|---|---|
| `--with-modem` | Mobile Daten bleiben als Verbindung aktiv (braucht den Halium-Container) |

**Voraussetzung:** Kernel mit Docker-Optionen (`zcat /proc/config.gz` enthält
`CONFIG_NF_TABLES=y`). Ist das nicht der Fall, bricht das Skript mit einer Meldung ab.

**Ablauf:** Pakete installieren, `rootfs/overlay-server` per `tar` übertragen, den Dienst
`tetris-display-off` aus dem Quelltext übersetzen, `tetris-mode server` ausführen, neu starten.

---

### `setup-desktop.sh`

**Zweck:** Macht ein laufendes Telefon zum **Desktop**-Profil (GNOME über RDP).

**Aufruf:** `scripts/setup-desktop.sh [--with-modem]`

**Voraussetzungen:**

- Kernel mit dem Patch `SO_PEERPIDFD` (`patches/kernel/0001`). Das Skript prüft das mit
  einem Socket-Aufruf auf dem Telefon.
- Für die GPU: der Halium-Container (`setup-phone.sh`). Sonst rendert GNOME auf dem Prozessor.

**Ablauf:**

1. Ruft `setup-server.sh` auf (Server-Profil, Neustart).
2. Wartet, bis das Telefon wieder da ist.
3. Installiert GDM, `gnome-remote-desktop` und den GNOME-Stack.
4. Überträgt `rootfs/overlay-desktop`.
5. Baut libhybris mit der glvnd-Variante auf dem Telefon, wenn der Container installiert ist.
6. `tetris-mode desktop`, Neustart. Dann wartet es auf die Datei
   `~/.config/tetris-rdp-password` und gibt das Passwort aus.

---

### `phone-ssh.sh`

**Zweck:** Öffnet eine SSH-Sitzung zum Telefon mit dem Projektschlüssel. Die Argumente werden
an `ssh` weitergegeben.

```sh
scripts/phone-ssh.sh                          # Shell
scripts/phone-ssh.sh 'uptime'                 # Einzelbefehl
```

Verbindet mit `alarm@172.16.42.1`, nutzt `out/ssh/id_ed25519` und `out/known_hosts`.
`StrictHostKeyChecking=accept-new` nimmt den Telefon-Schlüssel beim ersten Mal an.

---

### `phone-sh.sh`

**Zweck:** Führt einen Befehl im **Initramfs** aus (Telnet, Port 23). Das funktioniert auch,
wenn das Arch-System nicht startet.

```sh
scripts/phone-sh.sh 'dmesg | tail -20' [Sekunden]
```

Das Skript schickt den Befehl über `nc` an `172.16.42.1:23`, wartet die angegebene Zeit
(Standard 3 s) und filtert Steuerzeichen heraus.

---

### `host-nat.sh`

**Zweck:** Teilt die Internetverbindung des PC mit dem Telefon über das USB-Netz. Nötig, wenn
das Telefon Pakete laden oder kompilieren soll.

```sh
sudo scripts/host-nat.sh up     # Standard
sudo scripts/host-nat.sh down
```

**Wirkung:** Setzt `net.ipv4.ip_forward=1` und legt Regeln in `iptables` an (NAT über das
Standard-Netz, Weiterleitung für `172.16.42.0/24`). Die Regeln bleiben bis zum Neustart des PC.
Docker setzt die Weiterleitung auf `DROP`, deshalb gibt es eine eigene Erlaubnis-Regel.

---

### `desktop-rdp.sh`

**Zweck:** Verbindet mit dem GNOME-Desktop des Telefons, in Full HD.

```sh
scripts/desktop-rdp.sh                       # 172.16.42.1 über USB
scripts/desktop-rdp.sh tetris.local /f       # WLAN, Vollbild
```

Liest das RDP-Passwort von dem Telefon (`~/.config/tetris-rdp-password`) und startet
`xfreerdp3` mit 1920 × 1080, H.264 (`AVC444`) und TOFU-Zertifikat (`/cert:tofu`).

---

### `read-flashlog.sh`

**Zweck:** Liest das Protokoll des Initramfs aus `nt_reserve2`. Das ist nützlich, wenn das
Telefon nach einem Start nicht mehr erreichbar ist, aber das Original-System noch startet.

```sh
scripts/read-flashlog.sh out/flashlog1
```

Braucht `adb` und Root (Magisk) auf Nothing OS. Schreibt `init.log` (erste 16 MB) und
`dmesg.txt` (16 bis 18 MB) in das Zielverzeichnis. Details in
[09-fehlersuche.md](09-fehlersuche.md#93-protokolle-finden).

---

## 5.3 Skripte auf dem Telefon (`pkgbuilds/`)

Diese Skripte kopiert `setup-phone.sh` (oder `setup-desktop.sh`) auf das Telefon und führt sie
dort aus. Sie können sie auch direkt auf dem Telefon aufrufen, wenn Sie Quellen und Patches
ins Home-Verzeichnis von `alarm` gelegt haben.

Alle vier verwenden `SUDOPW` (Standard `alarm`) für `sudo`.

### `build-libhybris.sh`

**Zweck:** Baut **libhybris** für das Halium-16-GSI und installiert es nach `/opt/libhybris`.
Ein zweiter Build geht nach `/opt/libhybris-glvnd` (für GNOME im Desktop-Profil).

**Quellen (fest gepinnt):**

| Repository | Branch | Commit |
|---|---|---|
| `Halium/android-headers` | `herrie/halium-16.0` | `ad5912e` |
| `webOS-ports/libhybris` | `herrie/android16-tls` | `92b4e61` |

**Patches:** Alle `~/libhybris-patches/*.patch` (`patches/libhybris/`).

**Besonderheit:** Der Vulkan-Wrapper (`libvulkan.so`) wird nach `/opt/tetris-hybris-vulkan`
verschoben. Er darf nicht neben `libEGL` liegen, sonst stürzen GTK-Apps ab.

---

### `build-wlroots-phoc.sh`

**Zweck:** Baut **wlroots 0.20** aus dem Arch-Paket neu und fügt zwei Patches hinzu:

1. den Patch aus phoc 0.57 („Revert layer-shell: error on 0 dimension without anchors“). Ohne
   ihn beendet sich Phosh, weil die Home-Leiste die Höhe 0 hat;
2. `patches/wlroots/0002-hwcomposer-backend-hybris-allocator.patch`, unser Backend für den
   Android-Composer. Es wird mit `-Dhwcomposer=enabled` aktiviert. Die Bibliothek libhybris
   wird nur geladen, wenn `WLR_BACKENDS=hwcomposer` gesetzt ist. Der normale DRM-Pfad bleibt
   unverändert.

**Ablauf:** Klont das Arch-Paketrepository, ändert das `PKGBUILD` per Python-Skript, installiert
die Abhängigkeiten und baut mit `makepkg -A --skippgpcheck --nocheck`. Das neue Paket
`wlroots0.20-*.pkg.tar.*` wird mit `pacman -U` installiert (das Paket bekommt als
`pkgrel` die bisherige Nummer mit der Endung `.2`).

---

### `build-mm-plugin-tetris.sh`

**Zweck:** Baut nur das **Plugin** `libmm-plugin-tetris.so` für ModemManager und installiert es
nach `/usr/lib/ModemManager/`. Die Quellen von ModemManager werden in der Version geholt, die
auf dem Telefon installiert ist.

**Ablauf:** Liest die Version aus `pacman -Q modemmanager`, klont das Tag, kopiert
`~/mm-plugin-tetris` nach `src/plugins/tetris`, ergänzt `src/plugins/meson.build` um einen
Block (Python-Skript, mit `assert`), baut nur das Ziel `libmm-plugin-tetris.so` und installiert es.

**Nach jedem ModemManager-Update** neu bauen, sonst passt die Plugin-Schnittstelle nicht.

---

### `build-llama-vulkan.sh`

**Zweck:** Baut **llama.cpp** mit dem Vulkan-Backend auf dem Telefon nach `/opt/llama.cpp`
(experimentell). Die Revision ist gepinnt (`5851a8e`, 4. November 2026).

**Laufzeit:** Mit `tetris-llama` (Overlay Server) wird das Modell mit libhybris' Vulkan-Treiber
gestartet. Dafür muss der Halium-Container laufen, weil er den Mali-Treiber unter `/vendor`
bereitstellt. Die GPU hat nur zwei Kerne und 1 GHz, deshalb ist der Gewinn begrenzt.

---

## 5.4 Die Initramfs-Dateien (`initramfs/`)

| Datei | Typ | Zweck |
|---|---|---|
| `init` | Shell-Skript | Startskript, Prozess 1 im Initramfs. Siehe [03-architektur.md](03-architektur.md#schritt-3-das-initramfs-initramfsinit) |
| `modules.load.3` | Liste | Module der dritten Stufe. Eigene Liste, weil sie nicht aus dem Original stammt |
| `modules.blocklist` | Liste | Module, die nicht geladen werden. Heute `apu_top.ko`, `apusys.ko`, `sapu.ko` (Hängen beim Einschalten der KI-Einheit) |
| `tools/reboot-mode.c` | C | Startet das Telefon mit einem Modus neu (`reboot-mode bootloader`), weil busybox das nicht kann |
| `tools/tetris-splash.c` | C | Zeigt den Tux-Splash auf dem Display, bis Compositor oder Composer übernehmen. Beendet wird er von `tetris-splash-stop.service` |

Die Hilfsprogramme sind **freistehend** (ohne libc) und werden mit dem Android-Clang gebaut,
damit sie im Initramfs ohne Bibliotheken laufen.

---

## 5.5 Konfigurations-Dateien für die Skripte

| Datei | Gelesen von | Wirkung |
|---|---|---|
| `kernel/linux.config` | `build-kernel.sh config` | Unsere Kernel-Optionen (Telefon, Docker-Abschnitt „Server profile“) |
| `.gitignore` | git | Whitelist: nur bestimmte Verzeichnisse dürfen ins Repository |
| `initramfs/modules.blocklist` | `build-debug-boot.sh` | siehe oben |
| `initramfs/modules.load.3` | `build-debug-boot.sh` | siehe oben |
| `rootfs/overlay/var/lib/lxc/android/config` | LXC | Konfiguration des Halium-Containers, siehe [03-architektur.md](03-architektur.md#33-der-halium-container) |
| `rootfs/overlay/etc/tetris-android/vendor-init.allow` | `tetris-android-prepare` | Welche Vendor-Init-Dateien der Container ausführen darf |
| `rootfs/overlay-desktop/etc/tetris-software-gl.list` | `tetris-software-gl-desktop-files` | Programme, die auf Mesa statt auf libhybris laufen |

Eine Übersicht aller Overlay-Dateien steht in [06-rootfs-referenz.md](06-rootfs-referenz.md).
