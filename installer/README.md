# Installer mit grafischer Oberfläche

Der Installer führt die Skripte aus `scripts/` in der richtigen Reihenfolge aus und zeigt
dabei ein kleines Fenster mit Fortschritt, Protokoll und Warnungen. Er ersetzt die Skripte
nicht. Wer den Ablauf von Hand machen will, findet alle Befehle in
[docs/04-bauen-und-flashen.md](../docs/04-bauen-und-flashen.md).

Der Installer ist für Leute gedacht, die das Telefon einrichten wollen und keine Lust haben,
zwanzig Befehle in der richtigen Reihenfolge einzutippen. Er kennt die Gefahren, zeigt sie an
und fragt vor den gefährlichen Schritten nach.

> **Achtung:** Die Installation löscht alle Daten auf dem Telefon. Lesen Sie vorher das
> Kapitel „Risiken“ in [docs/04-bauen-und-flashen.md](../docs/04-bauen-und-flashen.md#42-risiken).

## Inhalt

- [Was der Installer tut und was nicht](#was-der-installer-tut-und-was-nicht)
- [Voraussetzungen](#voraussetzungen)
- [Start](#start)
- [Die Seiten](#die-seiten)
- [Betriebsarten](#betriebsarten)
- [Sicherheit](#sicherheit)
- [Kommandozeile](#kommandozeile)
- [Protokolle](#protokolle)
- [Tests](#tests)
- [Fehler und Lösungen](#fehler-und-lösungen)
- [Aufbau der Dateien](#aufbau-der-dateien)

## Was der Installer tut und was nicht

**Tut:**

- Fragt ab, was Sie möchten (nur bauen, neu installieren, Zweite Stufe, Server, Desktop).
- Prüft, ob alle Programme und Eingabedateien da sind, bevor etwas läuft.
- Prüft das Telefon: Fastboot-Modus, USB-Verbindung, Notfall-Shell, Internet.
- Führt die Schritte in der richtigen Reihenfolge aus. Jeder Schritt ist ein bestehendes Skript
  aus `scripts/` oder ein `fastboot`-Befehl.
- Wartet nach dem Neustart des Telefons, bis es per USB antwortet, und fährt dann mit der
  zweiten Stufe fort.
- Zeigt die Ausgabe der Skripte live an und speichert sie als Protokoll.
- Bricht ab, wenn ein Pflichtschritt fehlschlägt. Optionale Schritte (z. B. die NAT-Freigabe)
  blockieren den Ablauf nicht.

**Tut nicht:**

- Die Original-Firmware (`firmware/`), den Android-Compiler (`toolchain/`) oder das
  Arch-Linux-ARM-Abbild beschaffen. Das müssen Sie selbst tun (siehe
  [docs/04, Abschnitt 4.3](../docs/04-bauen-und-flashen.md#43-eingaben-selbst-beschaffen)).
- Den Bootloader entsperren. Das ist Ihre Entscheidung und hat Folgen für die Garantie und die
  Daten.
- Das Telefon in den Fastboot-Modus versetzen. Das müssen Sie mit den Tasten tun (Power und
  Lautstärke runter). Der Installer sagt Ihnen, wann.
- Ihre Passwörter speichern. Das sudo-Passwort wird nur im Speicher gehalten.

## Voraussetzungen

| Was | Warum | Auf Arch Linux |
|---|---|---|
| Python 3.9 oder neuer | Der Installer ist in Python geschrieben | `python` ist meist schon da |
| Tkinter (Tk) | das Fenster | `sudo pacman -S tk` |
| Ein Anzeigeserver (X11 oder Wayland) | das Fenster | auf dem Desktop vorhanden |
| Die Werkzeuge aus [docs, Abschnitt 4.4](../docs/04-bauen-und-flashen.md#44-host-pakete) | die Skripte brauchen sie | Der Installer zeigt fehlende Werkzeuge auf der Prüfseite |
| Die Eingaben aus [docs, Abschnitt 4.3](../docs/04-bauen-und-flashen.md#43-eingaben-selbst-beschaffen) | Firmware, Toolchain, Arch-Linux-ARM | Der Installer zeigt fehlende Dateien auf der Prüfseite |

Der Installer prüft beim Start nicht alles. Er zeigt die Prüfseite erst nach der Auswahl, denn
welche Voraussetzungen nötig sind, hängt von der Betriebsart ab.

## Start

Aus dem Repository-Wurzelverzeichnis:

```sh
python3 installer/tetris_installer.py
```

Er funktioniert auch von anderswo, weil er seinen Ort selbst kennt. Die Skripte finden ihre
Eingaben im Repository.

Ein Doppelklick im Dateimanager funktioniert je nach Desktop unterschiedlich. Verlässlich ist
der Start im Terminal.

## Die Seiten

Oben steht immer der Name der Seite und „Schritt x von 5“. Unten sind die Knöpfe **Zurück**
und **Weiter**.

### 1. Willkommen

Drei Bestätigungen, bevor es weitergeht:

- Ich habe ein Backup meiner Daten.
- Mein Bootloader ist entsperrt.
- Ich weiß, dass die Installation die Daten löscht (bei Neuinstallation).

Ohne alle drei bleibt **Weiter** gesperrt.

### 2. Auswahl

Hier wählen Sie:

- **Was passieren soll** (Betriebsart, siehe unten).
- **Profil** des Telefons (Telefon, Server, Desktop). Das Profil bestimmt das Abbild, das
  gebaut wird.
- **Halium-Abbild** (`system.img`) für die Zweite Stufe. Der Knopf „Durchsuchen“ hilft. Der
  Installer schlägt `downloads/halium/system.img` vor, wenn die Datei dort liegt.
- **Nothing OS vorher schreiben** (nur bei Neuinstallation). Das setzt das Telefon auf den
  Originalzustand zurück und löscht die Daten.

Unten zeigt die Vorschau den **genauen Ablauf** mit allen Befehlen. Hier sehen Sie, was
passieren wird, bevor Sie es starten. Ist die Auswahl nicht sinnvoll (z. B. Telefon-Profil ohne
Halium-Abbild), steht der Grund in roter Schrift, und **Weiter** ist gesperrt.

### 3. Prüfung

Eine Tabelle mit allen Voraussetzungen:

| Zeichen | Bedeutung |
|---|---|
| ✓ grün | erfüllt |
| ✗ rot | fehlt, und der Ablauf kann so nicht starten. Beheben und mit **Erneut prüfen** wiederholen |
| ! gelb | Hinweis. Das Telefon kann später angeschlossen werden (z. B. im Fastboot-Modus) |

**Weiter** ist erst frei, wenn alle roten Punkte erledigt sind. Die gelben Punkte blockieren
nicht.

### 4. Ablauf

- Die Liste der Schritte zeigt den Status jedes Schritts: *wartet*, *läuft*, *fertig*,
  *übersprungen* (optional fehlgeschlagen), *Fehler* oder *abgebrochen*.
- Das Protokoll zeigt die Ausgabe der Skripte live.
- **Starten** beginnt den Ablauf. **Abbrechen** beendet den laufenden Schritt (nach einer
  Rückfrage).
- Schließen Sie das Fenster während des Ablaufs, fragt der Installer, ob er abbrechen soll.

Destruktive Schritte fragen vor dem Start nach einer Bestätigung: Sie tippen `LOESCHEN`.

### 5. Fertig

Die Zusammenfassung mit dem Ergebnis, dem Pfad des Protokolls und den nächsten Schritten. Bei
Neuinstallation steht dort zum Beispiel, dass der schwarze Bildschirm beim ersten Start normal
ist und wie Sie das Telefon über USB erreichen.

## Betriebsarten

| Betriebsart | Was passiert | Telefon |
|---|---|---|
| **Nur bauen** | Quellen holen, Kernel bauen, Boot-Images bauen, Abbild bauen | bleibt unberührt |
| **Bauen und neu installieren** | wie „Nur bauen“, dann (optional Nothing OS), Abbild auf `userdata`, Kernel auf Slot b, warten, Zweite Stufe | **wird überschrieben, Daten gelöscht** |
| **Zweite Stufe am laufenden Telefon** | warten, NAT, `setup-phone.sh` (Halium, libhybris, wlroots, Nothing-Shell) | läuft, Telefon muss per USB erreichbar sein |
| **Server-Profil am laufenden Telefon** | warten, NAT, `setup-server.sh` | läuft, Telefon muss per USB erreichbar sein |
| **Desktop-Profil am laufenden Telefon** | warten, NAT, `setup-desktop.sh` | läuft, Telefon muss per USB erreichbar sein |

Die einzelnen Schritte im Detail, so wie sie der Installer aufruft:

| Schritt | Befehl | Hinweis |
|---|---|---|
| Kernel-Quellen holen | `scripts/fetch-sources.sh` | mehrere GB aus dem Internet |
| Kernel und Module bauen | `scripts/build-kernel.sh all` | |
| Boot-Images bauen | `scripts/build-debug-boot.sh` | |
| Arch-Linux-Abbild bauen | `env PROFILE=… scripts/build-rootfs.sh` (mit sudo) | dauert am längsten |
| Nothing OS schreiben (optional) | `scripts/flash-stock.sh firmware/…/images` | **löscht die Daten** |
| Abbild auf `userdata` | `fastboot flash userdata out/rootfs.simg` | **löscht die Daten** |
| Kernel auf Slot b | `scripts/flash-debug-b.sh` | Slot a bleibt unverändert |
| Warten auf das Telefon | eigene Prüfung über SSH (172.16.42.1:22) | bis zu fünf Minuten |
| Internet freigeben (optional) | `scripts/host-nat.sh up` (mit sudo) | |
| Zweite Stufe | `scripts/setup-phone.sh <system.img>` | |
| Server-Profil | `scripts/setup-server.sh` | Telefon startet neu |
| Desktop-Profil | `scripts/setup-desktop.sh` | Telefon startet neu |

Beim **Profil „Server“** endet die Neuinstallation nach dem Schreiben des Slots b. Das
Server-Abbild braucht keine Zweite Stufe. Beim **Profil „Desktop“** ist die Zweite Stufe
optional. Sie bringt die GPU-Beschleunigung für GNOME.

## Sicherheit

- **Bestätigungen.** Die drei Häkchen auf der ersten Seite, und vor jedem Ablauf mit
  destruktiven Schritten das Wort `LOESCHEN`.
- **sudo-Passwort.** Wird vor dem Start abgefragt und mit `sudo -v` geprüft. Es steht nur im
  Speicher. An die Schritte wird es über die Standardeingabe (stdin) weitergereicht, nie als
  Befehlszeilen-Argument (das wäre in `ps` sichtbar). Es landet nicht im Protokoll.
- **Abbrechen.** Jeder Schritt läuft in einer eigenen Prozessgruppe. **Abbrechen** beendet die
  ganze Gruppe, also auch Unterprozesse wie `make`.
- **Keine Stille.** Jede Ausgabe steht im Fenster und im Protokoll. Ein fehlgeschlagener
  Pflichtschritt beendet den Ablauf sofort.

## Kommandozeile

```sh
python3 installer/tetris_installer.py [Optionen]
```

| Option | Wirkung |
|---|---|
| `--dry-run` | Öffnet das Fenster, führt aber keinen Befehl aus. Das sudo-Passwort wird nicht abgefragt. Gut zum Ausprobieren |
| `--print-plan` | Gibt den Ablauf als Text aus und beendet sich. Kein Fenster |
| `--mode {build,full,stage2,server,desktop}` | Betriebsart (mit `--print-plan`) |
| `--profile {phone,server,desktop}` | Profil (mit `--print-plan`) |
| `--halium PFAD` | Halium-Abbild (mit `--print-plan`) |
| `--stock` | Nothing OS vorher schreiben (mit `--print-plan` und `--mode full`) |

Beispiele:

```sh
# Ablauf für eine Neuinstallation mit Halium-Abbild anzeigen
python3 installer/tetris_installer.py --print-plan --mode full --profile phone \
    --halium downloads/halium/system.img --stock

# Fenster ausprobieren, ohne etwas zu verändern
python3 installer/tetris_installer.py --dry-run
```

## Protokolle

Jeder Lauf schreibt ein Protokoll nach `out/installer-logs/installer-JJJJMMTT-HHMMSS.log`. Das
Verzeichnis `out/` liegt nicht im Git. Das Protokoll enthält:

- den Ablauf als Liste,
- jeden Befehl, den der Installer startet,
- die Ausgabe der Skripte,
- den Rückgabewert jedes Schritts,
- das Ende (`done`, `fail` oder `cancel`).

Ein Passwort steht nie im Protokoll. Teilen Sie es trotzdem nur geschwärzt, denn die Ausgabe der
Skripte kann Seriennummern und Netzadressen enthalten.

## Tests

Die Ablauf-Logik hat eigene Tests. Sie brauchen kein Telefon und kein Fenster:

```sh
python3 -m unittest discover -s installer -p 'test_*.py' -v
```

Die Tests prüfen unter anderem:

- die Reihenfolge der Schritte in jeder Betriebsart,
- dass destruktive Schritte als solche markiert sind,
- dass `sudo` nur dort gefordert wird, wo es nötig ist,
- dass das Telefon-Profil ohne Halium-Abbild abgelehnt wird,
- dass die Prüfung je nach Betriebsart die richtigen Werkzeuge und Dateien verlangt.

## Fehler und Lösungen

| Meldung oder Verhalten | Ursache | Lösung |
|---|---|---|
| `tkinter fehlt. Auf Arch Linux: sudo pacman -S tk` | Tk ist nicht installiert | `sudo pacman -S tk` |
| `Kein Anzeigeserver gefunden` | Installer ohne Desktop gestartet (z. B. per SSH) | Auf dem Desktop starten, oder `--print-plan` verwenden |
| Prüfseite: `Programm: … fehlt` | Werkzeug nicht installiert | Paket installieren (siehe [docs, 4.4](../docs/04-bauen-und-flashen.md#44-host-pakete)) |
| Prüfseite: `Arch-Linux-ARM-Basissystem` fehlt | Eingabedatei fehlt | [docs, 4.3](../docs/04-bauen-und-flashen.md#43-eingaben-selbst-beschaffen) |
| `Das Passwort stimmt nicht` | falsches sudo-Passwort | Erneut eingeben (dreimal) |
| Ablauf bleibt bei „warte auf das Telefon“ | Telefon nicht über USB erreichbar, oder Neustart dauert | Kabel prüfen, `ip addr` auf dem PC, Telefon ggf. im Initramfs (Notfall-Shell) |
| `fastboot … not found` oder kein Gerät | Telefon nicht im Fastboot-Modus | Power und Lautstärke runter halten, dann `fastboot devices` |
| Schritt „Arch-Linux-Abbild bauen“ bricht ab | Siehe Protokoll, meist ein fehlendes Paket oder QEMU | [docs, 4.12](../docs/04-bauen-und-flashen.md#412-häufige-fehler-beim-bauen) |
| Ablauf endet mit `fail` | Der Schritt im Protokoll zeigt die Ursache | Protokoll lesen, Ursache beheben, Installer neu starten und mit den gleichen Einstellungen erneut ausführen |

Ein fehlgeschlagener Lauf lässt sich bei den gleichen Einstellungen wiederholen. Die Schritte
sind größtenteils wiederholbar: `fetch-sources.sh` überspringt bereits vorhandene
Quellen, und `build-kernel.sh` baut neu, was sich geändert hat. Die Flash-Schritte schreiben
erneut, das schadet nicht.

## Aufbau der Dateien

| Datei | Inhalt |
|---|---|
| `tetris_installer.py` | Oberfläche (Tk), Ausführung im Hintergrund, Telefon-Prüfungen, Einstieg |
| `steps.py` | Ablaufplan: welche Schritte in welcher Reihenfolge, Voraussetzungen. Ohne Oberfläche |
| `test_steps.py` | Tests für `steps.py` |
| `README.md` | diese Anleitung |

Die Trennung ist Absicht. `steps.py` entscheidet, **was** passiert, und ist ohne Fenster prüfbar.
`tetris_installer.py` zeigt es an und führt es aus. Wenn Sie einen Schritt ändern oder ergänzen,
passen Sie `steps.py` an und ergänzen Sie einen Test in `test_steps.py`.

### Einen Schritt hinzufügen

1. In `steps.py` in `build_plan()` an der richtigen Stelle ein `Step(...)` einfügen.
   `root=True`, wenn der Befehl `sudo` braucht. `destructive=True`, wenn er Daten löscht.
   `optional=True`, wenn ein Fehlschlag den Ablauf nicht stoppen soll.
2. Einen Test in `test_steps.py` ergänzen, der die Reihenfolge prüft.
3. Mit `--print-plan` die Ausgabe ansehen.
4. Mit `--dry-run` das Fenster durchklicken.

## Stand der Prüfung

Der Installer ist mit Python 3.14 und Tk 8.6 entwickelt. Geprüft sind:

- die Ablauf-Logik mit Unit-Tests,
- der Ablauf als Text (`--print-plan`) in allen Betriebsarten,
- die Oberfläche im Trockenlauf: alle Seiten, Bestätigungen, Protokoll, Ausführung,
- der Ausführungspfad mit echten Befehlen: Erfolg, Fehlschlag, optionaler Fehlschlag, Abbrechen,
- die Größe der Seiten im Fenster.

**Nicht geprüft:** ein Durchlauf mit einem echten Telefon. Die Schritte rufen Skripte auf, die
im Projekt bereits verwendet werden. Die Kombination aus Installer und Gerät ist aber noch nicht
erprobt. Beim ersten echten Lauf bitte das Protokoll aufheben.
