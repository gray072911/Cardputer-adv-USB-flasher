#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "esp_loader.h"
#include "esp32_usb_cdc_acm_port.h"

#include <M5Unified.h>

static const char *TAG = "ADV_FLASHER";

enum AppState
{
    STATE_BOOT = 0,
    STATE_USB_STARTING,
    STATE_WAITING_DEVICE,
    STATE_CONNECTING,
    STATE_SYNCING,
    STATE_CONNECTED,
    STATE_CHIP_DETECTED,
    STATE_ERROR
};

static volatile AppState current_state = STATE_BOOT;
static volatile int current_error = 0;

static char status_line_1[64] = "";
static char status_line_2[64] = "";
static char status_line_3[64] = "";

static esp_loader_t loader;
static esp32_usb_cdc_acm_port_t usb_port;

static void copy_status(
    char *destination,
    size_t destination_size,
    const char *source
)
{
    if (destination == NULL || destination_size == 0)
    {
        return;
    }

    if (source == NULL)
    {
        destination[0] = '\0';
        return;
    }

    snprintf(
        destination,
        destination_size,
        "%s",
        source
    );
}

static void set_status(
    AppState state,
    const char *line1,
    const char *line2,
    const char *line3
)
{
    current_state = state;

    copy_status(
        status_line_1,
        sizeof(status_line_1),
        line1
    );

    copy_status(
        status_line_2,
        sizeof(status_line_2),
        line2
    );

    copy_status(
        status_line_3,
        sizeof(status_line_3),
        line3
    );
}

static void set_loader_error(
    int error_code,
    const char *stage
)
{
    current_error = error_code;

    char error_text[64];

    snprintf(
        error_text,
        sizeof(error_text),
        "ERROR: %d",
        error_code
    );

    set_status(
        STATE_ERROR,
        stage,
        error_text,
        "Send me this screen"
    );
}

static void draw_status(void)
{
    AppState state = current_state;

    char line1[64];
    char line2[64];
    char line3[64];

    copy_status(
        line1,
        sizeof(line1),
        status_line_1
    );

    copy_status(
        line2,
        sizeof(line2),
        status_line_2
    );

    copy_status(
        line3,
        sizeof(line3),
        status_line_3
    );

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
        "ADV USB FLASHER",
        M5.Display.width() / 2,
        13
    );

    M5.Display.drawFastHLine(
        5,
        30,
        M5.Display.width() - 10,
        TFT_WHITE
    );

    if (state == STATE_ERROR)
    {
        M5.Display.setTextSize(2);

        M5.Display.drawString(
            line1,
            M5.Display.width() / 2,
            51
        );

        M5.Display.setTextSize(1);

        M5.Display.drawString(
            line2,
            M5.Display.width() / 2,
            79
        );

        M5.Display.drawString(
            line3,
            M5.Display.width() / 2,
            108
        );

        return;
    }

    M5.Display.setTextSize(2);

    M5.Display.drawString(
        line1,
        M5.Display.width() / 2,
        50
    );

    M5.Display.setTextSize(1);

    M5.Display.drawString(
        line2,
        M5.Display.width() / 2,
        79
    );

    M5.Display.drawString(
        line3,
        M5.Display.width() / 2,
        106
    );
}

static const char *target_name(
    esp_loader_target_t target
)
{
    switch (target)
    {
        case ESP8266_CHIP:
            return "ESP8266";

        case ESP32_CHIP:
            return "ESP32";

        case ESP32S2_CHIP:
            return "ESP32-S2";

        case ESP32C3_CHIP:
            return "ESP32-C3";

        case ESP32S3_CHIP:
            return "ESP32-S3";

        case ESP32C2_CHIP:
            return "ESP32-C2";

        case ESP32H2_CHIP:
            return "ESP32-H2";

        case ESP32C6_CHIP:
            return "ESP32-C6";

        default:
            return "UNKNOWN ESP";
    }
}

