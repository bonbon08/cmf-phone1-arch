# Graphical installer

**🇬🇧 English** · [🇩🇪 Deutsch](de/README.md)

The installer runs the scripts in `scripts/` in the right order and shows them in a small window with
progress, log and warnings. It does not replace the scripts. If you want to do the steps by hand, all
commands are in [docs/04-build-and-flash.md](../docs/04-build-and-flash.md).

The installer is meant for people who want to set up the phone without typing twenty commands in the
right order by hand. It knows the risks, shows them, and asks before the dangerous steps.

> **Warning:** the installation erases all data on the phone. Read the section "Risks" in
> [docs/04-build-and-flash.md](../docs/04-build-and-flash.md#42-risks) first.

## Contents

- [What the installer does and does not do](#what-the-installer-does-and-does-not-do)
- [Requirements](#requirements)
- [Start](#start)
- [The pages](#the-pages)
- [Operating modes](#operating-modes)
- [Safety](#safety)
- [Command line](#command-line)
- [Logs](#logs)
- [Tests](#tests)
- [Errors and solutions](#errors-and-solutions)
- [Layout of the files](#layout-of-the-files)

## What the installer does and does not do

**It does:**

- asks what you want (build only, reinstall, second stage, server, desktop).
- checks that all tools and input files are there before anything runs.
- checks the phone: fastboot mode, USB connection, emergency shell, internet.
- runs the steps in the right order. Each step is an existing script from `scripts/` or a `fastboot` command.
- after the phone reboots, waits until it answers over USB, then continues with the second stage.
- shows the output of the scripts live and saves it as a log.
- stops when a required step fails. Optional steps (e.g. the NAT sharing) do not block the run.

**It does not:**

- obtain the original firmware (`firmware/`), the Android compiler (`toolchain/`) or the Arch Linux ARM image.
  You have to get those yourself (see [docs/04, section 4.3](../docs/04-build-and-flash.md#43-getting-the-inputs-yourself)).
- unlock the bootloader. That is your decision and has consequences for the warranty and your data.
- put the phone into fastboot mode. You do that with the buttons (Power and Volume down). The installer tells you when.
- store your passwords. The sudo password is only kept in memory.

## Requirements

| What | Why | On Arch Linux |
|---|---|---|
| Python 3.9 or newer | the installer is written in Python | usually already installed |
| Tkinter (Tk) | the window | `sudo pacman -S tk` |
| a display server (X11 or Wayland) | the window | present on a desktop |
| the tools from [docs, section 4.4](../docs/04-build-and-flash.md#44-host-packages) | the scripts need them | the installer shows missing tools on the check page |
| the inputs from [docs, section 4.3](../docs/04-build-and-flash.md#43-getting-the-inputs-yourself) | firmware, toolchain, Arch Linux ARM | the installer shows missing files on the check page |

The installer does not check everything at start. It shows the check page only after your selection,
because which requirements apply depends on the operating mode.

## Start

From the repository root:

```sh
python3 installer/tetris_installer.py
```

It also works from elsewhere, because it knows its own location. The scripts find their inputs inside the repository.

A double-click in the file manager works differently depending on the desktop. Starting from a terminal is reliable.

## The pages

The top always shows the name of the page and "Step x of 5". The buttons **Back** and **Next** are at the bottom.

### 1. Welcome

Three confirmations before you can continue:

- I have a backup of my data.
- My bootloader is unlocked.
- I know that the installation erases the data (for a reinstall).

Without all three, **Next** stays disabled.

### 2. Selection

Here you choose:

- **What should happen** (operating mode, see below).
- **Profile** of the phone (phone, server, desktop). The profile determines which image is built.
- **Halium image** (`system.img`) for the second stage. The "Browse" button helps. The installer suggests
  `downloads/halium/system.img` if the file is there.
- **Write Nothing OS first** (only for a reinstall). This resets the phone to the original state and erases the data.

The preview at the bottom shows the **exact sequence** with all commands. You see what will happen before you
start. If the selection does not make sense (e.g. phone profile without a Halium image), the reason is shown in red
and **Next** is disabled.

### 3. Check

A table of all requirements:

| Mark | Meaning |
|---|---|
| ✓ green | fulfilled |
| ✗ red | missing, and the run cannot start like this. Fix it and repeat with **Check again** |
| ! yellow | note. The phone may be connected later (e.g. in fastboot mode) |

**Next** is only enabled when all red points are resolved. The yellow points do not block.

### 4. Run

- The list of steps shows the status of each step: *waiting*, *running*, *done*, *skipped* (optional step failed),
  *error* or *cancelled*.
- The log shows the output of the scripts live.
- **Start** begins the run. **Cancel** stops the running step (after a confirmation).
- If you close the window during a run, the installer asks whether it should cancel.

Destructive steps ask for a confirmation before the start: you type `LOESCHEN` (German for "delete").

### 5. Done

The summary with the result, the path of the log and the next steps. For a reinstall it says, for example, that the
black screen at the first boot is normal and how to reach the phone over USB.

## Operating modes

| Mode | What happens | Phone |
|---|---|---|
| **Build only** | fetch sources, build kernel, build boot images, build the image | left untouched |
| **Build and reinstall** | as "build only", then (optionally Nothing OS), the image on `userdata`, the kernel on slot b, wait, second stage | **overwritten, data erased** |
| **Second stage on the running phone** | wait, NAT, `setup-phone.sh` (Halium, libhybris, wlroots, Nothing shell) | running, phone must be reachable over USB |
| **Server profile on the running phone** | wait, NAT, `setup-server.sh` | running, phone must be reachable over USB |
| **Desktop profile on the running phone** | wait, NAT, `setup-desktop.sh` | running, phone must be reachable over USB |

The individual steps as the installer calls them:

| Step | Command | Note |
|---|---|---|
| fetch kernel sources | `scripts/fetch-sources.sh` | several GB from the internet |
| build kernel and modules | `scripts/build-kernel.sh all` | |
| build boot images | `scripts/build-debug-boot.sh` | |
| build Arch Linux image | `env PROFILE=… scripts/build-rootfs.sh` (with sudo) | takes longest |
| write Nothing OS (optional) | `scripts/flash-stock.sh firmware/…/images` | **erases the data** |
| image to `userdata` | `fastboot flash userdata out/rootfs.simg` | **erases the data** |
| kernel to slot b | `scripts/flash-debug-b.sh` | slot a stays unchanged |
| wait for the phone | own check over SSH (172.16.42.1:22) | up to five minutes |
| share internet (optional) | `scripts/host-nat.sh up` (with sudo) | |
| second stage | `scripts/setup-phone.sh <system.img>` | |
| server profile | `scripts/setup-server.sh` | phone reboots |
| desktop profile | `scripts/setup-desktop.sh` | phone reboots |

With the **server profile** a reinstall ends after writing slot b. The server image needs no second stage. With the
**desktop profile** the second stage is optional. It brings GPU acceleration for GNOME.

## Safety

- **Confirmations.** The three checkboxes on the first page, and before every run with destructive steps the word `LOESCHEN`.
- **sudo password.** It is asked for before the start and verified with `sudo -v`. It is only kept in memory. It
  is passed to the steps over standard input (stdin), never as a command-line argument (that would be visible in
  `ps`). It never appears in the log.
- **Cancel.** Each step runs in its own process group. **Cancel** stops the whole group, including child processes
  such as `make`.
- **Nothing hidden.** Every output appears in the window and in the log. A failed required step stops the run immediately.

## Command line

```sh
python3 installer/tetris_installer.py [options]
```

| Option | Effect |
|---|---|
| `--dry-run` | opens the window but runs no command. The sudo password is not asked for. Good for trying it out |
| `--print-plan` | prints the sequence as text and exits. No window |
| `--mode {build,full,stage2,server,desktop}` | operating mode (with `--print-plan`) |
| `--profile {phone,server,desktop}` | profile (with `--print-plan`) |
| `--halium PATH` | Halium image (with `--print-plan`) |
| `--stock` | write Nothing OS first (with `--print-plan` and `--mode full`) |

Examples:

```sh
# show the sequence for a reinstall with the Halium image
python3 installer/tetris_installer.py --print-plan --mode full --profile phone \
    --halium downloads/halium/system.img --stock

# try the window without changing anything
python3 installer/tetris_installer.py --dry-run
```

## Logs

Each run writes a log to `out/installer-logs/installer-YYYYMMDD-HHMMSS.log`. The directory `out/` is not in Git.
The log contains:

- the sequence as a list,
- every command the installer starts,
- the output of the scripts,
- the return code of each step,
- the end (`done`, `fail` or `cancel`).

A password never appears in the log. Still, share it only redacted, because the scripts' output can contain
serial numbers and network addresses.

## Tests

The sequence logic has its own tests. They need no phone and no window:

```sh
python3 -m unittest discover -s installer -p 'test_*.py' -v
```

The tests check, among other things:

- the order of the steps in each operating mode,
- that destructive steps are marked as such,
- that `sudo` is only required where needed,
- that the phone profile without a Halium image is rejected,
- that the check demands the right tools and files for each operating mode.

## Errors and solutions

| Message or behaviour | Cause | Remedy |
|---|---|---|
| `tkinter fehlt. Auf Arch Linux: sudo pacman -S tk` | Tk is not installed | `sudo pacman -S tk` |
| `Kein Anzeigeserver gefunden` | installer started without a desktop (e.g. over SSH) | start it on the desktop, or use `--print-plan` |
| check page: `Programm: … fehlt` | tool not installed | install the package (see [docs, 4.4](../docs/04-build-and-flash.md#44-host-packages)) |
| check page: `Arch-Linux-ARM-Basissystem` missing | input file missing | [docs, 4.3](../docs/04-build-and-flash.md#43-getting-the-inputs-yourself) |
| `Das Passwort stimmt nicht` | wrong sudo password | enter it again (three attempts) |
| run stays at "warte auf das Telefon" | phone not reachable over USB, or the reboot takes long | check the cable, `ip addr` on the PC, the phone may be in the initramfs emergency shell |
| `fastboot … not found` or no device | phone not in fastboot mode | hold Power and Volume down, then `fastboot devices` |
| step "Build Arch Linux image" aborts | see the log, usually a missing package or QEMU | [docs, 4.12](../docs/04-build-and-flash.md#412-common-build-errors) |
| run ends with `fail` | the step in the log shows the cause | read the log, fix the cause, restart the installer and run again with the same settings |

A failed run can be repeated with the same settings. Most steps are repeatable: `fetch-sources.sh` skips
sources that are already there, and `build-kernel.sh` rebuilds what has changed. The flash steps write again,
which does no harm.

## Layout of the files

| File | Content |
|---|---|
| `tetris_installer.py` | window (Tk), run in the background, phone checks, entry point |
| `steps.py` | sequence: which steps in which order, requirements. Without a window |
| `test_steps.py` | tests for `steps.py` |
| `README.md` | this guide (English) |
| `de/README.md` | the same guide in German |

The separation is deliberate. `steps.py` decides **what** happens and can be checked without a window.
`tetris_installer.py` shows it and runs it. If you change or add a step, adjust `steps.py` and add a test in
`test_steps.py`.

### Adding a step

1. In `steps.py`, in `build_plan()`, insert a `Step(...)` at the right place. `root=True` if the command needs `sudo`.
   `destructive=True` if it erases data. `optional=True` if a failure should not stop the run.
2. Add a test in `test_steps.py` that checks the order.
3. Look at the output with `--print-plan`.
4. Click through the window with `--dry-run`.

## Status of testing

The installer was developed with Python 3.14 and Tk 8.6. Tested are:

- the sequence logic with unit tests,
- the sequence as text (`--print-plan`) in all operating modes,
- the window in dry-run mode: all pages, confirmations, log, execution,
- the execution path with real commands: success, failure, optional failure, cancel,
- the size of the pages in the window.

**Not tested:** a complete run with a real phone. The steps call scripts that the project already uses.
The combination of installer and device has not been tried yet. Please keep the log of the first real run.
