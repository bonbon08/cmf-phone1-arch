# 9. Fehlersuche

Dieses Kapitel hilft, wenn etwas nicht funktioniert. Es beginnt mit dem Grundsatz, und dann
folgen die Werkzeuge, die Protokolle, die typischen Fehler und der Weg zurück.

## 9.1 Grundsatz: Erst die Stufe bestimmen

Ein Telefon startet in Stufen. Der Fehler liegt immer in einer davon. Bevor Sie suchen,
bestimmen Sie, wie weit das Telefon gekommen ist:

| Was Sie sehen oder erreichen | Die Stufe ist | Weiter mit |
|---|---|---|
| Nichts. Kein Lebenszeichen, Telefon wird nicht warm | Bootloader oder Hardware | 9.8 |
| Fastboot-Bildschirm erscheint, aber `fastboot devices` findet nichts | USB oder Treiber am PC | 9.8 |
| Tux-Pinguin-Splash, dann nichts. Telnet unter `172.16.42.1` antwortet | Initramfs | 9.4 |
| Tux, dann nichts. Telnet antwortet nicht | Initramfs hängt, oft in einem Modul | 9.4 und 9.5 |
| Schwarzer Bildschirm, USB-Netz `172.16.42.1` antwortet, SSH geht | Arch läuft, Oberfläche fehlt | 9.6 |
| Schwarzer Bildschirm, SSH geht nicht | Arch startet nicht richtig | 9.6 und 9.7 |
| Oberfläche läuft, eine Funktion fehlt (WLAN, Modem, Fingerabdruck ...) | Dienst | 9.7 |

Der **schwarze Bildschirm nach dem ersten Start** ist im Phone-Profil erwartet. Die
Oberfläche braucht die zweite Stufe (`setup-phone.sh`). Entscheidend ist, ob SSH antwortet.

## 9.2 Werkzeuge

| Werkzeug | Woher | Wozu |
|---|---|---|
| `scripts/phone-ssh.sh` | PC | Shell auf dem Telefon mit Projektschlüssel |
| `scripts/phone-sh.sh 'befehl'` | PC | Befehl im Initramfs über Telnet (Port 23) |
| `journalctl` | Telefon | Protokoll von systemd, pro Dienst mit `-u` |
| `systemctl status <dienst>` | Telefon | Zustand eines Dienstes und die letzten Zeilen |
| `systemd-analyze` | Telefon | Startzeiten und die kritische Kette |
| `dmesg` | Telefon | Kernel-Protokoll des laufenden Starts |
| `lxc-info -n android`, `lxc-attach -n android -- …` | Telefon | Zustand und Befehle im Halium-Container |
| `scripts/read-flashlog.sh` | PC (Nothing OS mit Root) | Protokolle aus `nt_reserve2` lesen, wenn das Telefon nicht mehr bootet |

Ein Befehl, der die wichtigsten Dinge auf einmal zeigt, wenn das Telefon erreichbar ist:

```sh
scripts/phone-ssh.sh 'uptime; systemctl --failed; cat /run/tetris-phosh/mode 2>/dev/null; \
    journalctl -b -p warning --no-pager | tail -40'
```

## 9.3 Protokolle finden

Es gibt mehrere Orte. Der wichtigste hängt davon ab, wie weit das System gekommen ist.

| Protokoll | Ort | Wann verfügbar |
|---|---|---|
| **Initramfs-Protokoll** | `/tmp/init.log` (flüchtig), nach dem `switch_root` in `/var/log/tetris-initramfs.log` | nach dem Start mit Arch |
| **Protokoll im Flash** | Partition `nt_reserve2`: Anfang = `init.log`, 16 MB = letztes `dmesg` | immer, auch ohne Arch. Lesbar über `read-flashlog.sh` oder unter Nothing OS mit Root |
| **pstore (`pmsg`)** | `/sys/fs/pstore/pmsg-ramoops-0` | nach einem Neustart, auch nach einem Absturz (überlebt Warmstart) |
| **pstore (Konsole, dmesg)** | `/sys/fs/pstore/console-ramoops-0`, `dmesg-ramoops-0` | nach einer Kernel-Panik |
| **Systemd-Journal** | `journalctl -b` (aktueller Start), `journalctl -b -1` (vorheriger) | mit Arch |
| **Container** | `/var/log/lxc-android.log` | mit `tetris-android` |
| **Modem-Start** | `journalctl -u tetris-modem`, `/sys/kernel/ccci/boot` (`md1:4` = gestartet) | mit `tetris-modem` |
| **Benutzersitzung** | `journalctl --user` (als `alarm`) | mit Oberfläche oder Desktop |

