/*
 * usb_fingerprinter.c
 *
 * Linux kernel module for the eSrijan DDK v2.1 board.
 *
 * On USB device connect: classifies as KNOWN/UNKNOWN/SUSPICIOUS and
 * blinks the PGM LED with a trust-specific pattern via
 * CUSTOM_RQ_SET_LED_STATUS (bRequest=1):
 *   wValue=1 -> PGM ON
 *   wValue=0 -> PGM OFF
 *
 * Blink patterns:
 *   KNOWN      ->  3 slow blinks   (300ms on/off)  - Logitech G502 Mouse
 *   UNKNOWN    ->  5 medium blinks (150ms on/off)  - SanDisk Ultra Flair
 *   SUSPICIOUS -> 10 rapid blinks  ( 80ms on/off)  - Google Pixel (MTP)
 *
 * Hardcoded device fingerprints:
 *   KNOWN      : 046d:c08b  Logitech G502 SE HERO Gaming Mouse
 *   UNKNOWN    : 0781:5591  SanDisk Ultra Flair
 *   SUSPICIOUS : 18d1:4ee1  Google Pixel / Nexus (MTP mode)
 *
 * On disconnect: PGM turned off.
 * Also exposes /proc/usb_fingerprint for a live device registry.
 *
 * Required kernel components:
 *   1. /proc entry    (proc_create / seq_file)
 *   2. Kernel timer   (mod_timer - stale-entry reaper)
 *   3. Work queue     (INIT_WORK / queue_work - deferred classify + blink)
 *   4. Delays         (msleep - blink timing + settle delay)
 *   5. USB subsystem  (usb_register_notify + usb_control_msg for PGM LED)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>

/* ------------------------------------------------------------------ */
/*  DDK board identity                                                 */
/* ------------------------------------------------------------------ */
#define DDK_VENDOR_ID            0x16c0
#define DDK_PRODUCT_ID           0x05dc
#define CUSTOM_RQ_SET_LED_STATUS 1

#define PGM_ON   1
#define PGM_OFF  0

/* ------------------------------------------------------------------ */
/*  Blink patterns per trust level                                     */
/* ------------------------------------------------------------------ */
#define BLINK_KNOWN_COUNT         3
#define BLINK_KNOWN_DELAY_MS    300    /* slow  - trusted device      */

#define BLINK_UNKNOWN_COUNT       5
#define BLINK_UNKNOWN_DELAY_MS  150    /* medium - unverified device  */

#define BLINK_SUSPICIOUS_COUNT   10
#define BLINK_SUSPICIOUS_DELAY_MS 80   /* rapid  - risky device       */

/* ------------------------------------------------------------------ */
/*  Trust classification                                               */
/* ------------------------------------------------------------------ */
typedef enum { TRUST_KNOWN, TRUST_UNKNOWN, TRUST_SUSPICIOUS } trust_t;

/*
 * Hardcoded device fingerprint table.
 * Every device gets an explicit trust label.
 * Any device NOT in this table defaults to TRUST_UNKNOWN.
 *
 * To add a device: run `lsusb`, note the ID xxxx:yyyy, add a row below.
 */
static const struct {
    u16     vid;
    u16     pid;
    trust_t trust;
    char    name[48];
} device_list[] = {
    /* --- KNOWN: fully trusted peripherals --- */
    { 0x046d, 0xc08b, TRUST_KNOWN,      "Logitech G502 SE HERO Mouse"    },

    /* --- UNKNOWN: allowed but unverified --- */
    { 0x0781, 0x5591, TRUST_UNKNOWN,    "SanDisk Ultra Flair"            },

    /* --- SUSPICIOUS: high-risk devices --- */
    { 0x18d1, 0x4ee1, TRUST_SUSPICIOUS, "Google Pixel/Nexus MTP"         },
};

/* ------------------------------------------------------------------ */
/*  Device registry                                                    */
/* ------------------------------------------------------------------ */
#define MAX_DEVICES       16
#define STALE_TIMEOUT_SEC 30

struct fp_entry {
    u16     vid;
    u16     pid;
    u8      dev_class;
    u8      speed;
    trust_t trust;
    char    name[48];
    unsigned long last_seen;
    int     active;
};

static struct fp_entry registry[MAX_DEVICES];
static DEFINE_SPINLOCK(reg_lock);

