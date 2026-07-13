#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"

#include <M5Unified.h>

static const char *TAG = "ADV_FLASHER";

static volatile int current_state = 0;
static volatile int current_error = 0;

static cdc_acm_dev_hdl_t cdc_dev = NULL;

enum
{
    STATE_BOOT = 0,
    STATE_USB_STARTING,
    STATE_USB_READY,
    STATE_CDC_STARTING,
    STATE_WAITING_DEVICE,
    STATE_DEVICE_OPENED,
    STATE_LINE_CODING,
    STATE_BOOT_PULSE,
    STATE_READY,
    STATE_DISCONNECTED,
    STATE_ERROR
};

static void draw_status(void)
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
        "ADV USB FLASHER",
        M5.Display.width() / 2,
        14
    );

    M5.Display.drawFastHLine(
        5,
        31,
        M5.Display.width() - 10,
        TFT_WHITE
    );

    M5.Display.setTextSize(1);

    switch (current_state)
    {
        case STATE_BOOT:
            M5.Display.drawString(
                "BOOTING...",
                M5.Display.width() / 2,
                65
            );
            break;

        case STATE_USB_STARTING:
            M5.Display.drawString(
                "STARTING USB HOST...",
                M5.Display.width() / 2,
                58
            );

            M5.Display.drawString(
                "D- G19   D+ G20",
                M5.Display.width() / 2,
                82
            );
            break;

        case STATE_USB_READY:
            M5.Display.drawString(
                "USB HOST READY",
                M5.Display.width() / 2,
                58
            );

            M5.Display.drawString(
                "Starting serial driver...",
                M5.Display.width() / 2,
                82
            );
            break;

        case STATE_CDC_STARTING:
            M5.Display.drawString(
                "USB HOST READY",
                M5.Display.width() / 2,
                52
            );

            M5.Display.drawString(
                "STARTING CDC ACM...",
                M5.Display.width() / 2,
                76
            );
            break;

        case STATE_WAITING_DEVICE:
            M5.Display.setTextSize(2);

            M5.Display.drawString(
                "WAITING FOR",
                M5.Display.width() / 2,
                53
            );

            M5.Display.drawString(
                "M5STICK...",
                M5.Display.width() / 2,
                79
            );

            M5.Display.setTextSize(1);

            M5.Display.drawString(
                "Plug into OTG port",
                M5.Display.width() / 2,
                110
            );
            break;

        case STATE_DEVICE_OPENED:
            M5.Display.setTextSize(2);

            M5.Display.drawString(
                "USB DEVICE",
                M5.Display.width() / 2,
                54
            );

            M5.Display.drawString(
                "OPENED",
                M5.Display.width() / 2,
                80
            );

            M5.Display.setTextSize(1);

            M5.Display.drawString(
                "Configuring 115200...",
                M5.Display.width() / 2,
                110
            );
            break;

        case STATE_LINE_CODING:
            M5.Display.drawString(
                "USB SERIAL READY",
                M5.Display.width() / 2,
                54
            );

            M5.Display.drawString(
                "115200 8N1",
                M5.Display.width() / 2,
                76
            );

            M5.Display.drawString(
                "Preparing boot pulse...",
                M5.Display.width() / 2,
                101
            );
            break;

        case STATE_BOOT_PULSE:
            M5.Display.setTextSize(2);

            M5.Display.drawString(
                "ENTERING",
                M5.Display.width() / 2,
                53
            );

            M5.Display.drawString(
                "BOOTLOADER...",
                M5.Display.width() / 2,
                79
            );

            M5.Display.setTextSize(1);

            M5.Display.drawString(
                "DTR / RTS pulse",
                M5.Display.width() / 2,
                110
            );
            break;

        case STATE_READY:
            M5.Display.setTextSize(2);

            M5.Display.drawString(
                "USB SERIAL",
                M5.Display.width() / 2,
                48
            );

            M5.Display.drawString(
                "READY",
                M5.Display.width() / 2,
                75
            );

            M5.Display.setTextSize(1);

            M5.Display.drawString(
                "CH9102 transport test OK",
                M5.Display.width() / 2,
                104
            );

            M5.Display.drawString(
                "Next: ESP ROM sync",
                M5.Display.width() / 2,
                121
            );
            break;

        case STATE_DISCONNECTED:
            M5.Display.setTextSize(2);

            M5.Display.drawString(
                "DEVICE",
                M5.Display.width() / 2,
                53
            );

            M5.Display.drawString(
                "DISCONNECTED",
                M5.Display.width() / 2,
                79
            );
            break;

        case STATE_ERROR:
        default:
        {
            char error_text[40];

            M5.Display.setTextSize(2);

            M5.Display.drawString(
                "ERROR",
                M5.Display.width() / 2,
                50
            );

            snprintf(
                error_text,
                sizeof(error_text),
                "CODE: %d",
                current_error
            );

            M5.Display.setTextSize(1);

            M5.Display.drawString(
                error_text,
                M5.Display.width() / 2,
                80
            );

            M5.Display.drawString(
                "Send me this code",
                M5.Display.width() / 2,
                108
            );

            break;
        }
    }
}

static void set_state(int state)
{
    current_state = state;
    draw_status();
}

static void set_error(esp_err_t error)
{
    current_error = (int)error;
    current_state = STATE_ERROR;
    draw_status();
}

static void usb_lib_task(void *arg)
{
    set_state(STATE_USB_STARTING);

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    esp_err_t err = usb_host_install(
        &host_config
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "USB host install failed: %s",
            esp_err_to_name(err)
        );

        set_error(err);

        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(
        TAG,
        "USB host installed"
    );

    set_state(STATE_USB_READY);

    while (1)
    {
        uint32_t event_flags = 0;

        err = usb_host_lib_handle_events(
            portMAX_DELAY,
            &event_flags
        );

        if (err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "USB host event error: %s",
                esp_err_to_name(err)
            );
        }
    }
}

