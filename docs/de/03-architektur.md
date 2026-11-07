# 3. Architektur

**🇩🇪 Deutsch** · [🇬🇧 English](../03-architecture.md)

Dieses Kapitel zeigt, wie die Teile des Systems zusammenspielen. Es beschreibt den Weg
vom Einschalten bis zur Oberfläche, die Rolle des Android-Containers und die beiden
kritischen Pfade: Grafik und Mobilfunk. Begriffe wie Kernel oder Initramfs sind in
[01-grundlagen.md](01-grundlagen.md) erklärt.

## 3.1 Überblick

```
             PC (x86-64, Arch Linux)
  scripts/ ──► out/ (Kernel, Module, Images, rootfs.simg)
                  │  fastboot
                  ▼
 ┌───────────────────────── Telefon ─────────────────────────┐
 │ Preloader ─► LK (Bootloader)                              │
 │               │ liest boot_b, vendor_boot_b, init_boot_b  │
 │               ▼                                           │
 │  Kernel 6.1 (unser) + Initramfs (busybox, Module, NCM)    │
 │               │ switch_root                               │
 │               ▼                                           │
 │  systemd (Arch Linux ARM auf userdata)                    │
 │    ├─ Halium-Container "android" (lxc)                    │
 │    │    └─ Android-HALs: gralloc, composer, modem,        │
 │    │       camera, GNSS, fingerprint, power               │
 │    ├─ Oberfläche: Nothing-Shell oder Phosh (Compositor)   │
 │    ├─ Dienste: Modem, WLAN, Bluetooth, Sensoren, ...      │
 │    └─ Profil: Server (Docker, Ollama), Desktop (GNOME)    │
 └───────────────────────────────────────────────────────────┘
```

Die wichtigste Idee: **Der Kernel und die Systemsoftware sind unsere**, die
Hardware-Treiber, die nur in Android-Form existieren, laufen in einem **Container**, und
Linux spricht sie über eine Brücke an.

## 3.2 Die Startkette im Detail

### Schritt 1: Preloader und LK