/* DDK board usb_device pointer */
static struct usb_device *led_udev = NULL;
static DEFINE_MUTEX(led_mutex);

/* Dedicated workqueue */
static struct workqueue_struct *fp_wq;

/* ------------------------------------------------------------------ */
/*  Work items                                                         */
/* ------------------------------------------------------------------ */
struct fp_work {
    struct work_struct work;
    struct usb_device *udev;
};

static struct work_struct led_off_work;

/* ------------------------------------------------------------------ */
/*  String helpers                                                     */
/* ------------------------------------------------------------------ */
static const char *trust_str(trust_t t)
{
    switch (t) {
    case TRUST_KNOWN:      return "KNOWN";
    case TRUST_SUSPICIOUS: return "SUSPICIOUS";
    default:               return "UNKNOWN";
    }
}

static const char *speed_str(u8 s)
{
    switch (s) {
    case USB_SPEED_LOW:   return "Low  (1.5 Mbps)";
    case USB_SPEED_FULL:  return "Full (12  Mbps)";
    case USB_SPEED_HIGH:  return "High (480 Mbps)";
    case USB_SPEED_SUPER: return "Super(5   Gbps)";
    default:              return "Unknown";
    }
}

/* ------------------------------------------------------------------ */
/*  PGM LED control                                                    */
/* ------------------------------------------------------------------ */
static int pgm_set(u16 on)
{
    if (!led_udev)
        return -ENODEV;
    return usb_control_msg(
        led_udev,
        usb_sndctrlpipe(led_udev, 0),
        CUSTOM_RQ_SET_LED_STATUS,
        USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
        on,
        0,
        NULL, 0,
        1000
    );
}

static void pgm_blink(int count, int delay_ms)
{
    int i;
    mutex_lock(&led_mutex);
    if (!led_udev) { mutex_unlock(&led_mutex); return; }
    pgm_set(PGM_OFF);
    msleep(delay_ms);
    for (i = 0; i < count; i++) {
        pgm_set(PGM_ON);
        msleep(delay_ms);
        pgm_set(PGM_OFF);
        msleep(delay_ms);
    }
    mutex_unlock(&led_mutex);
}

static void pgm_off(void)
{
    mutex_lock(&led_mutex);
    if (led_udev)
        pgm_set(PGM_OFF);
    mutex_unlock(&led_mutex);
}

/* ------------------------------------------------------------------ */
/*  Classification — looks up device_list, defaults to UNKNOWN        */
/* ------------------------------------------------------------------ */
static trust_t classify(u16 vid, u16 pid, const char **name_out)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(device_list); i++) {
        if (device_list[i].vid == vid && device_list[i].pid == pid) {
            *name_out = device_list[i].name;
            return device_list[i].trust;
        }
    }
    *name_out = "Unknown Device";
    return TRUST_UNKNOWN;
}

/* ------------------------------------------------------------------ */
/*  Registry helpers                                                   */
/* ------------------------------------------------------------------ */
static void registry_add(u16 vid, u16 pid, u8 dev_class, u8 speed,
                          trust_t trust, const char *name)
{
    int i;
    unsigned long flags;
    spin_lock_irqsave(&reg_lock, flags);
    for (i = 0; i < MAX_DEVICES; i++) {
        if (!registry[i].active) {
            registry[i].vid       = vid;
            registry[i].pid       = pid;
            registry[i].dev_class = dev_class;
            registry[i].speed     = speed;
            registry[i].trust     = trust;
            registry[i].last_seen = jiffies;
            registry[i].active    = 1;
            strscpy(registry[i].name, name, sizeof(registry[i].name));
            break;
        }
    }
    spin_unlock_irqrestore(&reg_lock, flags);
}

static void registry_remove(u16 vid, u16 pid)
{
    int i;
    unsigned long flags;
    spin_lock_irqsave(&reg_lock, flags);
    for (i = 0; i < MAX_DEVICES; i++) {
        if (registry[i].active &&
            registry[i].vid == vid && registry[i].pid == pid) {
            registry[i].active = 0;
            break;
        }
    }
    spin_unlock_irqrestore(&reg_lock, flags);
}

/* ------------------------------------------------------------------ */
/*  Work queue: LED off on disconnect                                  */
/* ------------------------------------------------------------------ */
static void fp_led_off_work(struct work_struct *work)
{
    pgm_off();
    printk(KERN_INFO "fp: PGM off (device disconnected)\n");
}

