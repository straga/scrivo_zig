# Scrivo Zigbee MicroPython Integration

A collection of MicroPython C modules and examples to enable Zigbee functionality on ESP32-based boards.

## Overview

This project provides:

- A MicroPython user C module wrapping the Espressif Zigbee SDK
- Support for ESP32-S3 running MicroPython as the main controller
- Support for ESP32-H2 as a Radio Co-Processor (RCP) running OpenThread RCP firmware
- Python APIs for Zigbee clusters, attributes, and commands

## Prerequisites

- **ESP-IDF** v5.3.1 or later (requires Git submodules)
- **MicroPython** 1.25 for ESP32
- Linux/macOS development environment with `bash`, `make`, and Python 3

### IDF Component Manager Manifest

Add the following dependencies from your `idf_component.yml` in the MicroPython `ports/esp32/main/` directory:

```yaml
  espressif/esp-zboss-lib: "~1.6.0"
  espressif/esp-zigbee-lib: "~1.6.0"

```

## Building the Firmware

### Setup ESP-IDF

```bash
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.3.1  # recommended version
./install.sh
. ./export.sh
```

### Compile MicroPython Firmware (ESP32-S3)

```bash
idf.py -D MICROPY_BOARD=STRAGA_S3 \ 
       -D MICROPY_BOARD_VARIANT=SPIRAM_ZIG \ 
       -D USER_C_MODULES="../../../../cmodules/zigbee/scrivo_zig/src/micropython.cmake" \ 
       -B build_STRAGA_S3_SPIRAM_ZIG build flash monitor
```

### Flash Firmware with esptool

```bash
esptool.py --chip esp32s3 \
  -p /dev/ttyUSB0 -b 460800 --before default_reset --after no_reset \
  write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0000 build_STRAGA_S3_SPIRAM_ZIG/bootloader/bootloader.bin \
  0x10000 build_STRAGA_S3_SPIRAM_ZIG/micropython.bin \
  0x8000  build_STRAGA_S3_SPIRAM_ZIG/partition-table.bin \
  0xd000  build_STRAGA_S3_SPIRAM_ZIG/ota_data_initial.bin
```

### Compile RCP Firmware (ESP32-H2)

```bash
cd $IDF_PATH/examples/openthread/ot_rcp
idf.py set-target esp32h2
idf.py menuconfig
idf.py build
```

### Flash RCP Firmware

```bash
esptool.py --chip esp32h2 \
  -p /dev/ttyUSB1 -b 460800 --before default_reset --after no_reset \
  write_flash --flash_mode dio --flash_freq 48m --flash_size 2MB \
  0x0000 build/bootloader/bootloader.bin \
  0x10000 build/esp_ot_rcp.bin \
  0x8000  build/partition-table.bin
```

### Or Flash from bin directory

## Usage

1. Copy the `*.py` package from `py` into your MicroPython Board directory. 
You can use FTP:

```py
import network
sta = network.WLAN(network.STA_IF)
sta.active(True)
sta.connect("SSID", "PASS")
import uftpd
```

2. In your MicroPython REPL or script:
   ```python
   import zig_run  # starts the Zigbee

   ```
3. Interact with Zigbee clusters, attributes, and commands via Python APIs.



## License

This project is licensed under the [MIT License](LICENSE).

---
*Created by Viktor Vorobjov, 2025*