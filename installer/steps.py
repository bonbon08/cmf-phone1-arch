"""Ablaufplan des Installers: welche Schritte in welcher Reihenfolge laufen.

Dieses Modul kennt kein Fenster und führt nichts aus. Es baut nur die Liste der Schritte
aus der Auswahl im Installer. So lässt sich die Logik mit `test_steps.py` prüfen, ohne
ein Telefon oder eine Oberfläche zu brauchen.

Die Schritte rufen die bestehenden Skripte in `scripts/` auf. Der Installer ersetzt sie
nicht, er reiht sie nur in der richtigen Reihenfolge auf.
"""

import os
import shutil
from dataclasses import dataclass
from typing import List, Optional

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Original-Images von Nothing OS B4.1 (siehe docs/04-bauen-und-flashen.md)
STOCK_IMAGES = "firmware/Tetris_B4.1-260928-1140/images"
# Arch Linux ARM und busybox, beide aus downloads/alarm
ARCH_TARBALL = "downloads/alarm/ArchLinuxARM-aarch64-latest.tar.gz"
BUSYBOX = "downloads/alarm/usr/bin/busybox"
CLANG = "toolchain/clang-r487747c/bin/clang"
VENDOR_BOOT = STOCK_IMAGES + "/vendor_boot.img"

PHONE_IP = "172.16.42.1"

# Betriebsarten, so wie sie der Installer anbietet
MODE_BUILD = "build"  # nur bauen, nichts auf das Telefon schreiben
MODE_FULL = "full"  # bauen und auf das Telefon flashen (Neuinstallation)
MODE_STAGE2 = "stage2"  # zweite Stufe am laufenden Telefon (Halium, Nothing-Shell)
MODE_SERVER = "server"  # Server-Profil am laufenden Telefon
MODE_DESKTOP = "desktop"  # Desktop-Profil am laufenden Telefon

MODES = {
    MODE_BUILD: "Nur bauen (auf dem PC, Telefon bleibt unberührt)",
    MODE_FULL: "Bauen und neu installieren (löscht die Daten des Telefons)",
    MODE_STAGE2: "Zweite Stufe am laufenden Telefon (GPU, Nothing-Shell)",
    MODE_SERVER: "Server-Profil auf dem laufenden Telefon",
    MODE_DESKTOP: "Desktop-Profil auf dem laufenden Telefon",
}

PROFILES = {
    "phone": "Telefon mit Nothing-Shell oder Phosh",
    "server": "Server (Docker, Ollama, kein Bildschirm)",
    "desktop": "Desktop (GNOME über RDP)",
}


class PlanError(ValueError):
    """Die Auswahl ergibt keinen sinnvollen Ablauf (z. B. fehlendes Halium-Abbild)."""


@dataclass
class Step:
    """Ein Schritt des Ablaufs.

    argv:        Befehl, relativ zum Repository-Wurzelverzeichnis
    root:        Schritt braucht sudo (der Installer reicht das Passwort über stdin weiter)
    destructive: Schritt löscht oder überschreibt Daten auf dem Telefon
    wait:        "phone" = kein Befehl, sondern warten, bis das Telefon per USB erreichbar ist
    optional:    Fehlschlag bricht den Ablauf nicht ab
    """

    id: str
    title: str
    argv: Optional[List[str]] = None
    root: bool = False
    destructive: bool = False
    wait: Optional[str] = None
    optional: bool = False
    note: str = ""


@dataclass
class Check:
    """Eine Voraussetzung für den Ablauf, angezeigt auf der Prüfseite."""

    name: str
    ok: bool
    detail: str
    required: bool = True


def _path(rel: str) -> str:
    return os.path.join(REPO, rel)


