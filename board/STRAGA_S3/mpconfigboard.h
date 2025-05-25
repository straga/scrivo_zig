#ifndef MICROPY_HW_BOARD_NAME
// Can be set by mpconfigboard.cmake.
#define MICROPY_HW_BOARD_NAME               "Straga ESP32S3 module"
#endif

#ifndef MICROPY_HW_MCU_NAME
#define MICROPY_HW_MCU_NAME                 "ESP32S3"
#endif

// Enable UART REPL for modules that have an external USB-UART and don't use native USB.
#ifndef MICROPY_HW_ENABLE_UART_REPL
#define MICROPY_HW_ENABLE_UART_REPL         (1)
#endif

