# Cardputer ADV -> M5StickC Plus2 portable flasher

ESP-IDF prototype project for using the Cardputer ADV ESP32-S3 USB host port
(D- GPIO19, D+ GPIO20) to talk to the M5StickC Plus2 USB serial bridge and
flash an ESP32 image with Espressif ESP Serial Flasher.

## Why this uses USB CDC ACM

The CH9102 datasheet says the device can expose a standard UART interface using
an OS built-in CDC driver. Espressif ESP Serial Flasher v2 has an ESP32 USB
CDC ACM port and an end-to-end embedded-host flashing demo.

## Current state

This ZIP is a buildable project skeleton and hardware/transport probe. It is
deliberately NOT labeled as a finished one-button flasher yet: the exact
CH9102 descriptor/control-line behavior of the user's specific M5StickC Plus2
must be confirmed on the real hardware before erase/write is enabled.

The app:
- starts ESP32-S3 USB Host
- installs Espressif USB CDC ACM host
- waits for a USB serial device
- opens it
- configures 115200 8N1
- toggles DTR/RTS in the ESP auto-download sequence
- reports each state over the serial console

After this probe succeeds, wire the opened CDC handle into the ESP Serial
Flasher v2 USB CDC ACM port and enable SD image selection/write.

## Build

Requires ESP-IDF 5.5 or newer.

    idf.py set-target esp32s3
    idf.py build
    idf.py flash monitor

## Hardware

Cardputer ADV OTG:
- D- GPIO19
- D+ GPIO20

Plug the M5StickC Plus2 into the ADV OTG port.

## Expected log

    USB host installed
    CDC ACM driver installed
    Waiting for CH9102 / USB serial device
    USB serial opened
    Line coding set: 115200 8N1
    Auto-download pulse sent
    READY FOR ESP SERIAL FLASHER INTEGRATION

## Important

Do not enable destructive flash writes until the target enters the ROM
bootloader reliably. A wrong flash offset or incomplete merged image can leave
the target with a blank screen, although the ESP32 ROM bootloader normally
remains recoverable.
