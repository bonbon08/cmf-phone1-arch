# 7. Kernel, Treiber und Patches

**🇩🇪 Deutsch** · [🇬🇧 English](../07-kernel-drivers-patches.md)

Dieses Kapitel erklärt, woraus der Kernel des Telefons besteht, welche Änderungen wir
vornehmen und warum. Es gilt für alle Teile, die fremde Software verändern: den Kernel,
die Gerätetreiber, libhybris, wlroots und ModemManager.

## 7.1 Was ist ein Patch?

Ein **Patch** ist eine Textdatei, die beschreibt, welche Zeilen in einer fremden
Quelltext-Datei geändert werden. Sie hat das Format von `git diff`. Die Datei beginnt mit
einer Beschreibung (`Subject:` und der Text darunter), danach folgen die Änderungen.

Patches sind das Herz der Anpassung: Wir verändern **keine** fremden Quellen direkt.
Stattdessen liegt jede Änderung als eigene Datei im Repository. Das bleibt überprüfbar,
und bei einer neuen Version der fremden Software kann man die Patches gezielt wieder
anwenden.

Die Patches wendet das Skript `scripts/fetch-sources.sh` auf die Kernel-Quellen an. Die
Pakete libhybris, wlroots und ModemManager wenden die Pkgbuild-Skripte auf dem Telefon an.

## 7.2 Übersicht aller Patches

| Datei | Ziel | Was er ändert | Pflicht? |
|---|---|---|---|
| `patches/kernel/0001-net-backport-SO_PEERPIDFD.patch` | Kernel (Netzwerk) | Rückportiert `SO_PEERPIDFD` aus Linux 6.5 | Pflicht fürs Desktop-Profil |
| `patches/kernel_device_modules/0001-mtk-battery-kernel-gauge-default.patch` | Akku-Treiber | Startet die Warteschlange der Akku-Unterbrechung vor deren Anforderung | Pflicht (verhindert einen NULL-Zeiger-Absturz) |
| `patches/kernel_device_modules/0002-mt6369-readable-pmic-reg-clear.patch` | Audio-Codec MT6369 | Ergänzt eine Leseroutine für einen Mixer-Schalter | Pflicht für Audio-Erkennung (PipeWire) |
| `patches/kernel_device_modules/0003-s6e8fc3x02-hbm-mode-notifies-fingerprint.patch` | Display-Treiber | Meldet dem Fingerabdruck-Treiber, wenn der Hochhelligkeitsmodus (HBM) an ist | Pflicht für den Fingerabdruck |
| `patches/kernel_device_modules/0004-ft3519-resume-like-suspended.patch` | Touch-Treiber | Stellt den Touch nach dem Aufwachen korrekt wieder her | Pflicht (sonst bleibt der Touch tot) |
| `patches/kernel_modules/0001-bt-mt66xx-optional-linux-hci.patch` | Bluetooth (`mt66xx`) | Optionale Variante: Bluetooth als Linux-HCI-Gerät | Nein, nur mit `TETRIS_LINUX_HCI=1` (experimentell) |
| `patches/libhybris/0001-egl-stub-EGL_EXT_device_query-for-GTK4.patch` | libhybris | Gibt EGL-Erweiterungen an, die GTK 4 verlangt | Pflicht für GTK-4-Apps |
| `patches/libhybris/0002-egl-surfaceless-mesa-platform-for-mutter.patch` | libhybris | Unterstützt die „surfaceless“-EGL-Plattform für den GNOME-Shell (Mutter) | Pflicht für das Desktop-Profil |
| `patches/libhybris/0003-wayland-server-texture-format-for-rgbx-buffers.patch` | libhybris | Liefert für RGBX-Puffer ein Format, das Compositoren akzeptieren | Pflicht für Spiele (SDL) und GNOME |
| `patches/wlroots/0002-hwcomposer-backend-hybris-allocator.patch` | wlroots 0.20 | Neues Backend für den Android-Composer und einen Speicher-Zuteiler über libhybris | Pflicht für die GPU-Oberfläche |
| `patches/modemmanager/tetris/…` (5 Dateien) | ModemManager | Plugin für das MediaTek-Modem | Pflicht für Mobilfunk |

