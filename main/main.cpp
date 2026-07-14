#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"

#include "driver/gpio.h"

#include "usb/usb_host.h"
#include "usb/usb_types_ch9.h"

#include <M5Unified.h>

static const char *TAG = "ADV_USB_DETECT";

enum AppState
{
    STATE_BOOT,
    STATE_USB_STARTING,
    STATE_USB_READY,
    STATE_DEVICE_FOUND,
    STATE_DEVICE_INFO,
    STATE_DEVICE_REMOVED,
    STATE_ERROR
};

static volatile AppState current_state = STATE_BOOT;

static usb_host_client_handle_t usb_client = NULL;
static usb_device_handle_t usb_device = NULL;

static volatile bool device_connected = false;

static uint8_t device_address = 0;

static uint16_t device_vid = 0;
static uint16_t device_pid = 0;

static uint8_t device_class = 0;
static uint8_t device_subclass = 0;
static uint8_t device_protocol = 0;

static char device_name[64] = "UNKNOWN USB DEVICE";

#define G0_GPIO GPIO_NUM_0

static void identify_usb_device()
{
    strcpy(device_name, "UNKNOWN USB DEVICE");

    if (device_vid == 0x10C4)
    {
        strcpy(device_name, "SILABS CP210X");

        return;
    }

    if (device_vid == 0x1A86)
    {
        strcpy(device_name, "CH340 / CH341");

        return;
    }

    if (device_vid == 0x0403)
    {
        strcpy(device_name, "FTDI USB SERIAL");

        return;
    }

    if (device_vid == 0x303A)
    {
        strcpy(device_name, "ESPRESSIF USB");

        return;
    }

    if (device_vid == 0x067B)
    {
        strcpy(device_name, "PROLIFIC SERIAL");

        return;
    }

    if (device_class == 0x02)
    {
        strcpy(device_name, "USB CDC SERIAL");

        return;
    }

    if (device_class == 0x03)
    {
        strcpy(device_name, "USB HID DEVICE");

        return;
    }

    if (device_class == 0x08)
    {
        strcpy(device_name, "USB STORAGE");

        return;
    }

    if (device_class == 0x09)
    {
        strcpy(device_name, "USB HUB");

        return;
    }
}

static void draw_header()
{
    M5.Display.fillScreen(TFT_BLACK);

    M5.Display.setTextColor(
        TFT_WHITE,
        TFT_BLACK
    );

    M5.Display.setTextDatum(
        middle_center
    );

    M5.Display.setTextSize(2);

    M5.Display.drawString(
        "ADV USB DETECTOR",
        M5.Display.width() / 2,
        13
    );

    M5.Display.drawFastHLine(
        5,
        30,
        M5.Display.width() - 10,
        TFT_WHITE
    );
}

