#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "usb/usb_host.h"
#include "usb/usb_types_ch9.h"

#include <M5Unified.h>

static const char *TAG = "ADV_DETECTOR";

enum AppState
{
    STATE_BOOT = 0,
    STATE_USB_STARTING,
    STATE_USB_READY,
    STATE_DEVICE_CONNECTED,
    STATE_DEVICE_OPENED,
    STATE_DEVICE_INFO,
    STATE_DISCONNECTED,
    STATE_ERROR
};

static volatile AppState current_state = STATE_BOOT;
static volatile int current_error = 0;

static volatile bool usb_host_ready = false;
static volatile bool usb_device_present = false;
static volatile bool usb_device_opened = false;

static uint8_t current_device_address = 0;
static uint16_t device_vid = 0;
static uint16_t device_pid = 0;
static uint8_t device_class = 0;
static uint8_t device_subclass = 0;
static uint8_t device_protocol = 0;

static char status_line_1[64] = "";
static char status_line_2[64] = "";
static char status_line_3[64] = "";

static usb_host_client_handle_t usb_client = NULL;
static usb_device_handle_t usb_device = NULL;

#define G0_BUTTON_GPIO GPIO_NUM_0

static void copy_text(
    char *dst,
    size_t dst_size,
    const char *src
)
{
    if (dst == NULL || dst_size == 0)
    {
        return;
    }

    if (src == NULL)
    {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

static void set_status(
    AppState state,
    const char *line1,
    const char *line2,
    const char *line3
)
{
    current_state = state;

    copy_text(status_line_1, sizeof(status_line_1), line1);
    copy_text(status_line_2, sizeof(status_line_2), line2);
    copy_text(status_line_3, sizeof(status_line_3), line3);
}

static void set_error(
    int error_code,
    const char *line1
)
{
    current_error = error_code;
    set_status(
        STATE_ERROR,
        line1,
        "Press G0 to retry",
        "USB detector error"
    );
}

static void draw_screen(void)
{
    char line1[64];
    char line2[64];
    char line3[64];

    copy_text(line1, sizeof(line1), status_line_1);
    copy_text(line2, sizeof(line2), status_line_2);
    copy_text(line3, sizeof(line3), status_line_3);

    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextDatum(middle_center);

    M5.Display.setTextSize(2);
    M5.Display.drawString("ADV USB DETECTOR", M5.Display.width() / 2, 13);
    M5.Display.drawFastHLine(5, 30, M5.Display.width() - 10, TFT_WHITE);

    if (current_state == STATE_ERROR)
    {
        M5.Display.setTextSize(2);
        M5.Display.drawString(line1, M5.Display.width() / 2, 52);

        M5.Display.setTextSize(1);
        M5.Display.drawString(line2, M5.Display.width() / 2, 80);
        M5.Display.drawString(line3, M5.Display.width() / 2, 108);
        return;
    }

    if (current_state == STATE_BOOT)
    {
        M5.Display.setTextSize(2);
        M5.Display.drawString(line1, M5.Display.width() / 2, 52);

        M5.Display.setTextSize(1);
        M5.Display.drawString(line2, M5.Display.width() / 2, 80);
        M5.Display.drawString(line3, M5.Display.width() / 2, 108);
        return;
    }

    if (current_state == STATE_USB_STARTING)
    {
        M5.Display.setTextSize(2);
        M5.Display.drawString("STARTING USB", M5.Display.width() / 2, 52);

        M5.Display.setTextSize(1);
        M5.Display.drawString("Waiting for host stack", M5.Display.width() / 2, 80);
        M5.Display.drawString("D- G19   D+ G20", M5.Display.width() / 2, 108);
        return;
    }

    if (current_state == STATE_USB_READY)
    {
        M5.Display.setTextSize(2);
        M5.Display.drawString("USB HOST READY", M5.Display.width() / 2, 52);

        M5.Display.setTextSize(1);
        M5.Display.drawString("Plug in device", M5.Display.width() / 2, 80);
        M5.Display.drawString("Press G0 to rescan", M5.Display.width() / 2, 108);
        return;
    }

    if (current_state == STATE_DISCONNECTED)
    {
        M5.Display.setTextSize(2);
        M5.Display.drawString("DEVICE REMOVED", M5.Display.width() / 2, 52);

        M5.Display.setTextSize(1);
        M5.Display.drawString("Press G0 to rescan", M5.Display.width() / 2, 80);
        M5.Display.drawString("OTG port ready", M5.Display.width() / 2, 108);
        return;
    }

    if (current_state == STATE_DEVICE_CONNECTED)
    {
        M5.Display.setTextSize(2);
        M5.Display.drawString("DEVICE FOUND", M5.Display.width() / 2, 42);

        M5.Display.setTextSize(1);
        M5.Display.drawString(line1, M5.Display.width() / 2, 64);
        M5.Display.drawString(line2, M5.Display.width() / 2, 86);
        M5.Display.drawString(line3, M5.Display.width() / 2, 108);
        return;
    }

    if (current_state == STATE_DEVICE_OPENED)
    {
        M5.Display.setTextSize(2);
        M5.Display.drawString("DEVICE OPENED", M5.Display.width() / 2, 52);

        M5.Display.setTextSize(1);
        M5.Display.drawString(line1, M5.Display.width() / 2, 80);
        M5.Display.drawString(line2, M5.Display.width() / 2, 104);
        return;
    }

    if (current_state == STATE_DEVICE_INFO)
    {
        char text[64];

        M5.Display.setTextSize(2);
        M5.Display.drawString("USB DEVICE", M5.Display.width() / 2, 36);

        M5.Display.setTextSize(1);

        snprintf(text, sizeof(text), "ADDR: %u", current_device_address);
        M5.Display.drawString(text, M5.Display.width() / 2, 56);

        snprintf(text, sizeof(text), "VID: %04X   PID: %04X", device_vid, device_pid);
        M5.Display.drawString(text, M5.Display.width() / 2, 74);

        snprintf(text, sizeof(text), "CLASS: %02X  SUB: %02X", device_class, device_subclass);
        M5.Display.drawString(text, M5.Display.width() / 2, 92);

        snprintf(text, sizeof(text), "PROTO: %02X", device_protocol);
        M5.Display.drawString(text, M5.Display.width() / 2, 110);
        return;
    }
}

static void reset_device_state(void)
{
    if (usb_device != NULL && usb_client != NULL)
    {
        usb_host_device_close(usb_client, usb_device);
    }

    usb_device = NULL;
    usb_device_present = false;
    usb_device_opened = false;
    current_device_address = 0;
    device_vid = 0;
    device_pid = 0;
    device_class = 0;
    device_subclass = 0;
    device_protocol = 0;
}

static void scan_one_device(uint8_t address)
{
    if (usb_client == NULL)
    {
        set_error(-10, "USB client not ready");
        return;
    }

    usb_device_handle_t device = NULL;
    esp_err_t err = usb_host_device_open(usb_client, address, &device);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "usb_host_device_open failed: %s", esp_err_to_name(err));
        set_error((int)err, "USB open failed");
        return;
    }

    usb_device = device;
    usb_device_opened = true;
    current_device_address = address;

    const usb_device_desc_t *desc = NULL;
    err = usb_host_get_device_descriptor(device, &desc);

    if (err != ESP_OK || desc == NULL)
    {
        ESP_LOGE(TAG, "usb_host_get_device_descriptor failed: %s", esp_err_to_name(err));
        set_error((int)err, "Descriptor read failed");
        return;
    }

    device_vid = desc->idVendor;
    device_pid = desc->idProduct;
    device_class = desc->bDeviceClass;
    device_subclass = desc->bDeviceSubClass;
    device_protocol = desc->bDeviceProtocol;

    char l1[64];
    char l2[64];
    char l3[64];

    snprintf(l1, sizeof(l1), "ADDR: %u", current_device_address);
    snprintf(l2, sizeof(l2), "VID: %04X  PID: %04X", device_vid, device_pid);

    if (device_class == 0x02)
    {
        snprintf(l3, sizeof(l3), "USB COMM DEVICE");
    }
    else if (device_class == 0xFF)
    {
        snprintf(l3, sizeof(l3), "VENDOR SPECIFIC");
    }
    else
    {
        snprintf(l3, sizeof(l3), "CLASS: %02X", device_class);
    }

    set_status(STATE_DEVICE_INFO, l1, l2, l3);
}