static void flasher_task(void *argument)
{
    ESP_LOGI(
        TAG,
        "Starting ESP serial flasher"
    );

    set_status(
        STATE_USB_STARTING,
        "STARTING USB",
        "USB CDC ACM port",
        "D- G19   D+ G20"
    );

    vTaskDelay(
        pdMS_TO_TICKS(500)
    );

    memset(
        &usb_port,
        0,
        sizeof(usb_port)
    );

    usb_port.port.ops = &esp32_usb_cdc_acm_ops;

    usb_port.connection_timeout_ms = 0;
    usb_port.out_buffer_size = 1024;
    usb_port.in_buffer_size = 1024;

    set_status(
        STATE_WAITING_DEVICE,
        "WAITING",
        "Plug in M5StickC Plus2",
        "USB OTG port"
    );

    esp_loader_error_t loader_error;

    loader_error = esp_loader_init_serial(
        &loader,
        &usb_port.port
    );

    if (loader_error != ESP_LOADER_SUCCESS)
    {
        ESP_LOGE(
            TAG,
            "esp_loader_init_serial failed: %d",
            loader_error
        );

        set_loader_error(
            loader_error,
            "LOADER INIT"
        );

        vTaskDelete(NULL);
        return;
    }

    set_status(
        STATE_CONNECTING,
        "DEVICE FOUND",
        "Opening USB serial...",
        "Preparing ESP ROM sync"
    );

    vTaskDelay(
        pdMS_TO_TICKS(300)
    );

    esp_loader_connect_args_t connect_args =
        ESP_LOADER_CONNECT_DEFAULT();

    set_status(
        STATE_SYNCING,
        "ROM SYNC",
        "Entering download mode...",
        "Connecting to ESP..."
    );

    ESP_LOGI(
        TAG,
        "Calling esp_loader_connect"
    );

    loader_error = esp_loader_connect(
        &loader,
        &connect_args
    );

    if (loader_error != ESP_LOADER_SUCCESS)
    {
        ESP_LOGE(
            TAG,
            "esp_loader_connect failed: %d",
            loader_error
        );

        set_loader_error(
            loader_error,
            "ROM SYNC FAILED"
        );

        vTaskDelete(NULL);
        return;
    }

    set_status(
        STATE_CONNECTED,
        "ROM CONNECTED",
        "ESP bootloader synced",
        "Detecting target..."
    );

    ESP_LOGI(
        TAG,
        "ESP ROM connected"
    );

    vTaskDelay(
        pdMS_TO_TICKS(500)
    );

    esp_loader_target_t target =
        esp_loader_get_target(
            &loader
        );

    const char *chip_name =
        target_name(
            target
        );

    char chip_text[64];

    snprintf(
        chip_text,
        sizeof(chip_text),
        "TARGET: %s",
        chip_name
    );

    ESP_LOGI(
        TAG,
        "Detected target: %s",
        chip_name
    );

    set_status(
        STATE_CHIP_DETECTED,
        "CHIP DETECTED",
        chip_text,
        "ROM sync test PASSED"
    );

    while (1)
    {
        vTaskDelay(
            pdMS_TO_TICKS(1000)
        );
    }
}

extern "C" void app_main(void)
{
    auto cfg = M5.config();

    M5.begin(cfg);

    M5.Display.setRotation(1);

    M5.Display.setBrightness(
        180
    );

    set_status(
        STATE_BOOT,
        "BOOTING...",
        "Cardputer ADV",
        "ESP ROM sync tester"
    );

    draw_status();

    vTaskDelay(
        pdMS_TO_TICKS(700)
    );

    BaseType_t task_result =
        xTaskCreatePinnedToCore(
            flasher_task,
            "flasher_task",
            8192,
            NULL,
            10,
            NULL,
            0
        );

    if (task_result != pdPASS)
    {
        set_loader_error(
            -1000,
            "TASK CREATE"
        );
    }

    AppState last_state =
        (AppState)-1;

    while (1)
    {
        M5.update();

        if (current_state != last_state)
        {
            last_state = current_state;

            draw_status();
        }

        vTaskDelay(
            pdMS_TO_TICKS(20)
        );
    }
}