def build_steps(profile: str) -> List[Step]:
    """Die Schritte, die ein Abbild erzeugen (ohne etwas auf das Telefon zu schreiben)."""
    return [
        Step("fetch", "Kernel-Quellen holen und Patches anwenden",
             ["scripts/fetch-sources.sh"],
             note="Lädt mehrere Gigabyte aus dem Internet"),
        Step("kernel", "Kernel und Module bauen",
             ["scripts/build-kernel.sh", "all"]),
        Step("boot", "Boot-Images und Initramfs bauen",
             ["scripts/build-debug-boot.sh"]),
        Step("rootfs", f"Arch-Linux-Abbild bauen (Profil: {profile})",
             ["env", f"PROFILE={profile}", "scripts/build-rootfs.sh"], root=True,
             note="Läuft in einer ARM-chroot über QEMU und dauert am längsten"),
    ]


def build_plan(mode: str, profile: str = "phone", halium_img: str = "",
               flash_stock: bool = False) -> List[Step]:
    """Baut den Ablaufplan für eine Betriebsart.

    Raises:
        PlanError: wenn die Auswahl nicht zusammenpasst.
    """
    if mode not in MODES:
        raise PlanError(f"unbekannte Betriebsart: {mode}")
    if profile not in PROFILES:
        raise PlanError(f"unbekanntes Profil: {profile}")
    if mode in (MODE_FULL, MODE_STAGE2) and profile == "phone" and not halium_img:
        raise PlanError("Für das Telefon-Profil brauchen Sie das Halium-Abbild (system.img).")
    if mode in (MODE_FULL, MODE_STAGE2) and halium_img and not os.path.isfile(halium_img):
        raise PlanError(f"Halium-Abbild nicht gefunden: {halium_img}")

    plan: List[Step] = []
    if mode in (MODE_BUILD, MODE_FULL):
        plan += build_steps(profile)

    if mode == MODE_FULL:
        if flash_stock:
            plan.append(Step("flash-stock", "Nothing OS B4.1 auf beide Slots schreiben",
                             ["scripts/flash-stock.sh", STOCK_IMAGES], destructive=True,
                             note="Setzt das Telefon in den Ausgangszustand, löscht die Daten"))
        plan.append(Step("userdata", "Arch-Linux-Abbild auf userdata schreiben",
                         ["fastboot", "flash", "userdata", "out/rootfs.simg"],
                         destructive=True, note="Löscht alle Daten auf dem Telefon"))
        plan.append(Step("boot-b", "Kernel und Initramfs auf Slot b schreiben",
                         ["scripts/flash-debug-b.sh"], destructive=True,
                         note="Slot a (Nothing OS) bleibt unverändert"))
        if profile == "server":
            # Das Server-Abbild startet ohne weitere Einrichtung (docs/04, Abschnitt 4.9)
            return plan

    if mode in (MODE_FULL, MODE_STAGE2, MODE_SERVER, MODE_DESKTOP):
        plan.append(Step("wait", "Warten, bis das Telefon über USB erreichbar ist",
                         wait="phone", note="Das kann bis zu fünf Minuten dauern"))
        plan.append(Step("host-nat", "Internet des PC für das Telefon freigeben",
                         ["scripts/host-nat.sh", "up"], root=True, optional=True,
                         note="Das Telefon braucht Internet für Pakete und Quellen"))

    # Zweite Stufe (GPU, Halium): Pflicht beim Telefon-Profil, beim Desktop optional
    if mode in (MODE_FULL, MODE_STAGE2) and halium_img and profile in ("phone", "desktop"):
        plan.append(Step("stage2", "Zweite Stufe: Halium, libhybris, wlroots, Nothing-Shell",
                         ["scripts/setup-phone.sh", halium_img],
                         note="Baut auf dem Telefon und startet es am Ende neu"))
    if mode == MODE_SERVER:
        plan.append(Step("server", "Server-Profil einrichten",
                         ["scripts/setup-server.sh"], note="Startet das Telefon neu"))
    if mode == MODE_DESKTOP:
        plan.append(Step("desktop", "Desktop-Profil einrichten",
                         ["scripts/setup-desktop.sh"], note="Startet das Telefon neu"))
    return plan


