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

#include "esp_loader.h"
#include "esp32_usb_cdc_acm_port.h"

#include <M5Unified.h>

static const char *TAG = "ADV_FLASHER";

#define G0_BUTTON_GPIO GPIO_NUM_0

enum AppState
{
    STATE_BOOT = 0,
    STATE_WAITING_DEVICE,
    STATE_OPENING_DEVICE,
    STATE_CONNECTING,
    STATE_CONNECTED,
    STATE_DISCONNECTED,
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
    if (
        destination == NULL ||
        destination_size == 0
    )
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
        "Press G0 to retry"
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

    M5.Display.fillScreen(
        TFT_BLACK
    );

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

static void usb_error_callback(void)
{
    ESP_LOGE(
        TAG,
        "USB CDC ACM host error"
    );
}

static void usb_disconnected_callback(void)
{
    ESP_LOGW(
        TAG,
        "USB device disconnected"
    );

    set_status(
        STATE_DISCONNECTED,
        "DISCONNECTED",
        "USB device removed",
        "Press G0 to retry"
    );
}

static void usb_serial_state_callback(void)
{
    ESP_LOGI(
        TAG,
        "USB serial state event"
    );
}

static void flasher_task(
    void *argument
)
{
    ESP_LOGI(
        TAG,
        "Starting ESP serial flasher v2"
    );

    set_status(
        STATE_WAITING_DEVICE,
        "WAITING",
        "Plug in M5StickC Plus2",
        "USB OTG port"
    );

    vTaskDelay(
        pdMS_TO_TICKS(1000)
    );

    memset(
        &loader,
        0,
        sizeof(loader)
    );

    memset(
        &usb_port,
        0,
        sizeof(usb_port)
    );

    usb_port.port.ops =
        &esp32_usb_cdc_acm_ops;

    usb_port.device_vid =
        USB_VID_PID_AUTO_DETECT;

    usb_port.device_pid =
        USB_VID_PID_AUTO_DETECT;

    usb_port.connection_timeout_ms =
        120000;

    usb_port.out_buffer_size =
        4096;

    usb_port.acm_host_error_callback =
        usb_error_callback;

    usb_port.device_disconnected_callback =
        usb_disconnected_callback;

    usb_port.acm_host_serial_state_callback =
        usb_serial_state_callback;

    set_status(
        STATE_OPENING_DEVICE,
        "OPENING USB",
        "Waiting for USB serial",
        "Timeout: 120 seconds"
    );

    ESP_LOGI(
        TAG,
        "Calling esp_loader_init_serial"
    );

    esp_loader_error_t loader_error =
        esp_loader_init_serial(
            &loader,
            &usb_port.port
        );

    if (
        loader_error !=
        ESP_LOADER_SUCCESS
    )
    {
        ESP_LOGE(
            TAG,
            "esp_loader_init_serial failed: %d",
            loader_error
        );

        set_loader_error(
            loader_error,
            "USB OPEN FAILED"
        );

        vTaskDelete(NULL);

        return;
    }

    ESP_LOGI(
        TAG,
        "USB CDC ACM port initialized"
    );

    set_status(
        STATE_CONNECTING,
        "ROM SYNC",
        "USB serial opened",
        "Connecting to ESP..."
    );

    vTaskDelay(
        pdMS_TO_TICKS(1000)
    );

    esp_loader_connect_args_t connect_args =
        ESP_LOADER_CONNECT_DEFAULT();

    ESP_LOGI(
        TAG,
        "Calling esp_loader_connect"
    );

    loader_error =
        esp_loader_connect(
            &loader,
            &connect_args
        );

    if (
        loader_error !=
        ESP_LOADER_SUCCESS
    )
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

    ESP_LOGI(
        TAG,
        "ESP ROM connected successfully"
    );

    set_status(
        STATE_CONNECTED,
        "ROM CONNECTED",
        "ESP bootloader synced",
        "Connection test PASSED"
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
    auto cfg =
        M5.config();

    M5.begin(
        cfg
    );

    M5.Display.setRotation(
        1
    );

    M5.Display.setBrightness(
        180
    );

    gpio_config_t g0_config = {};

    g0_config.pin_bit_mask =
        (
            1ULL <<
            G0_BUTTON_GPIO
        );

    g0_config.mode =
        GPIO_MODE_INPUT;

    g0_config.pull_up_en =
        GPIO_PULLUP_ENABLE;

    g0_config.pull_down_en =
        GPIO_PULLDOWN_DISABLE;

    g0_config.intr_type =
        GPIO_INTR_DISABLE;

    gpio_config(
        &g0_config
    );

    set_status(
        STATE_BOOT,
        "BOOTING...",
        "Cardputer ADV",
        "ESP ROM sync tester"
    );

    draw_status();

    vTaskDelay(
        pdMS_TO_TICKS(1000)
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

    if (
        task_result !=
        pdPASS
    )
    {
        set_loader_error(
            -1000,
            "TASK CREATE"
        );
    }

    AppState last_state =
        (AppState)-1;

    bool previous_g0_state =
        true;

    while (1)
    {
        M5.update();

        bool g0_state =
            gpio_get_level(
                G0_BUTTON_GPIO
            );

        if (
            previous_g0_state &&
            !g0_state
        )
        {
            if (
                current_state ==
                    STATE_ERROR ||
                current_state ==
                    STATE_DISCONNECTED
            )
            {
                ESP_LOGI(
                    TAG,
                    "G0 pressed - restarting flasher"
                );

                set_status(
                    STATE_BOOT,
                    "RETRYING...",
                    "Restarting USB host",
                    "Please wait"
                );

                draw_status();

                vTaskDelay(
                    pdMS_TO_TICKS(500)
                );

                esp_restart();
            }
        }

        previous_g0_state =
            g0_state;

        if (
            current_state !=
            last_state
        )
        {
            last_state =
                current_state;

            draw_status();
        }

        vTaskDelay(
            pdMS_TO_TICKS(20)
        );
    }
}
