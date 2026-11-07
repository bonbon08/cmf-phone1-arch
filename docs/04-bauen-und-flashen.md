# 4. Bauen und Flashen: Schritt für Schritt

Dieses Kapitel führt vom leeren PC bis zum laufenden Telefon. Es erklärt jeden Schritt,
seine Ein- und Ausgaben und was dabei schiefgehen kann. Die Skripte selbst beschreibt
[05-skript-referenz.md](05-skript-referenz.md) im Detail.

> **Lesen Sie zuerst [Risiken](#42-risiken).** Das Flashen von `userdata` löscht alle Daten auf
> dem Telefon. Ein falsches Image auf den falschen Slot kann das System unbrauchbar machen.

## 4.1 Überblick

```
Phase 0  Eingaben beschaffen (Firmware, Toolchain, Arch-Linux-ARM-Abbild, Busybox)
Phase 1  PC vorbereiten (Pakete, Berechtigungen)
Phase 2  Bauen         fetch-sources → build-kernel → build-debug-boot → build-rootfs
Phase 3  Flashen       [optional: flash-stock] → userdata → flash-debug-b
Phase 4  Erster Start  Schwarzer Bildschirm ist normal. Erreichbar über USB (172.16.42.1)
Phase 5  Zweite Stufe  setup-phone.sh: Halium-Container, libhybris, wlroots, Nothing-Shell
Phase 6  Optional      Server- oder Desktop-Profil (setup-server.sh, setup-desktop.sh)
```

Zeitbedarf: Der Kernel-Build und der Rootfs-Bau dauern auf einem gewöhnlichen PC viele
Minuten bis Stunden. Die zweite Stufe kompiliert libhybris und wlroots **auf dem Telefon**
und braucht entsprechend lange. Planen Sie einen Abend oder mehr ein.

## 4.2 Risiken

| Risiko | Folge | Schutz |
|---|---|---|
| Flashen von `userdata` | **Alle Daten sind weg** (Fotos, Apps, Konten) | Sichern Sie vorher alles, was Sie brauchen |
| Bootloader wird erneut gesperrt | Das Telefon startet nur noch signierte Software. Ein verändertes System bleibt unbrauchbar | Bootloader **nie** wieder sperren, solange Ihr Kernel oder AVB aus ist |
| Modem-Partitionen werden überschrieben | IMEI und Funk-Kalibrierung sind weg. Das Mobilfunk-Modul funktioniert dann nicht mehr | `scripts/` schreibt diese Partitionen nie. Sichern Sie `nvdata`, `nvcfg`, `nvram`, `protect1`, `protect2`, `persist` vor Experimenten (siehe [02](02-geraet-und-partitionen.md#26-was-sie-nie-tun-sollten)) |
| Falscher Slot oder falsches Image | Ein Start schlägt fehl | Das Skript `flash-debug-b.sh` schreibt nur Slot b. Slot a bleibt Nothing OS |
| Kein Suspend | Der Akku hält nur etwa einen Tag, wenn der Bildschirm meist aus ist | Telefon nicht im Standby lassen, Ladekabel verwenden |
| Keine Garantie | Hobby-Projekt, Verantwortung liegt bei Ihnen | |

## 4.3 Eingaben selbst beschaffen

Diese Dateien gehören **nicht** ins Repository. Sie sind proprietär oder zu groß. Legen Sie
sie an den unten genannten Orten ab. Die Skripte finden sie über feste Pfade.

| Pfad | Inhalt | Woher |
|---|---|---|
| `firmware/Tetris_B4.1-260928-1140/images/` | Original-Images von Nothing OS **B4.1** für Tetris: `vendor_boot.img`, `vendor.img`, `boot.img`, `init_boot.img`, `vbmeta.img`, `preloader_raw.img` und alle Partitionen aus den Listen in `flash-stock.sh` | Ihre eigene Telefon-Firmware, z. B. über ein Firmware-Archiv für Tetris (die Liste der Partitionen steht in `scripts/flash-stock.sh`) |
| `toolchain/clang-r487747c/` | Android-Clang **r487747c** aus AOSP `prebuilts/clang/host/linux-x86`, die gleiche Version, die Nothing verwendet | AOSP-Prebuilts (Repository `platform/prebuilts/clang/host/linux-x86`) |
| `downloads/alarm/ArchLinuxARM-aarch64-latest.tar.gz` | Arch-Linux-ARM-Basissystem für aarch64 | [archlinuxarm.org](https://archlinuxarm.org) |
| `downloads/alarm/usr/bin/busybox` | statisch gelinktes busybox für das Initramfs | Paket `busybox` von Arch Linux ARM, entpacken, sodass dieser Pfad existiert |
| Halium-`system.img` (nur Phase 5) | LuneOS-Halium-16-GSI `halium-luneos-16.0-20261017-1-halium_arm64`, entpackt | [webOS-ports/halium-images](https://github.com/webOS-ports/halium-images) |
| `src/` | Kernel-Quellen | holt `scripts/fetch-sources.sh` selbst aus dem Internet |

Die Telefon-Firmware (`firmware/`) und die Partitionsdaten (`stock-info/`) dürfen Sie nur
für Ihr eigenes Gerät verwenden.

### Prüfung der Eingaben

Ein kurzer Check, bevor Sie anfangen:

```sh
cd cmfphone1archLinux
ls firmware/Tetris_B4.1-260928-1140/images/vendor_boot.img   # Firmware da?
ls toolchain/clang-r487747c/bin/clang                          # Compiler da?
ls downloads/alarm/ArchLinuxARM-aarch64-latest.tar.gz          # Basissystem da?
ls downloads/alarm/usr/bin/busybox                             # busybox da?
```

Alle vier Befehle müssen eine Datei anzeigen, ohne Fehlermeldung.

## 4.4 Host-Pakete

Installieren Sie auf dem PC (Arch Linux):

```sh
sudo pacman -S --needed android-tools arch-install-scripts qemu-user-static-binfmt \
    erofs-utils lz4 cpio e2fsprogs git curl
```

Das sind die Pakete laut Haupt-README. Außerdem brauchen die Skripte diese Werkzeuge,
die auf den meisten Systemen schon vorhanden sind: `make`, `gcc`, `bsdtar`, `depmod`,
`ssh`, `scp`, `nc`, `pkg-config`, `ld.lld` und `llvm-strip` (das letzte kommt mit dem
Android-Clang in `toolchain/`). `mkbootimg` und `unpack_bootimg` stammen laut Haupt-README
aus dem Paket `android-tools`. Ist das bei Ihrer Distribution anders, installieren Sie sie
aus dem AOSP-Repository `system/tools/mkbootimg`. Für den RDP-Test des Desktops brauchen
Sie `xfreerdp3`.

Prüfen Sie fehlende Befehle mit:

```sh
for t in git make gcc bsdtar mkbootimg unpack_bootimg fsck.erofs e2fsck resize2fs \
         mkfs.ext4 img2simg lz4 cpio depmod arch-chroot ssh scp nc fastboot adb pkg-config; do
    command -v "$t" >/dev/null || echo "fehlt: $t"
done
```

Für die Zusatzbefehle muss Ihr Benutzer in der Gruppe `wheel` sein oder `sudo` ausführen
dürfen (`build-rootfs.sh` läuft als root).

## 4.5 Phase 2: Bauen

Alle Build-Ergebnisse landen in `out/`. Ein erneuter Lauf überschreibt sie.

### 2a: Quellen holen (`fetch-sources.sh`)

```sh
scripts/fetch-sources.sh
```

- Klont flach (`--depth 1`) die Nothing-Repositorien für Kernel 6.1, die Device-Module,
  die Kernel-Module und das Build-System (`NothingOSS`, Branch `mt6878/Tetris/16b`) nach `src/`.
- Wendet unsere Patches aus `patches/kernel*` an. Bereits angewendete Patches werden
  erkannt und übersprungen. Das Skript ist also **erneut ausführbar**.
- Dauer: vom Netzwerk abhängig, mehrere Gigabyte.

### 2b: Kernel bauen (`build-kernel.sh`)

```sh
scripts/build-kernel.sh            # alles: config, kernel, modules, ext
```

Die Phasen einzeln, falls ein Schritt scheitert:

| Phase | Befehl | Was passiert |
|---|---|---|
| Konfiguration | `build-kernel.sh config` | `gki_defconfig` + Device-Konfiguration + `kernel/linux.config` zusammenführen |
| Kernel | `build-kernel.sh kernel` | `Image.gz` und Module des Kerns |
| Gerätemodule | `build-kernel.sh modules` | Device-Module (`kernel_device_modules-6.1`) |
| Externe Module | `build-kernel.sh ext [ab-Modul]` | WLAN, Bluetooth, GPU, Kamera, GPS (Liste in `EXT_MODULES`), dann unsere Module `tetris_mdtty` und `uleds`. Mit einem Modulnamen fängt es dort an, z. B. `ext connectivity/bt` |

Ausgabe: `out/kernel/` (Build-Verzeichnis), `out/kernel/arch/arm64/boot/Image`,
und `out/kernel/include/config/kernel.release` (die Kernel-Version, die alle weiteren
Skripte lesen).

Warnung „not applied“ aus `config` bedeutet: Eine Option aus `kernel/linux.config` hat der
Build nicht übernommen. Prüfen Sie diese Zeile, sie kann Funktionen wie Docker betreffen.

### 2c: Boot-Images bauen (`build-debug-boot.sh`)

```sh
scripts/build-debug-boot.sh
```

Erzeugt aus dem Kernel und den Modulen drei Images für Slot b plus eine Variante mit
Notausgang:

| Datei | Partition | Inhalt |
|---|---|---|
| `out/debug-boot.img` | `boot_b` | Kernel (`Image.lz4`) |
| `out/debug-vendor_boot.img` | `vendor_boot_b` | stock-DTB, Kommandozeile, unser Initramfs |
| `out/debug-vendor_boot-shell.img` | (Alternative zu `vendor_boot_b`) | wie oben, mit `tetris.debug=1`: bleibt immer im Initramfs |
| `out/debug-init_boot.img` | `init_boot_b` | **leer**, damit Androids `/init` nicht dazwischenfunkt |

Das Skript prüft beim Lauf die Liste der Module (`initramfs/modules.load.3`,
`initramfs/modules.blocklist`) und meldet fehlende Module als „not built“. Das ist bei
den Modulen der Liste aus dem Original-System normal, wenn das Gerät sie nicht
braucht. Fehlt ein Modul aus `modules.load.3`, ist das ein echter Fehler.

Beim ersten Lauf entpackt das Skript außerdem die Firmware aus `vendor.img` nach
`out/vendor-extract/`, weil der Kernel sie beim Laden der Module braucht.

### 2d: Rootfs bauen (`build-rootfs.sh`)

```sh
sudo scripts/build-rootfs.sh               # Telefon-Profil
sudo PROFILE=server scripts/build-rootfs.sh
sudo PROFILE=desktop scripts/build-rootfs.sh
```

Das Skript (läuft als root) baut das Dateisystem für die `userdata`-Partition. Die Schritte
sind nummeriert, damit man sie bei einem Fehler wiederfindet:

1. Erzeugt ein leeres ext4-Abbild `out/rootfs.img` (Größe `SIZE`, Standard `10G`).
2. Entpackt das Arch-Linux-ARM-Basissystem und kopiert die Kernel-Module hinein.
3. Kopiert die Overlays (`rootfs/overlay`, je nach Profil `-server`, `-desktop`) und die
   Firmware aus `out/vendor-extract/`.
4. Erzeugt einen SSH-Schlüssel `out/ssh/id_ed25519` (falls nicht vorhanden). Ihre eigenen
   öffentlichen Schlüssel aus `~/.ssh/*.pub` kommen ebenfalls auf das Telefon.
5. Installiert die Pakete in einer **aarch64-chroot** über QEMU (das dauert am längsten).
6. Kreuzkompiliert unsere C-Programme mit dem Android-Clang gegen die Bibliotheken im
   Rootfs.
7. Aktiviert die Dienste und schreibt `out/rootfs.simg`, die Sparse-Version für fastboot.

Ausgabe: `out/rootfs.img` und `out/rootfs.simg`.

Die Option `--finish` arbeitet auf einem bestehenden `out/rootfs.img`. Sie entpackt nichts neu
und installiert keine Pakete. Sie wendet Overlay und Dienste erneut an und verkleinert das
Abbild. Das ist schnell, wenn Sie nur Dateien aus `rootfs/overlay` geändert haben.

Mehr zu den Paketen und Profilen: [05-skript-referenz.md](05-skript-referenz.md#build-rootfssh).

## 4.6 Phase 3: Flashen

Das Telefon muss in den **Fastboot-Modus**. Schalten Sie es aus. Halten Sie dann
**Power + Lautstärke runter**, bis der Fastboot-Bildschirm erscheint. Verbinden Sie es per
USB mit dem PC.

Prüfen Sie die Verbindung:

```sh
fastboot devices                    # zeigt eine Seriennummer
fastboot getvar product             # erwartet: product: k6878v1_64
```

### 3a: Optional: Stock-System neu schreiben (`flash-stock.sh`)

Nur nötig, wenn Sie von einem unbekannten oder beschädigten Zustand ausgehen. Das Skript
schreibt Nothing OS B4.1 auf **beide** Slots, setzt AVB außer Kraft (nötig für unseren
Kernel) und löscht `userdata` und `metadata`:

```sh
scripts/flash-stock.sh firmware/Tetris_B4.1-260928-1140/images
```

Das Skript bricht ab, wenn ein Image fehlt oder die Produkt-Kennung nicht
`k6878v1_64` ist. Es wechselt in den fastbootd-Modus (`fastboot reboot fastboot`), legt logische
Partitionen neu an und schreibt sie. Das dauert einige Minuten und ist normal.

Mit `--keep-avb` bleibt die Original-`vbmeta` erhalten. Dann startet unser Kernel nicht.

### 3b: Daten-Partition (`userdata`)

```sh
fastboot flash userdata out/rootfs.simg     # LÖSCHT ALLE DATEN
```

### 3c: Kernel und Initramfs auf Slot b (`flash-debug-b.sh`)

```sh
scripts/flash-debug-b.sh                    # Normal: Notausgang deaktiviert
scripts/flash-debug-b.sh --shell            # Variante: bleibt immer im Initramfs
```

Das Skript:

1. Wartet, bis ein Gerät im Fastboot-Modus erscheint (und startet ein laufendes Android per
   `adb reboot bootloader` neu).
2. Schreibt `boot_b`, `init_boot_b` und `vendor_boot_b`.
3. Setzt Slot b aktiv (`fastboot --set-active=b`).
4. Startet das Telefon (`fastboot reboot`).

Slot a bleibt unverändert. Der Rückweg ist `fastboot --set-active=a && fastboot reboot`.

## 4.7 Phase 4: Erster Start

Nach dem ersten Start bleibt der **Bildschirm schwarz**. Das ist erwartet. Das
Telefon ist aber schon im Netz:

- **Initramfs:** Das Telefon meldet sich über USB als Netzwerkgerät an
  (`172.16.42.1`), Telnet läuft auf Port 23.
- **Arch Linux:** Startet das System, bleibt der Bildschirm schwarz. Die Standard-Version
  von wlroots in Arch fehlt ein Patch für `phoc` (Schicht-Oberfläche, `layer-shell`), deshalb
  beendet sich Phosh. Die zweite Stufe baut die richtige Version (`build-wlroots-phoc.sh`).

### Verbindung über USB einrichten (PC)

Der PC braucht eine feste Adresse im selben Netz:

```sh
sudo nmcli c add type ethernet con-name tetris-usb match.driver cdc_ncm \
    ipv4.method manual ipv4.addresses 172.16.42.2/24 ipv6.method ignore
```

Teilen Sie dem Telefon Internet über den PC mit (nur nötig für Phase 5 und 6):

```sh
sudo scripts/host-nat.sh up         # NAT für 172.16.42.0/24 über das Standard-Netz des PC
```

Test:

```sh
scripts/phone-ssh.sh                # ssh alarm@172.16.42.1 mit dem Projekt-Schlüssel
```

Das Passwort des Benutzers `alarm` ist `alarm`. **Ändern Sie es sofort** mit `passwd`,
weil SSH aktiv ist.

Notfall-Shell im Initramfs (ohne Arch): `scripts/phone-sh.sh 'dmesg | tail'` führt einen
Befehl über Telnet aus.

## 4.8 Phase 5: Zweite Stufe (`setup-phone.sh`)

Die zweite Stufe installiert alles, was **auf dem Telefon selbst** gebaut werden muss, und
den GPU-Pfad:

```sh
scripts/setup-phone.sh /pfad/zu/halium/system.img
```

Ablauf des Skripts:

1. Kopiert das Halium-`system.img` nach `/var/lib/lxc/android/`.
2. Kopiert die Quellen und Patches auf das Telefon.
3. Baut auf dem Telefon: **libhybris** (`build-libhybris.sh`), **wlroots 0.20** mit
   unserem Backend (`build-wlroots-phoc.sh`), das **ModemManager-Plugin**
   (`build-mm-plugin-tetris.sh`).
4. Baut und installiert Kamera-App, GPS-Client und Fingerabdruck-Client.
5. Installiert die **Nothing-Shell** (`setup-nothing.sh`), außer mit `PHOSH=1`.
6. Schaltet den GPU-Modus ein (`/etc/tetris-android/gpu-shell`) und startet neu.

```sh
PHOSH=1 scripts/setup-phone.sh /pfad/zu/system.img    # Phosh statt Nothing-Shell
```

Nach dem Neustart startet die Oberfläche auf der GPU. Schlägt das fehl, fällt das System
automatisch auf den Software-Modus zurück. Prüfen Sie es mit `cat /run/tetris-phosh/mode`
(auf dem Telefon). Der Wert ist `gpu` oder `sw`.

Die Nothing-Shell kann auch allein installiert werden:

```sh
scripts/setup-nothing.sh [~/Nothing_Wayland]
```

Das braucht das Quellverzeichnis `Nothing_Wayland` (mit `PKGBUILD`) oder klont es. Die
Nothing-Shell braucht unsere wlroots-Version, sonst zieht pacman Arch' Standard-wlroots nach.

## 4.9 Phase 6: Server- und Desktop-Profil

Mit einem laufenden Telefon (Phase 4 erledigt, Kernel von diesem Branch auf Slot b):

```sh
scripts/setup-server.sh [--with-modem]     # Docker, Avahi, zram, Server-Profil
scripts/setup-desktop.sh [--with-modem]    # wie Server, plus GNOME über RDP
```

Diese Skripte brauchen den Kernel mit den Docker-Optionen (`kernel/linux.config`). Das
Desktop-Skript braucht zusätzlich den Patch `SO_PEERPIDFD` (`patches/kernel/0001`). Beide
prüfen das am Anfang und brechen mit einer klaren Meldung ab, wenn der Kernel nicht passt.

Details: [08-profile.md](08-profile.md).

## 4.10 Rückweg

| Ziel | Befehl |
|---|---|
| Zurück auf Nothing OS (Slot a) | `fastboot --set-active=a && fastboot reboot` |
| Komplett Nothing OS neu installieren (**löscht Daten**) | `scripts/flash-stock.sh firmware/…/images` |
| Vom Server/Desktop zurück zum Telefon | `sudo tetris-mode phone && sudo reboot` auf dem Telefon |
| Nur einen Start im Server/Desktop | App **Startmodus** (siehe [08](08-profile.md)) |
| Zurück auf Phosh | `sudo rm /etc/tetris-shell && sudo reboot` |
| Im Notfall in das Initramfs | Lautstärketaste beim Start halten (siehe [03](03-architektur.md#schritt-3-das-initramfs-initramfsinit)) |

## 4.11 Abbild aktualisieren

Nach Änderungen am Kernel oder den Modulen:

- **Nur Module ändern sich** (Kernel gleich geblieben): `scripts/push-modules.sh` schickt
  die neuen Module per SSH auf das laufende Telefon.
- **Kernel geändert:** `build-kernel.sh`, `build-debug-boot.sh`, `flash-debug-b.sh`, dann
  `push-modules.sh` (die Module müssen zur Kernel-Version passen, sonst lehnt der Kernel
  sie ab).
- **Rootfs geändert** (Dateien aus `rootfs/overlay`): Am einfachsten auf dem Telefon die
  einzelne Datei kopieren. Ein neues Abbild ist aufwendig, aber sauber.

## 4.12 Häufige Fehler beim Bauen

| Symptom | Ursache | Abhilfe |
|---|---|---|
| `missing …/ArchLinuxARM-aarch64-latest.tar.gz` | Eingabe fehlt | Abschnitt 4.3 |
| `run as root` bei `build-rootfs.sh` | falscher Aufruf | `sudo scripts/build-rootfs.sh` |
| `!! device is not k6878v1_64` | falsches Gerät oder Fastboot nicht aktiv | Gerät neu in Fastboot starten, `fastboot getvar product` prüfen |
| `!! not in fastbootd` | Der Wechsel in fastbootd ist fehlgeschlagen | Skript erneut starten. Das Gerät muss dafür **im Fastboot-Modus** sein |
| Telefon lehnt Module ab (`Exec format error`, `version magic`) | Module stammen von einem anderen Kernel-Build | `scripts/push-modules.sh` nach dem Kernel-Build, oder `build-kernel.sh all` |
| `pacman` schlägt in der chroot fehl | QEMU-Binfmt nicht aktiv | `qemu-user-static-binfmt` installieren und den Dienst `systemd-binfmt` neu starten |
| Telefon erreicht `172.16.42.1` nicht | USB-Netz des PC nicht eingerichtet | Befehl `nmcli` oben, Kabel prüfen, `ip addr` auf dem PC |

Weitere Fehler und Protokolle: [09-fehlersuche.md](09-fehlersuche.md).
