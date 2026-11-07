# 8. Profile und Betriebsarten

Das Telefon kann vier Dinge sein, je nach Einstellung:

1. **Telefon mit Nothing-Shell** (Standard), oder mit **Phosh** als Alternative;
2. **Server**: kein Bildschirm, per SSH und Docker erreichbar, mit lokalen KI-Modellen;
3. **Desktop**: wie der Server, dazu ein vollständiger GNOME-Desktop über das Netzwerk (RDP).

Dieses Kapitel erklärt, wie man zwischen diesen Betriebsarten wechselt, was jede davon
genau tut und was man beachten sollte.

## 8.1 Die Begriffe

| Begriff | Bedeutung |
|---|---|
| **Profil** | Eine Sammlung aus Diensten und Einstellungen, die zusammen eine Betriebsart ergeben: `phone`, `server` oder `desktop` |
| **Oberfläche (Shell)** | Das Programm, das den Bildschirm zeichnet: Nothing-Shell oder Phosh. Gehört zum Profil `phone` |
| **Einmal-Start** (`once`) | Ein Start in ein anderes Profil, danach automatisch zurück zum Telefon |
| **dauerhaft** | Das Profil bleibt über alle Neustarts, bis man es ändert |

Unterschied zwischen **dauerhaft** und **Einmal-Start**: Beim Einmal-Start schreibt das
System nichts Bleibendes. Die Einstellungen liegen unter `/run`, dem flüchtigen Speicher, der
beim nächsten Start leer ist. Die Datei `/var/lib/tetris-mode/once` ist der einzige Hinweis
und wird beim Start gelöscht.

## 8.2 Die Umschalter

| Mittel | Wirkung | Dauerhaft? | Wo ausführen |
|---|---|---|---|
| `sudo tetris-mode phone` | Telefon mit Oberfläche | ja | auf dem Telefon |
| `sudo tetris-mode server` | Server | ja | auf dem Telefon |
| `sudo tetris-mode desktop` | Desktop über RDP | ja | auf dem Telefon |
| `tetris-mode` (ohne Argument) | zeigt das aktuelle Profil | – | auf dem Telefon |
| App **Startmodus** | Einmal-Start als Server oder Desktop; außerdem Wahl der Oberfläche | nein (Server/Desktop); ja (Oberfläche) | auf dem Telefon, in der Oberfläche |
| `sudo systemctl start tetris-shell@nothing` (bzw. `@phosh`) | setzt die Oberfläche für den nächsten Start | ja | auf dem Telefon |
| `sudo rm /etc/tetris-shell` | Phosh als Oberfläche | ja | auf dem Telefon |

Nach jeder dauerhaften Änderung ist ein Neustart nötig:

```sh
sudo tetris-mode server && sudo reboot
```

Der Schalter `--with-modem` behält bei den Profilen `server` und `desktop` die
Mobilfunk-Verbindung. Dafür muss der Halium-Container laufen, der das Modem bedient:

```sh
sudo tetris-mode server --with-modem && sudo reboot
```

## 8.3 Was ein Profil einschaltet

`tetris-mode` schaltet nur **Dienste** um. Die Dateien bleiben gleich. Das ist der Grund,
warum man jederzeit zurückwechseln kann. Die Zuordnung steht im Skript
`rootfs/overlay-server/usr/local/bin/tetris-mode`:

| Dienst | `phone` | `server` | `desktop` |
|---|---|---|---|
| `tetris-phosh` (Oberfläche) | an | aus | aus |
| `tetris-sensors`, `tetris-vibra`, `tetris-torch`, `tetris-gnss`, `tetris-fp` | an | aus | aus |
| `bluetooth`, `tetris-bt-bridge` | an | aus | aus |
| `ModemManager`, `tetris-modem` | an | aus (an mit `--with-modem`) | aus (an mit `--with-modem`) |
| `tetris-display-off` | aus | an | an |
| `docker`, `avahi-daemon` (`tetris.local`), `systemd-oomd`, `tetris-ai` | aus | an | an |
| `gdm`, `gnome-headless-session@alarm` | aus | aus | an |
| `tetris-android` (Container) | an (über Phosh) | aus, wenn nichts ihn braucht | an (für die GPU) |
| Standard-Ziel (`default.target`) | `graphical.target` | `multi-user.target` | `graphical.target` (GDM) |
| WLAN-Stromsparmodus | an | aus (sonst verzögert der Stromsparmodus eingehende Verbindungen) | aus |
| Seat-Markierung (`/etc/tetris-headless-seat`) | nein | nein | ja: das Panel gehört nicht zu seat0 |