static void usb_host_task(void *arg)
{
    set_status(
        STATE_USB_STARTING,
        "STARTING USB",
        "Waiting for host stack",
        "D- G19   D+ G20"
    );

    usb_host_config_t host_config = {};
    host_config.skip_phy_setup = false;
    host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;

    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "usb_host_install failed: %s", esp_err_to_name(err));
        set_error((int)err, "USB host install failed");
        vTaskDelete(NULL);
        return;
    }

    usb_host_client_config_t client_config = {};
    client_config.is_synchronous = true;
    client_config.max_num_event_msg = 0;

    err = usb_host_client_register(&client_config, &usb_client);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "usb_host_client_register failed: %s", esp_err_to_name(err));
        set_error((int)err, "USB client register failed");
        vTaskDelete(NULL);
        return;
    }

    usb_host_device_addr_t addrs[8];
    int num_addrs = 0;

    usb_host_lib_handle_events(portMAX_DELAY, NULL);

    while (true)
    {
        uint32_t flags = 0;
        err = usb_host_lib_handle_events(pdMS_TO_TICKS(20), &flags);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
        {
            ESP_LOGW(TAG, "usb_host_lib_handle_events: %s", esp_err_to_name(err));
        }

        num_addrs = 0;
        err = usb_host_device_addr_list_fill(8, addrs, &num_addrs);

        if (err == ESP_OK)
        {
            if (num_addrs > 0 && !usb_device_present)
            {
                usb_device_present = true;
                set_status(
                    STATE_DEVICE_CONNECTED,
                    "USB DEVICE FOUND",
                    "Reading descriptor...",
                    "Please wait"
                );
               
