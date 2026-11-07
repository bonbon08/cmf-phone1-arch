# 10. Glossary

**🇬🇧 English** · [🇩🇪 Deutsch](de/10-glossar.md)

Sorted alphabetically. The explanations are brief. Where useful they point to the chapter with the details.

**A**

**A/B slot.** Two copies of the most important partitions (`boot_a` and `boot_b` etc.). Slot a holds the
original system, slot b our system. The bootloader selects the active slot.
See [02](02-hardware-and-partitions.md#23-the-ab-slots).

**ALSA, UCM.** Linux's audio interface. UCM (Use Case Manager) describes which speakers, microphones and
routes belong to which purpose. Our profile lives under `overlay/usr/share/alsa/ucm2/`.

**APEX.** Android package format for system parts that are updated individually. Bound into the container
by `tetris-mount-apexes`.

**APN (access point name).** The address through which the mobile network sets up a data connection. The
project does not need its own APN: the modem uses the one the network offers.

**AT commands.** Text commands used to control a cellular modem (e.g. `AT+CGDATA`). ModemManager talks to the modem through them.

**AVB (Android Verified Boot).** A mechanism that checks whether the partitions are unchanged. Our kernel only
runs when AVB is disabled. See [04](04-build-and-flash.md#3a-optional-rewriting-the-stock-system-flash-stocksh).

**B**

**Bootloader.** The first program that runs after power-on. On this phone it is LK. See
[01](01-basics.md#12-terms-from-the-boot-chain).

**Bind mount.** A directory or file appears in a second place, without a copy. The container uses them to share parts of the host.

**BlueZ.** Linux's Bluetooth service.

**busybox.** A toolbox with many Unix commands in one small file. In the initramfs it is the shell.

**C**

**Cortex-A78 / A55.** The processor cores of the Dimensity 7300: four fast (A78) and four efficient (A55).

**clang / lld.** The C compiler (LLVM) and the linker. Android uses them, so do we.

**Compositor.** The program that assembles windows and puts them on the screen. Here phoc (Phosh) or
nothing-wm (Nothing shell). See [01](01-basics.md#14-graphics-how-does-a-picture-reach-the-screen).

**Container (LXC).** An isolated environment inside the same kernel. The Halium container provides the Android drivers.
See [03](03-architecture.md#33-the-halium-container).

**Crash guard.** The mechanism that sends the next start into software mode after a GPU crash.
See [09](09-troubleshooting.md#99-crash-guard).

**D**

**depmod.** Creates the dependency list of the kernel modules (`modules.dep`).

**Device tree (DTB).** A description of the hardware that the bootloader hands to the kernel. It lives in `vendor_boot`.

**DPMS.** The power-saving function of a screen. `tetris-display-off` uses it to switch the panel off.

**DRM (Direct Rendering Manager).** The kernel interface to the graphics hardware and the display. The software mode uses it directly.

**dm-linear.** The device mapper, which makes new devices out of parts of a partition. `tetris-map-super` uses it
to open the Android partitions from `super`.

**Docker / Ollama.** Docker runs containers. Ollama runs AI models. Both run in the server profile.
See [08](08-profiles.md#85-server-profile).

**Dimensity 7300 (MT6878).** The phone's chip, made by MediaTek.

**E**

**EGL.** The interface between an application and the graphics driver. libhybris provides it for the Android driver.

**F**

**Fastboot.** A mode of the bootloader in which the PC can write partitions over USB. Entered with Power + Volume down.

**fastbootd.** A stage within fastboot that manages logical partitions (`super`).

**Flash / flashing.** Writing an image into a partition.

**G**

**GDM.** GNOME's login screen. In the desktop profile it starts the headless session.

**glibc.** The standard C library of the system.

**glvnd.** A layer that selects EGL providers. In the desktop profile it points to libhybris.

**GNSS / GPS.** The satellite positioning system. `tetris-gnss` delivers the data to geoclue.

**GPL-2.0-or-later.** The license of our own code. See `LICENSE`.

**GSI (Generic System Image).** A general Android image that runs on many devices. Here the LuneOS Halium GSI.

**gralloc.** Android's memory service for images. The compositor requests buffers through it.

**H**

**Halium.** A project that provides Android drivers in a container for Linux phones.
See [01](01-basics.md#13-what-is-android-doing-here-and-why-do-we-need-it).

**HAL (Hardware Abstraction Layer).** The Android interface between system services and hardware, e.g. the composer HAL or the modem HAL.

**HBM (high brightness mode).** The high-brightness mode of the panel. The fingerprint sensor needs it for a good capture.

**hwcomposer / composer.** The Android service that puts images onto the screen. Our wlroots backend uses it.

**I**

**Image.** A file that describes a whole partition.

**Initramfs.** A mini system in memory that runs before the main system. See
[03](03-architecture.md#step-3-the-initramfs-initramfsinit).

**IMEI.** The device number of the mobile modem. It is stored in the modem partitions.

**K**

**Kernel.** The core of the operating system: processor, memory, devices. Here Linux 6.1 from Nothing.

**Kernel module (`.ko`).** A driver that is loaded at runtime.

**KMI.** Kernel Module Interface: an Android interface that keeps modules matched to the kernel. We switch it off
because we build all modules ourselves.

**L**

**LK (Little Kernel).** MediaTek's bootloader.

**libhybris.** A library that makes Android drivers usable for normal Linux programs.
See [07](07-kernel-drivers-patches.md#727-libhybris-three-adjustments).

**Layer shell.** A Wayland extension for interface elements such as bars and overlays. The home bar and the fingerprint spot need it.

**logind.** The systemd service that manages sessions, users and keys.

**M**

**Magisk.** A tool that gives root rights on Android. Only `read-flashlog.sh` needs it, on Nothing OS.

**ModemManager.** Linux service that controls mobile modems.

**Mali-G615.** The graphics unit of the chip. Usable only with the Android driver.

**mkbootimg.** The tool that creates boot images.

**N**

**NCM (USB Network Control Model).** A USB network interface. The initramfs sets it up so that the PC reaches the phone at `172.16.42.1`.

**NVRAM.** Persistent storage for calibration data (Wi-Fi, modem, Bluetooth). Lives in the partitions `nvdata` and `nvcfg`.

**nt_reserve2.** A reserve partition that our initramfs uses as log storage.

**O**

**Overlay.** A directory tree whose files are copied over the rootfs. See [06](06-rootfs-reference.md#61-what-is-an-overlay).

**P**

**Partition.** A delimited area of flash memory. See [02](02-hardware-and-partitions.md#22-what-is-a-partition).

**Patch.** A text file with changes to foreign source code. See [07](07-kernel-drivers-patches.md#71-what-is-a-patch).

**pacman.** Arch Linux's package manager.

**Phosh.** A free phone interface based on GNOME and phoc.

**pidfd.** A process reference that stays safe even when the process ID is reused. A patch brings it into the kernel for the desktop.

**pmsg / pstore.** Memory that the kernel does not lose on a crash or warm reset. Our initramfs writes to it.

**Profile.** An operating mode: phone, server or desktop. See [08](08-profiles.md).

**Q**

**QEMU.** An emulator. Here it runs the aarch64 chroot while the rootfs is built.

**R**

**RDP (Remote Desktop Protocol).** Microsoft's protocol for remote access to desktops. The phone's GNOME desktop is reachable through it.

**Rootfs.** The file system of the Arch Linux system.

**S**

**SCP sensor hub.** A microcontroller that reads the sensors. `tetris-sensors` talks to it.

**Sleep mode (suspend).** A power-saving state in which the system halts. On this phone it does not work, so it is forbidden.

**SO_PEERPIDFD.** A socket option with which the kernel delivers a pidfd of the peer. Missing in kernel 6.1, which is why we have the patch.

**squeekboard.** The on-screen keyboard for Wayland.

**super.** A partition that contains the Android system partitions (system, vendor ...) as logical partitions.

**switch_root.** The command that switches from the initramfs to the real system.

**systemd.** Linux's start-up and service manager. Process 1 in the finished system.

**T**

**TEE (Trusted Execution Environment).** A protected area of the processor. The fingerprint service needs it.

**tetris-\*.** The prefix for all our own programs and services.

**tetris-mode.** The command for switching profiles. See [08](08-profiles.md#82-the-switches).

**Tux.** Linux's mascot. Shown as the splash at boot.

**U**

**udev.** Linux's device manager. Assigns names and permissions to devices and loads rules.

**UDC (USB device controller).** The USB controller in the device. The initramfs needs it for NCM.

**userdata.** The partition for all your data and for the Arch Linux system.

**V**

**vendor / vendor_boot / vendor_dlkm.** The vendor parts of the system: drivers, firmware, modules. `vendor_boot` holds our initramfs.

**VT (virtual terminal).** A text console. GNOME and logind expect one.

**W**

**Wayland.** The protocol between applications and the compositor. Successor to X11.

**wlroots.** A library for Wayland compositors. Our version 0.20 has the hwcomposer backend.

**X**

**Xwayland.** A compatibility layer that runs old X11 programs under Wayland.

**Z**

**zram.** Compressed swap in memory. In the server profile, for larger models.
