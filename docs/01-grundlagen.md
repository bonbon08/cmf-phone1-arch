# 1. Grundlagen: Was hier eigentlich passiert

Dieses Kapitel ist für Leser ohne technisches Vorwissen. Es erklärt die Bausteine, die in
den übrigen Kapiteln immer wieder vorkommen. Wer das alles kennt, kann es überspringen.

## 1.1 Was macht das Projekt?

Ein Smartphone wird normalerweise mit dem Betriebssystem geliefert, das der Hersteller
installiert hat. Beim **CMF Phone 1** von Nothing ist das **Nothing OS**, eine Android-Variante.

Dieses Projekt ersetzt **nicht** den Android-Teil komplett, sondern tauscht das System aus,
das Sie täglich sehen:

- Auf dem Telefon läuft **Arch Linux ARM**, eine vollständige Linux-Distribution für
  ARM-Prozessoren (das sind die Prozessoren in fast allen Handys).
- Die Oberfläche ist entweder die **Nothing-Shell** (das Standardprofil, ein Startbildschirm mit
  App-Raster, Statusleiste und Home-Leiste im Stil eines Telefons) oder **Phosh** (eine
  freie Telefon-Oberfläche aus dem GNOME-Umfeld).
- Die Telefon-Hardware wird vom Linux-Kernel angesprochen. Für Teile, die nur mit
  Android-Treibern funktionieren (Grafikchip, Modem, Kamera), läuft im Hintergrund ein kleiner
  **Android-Container**, der diese Treiber bereitstellt. Dazu gleich mehr.

Das Ergebnis ist ein Telefon, das wie ein Linux-Rechner arbeitet: Sie können Pakete mit
`pacman` installieren, Dienste mit `systemd` verwalten und die gesamte Software aus der
Arch-Welt nutzen. Zusätzlich kann dasselbe Gerät als **Server** (ohne Bildschirm, per SSH
und Docker erreichbar) oder als **Desktop** (GNOME über das Netzwerk, RDP) starten. Siehe
[08-profile.md](08-profile.md).

