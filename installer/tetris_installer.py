#!/usr/bin/env python3
"""Grafischer Installer für Arch Linux auf dem CMF Phone 1 (Tetris).

Er führt die Skripte aus `scripts/` in der richtigen Reihenfolge aus und zeigt dabei ein
Fenster mit dem Fortschritt und dem Protokoll. Er ersetzt die Skripte nicht. Wer alles
per Hand machen will, findet die Befehle in docs/04-bauen-und-flashen.md.

Start (aus dem Repository-Wurzelverzeichnis):

    python3 installer/tetris_installer.py

Weitere Optionen:

    python3 installer/tetris_installer.py --dry-run         Fenster, aber nichts ausführen
    python3 installer/tetris_installer.py --print-plan --mode full --profile phone \\
        --halium /pfad/system.img                           Ablauf als Text, ohne Fenster

Die Seiten: Willkommen, Auswahl, Prüfung, Ablauf, Fertig. Destruktive Schritte fragen vor dem
Start nach einer Bestätigung. Das sudo-Passwort wird nur im Speicher gehalten, an die
Schritte mit `sudo` über stdin weitergereicht und nie in das Protokoll geschrieben.
"""

import argparse
import datetime
import os
import queue
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time

try:
    import tkinter as tk
    import tkinter.font as tkfont
    from tkinter import filedialog, messagebox, simpledialog, ttk
except ImportError:  # pragma: no cover - nur auf Systemen ohne Tk
    sys.exit("tkinter fehlt. Auf Arch Linux: sudo pacman -S tk")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import steps  # noqa: E402  (braucht den sys.path-Eintrag oben)

LOG_DIR = os.path.join(steps.REPO, "out", "installer-logs")
PHONE_TIMEOUT = 300  # Sekunden, bis das Telefon nach dem Neustart per USB erreichbar sein muss
CONFIRM_WORD = "LOESCHEN"

PAGES = ("Willkommen", "Auswahl", "Prüfung", "Ablauf", "Fertig")

STATUS_TEXT = {
    "wait": "wartet",
    "run": "läuft",
    "ok": "fertig",
    "warn": "übersprungen",
    "fail": "Fehler",
    "cancel": "abgebrochen",
}


# --- Telefon und Netz: kurze Prüfungen, die nie länger als wenige Sekunden blockieren ------

def port_open(host, port, timeout=1.5):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def fastboot_serial():
    """Seriennummer des Geräts im Fastboot-Modus oder None."""
    if not shutil.which("fastboot"):
        return None
    try:
        out = subprocess.run(["fastboot", "devices"], capture_output=True, text=True,
                             timeout=5).stdout
    except (OSError, subprocess.TimeoutExpired):
        return None
    for line in out.splitlines():
        if line.strip():
            return line.split()[0]
    return None


def phone_checks():
    """Telefon- und Netzwerk-Prüfungen. Nicht zwingend, weil sich der Zustand ändert."""
    serial = fastboot_serial()
    ssh = port_open(steps.PHONE_IP, 22)
    telnet = port_open(steps.PHONE_IP, 23)
    internet = port_open("github.com", 443, timeout=3)
    return [
        steps.Check("Telefon im Fastboot-Modus", serial is not None,
                    f"Seriennummer {serial}" if serial else
                    "nicht gefunden (nur fürs Flashen nötig)", required=False),
        steps.Check(f"Telefon über USB (SSH {steps.PHONE_IP})", ssh,
                    "erreichbar" if ssh else "nicht erreichbar (für Zweite Stufe, Profile)",
                    required=False),
        steps.Check(f"Notfall-Shell im Initramfs (Telnet {steps.PHONE_IP}:23)", telnet,
                    "erreichbar" if telnet else "nicht erreichbar", required=False),
        steps.Check("Internet", internet,
                    "erreichbar" if internet else "nicht erreichbar (Quellen und Pakete brauchen es)",
                    required=False),
    ]