/* ------------------------------------------------------------------ */
/*  Work queue: classify + blink PGM                                  */
/* ------------------------------------------------------------------ */
static void fp_classify_work(struct work_struct *work)
{
    struct fp_work *fw = container_of(work, struct fp_work, work);
    struct usb_device *udev = fw->udev;
    u16 vid, pid;
    u8  dev_class, speed;
    trust_t trust;
    const char *name;
    int count, delay_ms;

    msleep(100); /* settle delay */

    vid       = le16_to_cpu(udev->descriptor.idVendor);
    pid       = le16_to_cpu(udev->descriptor.idProduct);
    dev_class = udev->descriptor.bDeviceClass;
    speed     = udev->speed;

    trust = classify(vid, pid, &name);

    printk(KERN_INFO "fp: [%s] %s VID=%04X PID=%04X Class=%02X Speed=%d\n",
           trust_str(trust), name, vid, pid, dev_class, speed);

    registry_add(vid, pid, dev_class, speed, trust, name);

    switch (trust) {
    case TRUST_KNOWN:
        count    = BLINK_KNOWN_COUNT;
        delay_ms = BLINK_KNOWN_DELAY_MS;
        break;
    case TRUST_SUSPICIOUS:
        count    = BLINK_SUSPICIOUS_COUNT;
        delay_ms = BLINK_SUSPICIOUS_DELAY_MS;
        break;
    default:
        count    = BLINK_UNKNOWN_COUNT;
        delay_ms = BLINK_UNKNOWN_DELAY_MS;
        break;
    }

    printk(KERN_INFO "fp: PGM -> %d blinks @ %dms\n", count, delay_ms);
    pgm_blink(count, delay_ms);

    kfree(fw);
}

/* ------------------------------------------------------------------ */
/*  USB notifier                                                       */
/* ------------------------------------------------------------------ */
static int fp_usb_notify(struct notifier_block *nb,
                         unsigned long action, void *data)
{
    struct usb_device *udev = data;
    struct fp_work *fw;

    switch (action) {
    case USB_DEVICE_ADD:
        if (le16_to_cpu(udev->descriptor.idVendor)  == DDK_VENDOR_ID &&
            le16_to_cpu(udev->descriptor.idProduct) == DDK_PRODUCT_ID)
            break;
        fw = kmalloc(sizeof(*fw), GFP_ATOMIC);
        if (!fw) { printk(KERN_ERR "fp: kmalloc failed\n"); break; }
        fw->udev = udev;
        INIT_WORK(&fw->work, fp_classify_work);
        queue_work(fp_wq, &fw->work);
        break;

    case USB_DEVICE_REMOVE:
        if (le16_to_cpu(udev->descriptor.idVendor)  == DDK_VENDOR_ID &&
            le16_to_cpu(udev->descriptor.idProduct) == DDK_PRODUCT_ID)
            break;
        registry_remove(
            le16_to_cpu(udev->descriptor.idVendor),
            le16_to_cpu(udev->descriptor.idProduct));
        queue_work(fp_wq, &led_off_work);
        break;
    }
    return NOTIFY_OK;
}

static struct notifier_block fp_nb = { .notifier_call = fp_usb_notify };

/* ------------------------------------------------------------------ */
/*  Stale-entry reaper timer                                           */
/* ------------------------------------------------------------------ */
static struct timer_list stale_timer;

static void reap_stale_entries(struct timer_list *t)
{
    int i;
    unsigned long flags;
    unsigned long cutoff = jiffies - msecs_to_jiffies(STALE_TIMEOUT_SEC * 1000);

    spin_lock_irqsave(&reg_lock, flags);
    for (i = 0; i < MAX_DEVICES; i++) {
        if (registry[i].active && time_before(registry[i].last_seen, cutoff)) {
            printk(KERN_INFO "fp: reaping stale VID=%04X PID=%04X\n",
                   registry[i].vid, registry[i].pid);
            registry[i].active = 0;
        }
    }
    spin_unlock_irqrestore(&reg_lock, flags);
    mod_timer(&stale_timer,
              jiffies + msecs_to_jiffies(STALE_TIMEOUT_SEC * 1000));
}

