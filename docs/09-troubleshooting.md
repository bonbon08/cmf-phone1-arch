# 9. Troubleshooting

**🇬🇧 English** · [🇩🇪 Deutsch](de/09-fehlersuche.md)

This chapter helps when something does not work. It starts with the principle, followed by the tools, the
logs, the typical errors and the way back.

## 9.1 Principle: first determine the stage

A phone starts in stages. The fault is always in one of them. Before you search, determine how far the
phone got:

| What you see or reach | The stage is | Continue with |
|---|---|---|
| nothing. No sign of life, the phone does not get warm | bootloader or hardware | 9.8 |
| the fastboot screen appears, but `fastboot devices` finds nothing | USB or driver on the PC | 9.8 |
| the Tux penguin splash, then nothing. Telnet at `172.16.42.1` answers | initramfs | 9.4 |
| Tux, then nothing. Telnet does not answer | initramfs hangs, often in a module | 9.4 and 9.5 |
| black screen, USB network `172.16.42.1` answers, SSH works | Arch runs, interface missing | 9.6 |
| black screen, SSH does not work | Arch does not start properly | 9.6 and 9.7 |
| interface runs, one function is missing (Wi-Fi, modem, fingerprint ...) | service | 9.7 |

The **black screen after the first boot** is expected in the phone profile. The interface needs the second
stage (`setup-phone.sh`). What matters is whether SSH answers.

## 9.2 Tools

| Tool | From | Use |
|---|---|---|
| `scripts/phone-ssh.sh` | PC | shell on the phone with the project key |
| `scripts/phone-sh.sh 'command'` | PC | command in the initramfs over Telnet (port 23) |
| `journalctl` | phone | systemd log, per service with `-u` |
| `systemctl status <service>` | phone | state of a service and its last lines |
| `systemd-analyze` | phone | start times and the critical chain |
| `dmesg` | phone | kernel log of the current start |
| `lxc-info -n android`, `lxc-attach -n android -- …` | phone | state and commands in the Halium container |
| `scripts/read-flashlog.sh` | PC (Nothing OS with root) | read logs from `nt_reserve2` when the phone no longer boots |

One command that shows the most important things at once when the phone is reachable:

```sh
scripts/phone-ssh.sh 'uptime; systemctl --failed; cat /run/tetris-phosh/mode 2>/dev/null; \
    journalctl -b -p warning --no-pager | tail -40'
```

## 9.3 Finding logs

There are several places. The most important one depends on how far the system got.

| Log | Location | Available when |
|---|---|---|
| **initramfs log** | `/tmp/init.log` (volatile), after `switch_root` in `/var/log/tetris-initramfs.log` | after a start with Arch |
| **log in flash** | partition `nt_reserve2`: start = `init.log`, 16 MB = last `dmesg` | always, even without Arch. Readable with `read-flashlog.sh` or under Nothing OS with root |
| **pstore (`pmsg`)** | `/sys/fs/pstore/pmsg-ramoops-0` | after a reboot, also after a crash (survives a warm reset) |
| **pstore (console, dmesg)** | `/sys/fs/pstore/console-ramoops-0`, `dmesg-ramoops-0` | after a kernel panic |
| **systemd journal** | `journalctl -b` (current start), `journalctl -b -1` (previous) | with Arch |
| **container** | `/var/log/lxc-android.log` | with `tetris-android` |
| **modem start** | `journalctl -u tetris-modem`, `/sys/kernel/ccci/boot` (`md1:4` = started) | with `tetris-modem` |
| **user session** | `journalctl --user` (as `alarm`) | with the interface or desktop |

The initramfs writes its messages to memory (`/tmp/init.log`) and to the partition `nt_reserve2` (see
`initramfs/init`). That is why the log survives even when the phone does not get as far as Arch.

### Reading the log from flash

If the phone does not get as far as the shell, but Nothing OS (slot a) still works, read the log on the PC
with Magisk root:

```sh
scripts/read-flashlog.sh out/flashlog-$(date +%F)
less out/flashlog-$(date +%F)/init.log
less out/flashlog-$(date +%F)/dmesg.txt
```

The script copies the first 16 MB (initramfs log) and 2 MB from 16 MB on (kernel log of the last start).
In it you find the messages `init: load <module>`. The last module loaded shows where the start hangs.

## 9.4 Initramfs: the phone stays in the initramfs

The phone stays in the initramfs if one of these conditions holds:

1. a volume key was held during the first 3 seconds of the boot.
2. The kernel has `tetris.debug=1` (image `debug-vendor_boot-shell.img`).
3. The file `/.tetris-debug` exists on the rootfs.
4. There is no Arch or no `/usr/lib/systemd/systemd` on `userdata`.
5. Mounting `userdata` fails.

In the initramfs you reach the phone over Telnet:

```sh
scripts/phone-sh.sh 'cat /tmp/init.log | tail -30'
scripts/phone-sh.sh 'dmesg | tail -40' 5
scripts/phone-sh.sh 'ls /lib/modules/*/ | head'
```

Typical messages in `init.log`:

