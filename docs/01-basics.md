# 1. Basics: what actually happens here

**🇬🇧 English** · [🇩🇪 Deutsch](de/01-grundlagen.md)

This chapter is for readers without technical background. It explains the building blocks that
come up again and again in the other chapters. If you already know all of this, you can skip it.

## 1.1 What does the project do?

A smartphone normally ships with the operating system the manufacturer installed. On the
**CMF Phone 1** by Nothing that is **Nothing OS**, an Android variant.

This project does **not** replace the whole Android part. It swaps out the system you see every
day:

- The phone runs **Arch Linux ARM**, a complete Linux distribution for ARM processors (the kind
  of processor found in almost every phone).
- The user interface is either the **Nothing shell** (the default profile: a home screen with an
  app grid, status bar and home bar in the style of a phone) or **Phosh** (a free phone interface
  from the GNOME world).
- The phone hardware is driven by the Linux kernel. For parts that only work with Android
  drivers (graphics chip, modem, camera), a small **Android container** runs in the background and
  provides those drivers. More on that below.

The result is a phone that works like a Linux computer: you can install packages with `pacman`,
manage services with `systemd` and use the whole Arch software world. The same device can also
start as a **server** (no screen, reachable via SSH and Docker) or as a **desktop** (GNOME over
the network, RDP). See [08-profiles.md](08-profiles.md).

