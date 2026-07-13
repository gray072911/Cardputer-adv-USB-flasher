#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"

static const char *TAG = "ADV_FLASHER";

static void usb_lib_task(void *arg)
{
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    esp_err_t err = usb_host_install(&host_config);

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "USB host install failed: %s",
            esp_err_to_name(err)
        );

        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "USB host installed");

    while (1) {
        uint32_t event_flags = 0;

        err = usb_host_lib_handle_events(
            portMAX_DELAY,
            &event_flags
        );

        if (err != ESP_OK) {
            ESP_LOGE(
                TAG,
                "usb_host_lib_handle_events failed: %s",
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
    if (event == NULL) {
        return;
    }

    switch (event->type) {
        case CDC_ACM_HOST_ERROR:
            ESP_LOGE(
                TAG,
                "CDC ACM host error: %d",
                event->data.error
            );
            break;

        case CDC_ACM_HOST_DEVICE_DISCONNECTED:
            ESP_LOGW(
                TAG,
                "USB serial device disconnected"
            );
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

    for (size_t i = 0; i < data_len; i++) {
        printf("%02X ", data[i]);
    }

    printf("\n");

    return true;
}

void app_main(void)
{
    ESP_LOGI(
        TAG,
        "Cardputer ADV -> M5StickC Plus2 flasher probe"
    );

    ESP_LOGI(
        TAG,
        "USB OTG D- GPIO19 / D+ GPIO20"
    );

    BaseType_t task_result = xTaskCreatePinnedToCore(
        usb_lib_task,
        "usb_lib",
        4096,
        NULL,
        10,
        NULL,
        0
    );

    if (task_result != pdPASS) {
        ESP_LOGE(
            TAG,
            "Failed to create USB library task"
        );

        return;
    }

    vTaskDelay(
        pdMS_TO_TICKS(500)
    );

    const cdc_acm_host_driver_config_t driver_config = {
        .driver_task_stack_size = 4096,
        .driver_task_priority = 9,
        .xCoreID = 1,
        .new_dev_cb = NULL,
    };

    esp_err_t err = cdc_acm_host_install(
        &driver_config
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "CDC ACM driver install failed: %s",
            esp_err_to_name(err)
        );

        return;
    }

    ESP_LOGI(
        TAG,
        "CDC ACM driver installed"
    );

    ESP_LOGI(
        TAG,
        "Waiting for USB serial device"
    );

    cdc_acm_dev_hdl_t cdc_dev = NULL;

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

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "USB serial open failed: %s",
            esp_err_to_name(err)
        );

        ESP_LOGE(
            TAG,
            "Unable to open USB serial interface"
        );

        return;
    }

    ESP_LOGI(
        TAG,
        "USB serial device opened"
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

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Line coding failed: %s",
            esp_err_to_name(err)
        );

        return;
    }

    ESP_LOGI(
        TAG,
        "Line coding set to 115200 8N1"
    );

    ESP_LOGI(
        TAG,
        "Sending ESP32 auto-download pulse"
    );

    err = cdc_acm_host_set_control_line_state(
        cdc_dev,
        false,
        true
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "DTR/RTS stage 1 failed: %s",
            esp_err_to_name(err)
        );

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

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "DTR/RTS stage 2 failed: %s",
            esp_err_to_name(err)
        );

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

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "DTR/RTS release failed: %s",
            esp_err_to_name(err)
        );

        return;
    }

    vTaskDelay(
        pdMS_TO_TICKS(100)
    );

    ESP_LOGI(
        TAG,
        "Auto-download pulse complete"
    );

    ESP_LOGI(
        TAG,
        "READY FOR ESP SERIAL FLASHER INTEGRATION"
    );

    while (1) {
        vTaskDelay(
            pdMS_TO_TICKS(1000)
        );
    }
}