Das Skript liegt in `rootfs/overlay-server/usr/local/bin/tetris-mode`. Sein Kopfkommentar
nennt alle Befehle.

## 8.4 Die Oberfläche: Nothing-Shell oder Phosh

Das Profil `phone` nutzt eine von zwei Oberflächen. Die Wahl liegt in der Datei
`/etc/tetris-shell`:

| Inhalt der Datei | Oberfläche |
|---|---|
| `nothing` | Nothing-Shell (Standard nach `setup-nothing.sh`) |
| (Datei fehlt) | Phosh |

Beide laufen über denselben Dienst (`tetris-phosh.service`). Das Skript
`tetris-phosh-session` liest `/etc/tetris-shell` und startet dann die Nothing-Sitzung
(`tetris-nothing-session`) oder Phosh. Beide nutzen dieselbe GPU- oder Software-Entscheidung
(`tetris-phosh-select`).

### Bedienung der Nothing-Shell

| Geste oder Taste | Wirkung |
|---|---|
| Vom oberen Rand nach unten wischen | Kontrollzentrum (WLAN, Bluetooth, Rotation, Lautstärke, Helligkeit) |
| Von der Home-Leiste nach oben wischen | kurz: Startbildschirm, lang: offene Apps (Karte seitlich wischen zum Schließen) |
| Einschalttaste kurz | Bildschirm aus und sperren. Bei ausgeschaltetem Bildschirm wieder an |
| Einschalttaste lang | Ausschaltmenü (auch im Sperrbildschirm) |
| Lautstärketasten | Lautstärke mit Anzeige, auch gesperrt und bei ausgeschaltetem Bildschirm |
| beide Lautstärketasten gleichzeitig | Screenshot (`tetris-screenshot`, in `~/Pictures/Screenshots`) |
| Telefon drehen | Bildschirm dreht mit (Sensor `tetris-sensors`), gesperrt bleibt er aufrecht |

Die Bildschirmtastatur ist **squeekboard**. Sie erscheint in Textfeldern. Die Taste rechts
in der Home-Leiste schließt sie bis zum nächsten Antippen.

Die Einstellungen-App deckt WLAN, LAN, Bluetooth (einschließlich Kopplung) und Mobilfunk
ab (SIM-PIN, Datenverbindung, Zugangspunkt, Roaming). Den Startbildschirmhintergrund wählen
Sie unter **Einstellungen → Darstellung**.

### Startmodus (App)

Die App **Startmodus** (`tetris-modes`) steht in beiden Oberflächen zur Verfügung. Sie hat zwei
Aufgaben:

1. **Einmal-Start** als Server oder Desktop. Die App zeigt, unter welcher Adresse das Telefon
   danach erreichbar ist (SSH, RDP mit Passwort). Dann startet sie das Telefon neu.
2. **Oberfläche wählen:** Nothing-Shell oder Phosh (`tetris-shell@`), gilt ab dem nächsten Start.

Im Hintergrund schreibt die App `server` oder `desktop` in `/var/lib/tetris-mode/once` und
startet neu. Dann passiert Folgendes:

```
Startmodus ──► tetris-boot-as ──► tetris-boot-once (als root) ──► schreibt "once"
                                                                          │
                                                             Neustart    ▼
   Generator tetris-mode-once (vor systemd, liest "once", baut das Profil unter /run)
                                                                          │
                                                                          ▼
   tetris-mode-once-done.service löscht "once" ──► beim nächsten Start: wieder Telefon
```

Der **Generator** (`/etc/systemd/system-generators/tetris-mode-once`) ist ein kleines Programm,
das systemd beim Start ausführt. Es erzeugt aus der Datei `once` die Symbolverknüpfungen für
dieses eine Profil. Weil alles unter `/run` liegt, verschwindet es beim nächsten Neustart von
selbst.