def has_destructive(plan: List[Step]) -> bool:
    return any(step.destructive for step in plan)


def needs_sudo(plan: List[Step]) -> bool:
    return any(step.root for step in plan)


FASTBOOT_STEPS = ("flash-stock", "userdata", "boot-b")


def needs_fastboot(plan: List[Step]) -> bool:
    return any(step.id in FASTBOOT_STEPS for step in plan)


def _tool(name: str) -> Check:
    path = shutil.which(name)
    return Check(f"Programm: {name}", path is not None, path or "fehlt (Paket installieren)")


def _file(label: str, rel: str, hint: str, required: bool = True) -> Check:
    exists = os.path.exists(_path(rel))
    return Check(label, exists, rel if exists else hint, required)


def prerequisites(plan: List[Step]) -> List[Check]:
    """Voraussetzungen für den Ablauf, ohne Netzwerk und ohne Telefon.

    Telefon-Checks (Fastboot, USB-Netz) macht der Installer auf der Prüfseite selbst,
    weil sie sich während des Ablaufs ändern.
    """
    checks: List[Check] = []
    tools = {"git", "make", "bsdtar", "depmod", "ssh", "scp", "sudo"}
    if any(s.id in ("rootfs", "boot") for s in plan):
        tools |= {"mkbootimg", "unpack_bootimg", "lz4", "cpio", "fsck.erofs", "e2fsck",
                  "resize2fs", "mkfs.ext4", "img2simg", "arch-chroot", "pkg-config"}
    if needs_fastboot(plan):
        tools.add("fastboot")
    if any(s.id in ("stage2", "server", "desktop", "host-nat") for s in plan):
        tools.add("ssh")
    for name in sorted(tools):
        checks.append(_tool(name))

    ids = {s.id for s in plan}
    if "rootfs" in ids:
        checks.append(_file("Arch-Linux-ARM-Basissystem", ARCH_TARBALL,
                            "Datei fehlt: siehe docs/04-bauen-und-flashen.md, Abschnitt 4.3"))
        checks.append(_file("busybox für das Initramfs", BUSYBOX,
                            "Datei fehlt: busybox-Paket entpacken, siehe Abschnitt 4.3"))
    if "boot" in ids or "rootfs" in ids:
        checks.append(_file("Android-Clang (toolchain/)", CLANG,
                            "fehlt: AOSP-Prebuilt clang-r487747c, siehe Abschnitt 4.3"))
    if "boot" in ids or "flash-stock" in ids:
        checks.append(_file("Original-Firmware B4.1", VENDOR_BOOT,
                            "fehlt: Nothing-OS-B4.1-Images in firmware/, siehe Abschnitt 4.3"))
    if "fetch" in ids:
        checks.append(Check("Internet für Kernel-Quellen", True,
                            "wird beim Start geprüft", required=False))
    return checks


def halium_check(path: str) -> Check:
    """Prüft das Halium-Abbild, wenn der Ablauf es braucht."""
    if not path:
        return Check("Halium-Abbild (system.img)", False, "nicht ausgewählt")
    ok = os.path.isfile(path)
    return Check("Halium-Abbild (system.img)", ok,
                 path if ok else "Datei nicht gefunden")


def describe(plan: List[Step]) -> List[str]:
    """Der Ablauf als Textzeilen, z. B. für `--plan` und das Protokoll."""
    lines = []
    for n, step in enumerate(plan, 1):
        flags = []
        if step.root:
            flags.append("sudo")
        if step.destructive:
            flags.append("LÖSCHT DATEN")
        if step.optional:
            flags.append("optional")
        cmd = " ".join(step.argv) if step.argv else f"warten auf Telefon ({step.wait})"
        suffix = f"  [{', '.join(flags)}]" if flags else ""
        lines.append(f"{n:2d}. {step.title}\n      $ {cmd}{suffix}")
    return lines