**Important:** the project is an early preview for people who know fastboot and Linux.
Installing **erases all data** on the phone. Before you start, read
[02-hardware-and-partitions.md](02-hardware-and-partitions.md) and the risks in
[04-build-and-flash.md](04-build-and-flash.md#42-risks).

## 1.2 Terms from the boot chain

When you switch the phone on, several things happen one after another. Each step has its own
name.

| Term | What it is | Comparison |
|---|---|---|
| **Bootloader** | The first program the chip starts after power-on. It loads the operating system. On this phone it is called **LK** (Little Kernel) and is supplied by the manufacturer. | The doorman who checks who may come in |
| **Kernel** | The core of the operating system. It manages processor, memory, devices and drivers. Linux is a kernel. | The foundation of a house |
| **Driver / kernel module** | A piece of kernel code for a specific component (e.g. Wi-Fi). Modules can be loaded at runtime (`insmod`, `modprobe`). | An interpreter for one particular device |
| **Initramfs** | A small mini-system in memory that runs before the real system starts. It loads the necessary drivers and finds the system partition. | A porch that opens the door |
| **Rootfs** | The actual file system of the system (`/bin`, `/etc`, `/home` ...). Ours is Arch Linux ARM. | The house itself |
| **systemd** | The first program in the finished system. It starts and supervises all services. | The caretaker who switches everything on |
| **Service (daemon)** | A program that runs in the background, e.g. the Wi-Fi manager. Started by systemd. | An employee who works permanently |

Two words deserve special attention:

- **Fastboot** is a mode of the bootloader in which the PC can write partitions over USB. You
  enter it with *Power + Volume down* (see [04-build-and-flash.md](04-build-and-flash.md)).
  Writing to a partition is called **flashing**.
- A **partition** is a delimited area of the phone's flash memory, for example "the kernel lives
  here" or "your photos live here". More in [02-hardware-and-partitions.md](02-hardware-and-partitions.md).

## 1.3 What is Android doing here, and why do we need it?

A phone needs drivers that the manufacturer often only provides for Android (more precisely, for
its own Android image). These drivers are mostly **proprietary** (not freely available) and rely on
Android services that do not exist on a normal Linux system.

The project's solution is called **Halium**. A minimal Android system (here **LuneOS Halium 16**)
runs **in a container** next to the Linux system. A container is an isolated environment within the
same system, similar to a virtual machine but without its own kernel. The container only provides
the hardware services we need:

- **Graphics:** the allocator and the composer (they hand images to the display),
- **Modem:** the mobile network connection,
- **Camera**, **GPS**, **fingerprint sensor**.

The Linux system talks to these services through a bridge called **libhybris**. libhybris is a
library that calls Android drivers in a way normal Linux programs can use. See
[03-architecture.md](03-architecture.md#33-the-halium-container).

## 1.4 Graphics: how does a picture reach the screen?

A user interface consists of windows that are put together. On Linux this job belongs to the
**compositor**. In this project these are:

- **phoc** (for Phosh) or **nothing-wm** (for the Nothing shell). Both are based on **wlroots**, a
  library for Wayland compositors.
- **Wayland** is the protocol through which programs send their windows to the compositor.
- The **GPU** (graphics chip, here the Mali-G615) computes the rendering. The compositor can work
  on the GPU, or without a GPU on the processor (**software mode**, using `pixman`).

So there are two paths:

1. **GPU mode:** the compositor uses the Android composer through libhybris. Fast and smooth.
2. **Software mode:** everything runs on the processor. Slower, but reliable. It is the way out
   when GPU mode crashes.

The program `tetris-phosh-select` decides at startup which path is taken. There is also a
**crash guard**: if GPU mode crashes within 90 seconds, the next start uses software mode. Details
in [09-troubleshooting.md](09-troubleshooting.md#99-crash-guard).

## 1.5 What is a profile?

The phone can have different **profiles**, i.e. take on different roles:

- **Phone:** a phone with a user interface (default).
- **Server:** no screen, but reachable via SSH and Wi-Fi, with Docker containers and local AI
  models.
- **Desktop:** like the server, but with a complete GNOME desktop that you see and operate on
  another device over the network (RDP).

You switch with the command `tetris-mode` or with the app **Startmodus** (start mode). See
[08-profiles.md](08-profiles.md).

## 1.6 Which tools are needed on the PC?

To build you need a PC running Linux (developed on Arch Linux, x86-64 processor). The most
important tools are:

| Tool | What for |
|---|---|
| `git` | fetches source code from the internet and manages versions |
| `make`, `clang`, `lld` | compile source code, here for ARM64 |
| `fastboot` | talks to the phone's bootloader |
| `mkbootimg`, `unpack_bootimg` | build and unpack Android boot images |
| `arch-chroot`, `bsdtar` | work inside an Arch Linux ARM image without booting it |
| `ssh`, `scp` | connection to the running phone over USB |

A complete list with installation hints is in
[04-build-and-flash.md](04-build-and-flash.md#44-host-packages).

## 1.7 Where is what in the repository?

```
cmfphone1archLinux/
├── README.md            short guide and feature status
├── docs/                this documentation
├── scripts/             build, flash and setup scripts (on the PC)
├── pkgbuilds/           build scripts that run on the phone
├── kernel/              kernel configuration (linux.config)
├── patches/             changes to foreign software (kernel, libhybris, wlroots, ModemManager)
├── initramfs/           the mini system that starts before the main system
├── rootfs/              files and programs for the phone system
│   ├── overlay/         shared files (phone profile)
│   ├── overlay-server/  additionally for the server profile
│   ├── overlay-desktop/ additionally for the desktop profile
│   └── src/             our own C and Python programs
├── stock-info/          extracts from the original system (local only, not in Git)
├── firmware/            original images from Nothing (local only, not in Git)
├── downloads/           Arch Linux ARM image, busybox, Magisk, Halium (local only, not in Git)
├── toolchain/           Android compiler (local only, not in Git)
├── src/                 kernel sources (fetched by scripts/fetch-sources.sh, not in Git)
└── out/                 all build results, SSH keys (local only, not in Git)
```

Only the directories `scripts/`, `pkgbuilds/`, `kernel/`, `patches/`, `initramfs/`, `rootfs/` and
the files `README.md`, `LICENSE` and `.gitignore` are in Git (`git ls-files`). The `.gitignore` is
a **whitelist**: anything not explicitly allowed is ignored. This keeps firmware, device data,
serial numbers, IMEI backups and SSH keys off the public repository. Chapters 05 to 07 explain what
exactly is in each directory.

## 1.8 License and liability

Our own code (scripts, services, patches, apps) is licensed under **GPL-2.0-or-later** (see
`LICENSE`). Patches to other projects keep those projects' licenses. This is a hobby project
**without warranty**. A wrongly flashed phone can become unusable. Read
[04-build-and-flash.md](04-build-and-flash.md#42-risks) before every flash.
