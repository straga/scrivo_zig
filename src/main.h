// Copyright (c) 2025 Viktor Vorobjov
// Copyright (c) 2025 Viktor Vorobjov

#ifndef MOD_ZIG_MAIN_H
#define MOD_ZIG_MAIN_H

// MicroPython
#include "py/obj.h"
#include "py/runtime.h"

// Zigbee
#include "esp_zigbee_core.h"
#include "esp_zigbee_type.h"

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Core types and definitions
#include "mod_zig_types.h"

#define ZIGBEE_TAG "ZIG_SCRIVO_GATEWAY" 

// Zigbee Gateway Configuration
#define MAX_CHILDREN                    10          /* the max amount of connected devices */
#define INSTALLCODE_POLICY_ENABLE       false       /* enable the install code policy for security */
#define ESP_ZB_PRIMARY_CHANNEL_MASK     (1l << 13)  /* Zigbee primary channel mask */
#define ESP_ZB_GATEWAY_ENDPOINT         1           /* Gateway endpoint identifier */
#define RCP_VERSION_MAX_SIZE            80          /* Maximum size for RCP version string */

// Radio configuration
#define ZB_RADIO_MODE_NATIVE            0           /* Use native radio mode for ESP32-H2 */
#define ZB_RADIO_MODE_UART_RCP          1           /* Use UART RCP mode for external Zigbee radio */

// Manufacturer information defaults
#define ESP_MANUFACTURER_NAME           "\x09""ESPRESSIF"  /* Customized manufacturer name with length byte */
#define ESP_MODEL_IDENTIFIER            "\x07""ESP32"      /* Model identifier with length byte */
#define ESP_MANUFACTURER_CODE           0x131B             /* Espressif manufacturer code */

// When calling esp_zb_* SDK functions from MicroPython (from any task except main Zigbee task), 
// they need to be wrapped in a lock:
// Need esp_zb_lock_acquire()?

// Inside Zigbee callback                   ❌ No
// In esp_zb_main_loop_iteration()          ❌ No
// From custom FreeRTOS task (xTaskCreate)  ✅ Yes
// esp_zb_scheduler_alarm()                 ❌ No
// esp_zb_zcl_on_off_cmd_req()              ✅ Yes, if outside Zigbee task
#define ZB_LOCK()   esp_zb_lock_acquire(portMAX_DELAY)
#define ZB_UNLOCK() esp_zb_lock_release()

// Forward declaration of global Zigbee object
//extern esp32_zig_obj_t esp32_zig_obj;

// Глобальный указатель на главный объект для регистрации в GC
extern mp_obj_t global_esp32_zig_obj_ptr;
MP_REGISTER_ROOT_POINTER(mp_obj_t global_esp32_zig_obj_ptr);

// Zigbee type
extern const mp_obj_type_t machine_zig_type;

// Forward declarations for core functionality
extern mp_obj_t esp32_zig_init_helper(esp32_zig_obj_t *self, size_t n_args, 
    const mp_obj_t *pos_args, mp_map_t *kw_args);
extern esp_err_t esp32_zig_start_gateway(esp32_zig_obj_t *self);

// // Zigbee deinitialization
// void deinit_zigbee(void);

// Device persistence
esp_err_t save_devices_to_json(esp32_zig_obj_t *self);

#define FIRMWARE_VERSION "v0.1.2"

#endif // MOD_ZIG_MAIN_H