Der Chip startet einen fest eingebauten **Preloader** (`preloader_a/b`). Er lädt den
Bootloader **LK** aus `lk_a` bzw. `lk_b`. LK wählt den aktiven A/B-Slot (sein Zustand liegt im Bereich `misc`,
siehe [02-geraet-und-partitionen.md](02-geraet-und-partitionen.md#23-die-ab-slots))
und lädt Kernel und Ramdisk.

Besonderheit bei diesem Telefon, aus `initramfs/` und `scripts/build-debug-boot.sh`
belegt:

- LK ignoriert die Ramdisk im Boot-Image. Er **hängt die Ramdisks von `vendor_boot` und
  `init_boot` aneinander**.
- `init_boot` ist nur 8 MB groß. Darum liegt unser Initramfs (busybox und alle Module)
  in `vendor_boot` (64 MB). `init_boot_b` wird **leer** geflasht, damit Androids eigene
  `/init` nicht dazwischenfunkt.
- LK hat kein `fastboot boot`. Jeder Test heißt daher: auf Slot b flashen und starten.

### Schritt 2: Der Kernel

Der Kernel (`out/Image.lz4`, im Boot-Image `debug-boot.img`) ist die Nothing-Quelle mit
unseren Patches und der Konfiguration aus `kernel/linux.config` (siehe
[07-kernel-treiber-patches.md](07-kernel-treiber-patches.md)). Er startet, erkennt die
Hardware über den **Device Tree** (die Hardwarebeschreibung aus `vendor_boot`) und
übergibt die Kontrolle an das Initramfs.

### Schritt 3: Das Initramfs (`initramfs/init`)

Das Initramfs ist ein kleines Dateisystem im Speicher, das aus busybox und unseren
Helfern besteht. `initramfs/init` ist ein Shell-Skript und läuft als Prozess 1. Es
macht der Reihe nach:

1. **Grundumgebung:** mountet `/proc`, `/sys`, `/dev`, `/tmp` und `configfs`.
2. **Protokoll:** Alle Meldungen gehen nach `/tmp/init.log`. Zusätzlich speichert der Dienst
   sie über `pstore` (`/dev/pmsg0`, überlebt einen Warmstart) und nach dem Start des
   UFS-Speichers in der Partition `nt_reserve2`. Das ist die Grundlage der Fehlersuche, siehe
   [09-fehlersuche.md](09-fehlersuche.md#93-protokolle-finden).
3. **Treiber-Stufe 1:** lädt die Module aus `modules.load` (die Reihenfolge des
   Original-Systems, inklusive UFS-Treiber).
4. **Treiber-Stufe 2 und 3:** lädt `modules.load.2` (aus der Liste des Original-Systems,
   `vendor_dlkm`) und `modules.load.3` (unsere eigene Liste in `initramfs/modules.load.3`).
   Module in `initramfs/modules.blocklist` werden übersprungen. Ein Beispiel ist `apusys`, das den
   Start nach dem Einschalten der KI-Einheit (APU) hängen ließ.
5. **WLAN-Kalibrierung:** Der WLAN-Treiber `gen4m` registriert sich erst, wenn er die
   NVRAM-Daten aus der Partition `nvdata` bekommen hat. Das Skript liest die Datei
   `APCFG/APRDEB/WIFI` und schreibt sie in einem Stück an `/dev/wmtWifi`. Das muss vor dem
   ersten Entsperren des Bildschirms passieren.
6. **USB-Netzwerk:** Die Initramfs richtet einen USB-Gadget-Dienst (NCM) ein. Das Telefon
   ist dann unter `172.16.42.1` erreichbar. Außerdem laufen ein DHCP-Server
   (`udhcpd`) für den PC und ein Telnet-Server (`telnetd` auf Port 23) als Notfall-Shell.
7. **Bildschirm:** Der Splash-Screen `tetris-splash` zeigt den Linux-Pinguin Tux, bis
   Compositor oder Composer den Bildschirm übernehmen.
8. **Notausgang:** Halten Sie in den ersten 3 Sekunden eine **Lautstärketaste** gedrückt,
   bleibt das Telefon im Initramfs (Debug-Modus). Das Boot-Image
   `debug-vendor_boot-shell.img` setzt die Kernel-Option `tetris.debug=1`. Damit bleibt das
   Telefon immer im Initramfs.
9. **Umschalten:** Gibt es ein Arch-Linux-Rootfs auf `userdata` (ext4, mit
   `/usr/lib/systemd/systemd`) und keine Sperr-Datei `/.tetris-debug`, wird mit
   `switch_root` auf das Rootfs gewechselt. Dort startet `systemd` als Prozess 1.

Das Initramfs bleibt nie stehen, weil Prozess 1 nicht beendet werden darf (der Kernel
würde sonst in Panik gehen). Am Ende läuft eine Endlosschleife, die einen Herzschlag
ins Protokoll schreibt.

### Schritt 4: systemd im Rootfs

Nach dem `switch_root` startet das richtige System. Die Reihenfolge der wichtigsten
Dienste wird in den Unit-Dateien festgelegt (`rootfs/overlay/etc/systemd/system/`, Details
in [06-rootfs-referenz.md](06-rootfs-referenz.md#63-systemd-dienste)):

- `tetris-growfs`: vergrößert beim ersten Start das Dateisystem auf die volle `userdata`.
- `tetris-cpuidle`: schaltet zwei tiefe Schlafzustände der CPU ab (`mcusysoff*` und
  `system-vcore`). Ohne USB und Display würde das Telefon sonst zurücksetzen.
- `tetris-splash-stop`: beendet den Splash-Screen, bevor der Compositor kommt.
- `tetris-android`: startet den Halium-Container (siehe unten).
- `tetris-phosh` oder `tetris-phosh.service` mit `tetris-phosh-session`: startet die
  Oberfläche (Nothing-Shell über `/etc/tetris-shell`, sonst Phosh).
- `tetris-boot-success`: meldet dem Bootloader einen erfolgreichen Start.

## 3.3 Der Halium-Container

### Was er ist

Der Container `android` wird mit **LXC** (Linux Containers) gestartet. Er enthält ein
minimales **LuneOS-Halium-16-Android** (GSI, „Generic System Image“), das im Container als
`/system` liegt. Zusätzlich holt er die hardwarespezifischen Partitionen `vendor`, `odm`
und `vendor_dlkm` aus dem Original-System.

Das Konfigurationsdatei ist `rootfs/overlay/var/lib/lxc/android/config`. Die wichtigsten
Punkte:

- **Eigener Netzwerk-Namensraum** (`lxc.net.0.type = empty`). Android sieht weder das WLAN
  noch die Routen des Hosts. Es kann dem Host das Netzwerk nicht wegnehmen.
- **Eingeschränkte Rechte:** `lxc.cap.drop = mac_admin mac_override sys_boot`. Android darf
  den Host nicht neu starten. Ein früherer Fehler hatte genau das beim Absturz von `vold`
  getan.
- **Kein Suspend:** `/sys/power` wird nur lesbar eingehängt. Android kann das System also
  nicht in den Schlaf schicken, was hier nicht funktioniert.
- **Eingeschränkter Init:** Die Datei `/etc/tetris-android/vendor-init.allow` nennt die
  einzigen Vendor-Init-Dateien, die Android ausführen darf. Die übrigen würden USB,
  WLAN und Bluetooth umkonfigurieren, und das machen wir selbst.

### Der Start in drei Schritten

`tetris-android.service` führt nacheinander aus:

1. **`ExecStartPre=/usr/local/bin/tetris-android-prepare`** bereitet alles vor:
   - hängt das GSI-Abbild `system.img` schreibgeschützt ein und legt darüber ein
     beschreibbares **Overlay** (Dateisystem-Schichtung), weil Androids Init in `/`
     Verzeichnisse anlegen muss;
   - nutzt `tetris-map-super`, um `vendor`, `odm` und `vendor_dlkm` aus `super` (Slot a)
     als Mapper-Geräte zu öffnen und schreibgeschützt einzuhängen. Das ist die
     Android-Methode, die der erste Start von Android selbst macht;
   - entfernt eine Manifest-Datei des Software-Codecs und legt eine Kopfzeile für
     `netbpfload.rc` an, damit die Bibliothek `libmeminfo` (API 36) startet;
   - richtet `binderfs` ein und legt die klassischen Pfade `/dev/binder`, `/dev/hwbinder`
     und `/dev/vndbinder` an, die libhybris erwartet;
   - bindet die APEX-Module (Android-Pakete) ein über `tetris-mount-apexes`.
2. **`ExecStart=lxc-start -F -n android`** startet den Container im Vordergrund. Dessen Protokoll
   liegt unter `/var/log/lxc-android.log`.
3. **`ExecStartPost=-/usr/local/bin/tetris-android-start-hals`** startet die HALs
   (Hardware Abstraction Layer, die Android-Schnittstellen zur Hardware). Das Skript wartet,
   bis der Servicemanager bereit ist, startet dann `vendor.gralloc-v2` (Speicher für Bilder),
   `mtkpower` (für die Kamera) und `pq` (Bildverbesserung). Den Composer
   (`vendor.hwcomposer-3-2`) startet es nur, wenn die GPU-Oberfläche das Display übernimmt.
   Der Grund: Läuft der Composer, während ein anderer Prozess das Display hält, dreht er
   in einer Schleife mit Echtzeit-Priorität. Das hat schon einen Watchdog-Reset ausgelöst.

### Wer nutzt den Container?

| Dienst | Was er im Container nutzt |
|---|---|
| `tetris-modem` | `ccci_mdinit`, das Modem-Firmware startet (siehe 3.5) |
| `tetris-gnss` | GNSS-HAL für GPS |
| `tetris-fp` | Goodix-Fingerabdruck-HAL und TEE-Dienste |
| `tetris-camera` | Kamera-HAL, Codec2-HAL, `mtkpower` (on demand, kostet Strom) |
| `tetris-phosh` / Nothing-Shell (GPU-Modus) | Gralloc und Composer über libhybris |
| Apps mit GPU (`/opt/libhybris`) | Mali-Treiber über libhybris |

Der Container hat seine eigenen Prozesse, Dateien und Dienste. Auf dem Host sind nur die
Ergebnisse sichtbar: Geräteknoten, Sockets und Speicherbereiche, die über Bind-Mounts
freigegeben wurden.

## 3.4 Der GPU-Pfad (Grafik)

Die Mali-GPU kann nur mit dem Android-Treiber betrieben werden. Der Weg eines Bildes:

```
App (GTK/Qt/SDL) ──EGL──► libhybris ──► Mali-Treiber (/vendor, im Container)
                                              │
Compositor (nothing-wm oder phoc, wlroots      ▼
  mit unserem hwcomposer-Backend) ◄── gralloc (Speicher für Bilder)
        │                                     │
        └──────► Composer-HAL (Android) ──────► Display (DRM)
```

- **libhybris** (`/opt/libhybris`) lädt Android-Bibliotheken so, dass normale Programme sie
  nutzen können. Die Bausteine liegen in `patches/libhybris/`.
- **wlroots 0.20** mit unserem **hwcomposer-Backend** (`patches/wlroots/0002-…`). Es gibt
  Bilder an den Android-Composer ab, statt selbst über DRM zu scannen.
- **Konfiguration** für Apps: `/etc/tetris-android/hybris.env` und `/etc/tetris-android/gpu-shell`.

### Entscheidung beim Start: GPU oder Software?

`tetris-phosh-select` läuft vor jedem Start der Oberfläche (`ExecStartPre`):

| Bedingung | Ergebnis |
|---|---|
| Datei `/run/tetris-phosh/force-sw` existiert | Software (für diesen Start fest) |
| Crash-Marker `/var/lib/tetris-android/gpu-attempt` existiert | Software. Der letzte GPU-Start ist nicht 90 Sekunden gut gelaufen |
| `/etc/tetris-android/gpu-shell` existiert und der Composer läuft | **GPU** |
| sonst | Software |

Im **GPU-Modus** setzt das Skript den Marker und startet einen Timer von 90 Sekunden. Erst
wenn der Timer ihn entfernt hat, gilt der Start als erfolgreich. Stürzt die GPU-Sitzung
vorher ab, bleibt der Marker stehen, und der nächste Start fällt auf Software zurück.

Im **Software-Modus** beendet das Skript den Composer-Dienst, damit der Compositor das
Display über DRM selbst öffnen kann.

Die Crash-Schutz-Logik ist in [09-fehlersuche.md](09-fehlersuche.md#99-crash-schutz) beschrieben.

### Die Desktop-Variante

Im Desktop-Profil läuft GNOME ohne Display (`gnome-headless-session@alarm`). Der GNOME-Shell
nutzt die GPU über eine zweite Build-Variante von libhybris als **glvnd-EGL-Anbieter**
(`/opt/libhybris-glvnd`). Die Datei `/usr/local/bin/tetris-gnome-shell` und die
Umgebungsvariablen in `user-environment-generators/60-tetris-hybris-gl` steuern das.
Apps, die mit libhybris abstürzen, stehen in `/etc/tetris-software-gl.list` und laufen über
Mesa auf dem Prozessor.

## 3.5 Der Modem-Pfad (Mobilfunk)

```
ModemManager (Host) ──AT-Befehle──► /dev/ttyMD0 (Paar aus tetris_mdtty)
                                            │
          tetris-mdtty-bridge ◄── tetris_mdtty.ko (Kernel-Modul, erzeugt das Tty-Paar
          ttyMD0 + ttyMD1; die Brücke belegt ttyMD1)
                                            │
          ccci_mdinit (im Container) ──► Modem-CPU (Firmware aus modem_a/b)
                                            │
          Plugin libmm-plugin-tetris.so ◄── ModemManager lädt es (patches/modemmanager/tetris)
```

- **`tetris-modem.service`** startet nach dem Container die Brücke `tetris-mdtty-bridge`.
  Das Modem braucht rund 35 Sekunden, bis es antwortet. Deshalb steht die Reihenfolge
  in der Unit-Datei mit Absicht (`After=` ohne `Before=ModemManager`).
- **`tetris-android-start-modem`** wartet, bis `ccci_mdinit` die Modem-CPU gestartet hat
  (erkennbar am Wert `md1:4` in `/sys/kernel/ccci/boot`). Die Firmware ist schon vom
  Bootloader geladen. `ccci_mdinit` stellt das NVRAM-Dateisystem bereit, das die
  Modem-CPU braucht. Die Partitionen mit IMEI und Funk-Kalibrierung werden nur **innerhalb**
  des Containers und nur über ihren Namen eingehängt, damit das Host-System sie nicht
  anfasst.
- **Die Netzwerk-Erkennung** läuft über udev-Regeln (`78-tetris-modem.rules`): `ttyMD0` wird
  als AT-Port für ModemManager markiert, `ttyMD1` bleibt der Brücke vorbehalten, und das
  Datennetz `ccmni1` wird erst freigegeben, wenn `ttyMD0` existiert.
- **Das Plugin** `patches/modemmanager/tetris` macht aus dem Modem ein Gerät für
  ModemManager. Es nutzt die AT-Befehle und die Datenverbindung des MediaTek-Modems.
  Die Verbindung steht mit IPv4 und IPv6, ohne Konfiguration des Zugangspunkts (APN).
- **Anrufe fehlen.** Abgehende Anrufe verbinden, aber ohne Ton. Eingehende Anrufe laufen
  über einen MediaTek-eigenen Kanal (MIPC), den ModemManager nicht kennt. Die Versuche
  dazu liegen in `rootfs/src/tetris-mipc` und `rootfs/src/tetris-voicecall`, sie sind nicht
  Teil des Builds.

## 3.6 Weitere Teilsysteme

| Teil | Wie es funktioniert | Wo |
|---|---|---|
| **WLAN** | Treiber `gen4m`, registriert sich bei `conninfra` (Kombi-Chip). NetworkManager verwaltet die Verbindung. Stromsparmodus aus im Server-Profil | `tetris-wlan`, `tetris-wlan-on`, `overlay/usr/lib/NetworkManager/conf.d/` |
| **Bluetooth** | MediaTek-Gerät `/dev/stpbt` wird über `tetris-bt-bridge` an `/dev/vhci` angebunden. BlueZ spricht dann mit dem Chip | `tetris-bt-start`, `rootfs/src/tetris-bt-bridge.c` |
| **Sensoren** | `tetris-sensors` liest den SCP-Sensor-Hub und stellt den D-Bus-Dienst `net.hadess.SensorProxy` bereit. Die Nothing-Shell dreht den Bildschirm damit | `rootfs/src/tetris-sensors/` |
| **Audio** | ALSA mit UCM-Profil für MT6878 und MT6369. Lautsprecher und Mikrofon funktionieren | `overlay/usr/share/alsa/ucm2/conf.d/mt6878-mt6369/` |
| **Vibration** | `tetris-vibra` macht den Motor zu einem Force-Feedback-Gerät für feedbackd | `rootfs/src/tetris-vibra.c` |
| **Taschenlampe** | `tetris-torch` steuert die LED über das `uleds`-Modul als Blitz-Klasse (`white:torch`) | `rootfs/src/tetris-torch/` |
| **Kamera** | Kamera-HAL im Container. Die App `Kamera` (`tetris-camera-app`) nutzt Camera2-NDK über libhybris | `rootfs/src/tetris-camera-app/` |
| **Fingerabdruck** | `tetris-fp` (Registrierung, Sensor-Abfrage) und `tetris-fpd` (Entsperren). Der Sensor sitzt unter dem Glas, der Bereich wird über `tetris-fp-overlay` markiert | `rootfs/src/tetris-fp/`, `rootfs/overlay/usr/local/bin/tetris-fp*` |
| **GPS** | `tetris-gnss` liest NMEA aus der GNSS-HAL und speist geoclue. Der Empfänger ist nur an, wenn eine App fragt | `rootfs/src/tetris-gnss/` |
| **Thermik** | `tetris-thermal` drosselt in drei Stufen (70/80/90 °C SoC) und schaltet bei 100 °C ab | `overlay/usr/local/bin/tetris-thermal` |
| **Display und Berührung** | Der Patch `0003` (Hochhelligkeitsmodus des Panels) meldet dem Fingerabdruck-Dienst, wann der Scan läuft. Der Patch `0004` sorgt dafür, dass der Touch-Controller nach dem Schlaf wieder reagiert | `patches/kernel_device_modules/` |

## 3.7 Die Profile in der Architektur

Ein Profil ist eine Sammlung aus Diensten, Overlay-Dateien und Startzielen. Die Umschaltung
ändert nur die Startreihenfolge und aktiviert oder deaktiviert Dienste. Die Dateien
bleiben gleich. Das ist der Grund, warum man jederzeit zwischen den Profilen wechseln
kann.

| Profil | Overlay | Startziel | Display | Besonderheit |
|---|---|---|---|---|
| Phone | `overlay` | `graphical.target` | an, Oberfläche | Standard |
| Server | `overlay` + `overlay-server` | `multi-user.target` | aus (`tetris-display-off`) | Docker, Ollama, Avahi (`tetris.local`), zram |
| Desktop | + `overlay-desktop` | `graphical.target` (GDM) | aus, GNOME über RDP | GNOME-Session ohne Display |

Die Umschaltung im Detail steht in [08-profile.md](08-profile.md).

## 3.8 Wo Dateien landen

| Ort auf dem Telefon | Herkunft | Zweck |
|---|---|---|
| `/` (ext4 auf `userdata`) | `out/rootfs.simg`, dann Updates | Arch-Linux-System |
| `/usr/lib/modules/<Version>/` | `scripts/build-rootfs.sh` oder `push-modules.sh` | Kernel-Module |
| `/var/lib/lxc/android/` | `setup-phone.sh` (`system.img`) | Container-Konfiguration und GSI |
| `/var/lib/tetris-android/` | `tetris-android-prepare` | Overlay, `vendor`-Einhängung, Crash-Marker |
| `/etc/tetris-android/` | Overlay | Container-Init-Liste, GPU-Schalter, Umgebungsvariablen |
| `/opt/libhybris/`, `/opt/libhybris-glvnd/` | `pkgbuilds/build-libhybris.sh` | Android-Brücke für Apps |
| `/opt/llama.cpp/` | `pkgbuilds/build-llama-vulkan.sh` | KI-Laufzeit auf der GPU (experimentell) |
| `/var/lib/tetris-mode/` | `tetris-boot-as`, `tetris-mode` | Einmal-Profil (`once`) |
| `/run/…` | Systemd und Generatoren | Alles, was nur für diesen Start gilt |