Der Patch für wlroots (`0001`, „Revert layer-shell“) stammt von phoc 0.57 und wird nicht im
Projekt gespeichert. Das Skript `build-wlroots-phoc.sh` lädt ihn direkt von GitLab.

### 7.2.1 Kernel: `SO_PEERPIDFD` (`kernel/0001`)

**Problem:** Ab GNOME 49 holt sich der Desktop über den Nachrichtenbus D-Bus eine
**pidfd** des Anrufers (eine sichere Prozessreferenz). Der Bus `dbus-broker` gibt sie nur
heraus, wenn der Kernel den Socket-Schalter `SO_PEERPIDFD` kennt. Linux 6.1 kennt ihn noch
nicht. Ohne ihn schlägt die Fernanmeldung fehl (Meldung „D-Bus broker doesn't support pidfd“).

**Lösung:** Der Patch überträgt die Funktion aus Linux 6.5 (Commit `7b26952a91cf`) in unseren
Kernel. `pidfd_prepare()` ist im Kernel bereits vorhanden.

**Wo wirksam:** Nur im Desktop-Profil, aber der Kernel wird für alle Profile mit diesem Patch
gebaut.

### 7.2.2 Akku: Warteschlange vor dem Anfordern (`kernel_device_modules/0001`)

**Problem:** Der Treiber des Akku-Messgeräts (`mtk-battery`) fordert eine Unterbrechung an,
bevor seine Warteschlange (`wait_que`) initialisiert ist. Im Fehlerfall dereferenziert der
Kernel einen NULL-Zeiger.

**Lösung:** Die Initialisierung der Warteschlange wird vor das Anfordern der Unterbrechung
verschoben. Dazu kommt ein Kommentar im Patch, der die Ursache erklärt.

### 7.2.3 Audio: Lesbarer Mixer-Schalter (`kernel_device_modules/0002`)

**Problem:** Ein Schalter im Audio-Codec `MT6369` hat keine Lesefunktion. Das Lesen schlägt mit
`-EPERM` fehl. Dann kann `snd_mixer_load()` (von PipeWire für die Erkennung eines
Kopfhörers verwendet) für die ganze Soundkarte nicht laden.

**Lösung:** Eine Lesefunktion, die immer 0 liefert.

### 7.2.4 Display: Fingerabdruck und Hochhelligkeit (`kernel_device_modules/0003`)

**Problem:** Der optische Fingerabdrucksensor nimmt nur bei eingeschaltetem
Hochhelligkeitsmodus (HBM) ein Bild auf. Auf Android schaltet das der Composer.
Unter Linux gibt es keinen Composer in dieser Rolle, deshalb meldet der Panel-Treiber die
Umschaltung nun direkt an den Fingerabdruck-Treiber (`mtk_panel_proc_hbm(1)`).

### 7.2.5 Touch: Aufwachen nach dem Schlaf (`kernel_device_modules/0004`)

**Problem:** Der Touch-Treiber `ft3519` entschied beim Aufwachen anhand von Flags, ob er den
Gestenmodus oder den normalen Modus wiederherstellt. Wurde der Fingerabdruck-Modus im
ausgeschalteten Display gesetzt, nahm er den falschen Pfad. Die Unterbrechung blieb dann
aus, und der Touch funktionierte erst nach einem Neustart wieder.

**Lösung:** Der Treiber merkt sich beim Schlafen, in welchem Modus er war (`fts_suspended_in_gesture`).

### 7.2.6 Bluetooth: Linux-HCI (`kernel_modules/0001`, optional)

Standardmäßig spricht Bluetooth über den Android-Gerätepfad `/dev/stpbt`, und die Brücke
`tetris-bt-bridge` übersetzt nach `/dev/vhci`. Der Patch erlaubt, stattdessen direkt ein
normales HCI-Gerät anzulegen (`USE_DEVICE_NODE=0`). Die Option `TETRIS_LINUX_HCI=1` beim
Kernel-Build schaltet das ein. Der Kommentar in `build-kernel.sh` nennt: Diese Variante hat den
Start schon einmal verhindert. Deshalb ist sie standardmäßig aus.

### 7.2.7 libhybris: drei Anpassungen

| Patch | Problem | Lösung |
|---|---|---|
| `0001` EGL-Erweiterung | GTK 4 verlangt die Erweiterung `EGL_EXT_device_query`, libhybris kennt sie nicht | Erweiterung wird gemeldet, Funktionen geben nur das Nötigste zurück |
| `0002` Surfaceless | Mutter (der GNOME-Shell) fragt im Headless-Betrieb nach der Plattform `EGL_PLATFORM_SURFACELESS_MESA`. libhybris kennt sie nicht | Abbildung auf den offscreen-Modus des Android-Treibers (ohne Fensterprotokoll) |
| `0003` RGBX | Compositoren akzeptieren für EGL-Wayland-Puffer nur `RGB` oder `RGBA`. Spiele mit RGBX-Konfiguration erzeugten „Unsupported buffer format“ | `RGBX_8888` und `RGB_888` werden als `EGL_TEXTURE_RGB` gemeldet |

Die Patches sind mit den gepinnten Commits in `pkgbuilds/build-libhybris.sh` verbunden:
Änderungen am Commit erfordern neue Patches.

### 7.2.8 wlroots: das Hwcomposer-Backend (`wlroots/0002`)

Dieser Patch ist der größte (rund 1.500 Zeilen). Er fügt zu wlroots 0.20 ein **Backend** hinzu,
das statt DRM den Android-Composer nutzt, und einen **Speicher-Zuteiler**, der Puffer über
gralloc anfordert. Das Backend wird mit `-Dhwcomposer=enabled` gebaut. Es ist nur aktiv,
wenn `WLR_BACKENDS=hwcomposer` gesetzt ist, sonst bleibt der normale Pfad unverändert.

Zusätzlich enthält der Patch die Drehung des Bildschirms im Backend
(`WLR_OUTPUT_STATE_TRANSFORM`), wie in der Haupt-README erwähnt.

### 7.2.9 ModemManager-Plugin (`patches/modemmanager/tetris/`)

| Datei | Zweck |
|---|---|
| `mm-plugin-tetris.c` | Einstiegspunkt: Erkennt das Gerät (MediaTek, `tetris`) und registriert das Plugin |
| `mm-broadband-modem-tetris.c` | Das Modem: AT-Befehle, Warten auf die SMS-Speicher (`+CME ERROR: 100` wird wiederholt, bis der Speicher bereit ist) |
| `mm-broadband-modem-tetris.h` | Schnittstelle des Modems |
| `mm-broadband-bearer-tetris.c` | Die Datenverbindung: Aktivierung über `AT+EAPNACT`, IP-Adressen über `AT+EPDN`, DNS über `AT+CGCONTRDP` |
| `mm-broadband-bearer-tetris.h` | Schnittstelle der Verbindung |

Ein Hinweis aus dem Kopfkommentar der Datei: `AT+CGDATA="M-CCMNI"` darf **nicht** gesendet
werden. Es blockiert den AT-Kanal bis zum nächsten Neustart.

Das Plugin wird **nicht** mit dem Rootfs gebaut, sondern auf dem Telefon mit
`pkgbuilds/build-mm-plugin-tetris.sh`. Es braucht die Quellen der installierten ModemManager-
Version. Nach einem Update von ModemManager muss es neu gebaut werden.

---

## 7.3 Die Kernel-Konfiguration (`kernel/linux.config`)

Die Datei ist eine **Konfigurations-Fragment**: Sie wird über die Standardkonfiguration
(`gki_defconfig` und `mgk_64_k61_defconfig`) gelegt, und ihre Werte gewinnen. Die Datei enthält
rund 70 Einstellungen, gegliedert in Abschnitte:

| Abschnitt | Inhalt |
|---|---|
| `systemd / udev` | `devtmpfs` für `/dev`, `autofs`, POSIX-ACLs und `xattr` für den tmpfs, `cgroup` BPF |
| `console / VT` | Textkonsolen, weil GNOME und logind einen VT erwarten |
| `LXC container for the Android vendor side` | Namensräume (IPC, PID, Benutzer, UTS, Netz), Kontrollgruppen für Geräte und Prozesse, `veth`, Bridge, MACVLAN |
| `debugging over USB` | NCM-USB-Gadget und Telnet im Initramfs |
| `we rebuild every module ourselves` | keine Signaturprüfung, kein KMI-Trimmen, weil wir alle Module selbst bauen |
| `host has no pahole` | BTF ist nur Debug-Information und aus |
| `Bluetooth for BlueZ` | `/dev/vhci`, BNEP für das persönliche Netz (PAN) |
| `Server profile` | Docker und Podman (nach `check-config.sh` des Docker-Projekts): `nftables`, `iptables`, `IPVS`, `VXLAN`, `IPVLAN`, CFS-Bandbreitenbegrenzung (`--cpus`) |

Wichtige Einzelwerte, die Sie in der Datei finden:

| Option | Bedeutung |
|---|---|
| `CONFIG_NF_TABLES=y` | Basis der Firewall nftables (Docker braucht sie) |
| `CONFIG_CGROUP_BPF=y`, `CONFIG_VETH=y`, `CONFIG_BRIDGE=y` | Netz für Container |
| `CONFIG_CFS_BANDWIDTH=y` | `docker run --cpus` |
| `CONFIG_IP_VS=y` | Lastverteilung (IPVS) für Docker Swarm |
| `# CONFIG_MODULE_SIG_FORCE is not set` | Module müssen nicht signiert sein |
| `# CONFIG_TRIM_UNUSED_KSYMS is not set` | alle Symbole bleiben erhalten, damit die Module passen |

### Prüfen, ob die Konfiguration übernommen wurde

`build-kernel.sh config` meldet jede Zeile der Datei, die nach `olddefconfig` nicht
übernommen wurde, mit `!! not applied`. Das ist eine **Warnung**, die Sie ernst nehmen sollten,
besonders bei Docker-Optionen. Auf dem laufenden Telefon kann man die tatsächliche
Konfiguration prüfen:

```sh
scripts/phone-ssh.sh 'zcat /proc/config.gz | grep -E "CONFIG_NF_TABLES|CONFIG_CFS_BANDWIDTH"'
```

`setup-server.sh` nutzt dieselbe Prüfung, um einen falschen Kernel abzulehnen.

---

## 7.4 Die Modul-Listen

Kernel-Module werden in drei Stufen geladen (siehe [03-architektur.md](03-architektur.md#schritt-3-das-initramfs-initramfsinit)):

| Liste | Quelle | Inhalt |
|---|---|---|
| Stufe 1 | Original-System `vendor_boot`, `modules.load` | UFS-Speicher und alle Treiber, die Android früh lädt |
| Stufe 2 | Original-System `vendor_dlkm`, `modules.load` | weitere Module der Hardware |
| Stufe 3 | `initramfs/modules.load.3` (unsere Liste) | Module, die Android aus `.rc`-Dateien lädt, z. B. Touch (`tui-common.ko`, `focaltech_tp.ko`, `ft3683g.ko`, `eph861.ko`) |
| Sperrliste | `initramfs/modules.blocklist` | `apu_top.ko`, `apusys.ko`, `sapu.ko`: die KI-Einheit hängt den Start |

Die Abhängigkeiten erzeugt `depmod` für das Initramfs. Im Rootfs gilt dasselbe, aber mit
dem Hinweis aus [01-grundlagen.md](01-grundlagen.md): **nie** `depmod` über den gesamten
Modul-Satz, wenn doppelte Exporte vorhanden sind. Der Kernel wählt dann die falschen Module.
Außerhalb des Initramfs werden Module mit `insmod` geladen (zum Beispiel `tetris-torch`).

---

## 7.5 Patches erneuern

Wenn ein Patch nach einem Update der fremden Quellen nicht mehr passt, gehen Sie so vor:

1. **Den Fehler lesen.** `fetch-sources.sh` meldet, welcher Patch fehlschlägt (`git apply`).
2. **Den Patch im Quellbaum neu anwenden**, aber mit Drei-Wege-Zusammenführung:
   ```sh
   git -C src/kernel-6.1 apply --3way patches/kernel/0001-net-backport-SO_PEERPIDFD.patch
   ```
   Bei einem flachen Klon (`--depth 1`) scheitert `--3way` manchmal, weil die alten Objekte
   fehlen. Dann wenden Sie den Patch von Hand an.
   Ist ein Konflikt aufgetreten, markiert git die Stellen im Quelltext. Lösen Sie sie, dann
   `git add` und `git diff --cached > patches/…/neu.patch`.
3. **Die Änderung prüfen**, bevor Sie den Patch im Repository ersetzen. Die Änderungen im
   Patch müssen dieselbe Wirkung haben wie vorher.
4. **Den Kernel neu bauen** und die Funktion testen.
5. **Den Patch-Dateinamen beibehalten**, damit Verweise in Skripten und Dokumentation stimmen.

Ein Patch-Verzeichnis enthält **nur** eigene Änderungen. Wir kopieren keine ganzen
fremden Dateien ins Repository.

### Neuen eigenen Patch anlegen

```sh
cd src/kernel-6.1
# … Änderungen im Quelltext vornehmen …
git diff > ../../patches/kernel/0002-kurze-beschreibung.patch
```

Die Patch-Datei sollte oben eine Beschreibung haben (`Subject:` und Text), damit andere
verstehen, warum die Änderung nötig ist. Die vorhandenen Dateien zeigen das Muster.

Die Dateinamen beginnen mit einer vierstelligen Nummer (`0001`, `0002` …) und einem kurzen
Beschreibungswort. Die Reihenfolge ist wichtig, weil `git apply` sie in dieser Reihenfolge
anwendet.

---

## 7.6 Fehler mit Kernel und Modulen

| Symptom | Ursache | Lösung |
|---|---|---|
| `disagrees about version of symbol` beim Laden eines Moduls | Modul und Kernel stammen aus verschiedenen Builds | `scripts/push-modules.sh` oder `build-kernel.sh all` |
| `Invalid module format` | Modul für eine andere Architektur oder einen anderen Kernel | Modul neu bauen |
| `Unknown symbol in module` | Ein Modul fehlt, von dem es abhängt | Reihenfolge der Module prüfen (`modules.dep`) |
| Start hängt nach `apusys` | die KI-Einheit (APU) hängt den Start, sobald sie eingeschaltet wird | `apusys` steht in `modules.blocklist` und in `rootfs/overlay/etc/modprobe.d/tetris-blocklist.conf` |
| Touch reagiert nach dem Schlaf nicht | Fehler, den Patch `0004` behebt | Patch prüfen (`git -C src/kernel_device_modules-6.1 apply --check -R …`) |
| `!! not applied` beim Konfigurieren | eine Option aus `linux.config` wurde nicht übernommen | Abhängigkeit der Option prüfen (`make menuconfig`), dann `build-kernel.sh config` |

Weitere Fehler und Protokolle stehen in [09-fehlersuche.md](09-fehlersuche.md).

## 7.7 Mainline-Port: RT6010-Haptiktreiber

Der Vibrationsmotor des Telefons wird vom Chip RT6010 angesteuert. Der Treiber läuft auf dem
Vendor-Kernel nativ, hängt aber an eigenen Nothing-Hooks des Kernels. Das Verzeichnis `mainline/` enthält
einen Port, der gegen Mainline-Linux 7.x baut. Die Patches liegen in `patches/mainline/`: zuerst die
Device-Tree-Bindung, dann der Treiber. Der Treiber stellt den Motor als Force-Feedback-Gerät bereit, damit
`feedbackd` ihn direkt nutzen kann und `tetris-vibra` nicht gebraucht wird. Der Port ist **noch nicht auf
echter Hardware getestet**. Schritte und Stand stehen in [mainline/README.md](../../mainline/README.md).