Das Initramfs schreibt seine Meldungen in den Speicher (`/tmp/init.log`) und in die Partition
`nt_reserve2` (siehe `initramfs/init`). Deshalb bleibt das Protokoll auch dann erhalten, wenn
das Telefon nicht bis zu Arch kommt.

### Protokoll aus dem Flash lesen

Wenn das Telefon nicht bis zur Shell kommt, aber Nothing OS (Slot a) noch funktioniert,
lesen Sie das Protokoll am PC mit Magisk-Root:

```sh
scripts/read-flashlog.sh out/flashlog-$(date +%F)
less out/flashlog-$(date +%F)/init.log
less out/flashlog-$(date +%F)/dmesg.txt
```

Das Skript kopiert die ersten 16 MB (Initramfs-Protokoll) und 2 MB ab 16 MB (Kernel-Protokoll
vom letzten Start). Sie finden darin die Meldungen `init: load <modul>`. Das letzte geladene
Modul zeigt, wo der Start hängt.

## 9.4 Initramfs: Das Telefon bleibt im Initramfs

Das Telefon bleibt im Initramfs, wenn eine dieser Bedingungen gilt:

1. Lautstärketaste beim Start in den ersten 3 Sekunden gedrückt.
2. Der Kernel hat `tetris.debug=1` (Image `debug-vendor_boot-shell.img`).
3. Die Datei `/.tetris-debug` existiert auf dem Rootfs.
4. Auf `userdata` gibt es kein Arch oder kein `/usr/lib/systemd/systemd`.
5. Das Mounten von `userdata` schlägt fehl.

Im Initramfs erreichen Sie das Telefon über Telnet:

```sh
scripts/phone-sh.sh 'cat /tmp/init.log | tail -30'
scripts/phone-sh.sh 'dmesg | tail -40' 5
scripts/phone-sh.sh 'ls /lib/modules/*/ | head'
```

Typische Meldungen in `init.log`:

| Meldung | Bedeutung |
|---|---|
| `init: load <modul>` gefolgt von nichts | Das Laden von `<modul>` hängt. Modul in `initramfs/modules.blocklist` aufnehmen und neu bauen |
| `init: FAIL <modul>` | Das Modul konnte nicht geladen werden (Abhängigkeit fehlt oder Version falsch) |
| `init: nt_reserve2 not found` | Die Partition ist nicht vorhanden. Das Protokoll wird nicht gespeichert |
| `init: WLAN NVRAM written` | WLAN-Kalibrierung ist übergeben |
| `init: WLAN NVRAM write failed` | `/dev/wmtWifi` fehlt oder der Schreibvorgang schlug fehl. Das WLAN startet dann nicht |
| `init: UDC=` (leer) | Das USB-Gerät hat keinen Controller. Kabel oder `usb_role` prüfen |
| `init: no ext4 rootfs on userdata` | Arch fehlt oder `userdata` ist nicht ext4 |
| `init: staying in debug initramfs (rootfs incomplete or /.tetris-debug)` | Eine der Bedingungen oben |

### Notfall: Arch wieder bootbar machen

Im Initramfs können Sie das Rootfs von Hand einhängen und reparieren:

```sh
scripts/phone-sh.sh 'mount -t ext4 /dev/disk/by-partlabel/userdata /newroot && ls /newroot'
```

Mit `e2fsck` auf `userdata` (nur wenn es nicht eingehängt ist) prüfen Sie das Dateisystem.
Danach `umount /newroot` und `/tmp/init.log` lesen.

## 9.5 Das Initramfs hängt beim Laden der Module