static void draw_screen()
{
    draw_header();

    char text[64];

    switch (current_state)
    {
        case STATE_BOOT:
        {
            M5.Display.setTextSize(2);

            M5.Display.drawString(
                "BOOTING",
                M5.Display.width() / 2,
                55
            );

            M5.Display.setTextSize(1);

            M5.Display.drawString(
                "Starting detector",
                M5.Display.width() / 2,
                85
            );

            break;
        }

        case STATE_USB_STARTING:
        {
            M5.Display.setTextSize(2);

            M5.Display.drawString(
                "STARTING USB",
                M5.Display.width() / 2,
                55
            );

            M5.Display.setTextSize(1);

            M5.Display.drawString(
                "D- GPIO19",
                M5.Display.width() / 2,
                82
            );

            M5.Display.drawString(
                "D+ GPIO20",
                M5.Display.width() / 2,
                102
            );

            break;
        }

        case STATE_USB_READY:
        {
            M5.Display.setTextSize(2);

            M5.Display.drawString(
                "USB HOST READY",
                M5.Display.width() / 2,
                50
            );

            M5.Display.setTextSize(1);

            M5.Display.drawString(
                "Plug in USB device",
                M5.Display.width() / 2,
                80
            );

            M5.Display.drawString(
                "G0 = restart",
                M5.Display.width() / 2,
                105
            );

            break;
        }

        case STATE_DEVICE_FOUND:
        {
            M5.Display.setTextSize(2);

            M5.Display.drawString(
                "DEVICE FOUND",
                M5.Display.width() / 2,
                55
            );

            M5.Display.setTextSize(1);

            M5.Display.drawString(
                "Reading descriptor...",
                M5.Display.width() / 2,
                85
            );

            break;
        }

        case STATE_DEVICE_INFO:
        {
            M5.Display.setTextSize(1);

            M5.Display.drawString(
                device_name,
                M5.Display.width() / 2,
                43
            );

            snprintf(
                text,
                sizeof(text),
                "VID %04X   PID %04X",
                device_vid,
                device_pid
            );

            M5.Display.drawString(
                text,
                M5.Display.width() / 2,
                62
            );

            snprintf(
                text,
                sizeof(text),
                "CLASS %02X  SUB %02X",
                device_class,
                device_subclass
            );

            M5.Display.drawString(
                text,
                M5.Display.width() / 2,
                81
            );

            snprintf(
                text,
                sizeof(text),
                "PROTO %02X   ADDR %u",
                device_protocol,
                device_address
            );

            M5.Display.drawString(
                text,
                M5.Display.width() / 2,
                100
            );

            M5.Display.drawString(
                "G0 = rescan",
                M5.Display.width() / 2,
                119
            );

            break;
        }

        case STATE_DEVICE_REMOVED:
        {
            M5.Display.setTextSize(2);

            M5.Display.drawString(
                "DEVICE REMOVED",
                M5.Display.width() / 2,
                55
            );

            M5.Display.setTextSize(1);

            M5.Display.drawString(
                "Waiting for USB",
                M5.Display.width() / 2,
                83
            );

            M5.Display.drawString(
                "G0 = restart",
                M5.Display.width() / 2,
                105
            );

            break;
        }

        case STATE_ERROR:
        {
            M5.Display.setTextSize(2);

            M5.Display.drawString(
                "USB ERROR",
                M5.Display.width() / 2,
                55
            );

            M5.Display.setTextSize(1);

            M5.Display.drawString(
                "Press G0 to retry",
                M5.Display.width() / 2,
                88
            );

            break;
        }
    }
}

static void close_usb_device()
{
    if (
        usb_device != NULL &&
        usb_client != NULL
    )
    {
        usb_host_device_close(
            usb_client,
            usb_device
        );
    }

    usb_device = NULL;

    device_connected = false;

    device_address = 0;

    device_vid = 0;
    device_pid = 0;

    device_class = 0;
    device_subclass = 0;
    device_protocol = 0;

    strcpy(
        device_name,
        "UNKNOWN USB DEVICE"
    );
}

static void open_usb_device(uint8_t address)
{
    ESP_LOGI(
        TAG,
        "Opening USB address %u",
        address
    );

    current_state = STATE_DEVICE_FOUND;

    esp_err_t err;

    err = usb_host_device_open(
        usb_client,
        address,
        &usb_device
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Device open failed: %s",
            esp_err_to_name(err)
        );

        current_state = STATE_ERROR;

        return;
    }

    const usb_device_desc_t *descriptor = NULL;

    err = usb_host_get_device_descriptor(
        usb_device,
        &descriptor
    );

    if (
        err != ESP_OK ||
        descriptor == NULL
    )
    {
        ESP_LOGE(
            TAG,
            "Descriptor read failed"
        );

        current_state = STATE_ERROR;

        return;
    }

    device_address = address;

    device_vid = descriptor->idVendor;
    device_pid = descriptor->idProduct;

    device_class = descriptor->bDeviceClass;
    device_subclass = descriptor->bDeviceSubClass;
    device_protocol = descriptor->bDeviceProtocol;

    identify_usb_device();

    ESP_LOGI(
        TAG,
        "USB DEVICE VID=%04X PID=%04X",
        device_vid,
        device_pid
    );

    ESP_LOGI(
        TAG,
        "Detected as %s",
        device_name
    );

    device_connected = true;

    current_state = STATE_DEVICE_INFO;
}