| Message | Meaning |
|---|---|
| `init: load <module>` followed by nothing | loading `<module>` hangs. Add the module to `initramfs/modules.blocklist` and rebuild |
| `init: FAIL <module>` | the module could not be loaded (dependency missing or wrong version) |
| `init: nt_reserve2 not found` | the partition does not exist. The log is not saved |
| `init: WLAN NVRAM written` | the Wi-Fi calibration was handed over |
| `init: WLAN NVRAM write failed` | `/dev/wmtWifi` is missing or the write failed. Wi-Fi then does not start |
| `init: UDC=` (empty) | the USB device has no controller. Check the cable or `usb_role` |
| `init: no ext4 rootfs on userdata` | Arch is missing or `userdata` is not ext4 |
| `init: staying in debug initramfs (rootfs incomplete or /.tetris-debug)` | one of the conditions above |

### Emergency: making Arch bootable again

In the initramfs you can mount the rootfs by hand and repair it:

```sh
scripts/phone-sh.sh 'mount -t ext4 /dev/disk/by-partlabel/userdata /newroot && ls /newroot'
```

Check the file system of `userdata` with `e2fsck` (only when it is not mounted). Then `umount /newroot`
and read `/tmp/init.log`.

## 9.5 The initramfs hangs while loading modules

This is the most common error when developing kernels. Proceed as follows:

1. Note the last `init: load …` in the log. That module hangs.
2. Check whether the module should be in `initramfs/modules.blocklist`. The known examples are `apu_top`,
   `apusys` and `sapu` (AI unit).
3. If the module belongs to hardware you do not need at the moment: add it to the blocklist, rebuild the
   initramfs (`build-debug-boot.sh`), flash it (`flash-debug-b.sh`).

If the start succeeds with the blocklist, check the consequences: the function the module would have
provided is then missing.

## 9.6 Arch starts, but the interface is missing

On the phone:

```sh
systemctl --failed                          # which services have failed?
systemctl status tetris-phosh               # interface
journalctl -b -u tetris-phosh --no-pager | tail -60
cat /run/tetris-phosh/mode                  # gpu or sw
cat /etc/tetris-shell 2>/dev/null           # nothing or empty (Phosh)
```

