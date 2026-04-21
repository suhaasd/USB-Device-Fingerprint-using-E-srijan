/*
 * probe_leds.c
 *
 * Userspace tool to brute-force scan all vendor request codes (0-255)
 * and all wValue combinations to find which one lights B0-B7 LEDs
 * on the eSrijan DDK v2.1 board (VID=16C0 PID=05DC).
 *
 * Usage:
 *   gcc -o probe_leds probe_leds.c -lusb-1.0
 *   sudo ./probe_leds
 *
 * Watch the board LEDs while this runs. When B0-B7 light up,
 * note the "bRequest=X wValue=Y" printed on screen.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>

#define DDK_VID  0x16C0
#define DDK_PID  0x05DC

/* Known request codes from ddk.h - test these first */
#define RQ_SET_LED_STATUS    1
#define RQ_GET_LED_STATUS    2
#define RQ_SET_REGISTER     10
#define RQ_GET_REGISTER     11

static libusb_device_handle *dev = NULL;

static int send_ctrl(uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
                     unsigned char *data, uint16_t wLength)
{
    return libusb_control_transfer(
        dev,
        LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT,
        bRequest,
        wValue,
        wIndex,
        data,
        wLength,
        500
    );
}

static int send_ctrl_in(uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
                        unsigned char *data, uint16_t wLength)
{
    return libusb_control_transfer(
        dev,
        LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_IN,
        bRequest,
        wValue,
        wIndex,
        data,
        wLength,
        500
    );
}

int main(void)
{
    int r;
    uint8_t bRequest;
    uint16_t wValue;
    unsigned char buf[4];

    r = libusb_init(NULL);
    if (r < 0) { fprintf(stderr, "libusb_init failed\n"); return 1; }

    dev = libusb_open_device_with_vid_pid(NULL, DDK_VID, DDK_PID);
    if (!dev) {
        fprintf(stderr, "DDK board not found. Is it plugged in?\n");
        libusb_exit(NULL);
        return 1;
    }

    libusb_set_auto_detach_kernel_driver(dev, 1);
    r = libusb_claim_interface(dev, 0);
    if (r < 0) {
        fprintf(stderr, "claim_interface failed: %s\n", libusb_strerror(r));
        libusb_close(dev);
        libusb_exit(NULL);
        return 1;
    }

    printf("DDK board found. Starting LED probe...\n");
    printf("Watch the B0-B7 LEDs on the board.\n");
    printf("When they light up, note the bRequest and wValue printed.\n\n");

    /* ----------------------------------------------------------------
     * Phase 1: Test RQ_SET_LED_STATUS (bRequest=1) with every
     *          possible wValue 0x00-0xFF and wIndex 0x00-0xFF
     * ---------------------------------------------------------------- */
    printf("=== Phase 1: bRequest=1 (SET_LED_STATUS), all wValue, wIndex ===\n");
    for (wValue = 0; wValue <= 0xFF; wValue++) {
        printf("bRequest=1 wValue=0x%02X wIndex=0x%02X ... ", wValue, wValue);
        fflush(stdout);
        r = send_ctrl(1, wValue, wValue, NULL, 0);
        printf("ret=%d\n", r);
        usleep(300000); /* 300ms - watch the board */
    }

    /* ----------------------------------------------------------------
     * Phase 2: Test RQ_SET_REGISTER (bRequest=10) - shift register
     *          The 4094N shift register may be driven via this request.
     *          wValue = register address, wIndex = value to write
     * ---------------------------------------------------------------- */
    printf("\n=== Phase 2: bRequest=10 (SET_REGISTER), scanning wValue/wIndex ===\n");
    for (wValue = 0; wValue <= 0x10; wValue++) {
        uint16_t wIndex;
        for (wIndex = 0; wIndex <= 0xFF; wIndex++) {
            printf("bRequest=10 wValue=0x%02X wIndex=0x%02X ... ", wValue, wIndex);
            fflush(stdout);
            r = send_ctrl(10, wValue, wIndex, NULL, 0);
            printf("ret=%d\n", r);
            usleep(200000);
        }
    }

    /* ----------------------------------------------------------------
     * Phase 3: Try sending an 8-bit LED bitmask as a data byte
     *          (some firmware reads the LED pattern from the data stage)
     * ---------------------------------------------------------------- */
    printf("\n=== Phase 3: bRequest=1, LED bitmask in data byte ===\n");
    uint8_t patterns[] = {0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF,
                          0x55, 0xAA, 0xF0, 0x0F};
    int np = sizeof(patterns)/sizeof(patterns[0]);
    for (int i = 0; i < np; i++) {
        buf[0] = patterns[i];
        printf("bRequest=1 wValue=0 data[0]=0x%02X ... ", patterns[i]);
        fflush(stdout);
        r = libusb_control_transfer(
            dev,
            LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT,
            1, 0, 0, buf, 1, 500);
        printf("ret=%d\n", r);
        usleep(500000);
    }

    /* ----------------------------------------------------------------
     * Phase 4: Scan ALL request codes 0-20 with wValue=0xFF (all on)
     * ---------------------------------------------------------------- */
    printf("\n=== Phase 4: All bRequest 0-20, wValue=0xFF ===\n");
    for (bRequest = 0; bRequest <= 20; bRequest++) {
        printf("bRequest=%d wValue=0xFF ... ", bRequest);
        fflush(stdout);
        r = send_ctrl(bRequest, 0xFF, 0, NULL, 0);
        printf("ret=%d\n", r);
        usleep(400000);
    }

    printf("\nProbe complete. Check above output for which bRequest/wValue lit B0-B7.\n");

    libusb_release_interface(dev, 0);
    libusb_close(dev);
    libusb_exit(NULL);
    return 0;
}
