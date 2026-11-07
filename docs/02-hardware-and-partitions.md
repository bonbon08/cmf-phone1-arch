# 2. The device and its partitions

**🇬🇧 English** · [🇩🇪 Deutsch](de/02-geraet-und-partitionen.md)

This chapter describes the hardware of the CMF Phone 1 and the storage the project accesses.
It is required reading before the first flash, because partition mistakes are among the most
consequential mistakes possible: they can make a phone unusable or erase all data.

## 2.1 The hardware

| Component | Device | Meaning for the project |
|---|---|---|
| Code name | **Tetris** (`tetris-*`) | Prefix for our own programs, services and files |
| Manufacturer / model | Nothing, **CMF Phone 1** | |
| Product identifier | `k6878v1_64` | The flash script checks it before writing anything |
| SoC (processor chip) | MediaTek **Dimensity 7300** (MT6878) | 4 × Cortex-A78 (fast) and 4 × Cortex-A55 (efficient), ARM64 |
| Memory | about **7.2 GiB** usable | Enough for a desktop; tight for larger AI models |
| GPU | **Mali-G615** (2 cores) | Graphics. Only usable through the Android driver, hence libhybris |
| Display | Display with fingerprint sensor under the glass | Fingerprint via `tetris-fp` (enrolment and sensor), unlocking the lock screen via `tetris-fpd` |
| Mobile network | MediaTek modem (CCCI, inside the Android container) | Mobile data and SMS work, calls do not yet |
| Wi-Fi / Bluetooth | MediaTek combo chip (connac2, `conninfra`) | Driver `gen4m` for Wi-Fi, `mt66xx` for Bluetooth |
| Sensors | SCP sensor hub | Acceleration, light, proximity via `tetris-sensors` |
| Camera | 50 MP main camera, front camera, IR depth sensor | Only through the Android container (`tetris-camera`) |
| Audio | MT6369 codec with Awinic amplifier | Configuration in `rootfs/overlay/usr/share/alsa/ucm2` |
| Vibration | Motor with force feedback | `tetris-vibra` |
| Torch | LED flash | `tetris-torch` (via `uleds`) |
| GPS | MediaTek GNSS | `tetris-gnss` (via the Android container) |
| SIM | Nano SIM | Mobile network via ModemManager (SIM PIN, data connection, SMS) |
| NFC | not present | |

The figures come from the device data in the folder `stock-info/` (`cpuinfo.txt`,
`meminfo.txt`, `dt-model.txt`), which were saved from the original system. That folder is not in
Git.

### The original operating system

The phone ships with **Nothing OS**, an Android version. The firmware version this project uses
is **B4.1** (build `Tetris_B4.1-260928-1140`). All files of this build are local in the folder
`firmware/`. They are proprietary, which is why they are not in Git.

The kernel of the original system is Linux **6.1** (Android edition). Our kernel is based on
Nothing's open sources (`NothingOSS`) and has the same major version. This matters because driver
modules only fit the kernel they were built with.

## 2.2 What is a partition?

The phone's flash memory is divided into **partitions**. Each has a name (`boot_b`, `userdata`
...) and a fixed size. A flash operation writes to one of these partitions. A partition name is
just a name in the bootloader. The phone finds a partition by this name, not by a fixed number.

The list of partitions with their internal block devices is in `stock-info/by-name.txt`. Some
important examples (block devices as seen on the original system):

| Partition | Block device | Content |
|---|---|---|
| `preloader` | fixed in the chip | first start-up code, do not change |
| `lk_a`, `lk_b` | `sdc35`, `sdc65` | bootloader LK |
| `boot_a`, `boot_b` | `sdc36`, `sdc66` | kernel image |
| `init_boot_a/b` | `sdc38`, `sdc68` | ramdisk for Android (8 MB) |
| `vendor_boot_a/b` | `sdc37`, `sdc67` | device driver ramdisk (64 MB), for us the initramfs |
| `dtbo_a/b` | `sdc39`, `sdc72` | device overlays (device tree) |
| `vbmeta_a/b` | `sdc10`, `sdc13` | signature data for verified boot (AVB) |
| `modem_a/b` | `sdc22`, `sdc52` | modem firmware |
| `nvdata`, `nvcfg`, `nvram`, `protect1/2`, `persist` | `sdc7`, `sdc9` and others | calibration, IMEI, keys. **Never erase** |
| `misc` | `sdc1` | boot control: A/B slot state (retry counter) |
| `metadata` | `sdc16` | management data for data encryption |
| `super` | `sdc81` | container for the Android system partitions (dynamic) |
| `nt_reserve1`, `nt_reserve2` | `sdc79`, `sdc80` | reserve. For us: `nt_reserve2` stores the initramfs log |
| `userdata` | `sdc82` | your data; the Arch Linux rootfs lives here too |