| Symptom in the log | Cause | Remedy |
|---|---|---|
| `tetris-phosh-select: composer HAL not up -> software Phosh` | the composer service in the container is not running | `journalctl -u tetris-android`, `lxc-info -n android`. The container must run before the interface |
| `previous GPU session did not survive 90 s -> software Phosh` | the **crash guard** triggered (see 9.9) | wait, or remove the marker: `sudo rm -f /var/lib/tetris-android/gpu-attempt` |
| the interface exits immediately, error about the layer shell (`layer-shell`) in the log | the wlroots patch is missing (Arch's standard wlroots). The home bar has height 0 and then causes a protocol error | `pacman -Q wlroots0.20`, then `pkgbuilds/build-wlroots-phoc.sh` on the phone |
| interface starts but shows nothing (GPU mode) | graphics over libhybris do not work | for this start only, switch to software mode: `sudo mkdir -p /run/tetris-phosh && sudo touch /run/tetris-phosh/force-sw && sudo systemctl restart tetris-phosh`. Permanently: `sudo rm /etc/tetris-android/gpu-shell` |
| `EGL` or `libhybris` error in the journal | libhybris is missing or does not match the container | run `setup-phone.sh` again, section libhybris |

### Restarting the session without rebooting

```sh
sudo systemctl restart tetris-phosh
```

**Attention:** restarting the service within 90 seconds of the last GPU start triggers the crash guard. The
next interface then starts in software.

## 9.7 A function is missing

### Wi-Fi

```sh
journalctl -b -u tetris-wlan -u tetris-wlan.service --no-pager
ip link show wlan0
nmcli device status
```

- If `wlan0` is missing: the NVRAM calibration did not work. Check the initramfs log for `WLAN NVRAM written`.
  The partition `nvdata` and the file `APCFG/APRDEB/WIFI` must exist.
- Wi-Fi does not connect after the display was off: that is power saving. In the server profile it is off
  (`/etc/NetworkManager/conf.d/zz-tetris-headless.conf`).

### Mobile network (modem)

```sh
journalctl -b -u tetris-modem --no-pager | tail -40
cat /sys/kernel/ccci/boot                   # md1:4 = modem running
mmcli -L                                    # modem found?
mmcli -m 0                                  # status
```

- The modem needs about 35 seconds before it answers. Wait before restarting the service.
- `ccmni1` only appears once `ttyMD0` exists. The udev rule holds the network back until then.
- No data connection although the network is there: `journalctl -u ModemManager`. The plugin
  `libmm-plugin-tetris.so` must match the installed ModemManager version. Rebuild it after an update.
- **Calls do not work.** This is known (see the main README).

### Bluetooth

```sh
journalctl -b -u tetris-bt-bridge --no-pager
bluetoothctl show
```

The script reads the device address from `nvdata` (`APCFG/APRDEB/BT_Addr`). If it is missing, the bridge
does not start.

### Touch, screen, sensors

- **Touch dead after sleep:** check patch `kernel_device_modules/0004` (see
  [07](07-kernel-drivers-patches.md#725-touch-waking-up-after-sleep-kernel_device_modules0004)). A reboot
  helps in the short term.
- **Screen stays dark although the phone runs:** the server switch `tetris-display-off` may be active. Check `tetris-mode`.
- **Sensor does not rotate the screen:** `systemctl status tetris-sensors`. The service must offer
  `net.hadess.SensorProxy`: `busctl --system list | grep -i sensor`.

### Fingerprint

```sh
journalctl -b -u tetris-fp -u tetris-fpd --no-pager | tail -40
sudo tetris-fp enroll                       # enrol again
```

Unlocking only works when a finger is enrolled. The light spot (`tetris-fp-overlay`) needs the layer shell of the interface.

### GPS

`tetris-gnss` delivers NMEA sentences to geoclue as soon as an app asks for the location. The receiver is
only on while an app asks, so a fix is not always immediate. The project's status is "satellites found, first
fix not confirmed" (see the main README).

### Camera

The camera app starts the stack in the container via `tetris-camera`. If the picture is missing, check
`journalctl -u tetris-camera` and `lxc-attach -n android -- logcat -d | tail`.

## 9.8 The phone does not start and is not reachable

If nothing responds, work from the PC:

1. **Fastboot reachable?** Put the device into fastboot mode (Power + Volume down), then
   ```sh
   fastboot devices
   fastboot getvar product           # k6878v1_64
   fastboot getvar current-slot      # a or b
   ```
2. **Slot b does not boot?** Switch back to slot a:
   ```sh
   fastboot --set-active=a && fastboot reboot
   ```
   Nothing OS on slot a should start. Then the cause is on slot b.
3. **Nothing OS does not start (slot a broken)?** Rewrite the original software to both slots with
   `scripts/flash-stock.sh`. This erases the data; read [04-build-and-flash.md](04-build-and-flash.md#410-way-back).
4. **The bootloader is locked?** The phone then only starts signed software. This cannot be undone without
   unlocking the bootloader again (which erases the data). Avoid it.

## 9.9 Crash guard

The GPU mode of the interface is the most sensitive part of the system. The crash guard protects against an
endless loop of crashes:

```
start in GPU mode ──► marker /var/lib/tetris-android/gpu-attempt is created
                      + timer 90 s
                         │
     90 s pass without a crash ──► marker is removed. All good
                         │
     crash or reboot before 90 s ──► marker stays
                         │
     next start ──► "previous GPU session did not survive 90 s"
                         └──► software mode, fixed for this start (/run/tetris-phosh/force-sw)
                              marker is removed. The start after that tries GPU again
```

**Important for your work:**

- A restart of the interface within 90 seconds triggers the crash guard.
- A clean shutdown is not a GPU crash. The service `tetris-gpu-clean-shutdown` removes the marker at shutdown.
- A crash in the desktop profile (GNOME Shell) falls back to software within 60 seconds, for this start only
  (`tetris-gnome-shell`).
- **Force software permanently:** `sudo rm /etc/tetris-android/gpu-shell`. The GPU is then off until you
  create the file again (`sudo touch /etc/tetris-android/gpu-shell`).

## 9.10 Known problems in the project

The main README lists the feature status. Here are the points that often come up during development:

| Problem | Explanation | Status |
|---|---|---|
| **No standby** | the SoC hangs in deep sleep. The project forbids sleep (`sleep.conf.d/50-tetris-nosuspend.conf`). The screen can be off, the CPU stays awake | open |
| **Calls without sound, incoming calls do not arrive** | the audio HAL and the MIPC channel are only partly understood. See `rootfs/src/tetris-voicecall` and `tetris-mipc` | open |
| **GPS fix uncertain** | satellites are found, a fix is not confirmed | partly |
| **GTK 4 apps crash on libhybris** | when Android threads end, apps such as Chats, Calls and Camera can crash. That is why they render on the processor (drop-ins in `overlay/etc/systemd/user/`) | workaround |
| **GDM tries a local login** | in the desktop profile GDM tries three times to show a login screen. `tetris-display-off` holds the panel, so nothing is visible | known |
| **`depmod` over all modules** | duplicate exports make the kernel pick the wrong modules. Outside the initramfs, load modules with `insmod` | rule |

### Rule: do not run `depmod` over the whole set

The project has modules with duplicate symbols. A `depmod` over the entire set (`/usr/lib/modules`) can write
the wrong dependencies. The result is a module that loads a different version. Load modules one by one with
`insmod`, as `tetris-torch` does.

## 9.11 Common build errors

See [04-build-and-flash.md](04-build-and-flash.md#412-common-build-errors).

## 9.12 Reporting a fault

A good bug report contains:

1. **The stage** (9.1): how far did the phone get?
2. **The profile** (`tetris-mode`) and the interface (`cat /etc/tetris-shell`).
3. **The latest logs:** `journalctl -b -p warning` or the initramfs log.
4. **The steps before:** what you flashed, built or changed.
5. **The repository state:** `git log -1 --oneline` and `git status` on the PC.
6. **No serial numbers, IMEI, keys or passwords.** Redact `ip addr`, `cat /etc/machine-id` and the contents of `out/`.
