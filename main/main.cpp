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
    if (destination == NULL || destination_size == 0)
    {
        return;
    }

    if (source == NULL
