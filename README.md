# USB Device Fingerprinter — eSrijan DDK v2.1

## Authors

Sriram K, Samarth M, Vaishnav P S, Suhaas D

## Overview

Linux kernel module that fingerprints every USB device plugged into the host,
classifies it as KNOWN / UNKNOWN / SUSPICIOUS, and signals the trust level by
blinking the **PGM (Program Running) LED** on the eSrijan DDK v2.1 board via
USB vendor control messages.

Operates entirely in kernel space — no userspace daemon or polling loop required.

## Hardware Platform

| Component       | Details                                   |
|-----------------|-------------------------------------------|
| Board           | eSrijan DDK v2.1                          |
| Microcontroller | LPC3250 (ARM9)                            |
| Firmware        | LDDK v2.2                                 |
| USB VID:PID     | 16C0:05DC (Van Ooijen Technische Informatica) |
| LED Controlled  | PGM (Program Running) LED                 |
| Host System     | Ubuntu 24.04, Kernel 6.17.0-20-generic    |

## PGM LED Blink Patterns

The PGM LED is controlled via `CUSTOM_RQ_SET_LED_STATUS` (bRequest=1):
`wValue=1` → ON, `wValue=0` → OFF.

| Trust Level | Device (example)           | PGM Blink Pattern        |
|-------------|----------------------------|--------------------------|
| KNOWN       | Logitech G502 SE HERO Mouse | 3 slow blinks — 300ms   |
| UNKNOWN     | SanDisk Ultra Flair         | 5 medium blinks — 150ms |
| SUSPICIOUS  | Google Pixel/Nexus MTP      | 10 rapid blinks — 80ms  |
| Disconnected| —                           | PGM turned OFF           |

Any device not in the hardcoded table defaults to **UNKNOWN**.

## Trust Classification

Devices are matched against a kernel-side table by VID:PID:

| Trust      | VID:PID   | Device                      |
|------------|-----------|-----------------------------|
| KNOWN      | 046D:C08B | Logitech G502 SE HERO Mouse |
| UNKNOWN    | 0781:5591 | SanDisk Ultra Flair         |
| SUSPICIOUS | 18D1:4EE1 | Google Pixel/Nexus MTP      |

To add a device, edit the `device_list[]` array in `usb_fingerprinter.c`:

    static const struct { u16 vid; u16 pid; trust_t trust; char name[48]; }
    device_list[] = {
        { 0x046d, 0xc08b, TRUST_KNOWN,      "Logitech G502 SE HERO Mouse" },
        { 0x0781, 0x5591, TRUST_UNKNOWN,    "SanDisk Ultra Flair"         },
        { 0x18d1, 0x4ee1, TRUST_SUSPICIOUS, "Google Pixel/Nexus MTP"      },
        { 0xYOUR, 0xVID,  TRUST_KNOWN,      "your device"                 },
    };

Find your device's VID:PID with: `lsusb`

## Five Kernel Components Demonstrated

1. **/proc entry** — `proc_create` + `seq_file` → `/proc/usb_fingerprint`
2. **Kernel timer** — `mod_timer` fires every 30s to reap stale registry entries
3. **Work queue** — `alloc_workqueue` / `queue_work` defers classify + LED blink out of atomic context
4. **Delays** — `msleep(100)` settle delay after USB connect; `msleep` for blink on/off timing
5. **USB subsystem** — `usb_register_notify` hooks connect/disconnect events; `usb_control_msg` drives the PGM LED

## Prerequisites

- Ubuntu 24.04, kernel 6.17.0-20-generic
- `linux-headers-$(uname -r)` installed
- eSrijan DDK v2.1 board connected via USB

## Build

    make clean && make

## Load

    # Plug in the DDK board FIRST, then load the module
    sudo insmod usb_fingerprinter.ko
    sudo dmesg | tail -5

Expected output:

    fp: DDK board connected
    fp: USB Fingerprinter loaded
    fp: KNOWN=3x300ms | UNKNOWN=5x150ms | SUSPICIOUS=10x80ms

## Test

    # Plug in the Logitech mouse  -> 3 slow blinks   (300ms)
    # Plug in the SanDisk drive   -> 5 medium blinks  (150ms)
    # Plug in the Google Pixel    -> 10 rapid blinks  (80ms)

    cat /proc/usb_fingerprint   # view live device registry
    sudo dmesg | tail -10       # view kernel log

Sample `/proc/usb_fingerprint` output:

    VID    PID    Class  Speed              Trust       Device
    --------------------------------------------------------------
    046D   C08B   00     High (480 Mbps)    KNOWN       Logitech G502 SE HERO Mouse
    0781   5591   00     Super(5   Gbps)    UNKNOWN     SanDisk Ultra Flair
    18D1   4EE1   00     High (480 Mbps)    SUSPICIOUS  Google Pixel/Nexus MTP

## Unload

    sudo rmmod usb_fingerprinter

## File Structure

| File                  | Purpose                                                         |
|-----------------------|-----------------------------------------------------------------|
| `usb_fingerprinter.c` | Main kernel module — all five components implemented here       |
| `Makefile`            | Builds `usb_fingerprinter.ko` against the running kernel headers |
| `ddk.h`               | DDK board definitions — VID, PID, vendor request codes          |
| `ddk_led.h`           | DDK LED ioctl definitions (DDK_LED_GET, DDK_LED_SET)            |
| `ddk_io.h`            | DDK I/O register definitions (RegId enum, ddk_reg_access)       |
| `probe_leds.c`        | Libusb userspace tool used to identify the PGM LED request code |

## Kernel 6.17 Compatibility Notes

| Issue                  | Old API                       | Fixed API                              |
|------------------------|-------------------------------|----------------------------------------|
| Timer teardown         | `del_timer_sync()`            | `timer_delete_sync()` — renamed in 6.17 |
| Workqueue flush        | `flush_scheduled_work()`      | `drain_workqueue(fp_wq)` on dedicated WQ |
| Notify return value    | `ret = usb_register_notify()` | `usb_register_notify()` returns void in 6.x |
