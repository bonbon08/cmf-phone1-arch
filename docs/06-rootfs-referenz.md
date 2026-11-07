# 6. Rootfs-Referenz: Dateien und Dienste auf dem Telefon

Das **Rootfs** ist das Dateisystem des Arch-Linux-Systems auf dem Telefon. Dieses Kapitel
erklärt, was unser Projekt darin ergänzt oder ändert. Die Grundlage ist ein offizielles
Arch-Linux-ARM-Abbild. Alles, was wir hinzufügen, liegt im Verzeichnis `rootfs/`.

## 6.1 Was ist ein Overlay?

Ein **Overlay** ist ein Verzeichnisbaum, dessen Inhalt in das Rootfs kopiert wird
(`cp -a rootfs/overlay/. <rootfs>/`). Gleichnamige Dateien überschreiben das Original.
Das Projekt hat drei Overlays, die je nach Profil kombiniert werden:

| Verzeichnis | Wann kopiert | Inhalt (Dateien) |
|---|---|---|
| `rootfs/overlay/` | immer | Phone-Profil, Oberfläche, Dienste, Hardware-Anpassungen |
| `rootfs/overlay-server/` | Profil `server` und `desktop` | Docker, Ollama, SSH, zram, `tetris-mode` |
| `rootfs/overlay-desktop/` | nur Profil `desktop` | GDM, RDP, GNOME-Start, Mesa-Liste |

Zusammen sind es rund 108 Dateien. Die Besitzer und Rechte legt `build-rootfs.sh` fest
(alles `root:root`, außer den Dateien für den Benutzer `alarm`).

Die Verzeichnisstruktur im Overlay entspricht dem Dateisystem des Telefons. Die Datei
`rootfs/overlay/usr/local/bin/tetris-mode` liegt also auf dem Telefon unter
`/usr/local/bin/tetris-mode`.

Auf einem laufenden Telefon kann ein einzelnes Overlay so angewendet werden (wie es die
Skripte `setup-server.sh` und `setup-desktop.sh` tun):

```sh
tar -C rootfs/overlay-server --owner=0 --group=0 -cf - . | scripts/phone-ssh.sh 'cat > /tmp/o.tar'
scripts/phone-ssh.sh "echo alarm | sudo -S -p '' sh -c 'tar -C / --no-overwrite-dir -xf /tmp/o.tar && systemctl daemon-reload'"
```

---

## 6.2 Der Aufbau des Overlays

### `etc/`: Konfiguration