def wait_for_phone(log, cancelled):
    """Wartet, bis das Telefon per SSH über USB antwortet. True, wenn es klappt."""
    deadline = time.monotonic() + PHONE_TIMEOUT
    last_note = 0.0
    while time.monotonic() < deadline:
        if cancelled.is_set():
            return False
        if port_open(steps.PHONE_IP, 22, timeout=2):
            log("Telefon ist erreichbar.")
            return True
        if time.monotonic() - last_note > 20:
            log("warte auf das Telefon (USB-Kabel, Neustart, Arch-Start) ...")
            last_note = time.monotonic()
        time.sleep(2)
    log("Telefon nach fünf Minuten nicht erreichbar.")
    return False


def sudo_password_ok(password):
    """Prüft das sudo-Passwort mit `sudo -v`. Das Passwort landet nicht im Protokoll."""
    try:
        result = subprocess.run(["sudo", "-S", "-p", "", "-v"], input=password + "\n",
                                capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.TimeoutExpired):
        return False
    return result.returncode == 0


# --- Ausführung ----------------------------------------------------------------------------

class Runner(threading.Thread):
    """Führt den Ablaufplan in einem Hintergrund-Thread aus und meldet über eine Queue."""

    def __init__(self, plan, sudo_pw, dry_run, messages, log_path):
        super().__init__(daemon=True)
        self.plan = plan
        self.sudo_pw = sudo_pw
        self.dry_run = dry_run
        self.messages = messages
        self.cancelled = threading.Event()
        self.proc = None
        self.log_path = log_path

    def log(self, text):
        self.messages.put(("log", text))
        if getattr(self, "_logfile", None):
            self._logfile.write(text + "\n")
            self._logfile.flush()

    def run(self):
        os.makedirs(LOG_DIR, exist_ok=True)
        with open(self.log_path, "a", encoding="utf-8") as self._logfile:
            self._logfile.write(f"== Start {datetime.datetime.now():%Y-%m-%d %H:%M:%S}\n")
            outcome = "done"
            for i, step in enumerate(self.plan):
                if self.cancelled.is_set():
                    outcome = "cancel"
                    break
                self.messages.put(("step", i, "run"))
                self.log(f"\n== {step.title}")
                ok = self.run_step(step)
                if self.cancelled.is_set():
                    self.messages.put(("step", i, "cancel"))
                    outcome = "cancel"
                    break
                if ok:
                    self.messages.put(("step", i, "ok"))
                elif step.optional:
                    self.messages.put(("step", i, "warn"))
                    self.log("(optional, der Ablauf geht weiter)")
                else:
                    self.messages.put(("step", i, "fail"))
                    outcome = "fail"
                    break
            self._logfile.write(f"== Ende: {outcome}\n")
        self.sudo_pw = None  # Passwort nach dem Lauf nicht länger im Speicher halten
        self.messages.put(("end", outcome))

    def run_step(self, step):
        if step.wait == "phone":
            if self.dry_run:
                self.log("[Trockenlauf] würde auf das Telefon warten")
                return True
            return wait_for_phone(self.log, self.cancelled)

        argv = list(step.argv)
        if step.root:
            argv = ["sudo", "-S", "-p", ""] + argv
        self.log("$ " + " ".join(step.argv))
        if self.dry_run:
            self.log("[Trockenlauf] nichts ausgeführt")
            return True

        try:
            self.proc = subprocess.Popen(
                argv, cwd=steps.REPO, text=True, bufsize=1,
                stdin=subprocess.PIPE if step.root else subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                start_new_session=True)  # eigene Prozessgruppe, damit Abbrechen alles erwischt
        except OSError as err:
            self.log(f"konnte nicht starten: {err}")
            return False

        if step.root:
            # Passwort über stdin, nie als Argument (sichtbar in `ps`) und nie im Protokoll
            try:
                self.proc.stdin.write((self.sudo_pw or "") + "\n")
                self.proc.stdin.close()
            except BrokenPipeError:
                pass

        for line in self.proc.stdout:
            self.log(line.rstrip())
        code = self.proc.wait()
        self.log(f"(Rückgabewert {code})")
        return code == 0

    def cancel(self):
        self.cancelled.set()
        if self.proc and self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass


# --- Oberfläche ----------------------------------------------------------------------------