/* ------------------------------------------------------------------ */
/*  /proc/usb_fingerprint                                              */
/* ------------------------------------------------------------------ */
static int fp_proc_show(struct seq_file *m, void *v)
{
    int i;
    unsigned long flags;

    seq_puts(m, "VID    PID    Class  Speed              Trust       Device\n");
    seq_puts(m, "--------------------------------------------------------------\n");

    spin_lock_irqsave(&reg_lock, flags);
    for (i = 0; i < MAX_DEVICES; i++) {
        if (registry[i].active)
            seq_printf(m, "%04X   %04X   %02X     %-18s %-11s %s\n",
                       registry[i].vid, registry[i].pid,
                       registry[i].dev_class,
                       speed_str(registry[i].speed),
                       trust_str(registry[i].trust),
                       registry[i].name);
    }
    spin_unlock_irqrestore(&reg_lock, flags);
    return 0;
}

static int fp_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, fp_proc_show, NULL);
}

static const struct proc_ops fp_proc_ops = {
    .proc_open    = fp_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* ------------------------------------------------------------------ */
/*  DDK board USB driver                                               */
/* ------------------------------------------------------------------ */
static int led_probe(struct usb_interface *iface, const struct usb_device_id *id)
{
    mutex_lock(&led_mutex);
    led_udev = interface_to_usbdev(iface);
    mutex_unlock(&led_mutex);
    printk(KERN_INFO "fp: DDK board connected\n");
    pgm_off();
    return 0;
}

static void led_disconnect(struct usb_interface *iface)
{
    mutex_lock(&led_mutex);
    led_udev = NULL;
    mutex_unlock(&led_mutex);
    printk(KERN_INFO "fp: DDK board disconnected\n");
}

static struct usb_device_id led_table[] = {
    { USB_DEVICE(DDK_VENDOR_ID, DDK_PRODUCT_ID) },
    {}
};
MODULE_DEVICE_TABLE(usb, led_table);

static struct usb_driver led_driver = {
    .name       = "fp_led_ctrl",
    .probe      = led_probe,
    .disconnect = led_disconnect,
    .id_table   = led_table,
};

/* ------------------------------------------------------------------ */
/*  Module init / exit                                                 */
/* ------------------------------------------------------------------ */
static int __init fp_init(void)
{
    int ret;

    memset(registry, 0, sizeof(registry));

    fp_wq = alloc_workqueue("fp_wq", WQ_UNBOUND, 0);
    if (!fp_wq) {
        printk(KERN_ERR "fp: alloc_workqueue failed\n");
        return -ENOMEM;
    }

    INIT_WORK(&led_off_work, fp_led_off_work);

    ret = usb_register(&led_driver);
    if (ret) {
        printk(KERN_ERR "fp: usb_register failed: %d\n", ret);
        destroy_workqueue(fp_wq);
        return ret;
    }

    if (!proc_create("usb_fingerprint", 0444, NULL, &fp_proc_ops)) {
        printk(KERN_ERR "fp: proc_create failed\n");
        usb_deregister(&led_driver);
        destroy_workqueue(fp_wq);
        return -ENOMEM;
    }

    timer_setup(&stale_timer, reap_stale_entries, 0);
    mod_timer(&stale_timer,
              jiffies + msecs_to_jiffies(STALE_TIMEOUT_SEC * 1000));

    usb_register_notify(&fp_nb);

    printk(KERN_INFO "fp: USB Fingerprinter loaded\n");
    printk(KERN_INFO "fp: KNOWN=%dx%dms | UNKNOWN=%dx%dms | SUSPICIOUS=%dx%dms\n",
           BLINK_KNOWN_COUNT,      BLINK_KNOWN_DELAY_MS,
           BLINK_UNKNOWN_COUNT,    BLINK_UNKNOWN_DELAY_MS,
           BLINK_SUSPICIOUS_COUNT, BLINK_SUSPICIOUS_DELAY_MS);
    return 0;
}

static void __exit fp_exit(void)
{
    usb_unregister_notify(&fp_nb);
    timer_delete_sync(&stale_timer);
    drain_workqueue(fp_wq);
    destroy_workqueue(fp_wq);
    remove_proc_entry("usb_fingerprint", NULL);
    pgm_off();
    usb_deregister(&led_driver);
    printk(KERN_INFO "fp: USB Fingerprinter unloaded\n");
}

module_init(fp_init);
module_exit(fp_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Suhaas");
MODULE_DESCRIPTION("USB Fingerprinter with PGM LED blink patterns - eSrijan DDK v2.1");