**Kurztaste beim Einmal-Start:** Im Server- und Desktop-Einmalprofil belegt ein kurzer
Druck auf die Einschalttaste den Neustart. Damit kommt man direkt zurück zum Telefon.

## 8.5 Server-Profil

Das Telefon ist ein kleiner Server, der immer eingeschaltet bleibt. Der Bildschirm bleibt aus.

### Was es enthält

| Bereich | Umsetzung |
|---|---|
| **Kernel** | Docker-Optionen in `kernel/linux.config` (nftables, Netz-Namensräume, CFS-Bandbreitenbegrenzung) |
| **Bildschirm** | `tetris-display-off` schaltet das Panel per DPMS aus und hält den DRM-Master, damit kein anderer Prozess das Display übernimmt |
| **USB-Netz** | `172.16.42.1`, wie im Phone-Profil |
| **WLAN** | NetworkManager, Stromsparmodus **aus** (sonst verzögert er eingehende Verbindungen um ein Beacon-Intervall) |
| **Name im Netz** | `tetris.local` über Avahi (mDNS). Braucht `nss-mdns` auf dem PC |
| **SSH** | Nur Schlüssel über WLAN. Passwort nur über das USB-Kabel (`50-tetris-server.conf`) |
| **Docker** | Speicher `overlay2` auf dem ext4-Rootfs, Logs auf 3 × 10 MB je Container begrenzt, `live-restore`. Benutzer `alarm` ist in der Gruppe `docker` |
| **KI** | Ollama als Container (`/opt/tetris-server/ai/compose.yaml`, Dienst `tetris-ai`), API auf Port 11434, nur CPU |
| **Speicher** | zram-Swap (halber RAM, zstd), `systemd-oomd`. Der Halium-Container läuft nicht, außer mit `--with-modem` |
| **Aus** | Oberfläche, Sensoren, Vibration, Taschenlampe, GPS, Fingerabdruck, Bluetooth, Modem (mit `--with-modem` bleibt das Modem an) |

### Einrichten