class App(tk.Tk):

    def __init__(self, dry_run=False):
        super().__init__()
        self.dry_run = dry_run
        self.title("CMF Phone 1 – Installer")
        self.geometry("880x740")
        self.minsize(760, 640)

        self.mode = tk.StringVar(value=steps.MODE_BUILD)
        self.profile = tk.StringVar(value="phone")
        self.halium = tk.StringVar(value=self.default_halium())
        self.flash_stock = tk.BooleanVar(value=False)
        self.ack_backup = tk.BooleanVar(value=False)
        self.ack_bootloader = tk.BooleanVar(value=False)
        self.ack_data = tk.BooleanVar(value=False)

        self.plan = []
        self.sudo_pw = None
        self.runner = None
        self.messages = queue.Queue()
        self.log_path = None
        self.checks = []
        self.index = 0

        style = ttk.Style(self)
        if "clam" in style.theme_names():
            style.theme_use("clam")
        base = tkfont.nametofont("TkDefaultFont")
        title_font = base.copy()
        title_font.configure(size=base.cget("size") + 8, weight="bold")
        style.configure("Title.TLabel", font=title_font)
        style.configure("Warn.TLabel", foreground="#b00020")
        style.configure("Hint.TLabel", foreground="#555555")

        header = ttk.Frame(self, padding=(20, 16, 20, 4))
        header.pack(fill="x")
        self.title_label = ttk.Label(header, style="Title.TLabel")
        self.title_label.pack(side="left")
        self.step_label = ttk.Label(header, style="Hint.TLabel")
        self.step_label.pack(side="right")

        self.body = ttk.Frame(self, padding=(20, 8))
        self.body.pack(fill="both", expand=True)

        footer = ttk.Frame(self, padding=(20, 8, 20, 16))
        footer.pack(fill="x")
        self.back_btn = ttk.Button(footer, text="Zurück", command=self.go_back)
        self.back_btn.pack(side="left")
        self.next_btn = ttk.Button(footer, text="Weiter", command=self.go_next)
        self.next_btn.pack(side="right")

        self.pages = [self.page_welcome, self.page_choose, self.page_check, self.page_run,
                      self.page_done]
        self.show(0)
        self.protocol("WM_DELETE_WINDOW", self.on_close)

    # Hilfsfunktionen

    @staticmethod
    def default_halium():
        for rel in ("downloads/halium/system.img",):
            path = os.path.join(steps.REPO, rel)
            if os.path.isfile(path):
                return path
        return ""

    def clear_body(self):
        for child in self.body.winfo_children():
            child.destroy()

    def show(self, index):
        self.index = index
        self.title_label.configure(text=PAGES[index])
        self.step_label.configure(text=f"Schritt {index + 1} von {len(PAGES)}")
        self.clear_body()
        self.back_btn.configure(state="normal" if index in (1, 2) else "disabled")
        self.next_btn.configure(text="Schließen" if index == 4 else "Weiter")
        self.pages[index]()

    def go_back(self):
        if self.index in (1, 2):
            self.show(self.index - 1)

    def go_next(self):
        if self.index == 4:
            self.destroy()
        elif self.index < len(self.pages) - 1:
            self.show(self.index + 1)

    def on_close(self):
        if self.runner and self.runner.is_alive():
            if not messagebox.askyesno("Installer beenden",
                                       "Der Ablauf läuft noch. Abbrechen und beenden?"):
                return
            self.runner.cancel()
        self.destroy()

    def build_plan_or_error(self):
        """Ablaufplan aus der Auswahl, oder Fehlertext."""
        try:
            return steps.build_plan(self.mode.get(), self.profile.get(),
                                    self.halium.get().strip(), self.flash_stock.get()), None
        except steps.PlanError as err:
            return [], str(err)

    # Seite 0: Willkommen

    def page_welcome(self):
        text = (
            "Dieser Installer baut Arch Linux für das CMF Phone 1 und kann es auf das Telefon\n"
            "schreiben. Er führt die Skripte aus dem Ordner scripts/ aus und zeigt Fortschritt\n"
            "und Protokoll. Die Anleitung dazu steht in docs/04-bauen-und-flashen.md.\n"
        )
        ttk.Label(self.body, text=text, wraplength=760, justify="left").pack(anchor="w")
        ttk.Label(self.body, style="Warn.TLabel", wraplength=760, justify="left", text=(
            "Achtung: Das Installieren löscht alle Daten auf dem Telefon. Außerdem darf der\n"
            "Bootloader nie wieder gesperrt werden, solange ein eigener Kernel auf dem Gerät ist.\n"
        )).pack(anchor="w", pady=(4, 12))

        ttk.Checkbutton(self.body, text="Ich habe ein Backup meiner Daten.",
                        variable=self.ack_backup).pack(anchor="w", pady=2)
        ttk.Checkbutton(self.body, text="Mein Bootloader ist entsperrt.",
                        variable=self.ack_bootloader).pack(anchor="w", pady=2)
        ttk.Checkbutton(self.body, text="Ich weiß, dass die Installation die Daten löscht "
                                        "(bei Neuinstallation).",
                        variable=self.ack_data).pack(anchor="w", pady=2)
        ttk.Label(self.body, style="Hint.TLabel", wraplength=760, justify="left", text=(
            "Ohne Ihre eigene Telefon-Firmware (firmware/) und ohne den Android-Compiler\n"
            "(toolchain/) geht das Bauen nicht. Die Prüfseite zeigt, was fehlt.")
        ).pack(anchor="w", pady=(16, 0))

        def update(*_):
            ok = all(v.get() for v in (self.ack_backup, self.ack_bootloader, self.ack_data))
            self.next_btn.configure(state="normal" if ok else "disabled")
        for var in (self.ack_backup, self.ack_bootloader, self.ack_data):
            var.trace_add("write", update)
        update()

    # Seite 1: Auswahl

    def page_choose(self):
        frame = ttk.LabelFrame(self.body, text="Was soll passieren?", padding=10)
        frame.pack(fill="x")
        for mode, label in steps.MODES.items():
            ttk.Radiobutton(frame, text=label, value=mode, variable=self.mode,
                            command=self.refresh_choice).pack(anchor="w", pady=1)

        pframe = ttk.LabelFrame(self.body, text="Profil des Telefons", padding=10)
        pframe.pack(fill="x", pady=(10, 0))
        for value, label in steps.PROFILES.items():
            ttk.Radiobutton(pframe, text=label, value=value, variable=self.profile,
                            command=self.refresh_choice).pack(anchor="w", pady=1)

        hframe = ttk.Frame(self.body)
        hframe.pack(fill="x", pady=(10, 0))
        ttk.Label(hframe, text="Halium-Abbild (system.img):").pack(side="left")
        entry = ttk.Entry(hframe, textvariable=self.halium)
        entry.pack(side="left", fill="x", expand=True, padx=8)
        entry.bind("<KeyRelease>", lambda _e: self.refresh_choice())
        ttk.Button(hframe, text="Durchsuchen …", command=self.pick_halium).pack(side="left")

        ttk.Checkbutton(self.body, text="Nothing OS B4.1 vorher auf beide Slots schreiben "
                                        "(nur bei Neuinstallation, löscht die Daten)",
                        variable=self.flash_stock, command=self.refresh_choice
                        ).pack(anchor="w", pady=(10, 0))

        pv = ttk.Frame(self.body)
        pv.pack(fill="both", expand=True, pady=(12, 0))
        self.preview = tk.Text(pv, height=8, wrap="word", font="TkFixedFont", borderwidth=0)
        pscroll = ttk.Scrollbar(pv, command=self.preview.yview)
        self.preview.configure(yscrollcommand=pscroll.set, state="disabled")
        self.preview.pack(side="left", fill="both", expand=True)
        pscroll.pack(side="right", fill="y")
        self.error_label = ttk.Label(self.body, style="Warn.TLabel", wraplength=780)
        self.error_label.pack(anchor="w")
        self.refresh_choice()

    def pick_halium(self):
        path = filedialog.askopenfilename(title="Halium-Abbild auswählen",
                                          filetypes=[("Abbild", "*.img"), ("Alle", "*")])
        if path:
            self.halium.set(path)
            self.refresh_choice()

    def refresh_choice(self):
        self.plan, error = self.build_plan_or_error()
        self.set_preview("" if error else "\n".join(steps.describe(self.plan)))
        self.error_label.configure(text=error or "")
        self.next_btn.configure(state="disabled" if error else "normal")

    def set_preview(self, text):
        self.preview.configure(state="normal")
        self.preview.delete("1.0", "end")
        self.preview.insert("end", text)
        self.preview.configure(state="disabled")

    # Seite 2: Prüfung

    def page_check(self):
        self.plan, error = self.build_plan_or_error()
        if error:
            ttk.Label(self.body, text=error, style="Warn.TLabel").pack(anchor="w")
            self.next_btn.configure(state="disabled")
            return
        self.tree = ttk.Treeview(self.body, columns=("status", "detail"), show="tree headings",
                                 height=14)
        self.tree.heading("#0", text="Prüfung")
        self.tree.heading("status", text="Status")
        self.tree.heading("detail", text="Hinweis")
        self.tree.column("#0", width=330)
        self.tree.column("status", width=90, anchor="center")
        self.tree.column("detail", width=380)
        self.tree.tag_configure("ok", foreground="#1b7f3b")
        self.tree.tag_configure("bad", foreground="#b00020")
        self.tree.tag_configure("info", foreground="#8a6d00")
        self.tree.pack(fill="both", expand=True)

        buttons = ttk.Frame(self.body)
        buttons.pack(fill="x", pady=(10, 0))
        self.recheck_btn = ttk.Button(buttons, text="Erneut prüfen", command=self.start_checks)
        self.recheck_btn.pack(side="left")
        self.check_status = ttk.Label(buttons, style="Hint.TLabel")
        self.check_status.pack(side="left", padx=12)
        self.next_btn.configure(state="disabled")
        self.start_checks()

    def start_checks(self):
        self.recheck_btn.configure(state="disabled")
        self.check_status.configure(text="prüfe …")
        # Tk-Variablen nur im Hauptthread lesen, der Thread bekommt die Werte als Kopie
        plan = self.plan
        halium_path = self.halium.get().strip()

        result = queue.Queue()

        def work():
            checks = steps.prerequisites(plan)
            if halium_path:
                checks.append(steps.halium_check(halium_path))
            checks += phone_checks()
            result.put(checks)  # kein Tk-Aufruf aus dem Thread, nur die Queue

        def poll():
            try:
                checks = result.get_nowait()
            except queue.Empty:
                if self.index == 2:
                    self.after(100, poll)
                return
            self.fill_checks(checks)

        threading.Thread(target=work, daemon=True).start()
        self.after(100, poll)

    def fill_checks(self, checks):
        if self.index != 2:
            return
        self.checks = checks
        self.tree.delete(*self.tree.get_children())
        for check in checks:
            if check.ok:
                status, tag = "✓", "ok"
            elif check.required:
                status, tag = "✗", "bad"
            else:
                status, tag = "!", "info"
            self.tree.insert("", "end", text=check.name, values=(status, check.detail), tags=(tag,))
        required_ok = all(c.ok for c in checks if c.required)
        self.check_status.configure(
            text="Bereit." if required_ok else "Pflichtpunkte fehlen (✗). Bitte beheben und erneut prüfen.")
        self.next_btn.configure(state="normal" if required_ok else "disabled")
        self.recheck_btn.configure(state="normal")

    # Seite 3: Ablauf

    def page_run(self):
        top = ttk.Frame(self.body)
        top.pack(fill="x")
        self.run_btn = ttk.Button(top, text="Starten", command=self.start_run)
        self.run_btn.pack(side="left")
        self.cancel_btn = ttk.Button(top, text="Abbrechen", command=self.cancel_run,
                                     state="disabled")
        self.cancel_btn.pack(side="left", padx=8)
        self.run_status = ttk.Label(top, style="Hint.TLabel")
        self.run_status.pack(side="left", padx=8)

        self.step_tree = ttk.Treeview(self.body, columns=("status",), show="tree headings",
                                      height=7)
        self.step_tree.heading("#0", text="Schritt")
        self.step_tree.heading("status", text="Status")
        self.step_tree.column("#0", width=520)
        self.step_tree.column("status", width=140, anchor="center")
        self.step_tree.tag_configure("ok", foreground="#1b7f3b")
        self.step_tree.tag_configure("fail", foreground="#b00020")
        self.step_tree.pack(fill="x", pady=(10, 6))
        self.step_ids = []
        for step in self.plan:
            iid = self.step_tree.insert("", "end", text=step.title,
                                        values=(STATUS_TEXT["wait"],))
            self.step_ids.append(iid)

        self.log_text = tk.Text(self.body, height=14, wrap="word", font="TkFixedFont",
                                background="#1e1e1e", foreground="#e6e6e6", borderwidth=0)
        scroll = ttk.Scrollbar(self.body, command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scroll.set)
        row = ttk.Frame(self.body)
        row.pack(fill="both", expand=True)
        self.log_text.pack(in_=row, side="left", fill="both", expand=True)
        scroll.pack(in_=row, side="right", fill="y")
        self.log_text.configure(state="disabled")
        self.next_btn.configure(state="disabled")
        self.poll_messages_id = None
        if self.runner and self.runner.is_alive():
            self.run_btn.configure(state="disabled")
            self.cancel_btn.configure(state="normal")
        else:
            self.append_log(self.summary_header())

    def summary_header(self):
        lines = [f"Ablauf ({len(self.plan)} Schritte):"]
        lines += steps.describe(self.plan)
        return "\n".join(lines) + "\n"

    def append_log(self, text):
        self.log_text.configure(state="normal")
        self.log_text.insert("end", text + ("" if text.endswith("\n") else "\n"))
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def confirm_destructive(self):
        if not steps.has_destructive(self.plan):
            return True
        names = "\n".join(f"  • {s.title}" for s in self.plan if s.destructive)
        messagebox.showwarning("Datenverlust", "Diese Schritte löschen Daten auf dem Telefon:\n\n"
                               + names + f"\n\nZum Fortfahren tippen Sie {CONFIRM_WORD} ein.")
        answer = simpledialog.askstring("Bestätigung", f"Tippen Sie {CONFIRM_WORD}:", parent=self)
        return answer == CONFIRM_WORD

    def ask_sudo(self):
        for _ in range(3):
            pw = simpledialog.askstring(
                "sudo-Passwort", "Für die Schritte mit sudo brauche ich Ihr Passwort.\n"
                "Es wird nur im Speicher gehalten.", show="*", parent=self)
            if pw is None:
                return None
            if self.dry_run or sudo_password_ok(pw):
                return pw
            messagebox.showerror("sudo", "Das Passwort stimmt nicht. Bitte erneut.")
        return None

    def start_run(self):
        self.plan, error = self.build_plan_or_error()
        if error:
            messagebox.showerror("Auswahl", error)
            return
        if not self.confirm_destructive():
            return
        self.sudo_pw = None
        if steps.needs_sudo(self.plan) and not self.dry_run:
            self.sudo_pw = self.ask_sudo()
            if self.sudo_pw is None:
                self.append_log("Abgebrochen: kein gültiges sudo-Passwort.")
                return

        os.makedirs(LOG_DIR, exist_ok=True)
        stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
        self.log_path = os.path.join(LOG_DIR, f"installer-{stamp}.log")
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")
        self.append_log(self.summary_header())
        self.append_log(f"Protokoll: {self.log_path}\n")

        self.runner = Runner(self.plan, self.sudo_pw, self.dry_run, self.messages, self.log_path)
        self.runner.start()
        self.run_btn.configure(state="disabled")
        self.cancel_btn.configure(state="normal")
        self.run_status.configure(text="läuft …")
        self.sudo_pw = None  # nicht länger in der Oberfläche halten
        self.after(100, self.poll_messages)

    def cancel_run(self):
        if self.runner and messagebox.askyesno("Abbrechen", "Der laufende Schritt wird beendet. "
                                               "Wirklich abbrechen?"):
            self.cancel_btn.configure(state="disabled")
            self.run_status.configure(text="wird abgebrochen …")
            self.runner.cancel()

    def poll_messages(self):
        if not self.winfo_exists():
            return
        try:
            while True:
                kind, *rest = self.messages.get_nowait()
                if kind == "log":
                    self.append_log(rest[0])
                elif kind == "step":
                    index, status = rest
                    iid = self.step_ids[index]
                    tag = {"ok": "ok", "fail": "fail"}.get(status, "")
                    self.step_tree.item(iid, values=(STATUS_TEXT[status],), tags=(tag,))
                elif kind == "end":
                    self.finish_run(rest[0])
                    return
        except queue.Empty:
            pass
        self.after(100, self.poll_messages)

    def finish_run(self, outcome):
        self.run_btn.configure(state="disabled")
        self.cancel_btn.configure(state="disabled")
        self.run_status.configure(text={"done": "Fertig.", "fail": "Fehlgeschlagen.",
                                        "cancel": "Abgebrochen."}[outcome])
        self.outcome = outcome
        self.next_btn.configure(state="normal")
        self.append_log(f"\nProtokoll gespeichert: {self.log_path}")
        self.after(300, lambda: self.show(4))

    # Seite 4: Fertig

    def page_done(self):
        outcome = getattr(self, "outcome", "cancel")
        if outcome == "done":
            text = "Der Ablauf ist abgeschlossen."
        elif outcome == "fail":
            text = ("Ein Schritt ist fehlgeschlagen. Das Protokoll zeigt die Ursache. Ein "
                    "Neustart des Installers bei den gleichen Einstellungen versucht es erneut.")
        else:
            text = "Der Ablauf wurde abgebrochen."
        ttk.Label(self.body, text=text, wraplength=760, justify="left").pack(anchor="w")
        ttk.Label(self.body, style="Hint.TLabel", wraplength=760, justify="left", text=(
            f"Protokoll: {self.log_path or '(keins)'}\n\n"
            + self.next_steps_text())).pack(anchor="w", pady=(12, 0))

    def next_steps_text(self):
        mode = self.mode.get()
        if mode == steps.MODE_BUILD:
            return ("Die Abbilder liegen in out/. Zum Installieren wählen Sie "
                    "„Bauen und neu installieren“ oder flashen sie von Hand (siehe Dokumentation).")
        if mode == steps.MODE_FULL:
            return ("Das Telefon startet jetzt. Der Bildschirm bleibt beim ersten Start schwarz, "
                    "das ist normal. Erreichbar ist es über USB unter 172.16.42.1 (Benutzer alarm, "
                    "Passwort alarm, bitte sofort ändern).")
        if mode == steps.MODE_SERVER:
            return "Der Server ist nach dem Neustart unter tetris.local per SSH erreichbar."
        if mode == steps.MODE_DESKTOP:
            return ("Das RDP-Passwort steht im Protokoll. Verbinden Sie sich mit "
                    "scripts/desktop-rdp.sh.")
        return "Die zweite Stufe ist durch. Das Telefon startet mit der Nothing-Shell neu."