The `sdc…` numbers are internal device names of the storage. They can change between firmware
versions; the names (`userdata`, `boot_b` ...) stay the same.

## 2.3 The A/B slots

Many partitions exist **twice**: `boot_a` and `boot_b`, `vendor_boot_a` and `vendor_boot_b`, and so
on. This is called **A/B slots**.

- Slot **a** contains the original system (Nothing OS). This project leaves it untouched.
- Slot **b** contains our kernel and the initramfs. `fastboot --set-active=b` makes it the boot slot.

The advantage: if slot b does not boot, you can switch back to a. For this there is a counter for
failed attempts in `misc`. When the system starts successfully, the service `tetris-boot-success`
reports it and resets the counter. This corresponds to Android's `markBootSuccessful()` mechanism.

**Important:** the way back to slot a only works as long as Nothing OS is complete there. The script
`scripts/flash-stock.sh` rewrites both slots and is therefore the safe way out (see
[04-build-and-flash.md](04-build-and-flash.md#410-way-back)).

## 2.4 The dynamic super layout

Android does not create its system partitions (`system`, `vendor`, `product`, `odm` ...) as fixed
partitions. Instead they live in a single partition called **`super`**. Inside `super` are logical
partitions. This allows updates without repartitioning.

For us this matters for two reasons:

1. The **Halium container** needs the `vendor`, `odm` and `vendor_dlkm` partitions of the original
   system (drivers, firmware). They are extracted from `super` when the container starts, using
   `tetris-map-super` (see [03-architecture.md](03-architecture.md#33-the-halium-container)).
2. When flashing in **fastbootd** mode, the logical partitions are recreated
   (`create-logical-partition`, `wipe-super`). For that the bootloader must switch into fastbootd
   mode, which is why the script reboots into `fastboot` first.

## 2.5 The partitions we write to

The project writes to only a few partitions. Each change here has a specific effect:

| Partition | Writes | With | Effect on errors |
|---|---|---|---|
| `boot_b` | `out/debug-boot.img` | `flash-debug-b.sh` | phone does not boot via slot b |
| `vendor_boot_b` | `out/debug-vendor_boot.img` | `flash-debug-b.sh` | no drivers, no start |
| `init_boot_b` | `out/debug-init_boot.img` (empty) | `flash-debug-b.sh` | harmless, since it is empty |
| `userdata` | `out/rootfs.simg` | `fastboot flash userdata` | **erases all data** |
| `vbmeta_a/b`, `vbmeta_system/vendor` | patched (AVB off) | `flash-stock.sh` | with AVB disabled the bootloader accepts our kernel. With AVB only original images |
| `metadata` | empty | `flash-stock.sh` | the encryption management is reset, data is gone |
| all slots a and b | original images | `flash-stock.sh` | **complete return to Nothing OS** |

## 2.6 What you should never do

- **Do not unlock or re-lock the device** without a plan. A locked bootloader only starts signed
  software. A modified system then stays unusable. This project requires an **unlocked** bootloader.
- **Do not overwrite partitions such as `nvdata`, `nvcfg`, `protect1`, `protect2`, `persist` or
  `nvram`.** They contain the IMEI and the calibration data of the radio. Without them the phone has
  no valid radio profile. Back these partitions up before you experiment. The project has made
  backups in the folder `out/modem-backup/` (local).
- **Do not run `fastboot erase` on partitions you do not understand.**

## 2.7 Storage after the first boot

On the first boot the service `tetris-growfs` enlarges the file system to the full size of
`userdata`. The image itself is small (the script `build-rootfs.sh` shrinks it with
`resize2fs -M`) so that flashing is fast.

| Part | Size | Origin |
|---|---|---|
| `userdata` | size of the partition, the rest of the storage | from the device |
| Rootfs image | default `10G` (upper limit), then shrunk | `build-rootfs.sh` (environment variable `SIZE`) |
| `nt_reserve2` | 64 MB | `initramfs/init` writes the log (`init.log`, `dmesg`) |

## 2.8 Checking the device data

If the phone is connected to the PC in fastboot mode, first check the product identifier:

```sh
fastboot getvar product
# expected: product: k6878v1_64
```

The flash script `flash-stock.sh` performs this check as well. If it does not match, the script
aborts.

If the phone already runs Arch Linux, you reach it over USB:

```sh
ssh alarm@172.16.42.1        # password: alarm (please change it: passwd)
cat /proc/device-tree/model  # device name
```

On the phone you can list partition labels with `ls -l /dev/disk/by-partlabel/`.