static void usb_host_task(void *parameter)
{
    current_state = STATE_USB_STARTING;

    usb_host_config_t host_config = {};

    host_config.skip_phy_setup = false;

    host_config.intr_flags =
        ESP_INTR_FLAG_LEVEL1;

    esp_err_t err;

    err = usb_host_install(
        &host_config
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "USB install failed: %s",
            esp_err_to_name(err)
        );

        current_state = STATE_ERROR;

        vTaskDelete(NULL);

        return;
    }

    usb_host_client_config_t client_config = {};

    client_config.is_synchronous = true;

    client_config.max_num_event_msg = 0;

    err = usb_host_client_register(
        &client_config,
        &usb_client
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Client register failed: %s",
            esp_err_to_name(err)
        );

        current_state = STATE_ERROR;

        vTaskDelete(NULL);

        return;
    }

    current_state = STATE_USB_READY;

    uint8_t device_addresses[8];

    int device_count = 0;

    while (true)
    {
        uint32_t event_flags = 0;

        err = usb_host_lib_handle_events(
            pdMS_TO_TICKS(20),
            &event_flags
        );

        if (
            err != ESP_OK &&
            err != ESP_ERR_TIMEOUT
        )
        {
            ESP_LOGW(
                TAG,
                "Host event error: %s",
                esp_err_to_name(err)
            );
        }

        device_count = 0;

        err = usb_host_device_addr_list_fill(
            8,
            device_addresses,
            &device_count
        );

        if (err == ESP_OK)
        {
            if (
                device_count > 0 &&
                !device_connected
            )
            {
                open_usb_device(
                    device_addresses[0]
                );
            }

            if (
                device_count == 0 &&
                device_connected
            )
            {
                close_usb_device();

                current_state =
                    STATE_DEVICE_REMOVED;
            }

            if (
                device_count == 0 &&
                current_state ==
                    STATE_DEVICE_REMOVED
            )
            {
                current_state =
                    STATE_USB_READY;
            }
        }

        vTaskDelay(
            pdMS_TO_TICKS(100)
        );
    }
}

extern "C" void app_main(void)
{
    auto config = M5.config();

    M5.begin(config);

    M5.Display.setRotation(1);

    M5.Display.setBrightness(180);

    gpio_config_t button_config = {};

    button_config.pin_bit_mask =
        1ULL << G0_GPIO;

    button_config.mode =
        GPIO_MODE_INPUT;

    button_config.pull_up_en =
        GPIO_PULLUP_ENABLE;

    button_config.pull_down_en =
        GPIO_PULLDOWN_DISABLE;

    button_config.intr_type =
        GPIO_INTR_DISABLE;

    gpio_config(
        &button_config
    );

    current_state = STATE_BOOT;

    draw_screen();

    vTaskDelay(
        pdMS_TO_TICKS(1000)
    );

    xTaskCreatePinnedToCore(
        usb_host_task,
        "usb_host",
        8192,
        NULL,
        10,
        NULL,
        0
    );

    AppState previous_state =
        (AppState)-1;

    bool previous_g0 = true;

    while (true)
    {
        M5.update();

        bool g0_pressed =
            gpio_get_level(G0_GPIO);

        if (
            previous_g0 &&
            !g0_pressed
        )
        {
            ESP_LOGI(
                TAG,
                "G0 pressed - restarting"
            );

            esp_restart();
        }

        previous_g0 =
            g0_pressed;

        if (
            previous_state !=
            current_state
        )
        {
            previous_state =
                current_state;

            draw_screen();
        }

        vTaskDelay(
            pdMS_TO_TICKS(20)
        );
    }
}