| Pfad | Zweck |
|---|---|
| `etc/hostname`, `etc/issue` | Name des Telefons und Anmeldetext |
| `etc/sudoers.d/10-wheel` | Mitglieder der Gruppe `wheel` dürfen `sudo` nutzen |
| `etc/modprobe.d/tetris-blocklist.conf` | Blockiert `apusys` und Verwandte (Hänger beim Einschalten der KI-Einheit) |
| `etc/tetris-android/hybris.env` | Umgebung für Programme, die über libhybris auf die Mali-GPU zugreifen |
| `etc/tetris-android/vendor-init.allow` | Liste der Vendor-Init-Dateien, die der Container ausführen darf |
| `etc/tetris-android/init/tetris-fp.rc`, `tetris-gnss.rc` | Eigene Init-Dateien für Fingerabdruck und GPS im Container |
| `etc/udev/rules.d/78-tetris-modem.rules` | Markiert `ttyMD0` als AT-Port für ModemManager, `ccmni1` als Datennetz |
| `etc/udev/rules.d/78-tetris-camera.rules` | Gibt die Kamera-Knoten an den Android-Kamera-Dienst im Container |
| `etc/udev/rules.d/78-tetris-gnss.rules` | Gibt die GPS-Knoten an den Android-Benutzer `gps` (UID 1021) |
| `etc/udev/rules.d/78-tetris-usb-role.rules` | USB-Rolle folgt dem Kabel |
| `etc/udev/rules.d/79-tetris-vibra.rules` | Das Vibrationsgerät gehört `feedbackd` |
| `etc/NetworkManager/conf.d/10-unmanaged-usb0.conf` | NetworkManager lässt das USB-Netz (`usb0`) in Ruhe. Das verwaltet `systemd-networkd` |
| `etc/NetworkManager/system-connections/mobile-data.nmconnection` | Mobile Daten für jede SIM, **ohne** APN. Das Modem nutzt den Zugangspunkt, den das Netz anbietet |
| `etc/systemd/network/50-usb0.network` | Adresse `172.16.42.1` für das USB-Netz |
| `etc/systemd/logind.conf.d/50-tetris.conf` | Power-Taste und Leerlauf lösen keinen Schlaf aus |
| `etc/systemd/sleep.conf.d/50-tetris-nosuspend.conf` | **Schlaf ist verboten**: Suspend friert das System ein, weil es nicht aufwacht |
| `etc/systemd/system-generators/tetris-mode-once` | Generator für den Einmal-Start im Server- oder Desktop-Profil (siehe [08](08-profile.md)) |
| `etc/polkit-1/rules.d/50-tetris-boot-once.rules` | Erlaubt der App Startmodus den Neustart in ein anderes Profil |
| `etc/polkit-1/rules.d/50-tetris-camera.rules` | Erlaubt der Kamera-App, den Kamera-Stack zu starten und zu stoppen |
| `etc/geoclue/conf.d/90-tetris-gnss.conf` | GPS-Quelle für geoclue |
| `etc/geoclue/conf.d/91-tetris-beacondb.conf` | Standort über WLAN-Sender (BeaconDB) |
| `etc/UPower/UPower.conf.d/50-tetris.conf` | Akku-Anzeige: der Akku-Algorithmus läuft im Kernel |
| `etc/skel/.config/gtk-3.0/gtk.css` | Markiert die Stelle des Fingerabdrucksensors auf dem Sperrbildschirm (auch für neue Benutzer) |
| `etc/systemd/system/ModemManager.service.d/tetris-shutdown.conf` | Beim Herunterfahren stoppt zuerst der Container, dann ModemManager |
| `etc/systemd/user/sm.puri.Chatty-daemon.service.d/tetris-software-render.conf` | Chatty rendert auf dem Prozessor (sonst Absturz nach eingehender SMS) |
| `etc/systemd/user/calls-daemon.service.d/tetris-software-render.conf` | Gleicher Grund für `calls` |

### `usr/`: Programme, Bibliotheken, Vorgaben

| Pfad | Zweck |
|---|---|
| `usr/local/bin/…` | Alle `tetris-*`-Befehle, siehe 6.4 |
| `usr/lib/firefox/defaults/pref/tetris-phone.js` | Firefox: keine Hardware-Videodekodierung, damit er nicht ~170 Geräte prüft (Start von 15 s auf 2 s) |
| `usr/lib/NetworkManager/conf.d/50-tetris-wifi-powersave.conf` | WLAN-Stromsparmodus an. Das spart im Standby etwa ein Drittel des Stroms |
| `usr/share/alsa/ucm2/conf.d/mt6878-mt6369/` | Audio-Profil für den Codec MT6369 und den Lautsprecher-Verstärker aw882xx |
| `usr/share/applications/eu.tetris.Camera.desktop` | Starter für die Kamera-App |
| `usr/share/applications/eu.tetris.Modes.desktop` | Starter für die App Startmodus |
| `usr/local/share/dbus-1/services/org.gnome.Maps.service` | Überschreibt den D-Bus-Starter von GNOME Maps (Begründung im Kopfkommentar der Datei) |

### `var/`: Laufzeitdaten und Container