# --- Einstieg -------------------------------------------------------------------------------

def print_plan(mode, profile, halium, flash_stock):
    try:
        plan = steps.build_plan(mode, profile, halium, flash_stock)
    except steps.PlanError as err:
        print(f"Fehler: {err}", file=sys.stderr)
        return 2
    print("\n".join(steps.describe(plan)))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Installer für das CMF Phone 1")
    parser.add_argument("--dry-run", action="store_true",
                        help="Fenster öffnen, aber nichts ausführen")
    parser.add_argument("--print-plan", action="store_true",
                        help="den Ablauf als Text ausgeben und beenden")
    parser.add_argument("--mode", choices=sorted(steps.MODES), default=steps.MODE_BUILD)
    parser.add_argument("--profile", choices=sorted(steps.PROFILES), default="phone")
    parser.add_argument("--halium", default="", help="Pfad zum Halium-Abbild (system.img)")
    parser.add_argument("--stock", action="store_true",
                        help="Nothing OS B4.1 vorher flashen (nur mit --mode full)")
    args = parser.parse_args(argv)

    if args.print_plan:
        return print_plan(args.mode, args.profile, args.halium, args.stock)
    if not os.environ.get("DISPLAY") and not os.environ.get("WAYLAND_DISPLAY"):
        print("Kein Anzeigeserver gefunden. Für den Ablauf als Text: --print-plan", file=sys.stderr)
        return 1
    app = App(dry_run=args.dry_run)
    app.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
