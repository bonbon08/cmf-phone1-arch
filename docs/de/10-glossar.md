# 10. Glossar

**🇩🇪 Deutsch** · [🇬🇧 English](../10-glossary.md)

Alphabetisch sortiert. Die Erklärungen sind knapp gehalten. Wo es nötig ist, verweisen sie
auf das Kapitel mit den Details.

**A**

**A/B-Slot.** Zwei Kopien der wichtigsten Partitionen (`boot_a` und `boot_b` usw.). Slot a
enthält das Original-System, Slot b unser System. Der Bootloader wählt den aktiven Slot.
Siehe [02](02-geraet-und-partitionen.md#23-die-ab-slots).

**ALSA, UCM.** Die Audio-Schnittstelle von Linux. UCM (Use Case Manager) beschreibt, welche
Lautsprecher, Mikrofone und Wege zu welchem Zweck gehören. Unser Profil liegt unter
`overlay/usr/share/alsa/ucm2/`.

**APEX.** Android-Paketformat für Systemteile, die einzeln aktualisiert werden. Im Container
eingebunden von `tetris-mount-apexes`.

**APN (Access Point Name).** Die Adresse, über die das Mobilfunknetz eine Datenverbindung
herstellt. Das Projekt braucht keinen eigenen APN: Das Modem nutzt den, den das Netz anbietet.

**AT-Befehle.** Textbefehle, mit denen man ein Mobilfunkmodem steuert (z. B. `AT+CGDATA`). ModemManager
spricht mit dem Modem über sie.

**AVB (Android Verified Boot).** Mechanismus, der prüft, ob die Partitionen unverändert sind.
Unser Kernel läuft nur, wenn AVB außer Kraft gesetzt ist. Siehe
[04](04-bauen-und-flashen.md#3a-optional-stock-system-neu-schreiben-flash-stocksh).

**B**

**Bootloader.** Das erste Programm, das nach dem Einschalten läuft. Bei diesem Telefon ist es
LK. Siehe [01](01-grundlagen.md#12-begriffe-aus-der-startkette).

**Bind-Mount.** Ein Verzeichnis oder eine Datei erscheint an einer zweiten Stelle, ohne Kopie.
Der Container nutzt sie, um Teile des Hosts freizugeben.

**BlueZ.** Der Bluetooth-Dienst von Linux.

**busybox.** Ein Werkzeugkasten mit vielen Unix-Befehlen in einer einzigen kleinen Datei. Im
Initramfs ist er die Shell.

**C**

**Cortex-A78 / A55.** Die Prozessorkerne des Dimensity 7300: vier schnelle (A78) und vier
sparsame (A55).

**clang / lld.** Der C-Compiler (LLVM) und der Linker. Android nutzt sie, wir auch.

**Compositor.** Das Programm, das Fenster zusammensetzt und auf den Bildschirm bringt. Hier
phoc (Phosh) oder nothing-wm (Nothing-Shell). Siehe [01](01-grundlagen.md#14-grafik-wie-kommt-ein-bild-auf-den-bildschirm).

**Container (LXC).** Eine abgeschottete Umgebung im selben Kernel. Der Halium-Container
stellt die Android-Treiber bereit. Siehe [03](03-architektur.md#33-der-halium-container).

**Crash-Schutz.** Mechanismus, der nach einem GPU-Absturz den nächsten Start in den
Software-Modus schickt. Siehe [09](09-fehlersuche.md#99-crash-schutz).

**D**

**depmod.** Erzeugt die Abhängigkeitsliste der Kernel-Module (`modules.dep`).

**Device Tree (DTB).** Eine Beschreibung der Hardware, die der Bootloader dem Kernel übergibt.
Liegt in `vendor_boot`.

**DPMS.** Energiesparfunktion des Bildschirms. `tetris-display-off` schaltet das Panel damit aus.

**DRM (Direct Rendering Manager).** Die Kernel-Schnittstelle zur Grafikkarte und zum Bildschirm.
Der Software-Modus nutzt sie direkt.

**dm-linear.** Der Device-Mapper, der aus Teilen einer Partition neue Geräte macht. Damit
öffnet `tetris-map-super` die Android-Partitionen aus `super`.

**Docker / Ollama.** Docker startet Container mit Software. Ollama führt KI-Modelle aus. Im
Server-Profil laufen beide. Siehe [08](08-profile.md#85-server-profil).

**Dimensity 7300 (MT6878).** Der Chip des Telefons, gebaut von MediaTek.

**E**

**EGL.** Schnittstelle zwischen einer Anwendung und dem Grafiktreiber. libhybris stellt sie für
den Android-Treiber bereit.

**F**

**Fastboot.** Modus des Bootloaders, in dem der PC über USB Partitionen beschreiben kann.
Gestartet mit Power + Lautstärke runter.

**fastbootd.** Eine Stufe innerhalb von Fastboot, die logische Partitionen (`super`) verwaltet.

**Flash / flashen.** Ein Image in eine Partition schreiben.

**G**

**GDM.** Der Anmeldebildschirm von GNOME. Im Desktop-Profil startet er die Headless-Sitzung.

**glibc.** Die C-Standardbibliothek des Systems.

**glvnd.** Eine Schicht, die EGL-Anbieter auswählt. Im Desktop-Profil zeigt sie auf libhybris.

**GNSS / GPS.** Das Satellitenortungssystem. `tetris-gnss` liefert die Daten an geoclue.

**GPL-2.0-or-later.** Die Lizenz des eigenen Codes. Siehe `LICENSE`.

**GSI (Generic System Image).** Ein allgemeines Android-Abbild, das auf vielen Geräten läuft.
Hier das Halium-GSI von LuneOS.

**gralloc.** Der Android-Speicherdienst für Bilder. Der Compositor holt sich über ihn Puffer.

**H**

**Halium.** Projekt, das Android-Treiber in einem Container für Linux-Telefone bereitstellt.
Siehe [01](01-grundlagen.md#13-was-ist-android-hier-und-warum-brauchen-wir-es).

**HAL (Hardware Abstraction Layer).** Android-Schnittstelle zwischen Systemdiensten und
Hardware, z. B. der Composer-HAL oder die Modem-HAL.

**HBM (High Brightness Mode).** Hochhelligkeitsmodus des Panels. Der Fingerabdrucksensor
braucht ihn für eine gute Aufnahme.

**hwcomposer / Composer.** Der Android-Dienst, der Bilder auf den Bildschirm legt. Unser
wlroots-Backend nutzt ihn.

**I**

**Image (Abbild).** Eine Datei, die eine ganze Partition beschreibt.

**Initramfs.** Mini-System im Speicher, das vor dem Hauptsystem läuft. Siehe
[03](03-architektur.md#schritt-3-das-initramfs-initramfsinit).

**IMEI.** Die Gerätenummer des Mobilfunkmoduls. Steht in den Modem-Partitionen.

**K**

**Kernel.** Der Kern des Betriebssystems: Prozessor, Speicher, Geräte. Hier Linux 6.1 von Nothing.

**Kernel-Modul (`.ko`).** Ein Treiber, der zur Laufzeit geladen wird.

**KMI.** Kernel Module Interface: Android-Schnittstelle, die Module zum Kernel passend hält. Wir
schalten sie ab, weil wir alle Module selbst bauen.

**L**

**LK (Little Kernel).** Der Bootloader von MediaTek.

**libhybris.** Bibliothek, die Android-Treiber für normale Linux-Programme nutzbar macht.
Siehe [07](07-kernel-treiber-patches.md#727-libhybris-drei-anpassungen).

**Layer-Shell.** Wayland-Erweiterung für Oberflächenelemente wie Leisten und Overlays. Die
Home-Leiste und der Fingerabdruck-Fleck brauchen sie.

**logind.** Der systemd-Dienst, der Sitzungen, Benutzer und Tasten verwaltet.

**M**

**Magisk.** Werkzeug, das auf Android Root-Rechte gibt. Braucht nur `read-flashlog.sh` auf
Nothing OS.

**ModemManager.** Linux-Dienst, der Mobilfunkmodems steuert.

**Mali-G615.** Die Grafikeinheit des Chips. Nur mit dem Android-Treiber nutzbar.

**mkbootimg.** Werkzeug, das Boot-Images erzeugt.

**N**

**NCM (USB Network Control Model).** Eine USB-Netzwerkschnittstelle. Das Initramfs richtet sie
ein, damit der PC das Telefon unter `172.16.42.1` erreicht.

**NVRAM.** Dauerhafter Speicher für Kalibrierdaten (WLAN, Modem, Bluetooth). Liegt in den
Partitionen `nvdata` und `nvcfg`.

**nt_reserve2.** Eine Reservepartition, die unser Initramfs als Protokollspeicher nutzt.

**O**

**Overlay.** Ein Verzeichnisbaum mit Dateien, die über das Rootfs kopiert werden. Siehe
[06](06-rootfs-referenz.md#61-was-ist-ein-overlay).

**P**

**Partition.** Abgegrenzter Bereich im Flash-Speicher. Siehe [02](02-geraet-und-partitionen.md#22-was-ist-eine-partition).

**Patch.** Textdatei mit Änderungen an fremdem Quelltext. Siehe [07](07-kernel-treiber-patches.md#71-was-ist-ein-patch).

**pacman.** Der Paketmanager von Arch Linux.

**Phosh.** Eine freie Telefon-Oberfläche auf Basis von GNOME und phoc.

**pidfd.** Eine Prozessreferenz, die sicher ist, auch wenn die Prozess-ID wiederverwendet wird.
Ein Patch bringt sie für den Desktop in den Kernel.

**pmsg / pstore.** Speicher, den der Kernel bei einem Absturz oder Warmstart nicht verliert.
Unser Initramfs schreibt dorthin.

**Profil.** Eine Betriebsart: phone, server oder desktop. Siehe [08](08-profile.md).

**Q**

**QEMU.** Emulator. Hier läuft die aarch64-chroot beim Bauen des Rootfs.

**R**

**RDP (Remote Desktop Protocol).** Protokoll für Fernzugriff auf Desktops (Microsoft). Der
GNOME-Desktop des Telefons ist darüber erreichbar.

**Rootfs.** Das Dateisystem des Arch-Linux-Systems.

**S**

**SCP-Sensor-Hub.** Mikrocontroller, der Sensoren ausliest. `tetris-sensors` spricht mit ihm.

**Schlafmodus (Suspend).** Energiesparzustand, in dem das System anhält. Auf diesem Telefon
ist er nicht nutzbar, daher verboten.

**SO_PEERPIDFD.** Socket-Schalter, mit dem der Kernel eine pidfd des Gegenübers liefert. Fehlt
im Kernel 6.1, deshalb unser Patch.

**squeekboard.** Die Bildschirmtastatur für Wayland.

**super.** Partition, die die Android-Systempartitionen (system, vendor ...) als logische
Partitionen enthält.

**switch_root.** Der Befehl, der aus dem Initramfs in das richtige System wechselt.

**systemd.** Start- und Dienstverwaltung von Linux. Prozess 1 im fertigen System.

**T**

**TEE (Trusted Execution Environment).** Gesicherter Bereich im Prozessor. Der
Fingerabdruck-Dienst braucht ihn.

**tetris-\*.** Präfix für alle eigenen Programme und Dienste dieses Projekts.

**tetris-mode.** Befehl zum Umschalten der Profile. Siehe [08](08-profile.md#82-die-umschalter).

**Tux.** Das Maskottchen von Linux. Erscheint als Splash beim Starten.

**U**

**udev.** Der Gerätemanager von Linux. Vergibt Namen und Rechte für Geräte und lädt Regeln.

**UDC (USB Device Controller).** Der USB-Anschluss im Gerät. Das Initramfs braucht ihn für NCM.

**userdata.** Die Partition für alle Ihre Daten und für das Arch-Linux-System.

**V**

**vendor / vendor_boot / vendor_dlkm.** Herstellerteile des Systems: Treiber, Firmware,
Module. `vendor_boot` enthält bei uns das Initramfs.

**VT (virtuelles Terminal).** Eine Textkonsole. GNOME und logind erwarten eine.

**W**

**Wayland.** Protokoll zwischen Anwendungen und Compositor. Nachfolger von X11.

**wlroots.** Bibliothek für Wayland-Compositoren. Unsere Version 0.20 hat das Hwcomposer-Backend.

**X**

**Xwayland.** Kompatibilitätsschicht, mit der alte X11-Programme unter Wayland laufen.

**Z**

**zram.** Komprimierter Swap im Arbeitsspeicher. Im Server-Profil für größere Modelle.