| Pfad | Zweck |
|---|---|
| `var/lib/lxc/android/config` | Konfiguration des Halium-Containers (siehe [03](03-architektur.md#33-der-halium-container)) |

---

## 6.3 Systemd-Dienste

Die Dienste liegen in `etc/systemd/system/`, `etc/systemd/user/` und in den Overlays für die
Profile. Die Spalten „Startet nach“ und „Startet vor“ zeigen die wichtigsten
Ordnungsbeziehungen. Alle Dienste mit `tetris-` gehören zu diesem Projekt.

### Systemweite Dienste (Phone-Profil)

| Dienst | Beschreibung | Startet nach | Aktiv durch |
|---|---|---|---|
| `tetris-growfs` | Vergrößert beim ersten Start das Dateisystem auf die volle `userdata` | `local-fs.target`, vor `sysinit.target` | `sysinit.target` |
| `tetris-cpuidle` | Schaltet zwei tiefe CPU-Schlafzustände ab (`mcusysoff*`, `system-vcore`) | `sysinit.target` | `basic.target` |
| `tetris-splash-stop` | Beendet den Tux-Splash aus dem Initramfs | `sysinit.target` | `basic.target` |
| `tetris-android` | Halium-Container: `prepare`, `lxc-start`, HALs | `local-fs.target` | `multi-user.target` |
| `tetris-phosh` | Oberfläche (`tetris-phosh-session`), autologin `alarm` auf tty7 | Datenbus, ModemManager, `tetris-android` | `graphical.target` |
| `tetris-boot-success` | Meldet dem Bootloader den erfolgreichen Start (Retry-Zähler in `misc`) | `tetris-phosh` | `graphical.target` |
| `tetris-gpu-clean-shutdown` | Löscht beim sauberen Herunterfahren den Crash-Marker der GPU | `tetris-phosh` | `multi-user.target` |
| `tetris-wlan` | Schaltet den WLAN-Chip ein, nachdem `conninfra` kalibriert hat | `tetris-phosh` | `multi-user.target` |
| `tetris-bt-bridge` | Verbindet `/dev/stpbt` mit `/dev/vhci` für BlueZ | `tetris-wlan`, vor `bluetooth` | `multi-user.target` |
| `tetris-modem` | Startet die AT-Brücke `tetris-mdtty-bridge` nach dem Container | `tetris-android` | `multi-user.target` |
| `tetris-sensors` | D-Bus-Dienst `net.hadess.SensorProxy` (SCP-Sensor-Hub) | – | `multi-user.target` |
| `tetris-vibra` | Vibrationsmotor als Force-Feedback-Gerät | – | `multi-user.target` |
| `tetris-torch` | Taschenlampe als LED-Klasse (`white:torch`), über `insmod` geladen | – | `multi-user.target` |
| `tetris-gnss` | GPS-NMEA aus dem Container für geoclue | `tetris-android`, vor `geoclue` | `multi-user.target` |
| `tetris-fp` | Fingerabdruck-HAL im Container (TEE, Goodix) | `tetris-android` | `multi-user.target` |
| `tetris-fpd` | Entsperren des Sperrbildschirms per Fingerabdruck | `tetris-fp` | `multi-user.target` |
| `tetris-camera` | Kamera-Stack im Container (`start`/`stop`, auf Abruf) | `tetris-android` | – (von der App gestartet) |
| `tetris-usb-role` | USB-Datenrolle nach dem Kabel | `systemd-udev-trigger` | `multi-user.target` |
| `tetris-thermal` | Wärmeschutz: drosselt CPU/GPU und schaltet ab | `sysinit.target` | `multi-user.target` |
| `tetris-shell@` | Vorlage: `tetris-shell@nothing` oder `@phosh` setzt die Oberfläche für den nächsten Start | – | von Hand oder App |
| `tetris-boot-once@` | Vorlage: einmal im Profil `server` oder `desktop` starten | – | App Startmodus |
| `tetris-mode-once-done` | Löscht die Datei für den Einmal-Start nach dem Start | `local-fs.target` | `multi-user.target` (über Server-Profil) |

### Benutzerdienste (Dienste pro Benutzer)

| Dienst | Beschreibung | Aktiv durch |
|---|---|---|
| `tetris-fp-overlay` | Lichtfleck für den Fingerabdrucksensor auf dem Sperrbildschirm (von `tetris-fpd` gestartet) | `tetris-fpd` |
| `tetris-screenshot` | Screenshot, wenn beide Lautstärketasten gedrückt werden | `graphical-session.target` |

### Dienste des Server-Profils (`overlay-server`)

| Dienst | Beschreibung |
|---|---|
| `tetris-ai` | startet die Ollama-Container aus `/opt/tetris-server/ai/compose.yaml` (wartet auf Docker und Netz) |
| `tetris-display-off` | schaltet das Panel aus (DPMS) und hält den DRM-Master, solange kein Compositor läuft |

Dienste wie `docker`, `avahi-daemon` und `systemd-oomd` aktiviert `tetris-mode server` selbst.

### Dienste des Desktop-Profils (`overlay-desktop`)

| Datei | Beschreibung |
|---|---|
| `etc/systemd/system/gnome-headless-session@.service.d/tetris-restart.conf` | Startet die GNOME-Sitzung neu, wenn sie endet. Der Desktop ist die einzige Oberfläche |
| `etc/systemd/user/org.gnome.Shell@user.service.d/tetris-gpu.conf` | GNOME Shell auf der Mali-GPU (über `tetris-gnome-shell`) |
| `etc/systemd/user/gnome-remote-desktop-headless.service.d/tetris.conf` | RDP nur im Desktop-Profil |
| `etc/systemd/user/tetris-rdp-setup.service` | Erstes Anmelden: erzeugt Zertifikat und Passwort für RDP |
| `etc/systemd/user-environment-generators/60-tetris-hybris-gl` | Stellt die glvnd-EGL-Variante von libhybris vor Mesa |

### Ablauf der Dienste beim Start (vereinfacht)

```
local-fs ─► tetris-growfs ─► sysinit ─► tetris-cpuidle ─► basic ─► tetris-splash-stop
                                                                       │
                       tetris-android (Container + HALs) ◄─────────────┤
                         │                    │                        │
          tetris-modem ◄─┘                    ▼                        ▼
          tetris-gnss, tetris-fp ──► tetris-fpd            tetris-phosh (Oberfläche)
                                                                       │
                                               tetris-wlan ─► tetris-bt-bridge ─► bluetooth
                                                                       │
                                                       tetris-boot-success (graphical.target)
```

Die Reihenfolge ist aus den Unit-Dateien abgeleitet (`After=`, `Before=`, `Requires=`). Der
genaue Ablauf eines Starts zeigt `systemd-analyze critical-chain` auf dem Telefon.

---

## 6.4 Eigene Befehle (`usr/local/bin/`)

### Start, Profile und Systemverwaltung

| Befehl | Sprache | Beschreibung |
|---|---|---|
| `tetris-phosh-session` | sh | Startet die Oberfläche, die `tetris-phosh-select` ausgesucht hat (Phosh oder Nothing) |
| `tetris-phosh-select` | sh | Entscheidet GPU oder Software und führt den Crash-Schutz (siehe [03](03-architektur.md#entscheidung-beim-start-gpu-oder-software)) |
| `tetris-phosh-session-hwc` | sh | Phosh-Variante mit dem Android-Composer und der Mali-GPU |
| `tetris-nothing-session` | sh | Sitzung der Nothing-Shell, wird über `tetris-phosh.service` gestartet, wenn `/etc/tetris-shell` `nothing` sagt |
| `tetris-modes` | Python (GTK) | App **Startmodus**: einmal als Server oder Desktop starten, Oberfläche wählen |
| `tetris-boot-as` | sh | Aufruf aus den Apps: Einmal-Start im Server- oder Desktop-Profil, dann Neustart |
| `tetris-boot-once` | sh | Teil von `tetris-boot-as` als root: schreibt `/var/lib/tetris-mode/once` und startet neu |
| `tetris-boot-success` | Python | Setzt den A/B-Zustand in `misc` auf „erfolgreich“ (CRC-geschützter Block bei Offset `0x800`) |
| `tetris-thermal` | sh | Wärmeschutz: drei Drosselstufen, Abschalten bei 100 °C SoC, 58 °C Platine oder 60 °C Akku |
| `tetris-usb-role` | sh | Setzt die USB-Datenrolle (Gerät, wenn ein Kabel steckt) |
| `tetris-map-super` | Python | Legt die Android-Partitionen aus `super` als Mapper-Geräte an (`dm-linear`) |
| `tetris-mount-apexes` | Python | Bindet die APEX-Module des Halium-Systems für den Container ein |
| `tetris-android-prepare` | sh | Vorbereitung des Containers (siehe [03](03-architektur.md#33-der-halium-container)) |
| `tetris-android-start-hals` | sh | Startet die HALs im Container, wartet auf sie |
| `tetris-android-start-modem` | sh | Startet die Modem-CPU über `ccci_mdinit` |
| `tetris-wlan-on` | sh | Schaltet das WLAN ein, sobald die Kalibrierung fertig ist |
| `tetris-bt-start` | sh | Startet die Bluetooth-Brücke. Die Geräteadresse kommt aus `nvdata` (`APCFG/APRDEB/BT_Addr`) |

### Hardware-Helfer

| Befehl | Sprache | Beschreibung |
|---|---|---|
| `tetris-camera` | sh | Startet oder stoppt den Kamera-Stack im Container, wenn die App ihn braucht |
| `tetris-camera-app` | sh | Startet die Kamera-App auf libhybris und stoppt den Stack beim Beenden |
| `tetris-gnss-hal` | sh | Startet die GPS-Dienste im Container und wartet auf die GNSS-HAL |
| `tetris-fp-hal` | sh | Startet den Fingerabdruck-Stack im Container (TEE, dann die Goodix-HAL) |
| `tetris-fpd` | sh | Entsperren per Fingerabdruck. Läuft nur, wenn ein Finger registriert ist |
| `tetris-fp-overlay` | Python (GTK 3) | Zeigt einen weißen Kreis über dem Sensor als Layer-Shell-Fenster über dem Sperrbildschirm. Der helle Fleck beleuchtet den Finger für den optischen Sensor |
| `tetris-screenshot` | Python | Screenshot bei beiden Lautstärketasten. Speichert in `~/Pictures/Screenshots` |
| `prismlauncher` | sh | Startet Prism Launcher mit sauberer Grafikumgebung (SDL auf Wayland, Mesa-Software-OpenGL) |

### Desktop-Profil (`overlay-desktop/usr/local/bin/`)

| Befehl | Beschreibung |
|---|---|
| `tetris-gnome-shell` | Startet GNOME Shell auf der Mali-GPU. Fällt nach einem Absturz innerhalb von 60 s auf Software zurück |
| `tetris-software-gl` | Startet ein Programm über Mesa statt libhybris. Für Apps, die auf libhybris abstürzen |
| `tetris-software-gl-desktop-files` | Schreibt die Starter der Apps aus `tetris-software-gl.list` um, damit sie über `tetris-software-gl` laufen. Die Pacman-Regel `tetris-software-gl.hook` ruft es nach jeder Änderung von `/usr/share/applications/*.desktop` auf |
| `firefox` | Startet Firefox über `tetris-software-gl` (Mesa), weil WebRender auf libhybris abstürzt |
| `tetris-rdp-setup` | Erzeugt beim ersten Anmelden das TLS-Zertifikat und das RDP-Passwort in `~/.config/tetris-rdp-password` |

### Server-Profil (`overlay-server/usr/local/bin/`)

| Befehl | Beschreibung |
|---|---|
| `tetris-mode` | Schaltet das Profil dauerhaft: `server`, `desktop` oder `phone` (`--with-modem` behält die Mobilfunk-Verbindung). Ohne Argument zeigt es das aktuelle Profil |
| `tetris-llama` | Startet llama.cpp auf der GPU über libhybris (Vulkan). Braucht den Container für `/vendor` |
| `tetris-ollama-pull` | Lädt ein Ollama-Modell und setzt es auf 4 Threads (20 % schneller als der Standard von 8 Threads) |

---

## 6.5 Eigene Programme (`rootfs/src/`)

Diese Programme werden **im Rootfs** installiert oder **auf dem Telefon** gebaut.

| Verzeichnis oder Datei | Sprache | Wird gebaut | Zweck |
|---|---|---|---|
| `tetris-bt-bridge.c` | C | `build-rootfs.sh` | `/dev/stpbt` ↔ `/dev/vhci` für BlueZ |
| `tetris-mdtty-bridge.c` | C | `build-rootfs.sh` | Kopiert zwischen dem AT-Kanal des Modems (`/dev/ttyC0`) und `ttyMD1`, damit ModemManager auf `ttyMD0` das Modem sieht |
| `tetris-mdtty/tetris_mdtty.c` | C (Kernel-Modul) | `build-kernel.sh ext` | Legt das Tty-Paar `ttyMD0`/`ttyMD1` an |
| `tetris-vibra.c` | C | `build-rootfs.sh` | Vibrationsmotor als Force-Feedback-Gerät (uinput) |
| `tetris-torch/tetris-torch.c` | C | `build-rootfs.sh` | Steuert die Taschenlampe |
| `tetris-torch/uleds.c` | C (Kernel-Modul) | `build-kernel.sh ext` | Userspace-LEDs (`uleds`), weil `CONFIG_LEDS_USER` aus ist |
| `tetris-display-off.c` | C | `build-rootfs.sh` | Schaltet das Panel aus (DRM DPMS) und hält DRM-Master |
| `tetris-sensors/tetris-sensors.c`, `hf_abi.h` | C (GIO) | `build-rootfs.sh` | Sensor-Hub, D-Bus-Dienst `net.hadess.SensorProxy` |
| `tetris-fp/tetris-fp.c` | C | `setup-phone.sh` auf dem Telefon | Fingerabdruck-Client (Registrierung, Entsperren) |
| `tetris-gnss/tetris-gnss.c` | C | `setup-phone.sh` auf dem Telefon | GPS-Client: NMEA für geoclue |
| `tetris-camera-app/` | C mit NDK | `setup-phone.sh` auf dem Telefon | Kamera-App (`app.c`), NDK-Zugriff (`ndkcam.c`), Testwerkzeug (`probe.c`), Makefile |
| `tetris-novtswitch/tetris_novtswitch.c` | C (Kernel-Modul) | nur `push-modules.sh`, wenn gebaut | Verhindert beim Schlafen den Wechsel auf eine Textkonsole. Sonst würde das Display bei jedem Aufwachen angehen |
| `tetris-kmsgnet/tetris_kmsgnet.c` | C (Kernel-Modul) | nicht im Build | Debug-Hilfe: sendet das Kernel-Log per UDP über USB an den PC, auch wenn Journald beim Schlaf-Hänger eingefroren ist |
| `tetris-audiohal-shim/tetris_audiohal_shim.cpp` | C++ | nicht im Build | Experiment: `LD_PRELOAD`-Hülle für den Audio-Dienst im Container. Sie hält die Binder-Gerätepfade getrennt |
| `tetris-voicecall/tetris-voicecall.c` | C | nicht im Build | Experiment: lädt die Audio-HAL von MediaTek über libhybris und schaltet den Anrufmodus ein |
| `tetris-mipc/` | C | nicht im Build | Experiment für eingehende Anrufe. `tetris-mipc-calld.c` genehmigt sie über den MIPC-Kanal. `mipc-ids.c` liest die Nachrichten-IDs aus der Vendor-Bibliothek |
| `hwc-buffer-probe.c` | C | nicht im Build | Prototyp des Composer-Pfads: Gralloc-Puffer rendern und an den Android-Composer übergeben |
| `hybris-gles-probe.c` | C | nicht im Build | Offscreen-Test: zeichnet ein Dreieck auf der Mali-GPU über libhybris, ohne das Display zu nutzen |

Die Experimente ohne Build sind bewusst im Repository, damit der Stand der Arbeit
nachvollziehbar bleibt. Die Dateien in `rootfs/src/` mit Endungen `.ko`, `.o` oder
`Module.symvers` sind Build-Ergebnisse. Sie stehen in `.gitignore`.

### Build-Regeln

- **Kreuzkompilierung:** Der Android-Clang (`toolchain/clang-r487747c/bin/clang`) mit
  `--target=aarch64-linux-gnu --sysroot=<rootfs>` und `-fuse-ld=lld`. Das Ergebnis läuft auf
  dem Telefon mit seiner eigenen glibc.
- **Kernel-Module:** gegen den Kernel in `out/kernel` mit `make M=… modules`.

---

## 6.6 Konfigurationsdateien im Detail

### Halium-Container (`var/lib/lxc/android/config`)

Der vollständige Text steht in der Datei. Die Abschnitte:

| Abschnitt | Zweck |
|---|---|
| `lxc.rootfs.path` | Verzeichnis des Containers (`/var/lib/lxc/android/rootfs`, das Overlay) |
| `lxc.cap.drop` | entzieht Rechte: `mac_admin`, `mac_override`, `sys_boot` |
| `lxc.net.0.type = empty` | eigener Netzwerk-Namensraum ohne Verbindung |
| `lxc.autodev = 0` | keine automatische Gerätedatei-Liste. Die Knoten werden einzeln eingebunden |
| `lxc.mount.entry` | Bind-Mounts: `/dev/binderfs`, `/dev/__properties__`, `/dev/socket`, `/dev/kmsg`, `/apex`, `/vendor`, `/odm`, `/vendor_dlkm` und `/sys/power` (schreibgeschützt) |

### Vendor-Init-Liste (`etc/tetris-android/vendor-init.allow`)

Jede Zeile nennt eine Datei aus `/vendor/etc/init`. Die Datei wird in den Container kopiert,
wenn sie in der Liste steht. Der Vorteil: Android bekommt nur die Dienste, die wir brauchen.

Warum das nötig ist: Die Vendor-Init-Dateien des Originals würden USB, WLAN und Bluetooth
umkonfigurieren, Module laden und den Audio-Dienst starten. Das macht unser Host-System selbst.

### Mesa-Liste (`overlay-desktop/etc/tetris-software-gl.list`)

Eine Zeile je Programm, das auf Mesa laufen soll. Die Datei wird beim Start der Desktop-Session
und von der Pacman-Regel (`tetris-software-gl.hook`) gelesen.

---

## 6.7 Was Sie ändern dürfen und was nicht

| Änderung | Sicher? | Hinweis |
|---|---|---|
| Neue `tetris-*`-Skripte im Overlay hinzufügen | ja | `chmod 755` nicht vergessen, Besitzer `root:root` setzen |
| Einen Dienst deaktivieren: `sudo systemctl disable --now <dienst>` | ja | Die Änderung bleibt bis zum nächsten `build-rootfs.sh` oder `tetris-mode` |
| `tetris-mode` oder `tetris-phosh-select` ändern | mit Vorsicht | Diese Dateien steuern den Start. Ein Fehler kann das Telefon im Initramfs festhalten. Testen Sie mit `tetris.debug=1` |
| `vendor-init.allow` erweitern | mit Vorsicht | Android-Dienste können USB oder den Akku wieder umschalten |
| `ModemManager` oder `wlroots` aktualisieren | nur mit Neubau | Das Plugin und `wlroots0.20` müssen neu gebaut werden (siehe [05](05-skript-referenz.md#53-skripte-auf-dem-telefon-pkgbuilds)) |
| `/etc/tetris-shell`, `/etc/tetris-android/gpu-shell` | sicher | Sie sind die Schalter für Oberfläche und GPU |
