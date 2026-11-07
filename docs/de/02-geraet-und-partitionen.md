# 2. Das Gerät und seine Partitionen

**🇩🇪 Deutsch** · [🇬🇧 English](../02-hardware-and-partitions.md)

Dieses Kapitel beschreibt die Hardware des CMF Phone 1 und den Speicher, auf den das
Projekt zugreift. Es ist Pflichtlektüre vor dem ersten Flash-Vorgang, denn Fehler bei
Partitionen gehören zu den folgenreichsten Fehlern überhaupt: Sie können ein Telefon
unbrauchbar machen oder alle Daten löschen.

## 2.1 Die Hardware

| Bauteil | Gerät | Bedeutung für das Projekt |
|---|---|---|
| Name im Code | **Tetris** (`tetris-*`) | Präfix für eigene Programme, Dienste und Dateien |
| Hersteller / Modell | Nothing, **CMF Phone 1** | |
| Produkt-Kennung | `k6878v1_64` | Der Flash-Skript prüft sie, bevor er irgendetwas schreibt |
| SoC (Prozessor-Chip) | MediaTek **Dimensity 7300** (MT6878) | 4 × Cortex-A78 (schnell) und 4 × Cortex-A55 (sparsam), ARM64 |
| Arbeitsspeicher | rund **7,2 GiB** nutzbar | Ausreichend für Desktop; für größere KI-Modelle knapp |
| GPU | **Mali-G615** (2 Kerne) | Grafik. Nur über den Android-Treiber nutzbar, deshalb libhybris |
| Display | Display mit Fingerabdrucksensor unter dem Glas | Fingerabdruck über `tetris-fp` (Registrierung und Sensor), Entsperren des Sperrbildschirms über `tetris-fpd` |
| Mobilfunk | MediaTek-Modem (CCCI, im Android-Container) | Mobile Daten und SMS funktionieren, Anrufe noch nicht |
| WLAN / Bluetooth | MediaTek Combo-Chip (connac2, `conninfra`) | Treiber `gen4m` für WLAN, `mt66xx` für Bluetooth |
| Sensoren | SCP-Sensor-Hub | Beschleunigung, Licht, Näherung über `tetris-sensors` |
| Kamera | 50 MP Hauptkamera, Frontkamera, IR-Tiefensensor | Nur über den Android-Container (`tetris-camera`) |
| Audio | MT6369-Codec mit Awinic-Verstärker | Konfiguration in `rootfs/overlay/usr/share/alsa/ucm2` |
| Vibration | Motor über Force-Feedback | `tetris-vibra` |
| Taschenlampe | LED-Blitz | `tetris-torch` (über `uleds`) |
| GPS | MediaTek GNSS | `tetris-gnss` (über den Android-Container) |
| SIM | Nano-SIM | Mobilfunk über ModemManager (SIM-PIN, Datenverbindung, SMS) |
| NFC | nicht vorhanden | |

Die Angaben stammen aus den Gerätedaten im Ordner `stock-info/` (`cpuinfo.txt`,
`meminfo.txt`, `dt-model.txt`), die beim Original-System gesichert wurden. Der Ordner
liegt nicht im Git.

### Das Original-Betriebssystem

Das Telefon wird mit **Nothing OS** ausgeliefert, einer Android-Version. Die
Firmware-Version dieses Projekts ist **B4.1** (Build `Tetris_B4.1-260928-1140`). Alle
Dateien dieses Builds liegen lokal im Ordner `firmware/`. Sie sind proprietär, deshalb
nicht im Git.

Der Kernel des Original-Systems ist Linux **6.1** (Android-Ausgabe). Unser Kernel basiert
auf den offenen Quellen von Nothing (`NothingOSS`) und hat dieselbe Hauptversion. Das ist
wichtig, weil Treiber-Module nur zu dem Kernel passen, mit dem sie gebaut wurden.

## 2.2 Was ist eine Partition?

Der Flash-Speicher des Telefons ist in **Partitionen** unterteilt. Jede hat einen Namen
(`boot_b`, `userdata` ...) und eine feste Größe. Ein Flash-Vorgang schreibt in eine dieser
Partitionen. Ein Partitionsname ist nur ein Name im Bootloader. Das Telefon findet die
Partition über diesen Namen, nicht über eine feste Nummer.