Das ist der häufigste Fehler beim Entwickeln von Kerneln. Gehen Sie so vor:

1. Das letzte `init: load …` im Protokoll notieren. Dieses Modul hängt.
2. Prüfen, ob das Modul in `initramfs/modules.blocklist` stehen sollte. Das bekannte Beispiel
   sind `apu_top`, `apusys` und `sapu` (KI-Einheit).
3. Das Modul gehört zu einer Hardware, die Sie gerade nicht brauchen: in die Sperrliste
   aufnehmen, Initramfs neu bauen (`build-debug-boot.sh`), flashen (`flash-debug-b.sh`).

Gelingt der Start mit der Sperrliste, prüfen Sie die Folgen: Dann fehlt die Funktion, die das
Modul bereitgestellt hätte.

## 9.6 Arch startet, aber die Oberfläche fehlt

Auf dem Telefon:

```sh
systemctl --failed                          # welche Dienste sind fehlgeschlagen?
systemctl status tetris-phosh               # Oberfläche
journalctl -b -u tetris-phosh --no-pager | tail -60
cat /run/tetris-phosh/mode                  # gpu oder sw
cat /etc/tetris-shell 2>/dev/null           # nothing oder leer (Phosh)
```

| Symptom im Protokoll | Ursache | Lösung |
|---|---|---|
| `tetris-phosh-select: composer HAL not up -> software Phosh` | Der Composer-Dienst im Container läuft nicht | `journalctl -u tetris-android`, `lxc-info -n android`. Der Container muss vor der Oberfläche laufen |
| `previous GPU session did not survive 90 s -> software Phosh` | **Crash-Schutz** hat ausgelöst (siehe 9.9) | Abwarten, oder Marker entfernen: `sudo rm -f /var/lib/tetris-android/gpu-attempt` |
| Oberfläche beendet sich sofort, Fehler zur Schicht-Oberfläche (`layer-shell`) im Protokoll | Der wlroots-Patch fehlt (Arch' Standard-wlroots). Die Home-Leiste hat Höhe 0 und löst dann einen Protokollfehler aus | `pacman -Q wlroots0.20`, dann `pkgbuilds/build-wlroots-phoc.sh` auf dem Telefon |
| Oberfläche startet, zeigt aber nichts (GPU-Modus) | Die Grafik über libhybris funktioniert nicht | Für diesen Start in den Software-Modus: `sudo mkdir -p /run/tetris-phosh && sudo touch /run/tetris-phosh/force-sw && sudo systemctl restart tetris-phosh`. Dauerhaft: `sudo rm /etc/tetris-android/gpu-shell` |
| `EGL` oder `libhybris` Fehler im Journal | libhybris fehlt oder passt nicht zum Container | `setup-phone.sh` erneut ausführen, Abschnitt libhybris |

### Sitzung neu starten, ohne neu zu booten

```sh
sudo systemctl restart tetris-phosh
```

**Achtung:** Ein Neustart des Dienstes innerhalb von 90 Sekunden nach dem letzten GPU-Start
löst den Crash-Schutz aus. Die nächste Oberfläche startet dann in Software.

## 9.7 Eine Funktion fehlt

### WLAN

```sh
journalctl -b -u tetris-wlan -u tetris-wlan.service --no-pager
ip link show wlan0
nmcli device status
```

- Fehlt `wlan0`: Die NVRAM-Kalibrierung hat nicht geklappt. Prüfen Sie im Initramfs-Protokoll
  `WLAN NVRAM written`. Die Partition `nvdata` und die Datei `APCFG/APRDEB/WIFI` müssen
  vorhanden sein.
- WLAN verbindet nicht, nachdem das Display aus war: Das ist der Stromsparmodus. Im Server-
  Profil ist er aus (`/etc/NetworkManager/conf.d/zz-tetris-headless.conf`).

### Mobilfunk (Modem)

```sh
journalctl -b -u tetris-modem --no-pager | tail -40
cat /sys/kernel/ccci/boot                   # md1:4 = Modem läuft
mmcli -L                                    # Modem gefunden?
mmcli -m 0                                  # Status
```

- Das Modem braucht rund 35 Sekunden, bis es antwortet. Warten Sie, bevor Sie den Dienst neu starten.
- `ccmni1` erscheint erst, wenn `ttyMD0` da ist. Die udev-Regel hält das Netz vorher zurück.
- Keine Datenverbindung, obwohl das Netz da ist: `journalctl -u ModemManager`. Das Plugin
  `libmm-plugin-tetris.so` muss zur installierten ModemManager-Version passen. Nach einem
  Update neu bauen.
- **Anrufe funktionieren nicht.** Das ist bekannt (siehe Haupt-README).

### Bluetooth

```sh
journalctl -b -u tetris-bt-bridge --no-pager
bluetoothctl show
```

Die Geräteadresse liest das Skript aus `nvdata` (`APCFG/APRDEB/BT_Addr`). Fehlt sie, startet
die Brücke nicht.

### Touch, Bildschirm, Sensoren

- **Touch tot nach dem Schlaf:** Patch `kernel_device_modules/0004` prüfen (siehe
  [07](07-kernel-treiber-patches.md#725-touch-aufwachen-nach-dem-schlaf-kernel_device_modules0004)).
  Neustart hilft kurzfristig.
- **Bildschirm bleibt dunkel, obwohl das Telefon läuft:** Eventuell ist der Server-Schalter
  `tetris-display-off` aktiv. Prüfen Sie `tetris-mode`.
- **Sensor dreht den Bildschirm nicht:** `systemctl status tetris-sensors`. Der Dienst muss
  `net.hadess.SensorProxy` anbieten: `busctl --system list | grep -i sensor`.

### Fingerabdruck

```sh
journalctl -b -u tetris-fp -u tetris-fpd --no-pager | tail -40
sudo tetris-fp enroll                       # neu registrieren
```

Das Entsperren funktioniert nur, wenn ein Finger registriert ist. Der Lichtfleck
(`tetris-fp-overlay`) braucht die Layer-Shell der Oberfläche.

### GPS

`tetris-gnss` liefert NMEA-Sätze an geoclue, sobald eine App den Standort anfragt. Der
Empfänger ist nur an, wenn eine App fragt, deshalb ist ein Fix nicht immer sofort da. Im
Projekt ist der Stand „Satelliten gefunden, erster Fix nicht bestätigt“ (siehe Haupt-README).

### Kamera

Die Kamera-App startet den Stack im Container über `tetris-camera`. Wenn das Bild fehlt,
prüfen Sie `journalctl -u tetris-camera` und `lxc-attach -n android -- logcat -d | tail`.

## 9.8 Telefon startet nicht und ist nicht erreichbar

Wenn gar nichts reagiert, arbeiten Sie vom PC aus:

1. **Fastboot erreichbar?** Gerät in Fastboot-Modus (Power + Lautstärke runter), dann
   ```sh
   fastboot devices
   fastboot getvar product           # k6878v1_64
   fastboot getvar current-slot      # a oder b
   ```
2. **Bootet Slot b nicht?** Zurück auf Slot a:
   ```sh
   fastboot --set-active=a && fastboot reboot
   ```
   Nothing OS auf Slot a sollte starten. Dann stimmt die Ursache auf Slot b.
3. **Nothing OS startet nicht (Slot a kaputt)?** Mit `scripts/flash-stock.sh` die
   Originalsoftware auf beide Slots neu schreiben. Das löscht die Daten, lesen Sie
   [04-bauen-und-flashen.md](04-bauen-und-flashen.md#410-rückweg).
4. **Der Bootloader ist gesperrt?** Das Telefon startet nur noch signierte Software. Das
   ist nicht rückgängig zu machen, ohne dass Sie den Bootloader wieder entsperren (das löscht
   die Daten). Vermeiden Sie das.

## 9.9 Crash-Schutz

Der GPU-Modus der Oberfläche ist der empfindlichste Teil des Systems. Der Crash-Schutz schützt
vor einer Endlosschleife aus Abstürzen:

```
Start im GPU-Modus ──► Marker /var/lib/tetris-android/gpu-attempt wird angelegt
                       + Timer 90 s
                         │
     90 s laufen ohne Absturz ──► Marker wird entfernt. Alles gut
                         │
     Absturz oder Neustart vor 90 s ──► Marker bleibt stehen
                         │
     nächster Start ──► "previous GPU session did not survive 90 s"
                         └──► Software-Modus, für diesen Start fest (/run/tetris-phosh/force-sw)
                              Marker wird entfernt. Der übernächste Start versucht wieder GPU
```

**Wichtig für Ihre Arbeit:**

- Ein Neustart der Oberfläche innerhalb von 90 Sekunden löst den Crash-Schutz aus.
- Ein sauberes Herunterfahren ist kein GPU-Absturz. Der Dienst `tetris-gpu-clean-shutdown`
  entfernt den Marker beim Herunterfahren.
- Ein Absturz im Desktop-Profil (GNOME Shell) fällt innerhalb von 60 Sekunden auf Software
  zurück, nur für diesen Start (`tetris-gnome-shell`).
- **Dauerhaft** Software erzwingen: `sudo rm /etc/tetris-android/gpu-shell`. Dann ist die GPU
  aus, bis Sie die Datei wieder anlegen (`sudo touch /etc/tetris-android/gpu-shell`).

## 9.10 Bekannte Probleme aus dem Projekt

Die Haupt-README listet den Funktionsstand. Hier die Punkte, die beim Entwickeln häufig
auffallen:

| Problem | Erklärung | Stand |
|---|---|---|
| **Kein Standby** | Der SoC hängt im Tiefschlaf. Das Projekt verbietet Schlaf (`sleep.conf.d/50-tetris-nosuspend.conf`). Der Bildschirm kann aus sein, die CPU bleibt wach | offen |
| **Anrufe ohne Ton, eingehende Anrufe kommen nicht an** | Audio-HAL und MIPC-Kanal sind nur teilweise verstanden. Siehe `rootfs/src/tetris-voicecall` und `tetris-mipc` | offen |
| **GPS-Fix unsicher** | Satelliten werden gefunden, ein Fix ist nicht bestätigt | teilweise |
| **GTK-4-Apps stürzen auf libhybris ab** | Wenn Android-Threads enden, können Apps wie Chats, Anrufe und Kamera abstürzen. Deshalb rendern sie auf dem Prozessor (Drop-ins in `overlay/etc/systemd/user/`) | Umgehung |
| **GDM versucht lokale Anmeldung** | Im Desktop-Profil versucht GDM dreimal, einen Anmeldebildschirm zu zeigen. `tetris-display-off` hält das Panel, deshalb ist nichts zu sehen | bekannt |
| **`depmod` über alle Module** | Doppelte Exporte führen dazu, dass der Kernel die falschen Module wählt. Außerhalb des Initramfs Module mit `insmod` laden | Regel |

### Regel: `depmod` nicht über den ganzen Satz

Das Projekt hat Module mit doppelten Symbolen. Ein `depmod` über den gesamten Satz (`/usr/lib/modules`)
kann die falschen Abhängigkeiten schreiben. Das Ergebnis ist ein Modul, das eine andere Version
lädt. Module einzeln mit `insmod` laden, wie es `tetris-torch` tut.

## 9.11 Typische Fehler beim Bauen

Siehe [04-bauen-und-flashen.md](04-bauen-und-flashen.md#412-häufige-fehler-beim-bauen).

## 9.12 Ein Fehler melden

Ein guter Fehlerbericht enthält:

1. **Die Stufe** (9.1): Wie weit kam das Telefon?
2. **Das Profil** (`tetris-mode`) und die Oberfläche (`cat /etc/tetris-shell`).
3. **Die letzten Protokolle:** `journalctl -b -p warning` oder das Initramfs-Protokoll.
4. **Die Schritte** davor: was Sie geflasht, gebaut oder geändert haben.
5. **Den Stand des Repositorys:** `git log -1 --oneline` und `git status` auf dem PC.
6. **Keine Seriennummern, IMEI, Schlüssel oder Passwörter.** Zensieren Sie `ip addr`, `cat /etc/machine-id`
   und den Inhalt von `out/`.