static void print_device_event(
    const cdc_acm_host_dev_event_data_t *event,
    void *user_ctx
)
{
    if (event == NULL)
    {
        return;
    }

    switch (event->type)
    {
        case CDC_ACM_HOST_ERROR:
            ESP_LOGE(
                TAG,
                "CDC ACM error: %d",
                event->data.error
            );

            current_error = event->data.error;
            current_state = STATE_ERROR;
            break;

        case CDC_ACM_HOST_DEVICE_DISCONNECTED:
            ESP_LOGW(
                TAG,
                "USB serial disconnected"
            );

            cdc_dev = NULL;
            current_state = STATE_DISCONNECTED;
            break;

        case CDC_ACM_HOST_SERIAL_STATE:
            ESP_LOGI(
                TAG,
                "Serial state: 0x%04x",
                event->data.serial_state.val
            );
            break;

        default:
            ESP_LOGI(
                TAG,
                "CDC event: %d",
                event->type
            );
            break;
    }
}

static bool rx_callback(
    const uint8_t *data,
    size_t data_len,
    void *user_arg
)
{
    ESP_LOGI(
        TAG,
        "RX %u bytes",
        (unsigned int)data_len
    );

    printf("RX: ");

    for (size_t i = 0; i < data_len; i++)
    {
        printf(
            "%02X ",
            data[i]
        );
    }

    printf("\n");

    return true;
}

extern "C" void app_main(void)
{
    auto cfg = M5.config();

    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(180);

    set_state(STATE_BOOT);

    vTaskDelay(
        pdMS_TO_TICKS(500)
    );

    ESP_LOGI(
        TAG,
        "Cardputer ADV USB flasher display probe"
    );

    BaseType_t task_result =
        xTaskCreatePinnedToCore(
            usb_lib_task,
            "usb_lib",
            4096,
            NULL,
            10,
            NULL,
            0
        );

    if (task_result != pdPASS)
    {
        current_error = -100;
        current_state = STATE_ERROR;

        draw_status();

        return;
    }

    while (
        current_state != STATE_USB_READY &&
        current_state != STATE_ERROR
    )
    {
        vTaskDelay(
            pdMS_TO_TICKS(50)
        );
    }

    if (current_state == STATE_ERROR)
    {
        return;
    }

    vTaskDelay(
        pdMS_TO_TICKS(300)
    );

    set_state(STATE_CDC_STARTING);

    const cdc_acm_host_driver_config_t driver_config = {
        .driver_task_stack_size = 4096,
        .driver_task_priority = 9,
        .xCoreID = 1,
        .new_dev_cb = NULL,
    };

    esp_err_t err = cdc_acm_host_install(
        &driver_config
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "CDC ACM install failed: %s",
            esp_err_to_name(err)
        );

        set_error(err);
        return;
    }

    ESP_LOGI(
        TAG,
        "CDC ACM driver installed"
    );

    set_state(STATE_WAITING_DEVICE);

    const cdc_acm_host_device_config_t dev_config = {
        .connection_timeout_ms = 0,
        .out_buffer_size = 512,
        .in_buffer_size = 512,
        .event_cb = print_device_event,
        .data_cb = rx_callback,
        .user_arg = NULL,
    };

    err = cdc_acm_host_open(
        CDC_HOST_ANY_VID,
        CDC_HOST_ANY_PID,
        0,
        &dev_config,
        &cdc_dev
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "USB serial open failed: %s",
            esp_err_to_name(err)
        );

        set_error(err);
        return;
    }

    set_state(STATE_DEVICE_OPENED);

    vTaskDelay(
        pdMS_TO_TICKS(300)
    );

    cdc_acm_line_coding_t line_coding = {
        .dwDTERate = 115200,
        .bCharFormat = 0,
        .bParityType = 0,
        .bDataBits = 8,
    };

    err = cdc_acm_host_line_coding_set(
        cdc_dev,
        &line_coding
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Line coding failed: %s",
            esp_err_to_name(err)
        );

        set_error(err);
        return;
    }

    set_state(STATE_LINE_CODING);

    vTaskDelay(
        pdMS_TO_TICKS(500)
    );

    set_state(STATE_BOOT_PULSE);

    err = cdc_acm_host_set_control_line_state(
        cdc_dev,
        false,
        true
    );

    if (err != ESP_OK)
    {
        set_error(err);
        return;
    }

    vTaskDelay(
        pdMS_TO_TICKS(100)
    );

    err = cdc_acm_host_set_control_line_state(
        cdc_dev,
        true,
        false
    );

    if (err != ESP_OK)
    {
        set_error(err);
        return;
    }

    vTaskDelay(
        pdMS_TO_TICKS(50)
    );

    err = cdc_acm_host_set_control_line_state(
        cdc_dev,
        false,
        false
    );

    if (err != ESP_OK)
    {
        set_error(err);
        return;
    }

    vTaskDelay(
        pdMS_TO_TICKS(100)
    );

    set_state(STATE_READY);

    ESP_LOGI(
        TAG,
        "READY FOR ESP SERIAL FLASHER INTEGRATION"
    );

    int last_drawn_state = current_state;

    while (1)
    {
        M5.update();

        if (current_state != last_drawn_state)
        {
            last_drawn_state = current_state;
            draw_status();
        }

        vTaskDelay(
            pdMS_TO_TICKS(20)
        );
    }
}