Die Liste der Partitionen mit ihren internen Blockgeräten steht in
`stock-info/by-name.txt`. Einige wichtige Beispiele (Blockgeräte nach dem Original-System):

| Partition | Blockgerät | Inhalt |
|---|---|---|
| `preloader` | fest im Chip | erster Startcode, nicht verändern |
| `lk_a`, `lk_b` | `sdc35`, `sdc65` | Bootloader LK |
| `boot_a`, `boot_b` | `sdc36`, `sdc66` | Kernel-Abbild |
| `init_boot_a/b` | `sdc38`, `sdc68` | Ramdisk für Android (8 MB) |
| `vendor_boot_a/b` | `sdc37`, `sdc67` | Gerätetreiber-Ramdisk (64 MB), bei uns das Initramfs |
| `dtbo_a/b` | `sdc39`, `sdc72` | Geräte-Overlays (Device Tree) |
| `vbmeta_a/b` | `sdc10`, `sdc13` | Signaturdaten für das verifizierte Starten (AVB) |
| `modem_a/b` | `sdc22`, `sdc52` | Modem-Firmware |
| `nvdata`, `nvcfg`, `nvram`, `protect1/2`, `persist` | `sdc7`, `sdc9` u. a. | Kalibrierung, IMEI, Schlüssel. **Nie löschen** |
| `misc` | `sdc1` | Bootsteuerung: A/B-Slot-Zustand (Retry-Zähler) |
| `metadata` | `sdc16` | Verwaltungsdaten für die Verschlüsselung der Daten |
| `super` | `sdc81` | Container für die Android-Systempartitionen (dynamisch) |
| `nt_reserve1`, `nt_reserve2` | `sdc79`, `sdc80` | Reserve. Bei uns: `nt_reserve2` speichert das Protokoll des Initramfs |
| `userdata` | `sdc82` | Ihre Daten, hier liegt auch das Arch-Linux-Rootfs |

Die Nummern `sdc…` sind interne Geräte-Namen des Speichers. Sie können sich zwischen
Firmware-Versionen ändern, die Namen (`userdata`, `boot_b` ...) bleiben.

## 2.3 Die A/B-Slots

Viele Partitionen gibt es **doppelt**: `boot_a` und `boot_b`, `vendor_boot_a` und
`vendor_boot_b` und so weiter. Man nennt das **A/B-Slots**.

- Slot **a** enthält das Original-System (Nothing OS). Er bleibt in diesem Projekt
  unangetastet.
- Slot **b** enthält unser Kernel und das Initramfs. Mit `fastboot --set-active=b` wird er zum
  Startslot.

Der Vorteil: Bootet der Slot b nicht, kann man zurück auf a schalten. Dazu gibt es in
`misc` einen Zähler für Fehlversuche. Startet das System erfolgreich, meldet der Dienst
`tetris-boot-success` das und setzt den Zähler zurück. Das entspricht dem
Android-Mechanismus `markBootSuccessful()`.