Einrichtung aus dem Repository, mit einem laufenden Telefon (siehe
[04-bauen-und-flashen.md](04-bauen-und-flashen.md#49-phase-6-server--und-desktop-profil)):

```sh
scripts/setup-server.sh            # oder: scripts/setup-server.sh --with-modem
```

Oder ein neues Telefon-Abbild mit dem Profil bauen:

```sh
sudo PROFILE=server scripts/build-rootfs.sh
```

### Benutzen

```sh
ssh alarm@tetris.local                                # über WLAN
ssh alarm@172.16.42.1                                 # über USB
docker context create tetris --docker host=ssh://alarm@tetris.local
docker --context tetris run --rm hello-world
docker --context tetris exec -it ollama ollama pull qwen3:1.7b
curl http://tetris.local:11434/api/generate -d '{"model":"qwen3:1.7b","prompt":"Hallo"}'
```

- Images müssen für **`linux/arm64`** gebaut sein. Für Images nur für x86 bauen Sie selbst
  mit `docker buildx build --platform linux/arm64`.
- Der erste Start lädt das Ollama-Image (rund 3 GB) und braucht dafür Zeit. Verfolgen mit
  `journalctl -u tetris-ai`.
- **Modelle:** `tetris-ollama-pull <modell>` setzt die Thread-Zahl auf 4. Der Grund steht im
  Skript: Die vier langsamen Kerne (Cortex-A55) bremsen die vier schnellen (A78). Mit vier
  Threads ist die Erzeugung rund 20 % schneller als mit dem Standard von acht. Als Richtwert
  nennt das Projekt `qwen3:1.7b` mit rund 12 Token pro Sekunde.
- Für 1 bis 4 Milliarden Parameter im Q4-Format sind Modelle praktikabel. Der Arbeitsspeicher
  setzt die Obergrenze (Angabe aus der Haupt-README).
- **GPU (experimentell):** `tetris-llama` nutzt Vulkan über libhybris auf der Mali-GPU. Die GPU
  hat zwei Kerne mit 1 GHz. Erwarten Sie keine Wunder. Die KI-Einheit (APU) funktioniert
  nur mit Modellen, die für NeuroPilot unter Android kompiliert sind. Ihre Kerntreiber
  hängen bei uns den Start, deshalb ist sie gesperrt.
- Das Telefon hängt meist am Ladegerät. Halten Sie es kühl und aus der direkten Sonne.

Zurück zum Telefon: `sudo tetris-mode phone && sudo reboot`.

## 8.6 Desktop-Profil

Das Telefon ist ein Server mit einem **GNOME-Desktop**, den Sie über RDP von einem PC, einem
Tablet oder einem anderen Telefon bedienen. Der Bildschirm bleibt aus.

### Was es enthält

| Bereich | Umsetzung |
|---|---|
| **Sitzung** | `gnome-headless-session@alarm`: GNOME ohne Display. GDM startet sie beim Systemstart |
| **Seat** | `72-tetris-headless-seat.rules` nimmt das Panel aus seat0, damit GDM keinen Anmeldebildschirm darauf zeigt |
| **RDP** | `gnome-remote-desktop` im Headless-Modus, Port **3389**. Zertifikat und zufälliges Passwort entstehen beim ersten Start (`tetris-rdp-setup`) und stehen in `~/.config/tetris-rdp-password` |
| **GPU** | GNOME Shell nutzt die Mali-GPU über eine zweite libhybris-Variante als **glvnd-EGL-Anbieter** (`/opt/libhybris-glvnd`). Xwayland nutzt Mesa |
| **Ausnahmen** | Programme aus `/etc/tetris-software-gl.list` laufen auf Mesa (Firefox). Mit `tetris-software-gl <programm>` ein beliebiges Programm auf Mesa starten |
| **Kernel** | Patch `SO_PEERPIDFD`. Ohne ihn scheitert die Fernanmeldung (siehe [07](07-kernel-treiber-patches.md#721-kernel-so_peerpidfd-kernel0001)) |

Verbinden:

```sh
scripts/desktop-rdp.sh                       # USB, 1920×1080, H.264
scripts/desktop-rdp.sh tetris.local /f       # WLAN, Vollbild
```

Mit anderen Clients (Remmina, Windows-Remotedesktop, Microsoft-RDP-Apps) stellen Sie
1920 × 1080 oder Vollbild ein. Der Desktop läuft weiter, wenn Sie die Verbindung trennen.

**Neues RDP-Passwort:** `tetris-rdp-setup` auf dem Telefon.

### Einrichten

```sh
scripts/setup-desktop.sh [--with-modem]
```

Das Skript prüft zuerst, ob der Kernel `SO_PEERPIDFD` hat. Dann richtet es das Server-Profil
ein, installiert GNOME und baut libhybris mit der glvnd-Variante (wenn der Halium-Container
installiert ist). Am Ende gibt es das RDP-Passwort aus.

Ohne Halium-Container rendert GNOME auf dem Prozessor (Mesa llvmpipe). Das funktioniert, ist
aber langsamer.

### Speicher

Im Leerlauf (GNOME, Docker und Ollama, ohne Last) sind rund 850 MB belegt (Angabe aus der
Haupt-README).

## 8.7 Probleme mit Profilen

| Problem | Ursache | Lösung |
|---|---|---|
| Nach `tetris-mode server` kommt das Telefon nicht ins Netz | WLAN-Stromsparmodus oder Netzkonfiguration | Über USB verbinden (`172.16.42.1`), `nmcli` prüfen |
| `tetris-mode: gdm and gnome-remote-desktop are not installed` | Desktop-Pakete fehlen | `scripts/setup-desktop.sh` ausführen |
| Desktop zeigt GDM-Anmeldebildschirm am Panel | Der Seat-Schalter fehlt | `sudo tetris-mode desktop` erneut ausführen, Neustart |
| GDM versucht dreimal beim Start einen lokalen Anmeldebildschirm | bekannt, siehe Haupt-README | `tetris-display-off` hält das Panel, daher ist nichts zu sehen |
| Einmal-Start startet nicht im Server-Profil | Datei `once` fehlt oder hat einen anderen Inhalt | `cat /var/lib/tetris-mode/once` prüfen, nur `server` oder `desktop` ist gültig |
| Telefon bleibt nach dem Einmal-Start im Server-Profil | `tetris-mode-once-done` lief nicht | `sudo rm -f /var/lib/tetris-mode/once && sudo reboot` |

Weitere Fehlerquellen: [09-fehlersuche.md](09-fehlersuche.md).
