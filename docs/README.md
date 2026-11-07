# Technical documentation: Arch Linux on the CMF Phone 1

**🇬🇧 English** · [🇩🇪 Deutsch](de/README.md)

This documentation explains the project **completely**: what runs on the phone, how it is
built, what each script does and why things are solved the way they are. It is written for
two groups:

- **Beginners with no technical background.** You want to understand what this project does,
  and maybe install it yourself later. Read in this order: `01-basics.md`,
  `02-hardware-and-partitions.md`, `08-profiles.md`, `10-glossary.md`. Terms you do not know
  are explained in the glossary.
- **Developers and tinkerers** who want to build, flash, debug or change code. The details are
  in `03-architecture.md`, `04-build-and-flash.md`, `05-script-reference.md`,
  `06-rootfs-reference.md`, `07-kernel-drivers-patches.md` and `09-troubleshooting.md`.

The short version for people in a hurry is the main [README](../README.md).

## Contents

| File | Content | For whom |
|---|---|---|
| [01-basics.md](01-basics.md) | What Linux, a kernel, a bootloader, a compositor and a container are. No prior knowledge needed | Everyone |
| [02-hardware-and-partitions.md](02-hardware-and-partitions.md) | The phone (chip, memory, partitions, A/B slots) and what lives on which partition | Everyone, required before flashing |
| [03-architecture.md](03-architecture.md) | Boot chain from power-on to desktop, Halium container, GPU path, modem path | Developers |
| [04-build-and-flash.md](04-build-and-flash.md) | Step by step: getting the inputs, building, flashing, second stage, way back | Users, tinkerers |
| [05-script-reference.md](05-script-reference.md) | Every script in `scripts/`, `pkgbuilds/` and `initramfs/`: purpose, arguments, inputs and outputs | Developers |
| [06-rootfs-reference.md](06-rootfs-reference.md) | The phone's file system: overlays, systemd services, own programs, configuration | Developers |
| [07-kernel-drivers-patches.md](07-kernel-drivers-patches.md) | Kernel configuration, patches for the kernel, drivers, libhybris, wlroots, ModemManager | Developers |
| [08-profiles.md](08-profiles.md) | The operating modes: phone (Nothing shell or Phosh), server, desktop and one-boot switching | Everyone |
| [09-troubleshooting.md](09-troubleshooting.md) | Finding logs, typical errors and their causes, crash guard, way back when things go wrong | Everyone |
| [10-glossary.md](10-glossary.md) | Explanation of all technical terms and abbreviations | Everyone |

## Conventions in this documentation

- **Paths** are relative to the repository root (`cmfphone1archLinux/`) unless stated otherwise.
  For example `scripts/build-kernel.sh` means `<repo>/scripts/build-kernel.sh`.
- **Commands** are in code blocks. The text before or above them says where they run: on the
  **PC** or on the **phone** (via `scripts/phone-ssh.sh` or directly as `alarm`).
- **`out/`, `src/`, `downloads/`, `firmware/`, `toolchain/`** are local directories. They are not
  in Git (see `.gitignore`) because they hold proprietary files, build results or very large
  sources. What they should contain is described in
  [04-build-and-flash.md](04-build-and-flash.md#43-getting-the-inputs-yourself).
- **Device names:** the code name of the device is `Tetris`, the chip is a MediaTek
  Dimensity 7300 (`MT6878`). Both names appear in the source. `tetris-*` is the prefix for our own
  programs and services.
- **Status statements** (works, partly, not working) come from the table in the main README and
  apply to the `main` branch.

## Status

This documentation describes the state of `main` (November 2026). When scripts change, the
affected sections should be updated in the same commit.