**Wichtig:** Der Rückweg auf Slot a funktioniert nur, solange Nothing OS dort vollständig
ist. Das Skript `scripts/flash-stock.sh` schreibt beide Slots neu und ist damit der sichere
Ausweg (siehe [04-bauen-und-flashen.md](04-bauen-und-flashen.md#410-rückweg)).

## 2.4 Das Dynamic-Super-Layout

Android legt seine Systempartitionen (`system`, `vendor`, `product`, `odm` ...) nicht als
feste Partitionen an, sondern in einer einzigen Partition namens **`super`**. Innerhalb
von `super` liegen logische Partitionen. Das ermöglicht Updates ohne Neupartitionierung.

Für uns ist das aus zwei Gründen wichtig:

1. Der **Halium-Container** braucht die `vendor`-, `odm`- und `vendor_dlkm`-Partitionen des
   Original-Systems (Treiber, Firmware). Sie werden beim Start des Containers mit
   `tetris-map-super` aus `super` herausgeholt (siehe
   [03-architektur.md](03-architektur.md#33-der-halium-container)).
2. Beim Flashen im **fastbootd**-Modus werden die logischen Partitionen neu angelegt
   (`create-logical-partition`, `wipe-super`). Dafür muss der Bootloader in den
   fastbootd-Modus wechseln, deshalb gibt es im Skript einen Neustart in `fastboot`.

## 2.5 Die Partitionen, die wir beschreiben

Das Projekt schreibt nur in wenige Partitionen. Jede Änderung hier hat eine bestimmte
Wirkung:

| Partition | Schreibt | Mit | Wirkung bei Fehlern |
|---|---|---|---|
| `boot_b` | `out/debug-boot.img` | `flash-debug-b.sh` | Telefon bootet nicht über Slot b |
| `vendor_boot_b` | `out/debug-vendor_boot.img` | `flash-debug-b.sh` | Keine Treiber, kein Start |
| `init_boot_b` | `out/debug-init_boot.img` (leer) | `flash-debug-b.sh` | Harmlos, da leer |
| `userdata` | `out/rootfs.simg` | `fastboot flash userdata` | **Löscht alle Daten** |
| `vbmeta_a/b`, `vbmeta_system/vendor` | gepatcht (AVB aus) | `flash-stock.sh` | Mit deaktiviertem AVB akzeptiert der Bootloader unseren Kernel. Mit AVB nur Original-Images |
| `metadata` | leer | `flash-stock.sh` | Die Verschlüsselungs-Verwaltung wird zurückgesetzt, Daten sind weg |
| alle Slots a und b | Original-Images | `flash-stock.sh` | **Komplette Rückkehr zu Nothing OS** |

## 2.6 Was Sie nie tun sollten

- **Das Gerät nicht entsperren oder erneut sperren** ohne Plan. Ein gesperrter
  Bootloader startet nur signierte Software. Ein verändertes System bleibt dann unbrauchbar.
  Dieses Projekt setzt einen **entsperrten** Bootloader voraus.
- **Partitionen wie `nvdata`, `nvcfg`, `protect1`, `protect2`, `persist` oder `nvram` nicht
  überschreiben.** Sie enthalten die IMEI und Kalibrierdaten des Funkmoduls. Ohne sie hat das Telefon
  kein gültiges Funkprofil. Sichern Sie diese Partitionen, bevor Sie experimentieren.
  Das Projekt hat dafür Sicherungen im Ordner `out/modem-backup/` angelegt (lokal).
- **Nicht `fastboot erase` auf Partitionen ausführen,** die Sie nicht verstehen.

## 2.7 Speicher nach dem ersten Start

Beim ersten Start vergrößert der Dienst `tetris-growfs` das Dateisystem auf die volle Größe
von `userdata`. Das Abbild selbst ist klein (das Skript `build-rootfs.sh` verkleinert es
mit `resize2fs -M`), damit das Flashen schnell geht.

| Abschnitt | Größe | Herkunft |
|---|---|---|
| `userdata` | Größe der Partition, Rest des Speichers | vom Gerät |
| Rootfs-Abbild | Standard `10G` (Obergrenze), wird verkleinert | `build-rootfs.sh` (Umgebungsvariable `SIZE`) |
| `nt_reserve2` | 64 MB | `initramfs/init` schreibt Protokoll (`init.log`, `dmesg`) |

## 2.8 Gerätedaten prüfen

Wenn das Telefon in Fastboot-Modus am PC hängt, prüfen Sie zuerst die Produkt-Kennung:

```sh
fastboot getvar product
# erwartet: product: k6878v1_64
```

Diese Prüfung macht auch das Flash-Skript `flash-stock.sh`. Stimmt sie nicht, bricht es ab.

Wenn das Telefon bereits mit Arch Linux läuft, erreichen Sie es über USB:

```sh
ssh alarm@172.16.42.1        # Passwort: alarm (bitte ändern: passwd)
cat /proc/device-tree/model  # Gerätename
```

Die Kennung der Partition finden Sie auf dem Telefon mit `ls -l /dev/disk/by-partlabel/`.
