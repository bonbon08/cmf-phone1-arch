# Technische Dokumentation: Arch Linux auf dem CMF Phone 1

Diese Dokumentation erklärt das Projekt **vollständig**: was auf dem Telefon läuft, wie
es zusammengebaut wird, welches Skript was tut und warum etwas so gelöst wurde. Sie
richtet sich an zwei Gruppen:

- **Einsteiger ohne technisches Vorwissen.** Sie wollen verstehen, was dieses Projekt
  macht, und vielleicht später selbst installieren. Lesen Sie in dieser Reihenfolge:
  `01-grundlagen.md`, `02-geraet-und-partitionen.md`, `08-profile.md`, `10-glossar.md`.
  Begriffe, die Ihnen unbekannt vorkommen, stehen im Glossar.
- **Entwickler und Bastler**, die bauen, flashen, Fehler suchen oder Code ändern wollen.
  Dort stehen die Details: `03-architektur.md`, `04-bauen-und-flashen.md`,
  `05-skript-referenz.md`, `06-rootfs-referenz.md`, `07-kernel-treiber-patches.md`,
  `09-fehlersuche.md`.

Die Kurzfassung für den eiligen Leser steht in der Haupt-[README](../README.md).

## Inhalt

| Datei | Inhalt | Für wen |
|---|---|---|
| [01-grundlagen.md](01-grundlagen.md) | Was Linux, ein Kernel, ein Bootloader, ein Compositor und ein Container sind. Ohne Vorkenntnisse | Alle |
| [02-geraet-und-partitionen.md](02-geraet-und-partitionen.md) | Das Telefon (Chip, Speicher, Partitionen, A/B-Slots) und was auf welcher Partition liegt | Alle, Pflicht vor dem Flashen |
| [03-architektur.md](03-architektur.md) | Startkette vom Einschalten bis zum Desktop, Halium-Container, GPU-Pfad, Modem-Pfad | Entwickler |
| [04-bauen-und-flashen.md](04-bauen-und-flashen.md) | Schritt-für-Schritt: Eingaben beschaffen, bauen, flashen, zweite Stufe, Rückweg | Anwender, Bastler |
| [05-skript-referenz.md](05-skript-referenz.md) | Jedes Skript in `scripts/`, `pkgbuilds/` und `initramfs/`: Zweck, Argumente, Ein- und Ausgaben | Entwickler |
| [06-rootfs-referenz.md](06-rootfs-referenz.md) | Das Dateisystem des Telefons: Overlays, Systemd-Dienste, eigene Programme, Konfigurationen | Entwickler |
| [07-kernel-treiber-patches.md](07-kernel-treiber-patches.md) | Kernel-Konfiguration, Patches für Kernel, Treiber, libhybris, wlroots, ModemManager | Entwickler |
| [08-profile.md](08-profile.md) | Die vier Betriebsarten: Phone (Nothing-Shell oder Phosh), Server, Desktop und die Einmal-Umschaltung | Alle |
| [09-fehlersuche.md](09-fehlersuche.md) | Logs finden, typische Fehler und ihre Ursachen, Crash-Schutz, Rückweg bei Problemen | Alle |
| [10-glossar.md](10-glossar.md) | Erklärung aller Fachbegriffe und Abkürzungen | Alle |

## Konventionen in dieser Dokumentation

- **Pfade** sind relativ zum Repository-Wurzelverzeichnis (`cmfphone1archLinux/`), wenn
  nichts anderes steht. Beispiel: `scripts/build-kernel.sh` meint
  `<Repo>/scripts/build-kernel.sh`.
- **Befehle** stehen in Code-Blöcken. Wo sie laufen, steht in der Überschrift oder im Text davor:
  auf dem **PC** oder auf dem **Telefon** (über `scripts/phone-ssh.sh` oder direkt als `alarm`).
- **`out/`, `src/`, `downloads/`, `firmware/`, `toolchain/`** sind lokale Verzeichnisse.
  Sie liegen nicht im Git (siehe `.gitignore`), weil sie proprietäre Dateien, Build-Ergebnisse
  oder sehr große Quellen enthalten. Was sie enthalten sollen, steht in
  [04-bauen-und-flashen.md](04-bauen-und-flashen.md#43-eingaben-selbst-beschaffen).
- **Telefon-Namen:** Der Code-Name des Geräts ist `Tetris`, der Chip ist ein MediaTek
  Dimensity 7300 (`MT6878`). Im Quelltext erscheinen beide Namen, `tetris-*` ist das
  Präfix für unsere eigenen Programme und Dienste.
- **Status-Angaben** (funktioniert, teilweise, nicht) stammen aus der Tabelle in der
  Haupt-README und gelten für den Branch `main`.

## Stand

Diese Dokumentation beschreibt den Stand von `main` (November 2026). Wenn sich Skripte ändern, sollten die betroffenen Abschnitte
im selben Commit angepasst werden.