**Wichtig:** Das Projekt ist eine frühe Vorschau für Leute, die Fastboot und Linux kennen.
Die Installation **löscht alle Daten** auf dem Telefon. Bevor Sie anfangen, lesen Sie
[02-geraet-und-partitionen.md](02-geraet-und-partitionen.md) und die Risiken in
[04-bauen-und-flashen.md](04-bauen-und-flashen.md#42-risiken).

## 1.2 Begriffe aus der Startkette

Wenn Sie das Telefon einschalten, passieren nacheinander mehrere Dinge. Jeder Schritt
hat einen eigenen Namen.

| Begriff | Was es ist | Vergleich |
|---|---|---|
| **Bootloader** | Das erste Programm, das der Chip nach dem Einschalten startet. Es lädt das Betriebssystem. Bei diesem Telefon heißt es **LK** (Little Kernel) und wird vom Hersteller geliefert. | Der Pförtner, der prüft, wer hinein darf |
| **Kernel** | Der Kern des Betriebssystems. Er verwaltet Prozessor, Speicher, Geräte und Treiber. Linux ist ein Kernel. | Das Fundament eines Hauses |
| **Treiber / Kernel-Modul** | Ein Stück Kernel-Code für ein bestimmtes Bauteil (z. B. WLAN). Module können zur Laufzeit geladen werden (`insmod`, `modprobe`). | Ein Dolmetscher für ein bestimmtes Gerät |
| **Initramfs** | Ein kleines Mini-System im Speicher, das läuft, bevor das eigentliche System startet. Es lädt die nötigen Treiber und sucht die Systempartition. | Ein Vorbau, der die Tür öffnet |
| **Rootfs** | Das eigentliche Dateisystem des Systems (`/bin`, `/etc`, `/home` ...). Bei uns ist es Arch Linux ARM. | Das Haus selbst |
| **systemd** | Das erste Programm im fertigen System. Es startet und überwacht alle Dienste. | Der Hausmeister, der alles einschaltet |
| **Dienst (Service)** | Ein Programm, das im Hintergrund läuft, z. B. das WLAN-Management. Wird von systemd gestartet. | Ein Angestellter, der dauerhaft arbeitet |

Zwei Wörter verdienen besondere Aufmerksamkeit:

- **Fastboot** ist ein Modus des Bootloaders, in dem der PC über USB Partitionen
  beschreiben kann. Man startet ihn mit *Power + Lautstärke runter* (siehe
  [04-bauen-und-flashen.md](04-bauen-und-flashen.md)). Das Schreiben in eine Partition
  heißt **flashen**.
- **Partition** ist ein abgegrenzter Bereich im Flash-Speicher des Telefons, etwa
  „hier liegt der Kernel“ oder „hier liegen Ihre Fotos“. Mehr dazu in
  [02-geraet-und-partitionen.md](02-geraet-und-partitionen.md).

## 1.3 Was ist Android hier, und warum brauchen wir es?

Ein Handy braucht Treiber, die der Hersteller oft nur für Android (genauer: für sein eigenes
Android-Abbild) liefert. Diese Treiber sind meistens **proprietär** (nicht frei verfügbar)
und setzen auf Android-Dienste, die es unter normalem Linux nicht gibt.

Die Lösung dieses Projekts heißt **Halium**. Dabei läuft ein minimales Android-System (hier
**LuneOS Halium 16**) **in einem Container** neben dem Linux-System. Ein Container ist eine
abgeschottete Umgebung innerhalb desselben Systems, ähnlich einer virtuellen Maschine, aber
ohne eigenen Kernel. Der Container stellt nur die Hardware-Dienste bereit, die wir brauchen:

- **Grafik:** Allocator und Composer (sie geben Bilder an den Bildschirm weiter),
- **Modem:** Mobilfunk-Anbindung,
- **Kamera**, **GPS**, **Fingerabdrucksensor**.

Das Linux-System spricht diese Dienste über eine Brücke namens **libhybris** an. libhybris
ist eine Bibliothek, die Android-Treiber so aufruft, dass normale Linux-Programme sie
nutzen können. Siehe [03-architektur.md](03-architektur.md#33-der-halium-container).

## 1.4 Grafik: Wie kommt ein Bild auf den Bildschirm?

Eine Oberfläche besteht aus Fenstern, die zusammengesetzt werden. Dafür gibt es in Linux
den **Compositor**. In diesem Projekt sind das:

- **phoc** (für Phosh) oder **nothing-wm** (für die Nothing-Shell). Beide basieren auf
  **wlroots**, einer Bibliothek für Wayland-Compositoren.
- **Wayland** ist das Protokoll, über das Programme ihre Fenster an den Compositor schicken.
- Die **GPU** (Grafikchip, hier Mali-G615) berechnet die Darstellung. Der Compositor kann
  auf der GPU arbeiten, oder ohne GPU auf dem Prozessor (**Software-Modus**, mit `pixman`).

Es gibt also zwei Wege:

1. **GPU-Modus:** Der Compositor nutzt den Android-Composer über libhybris. Schnell und flüssig.
2. **Software-Modus:** Alles läuft auf dem Prozessor. Langsamer, aber zuverlässig. Er
   ist der Ausweg, wenn der GPU-Modus abstürzt.

Das Programm `tetris-phosh-select` entscheidet beim Start, welcher Weg genommen wird. Dazu
gibt es einen **Crash-Schutz**: Stürzt der GPU-Modus innerhalb von 90 Sekunden ab, startet der
nächste Start im Software-Modus. Details in [09-fehlersuche.md](09-fehlersuche.md#99-crash-schutz).

## 1.5 Was ist ein Profil?

Das Telefon kann verschiedene **Profile** haben, also verschiedene Aufgaben übernehmen:

- **Phone:** Telefon mit Oberfläche (Standard).
- **Server:** kein Bildschirm, dafür per SSH und WLAN erreichbar, mit Docker-Containern und
  lokalen KI-Modellen.
- **Desktop:** wie Server, aber mit einem vollständigen GNOME-Desktop, den Sie über das
  Netzwerk (RDP) auf einem anderen Gerät sehen und bedienen.

Umgeschaltet wird über das Kommando `tetris-mode` oder über die App **Startmodus**. Siehe
[08-profile.md](08-profile.md).

## 1.6 Was sind die Werkzeuge auf dem PC?

Zum Bauen brauchen Sie einen PC mit Linux (entwickelt auf Arch Linux, mit x86-64-Prozessor).
Die wichtigsten Werkzeuge sind:

| Werkzeug | Wofür |
|---|---|
| `git` | holt Quellcode aus dem Internet und verwaltet Versionen |
| `make`, `clang`, `lld` | übersetzen Quellcode, hier für ARM64 |
| `fastboot` | spricht mit dem Bootloader des Telefons |
| `mkbootimg`, `unpack_bootimg` | bauen und zerlegen Android-Boot-Images |
| `arch-chroot`, `bsdtar` | arbeiten in einem Arch-Linux-ARM-Abbild, ohne es zu starten |
| `ssh`, `scp` | Verbindung zum laufenden Telefon über USB |

Eine vollständige Liste mit Installationshinweisen steht in
[04-bauen-und-flashen.md](04-bauen-und-flashen.md#44-host-pakete).

## 1.7 Wo finde ich was im Repository?

```
cmfphone1archLinux/
├── README.md            Kurzanleitung und Funktionsstatus
├── docs/                diese Dokumentation
├── scripts/             Bau-, Flash- und Einrichtungs-Skripte (auf dem PC)
├── pkgbuilds/           Bau-Skripte, die auf dem Telefon laufen
├── kernel/              Kernel-Konfiguration (linux.config)
├── patches/             Änderungen an fremder Software (Kernel, libhybris, wlroots, ModemManager)
├── initramfs/           das Mini-System, das vor dem Hauptsystem startet
├── rootfs/              Dateien und Programme für das Telefon-System
│   ├── overlay/         Gemeinsame Dateien (Phone-Profil)
│   ├── overlay-server/  Zusätzlich für das Server-Profil
│   ├── overlay-desktop/ Zusätzlich für das Desktop-Profil
│   └── src/             eigene C- und Python-Programme
├── stock-info/          Auszüge aus dem Original-System (nur lokal, nicht im Git)
├── firmware/            Original-Images von Nothing (nur lokal, nicht im Git)
├── downloads/           Arch-Linux-ARM-Abbild, Busybox, Magisk, Halium (nur lokal, nicht im Git)
├── toolchain/           Android-Compiler (nur lokal, nicht im Git)
├── src/                 Kernel-Quellen (werden von scripts/fetch-sources.sh geholt, nicht im Git)
└── out/                 alle Build-Ergebnisse, SSH-Schlüssel (nur lokal, nicht im Git)
```

Im Git (`git ls-files`) liegen nur die Verzeichnisse `scripts/`, `pkgbuilds/`, `kernel/`,
`patches/`, `initramfs/`, `rootfs/` und die Dateien `README.md`, `LICENSE` und `.gitignore`.
Die `.gitignore` ist eine **Whitelist**: Alles, was nicht ausdrücklich erlaubt ist, wird
ignoriert. Damit verlassen Firmware, Gerätedaten, Seriennummern, IMEI-Sicherungen und
SSH-Schlüssel den PC nicht. Was genau in den einzelnen Verzeichnissen steckt, erklären die
Kapitel 05 bis 07.

## 1.8 Lizenz und Haftung

Eigener Code (Skripte, Dienste, Patches, Apps) steht unter **GPL-2.0-or-later** (siehe
`LICENSE`). Die Patches für fremde Projekte behalten deren Lizenz. Das Projekt ist ein
Hobby-Projekt **ohne Garantie**. Ein falsch geflashtes Telefon kann unbrauchbar werden. Lesen
Sie vor jedem Flash-Vorgang [04-bauen-und-flashen.md](04-bauen-und-flashen.md#42-risiken).
