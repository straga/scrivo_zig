// Copyright (c) 2025 Viktor Vorobjov
// Header file for device management
#ifndef MOD_ZIG_DEVICES_H
#define MOD_ZIG_DEVICES_H

#include "mod_zig_types.h"
#include "device_manager.h"
#include "device_storage.h"
#include "device_json.h"
#include "esp_err.h"
#include "py/obj.h"
#include "py/runtime.h"

// Python API function objects
extern const mp_obj_fun_builtin_var_t esp32_zig_save_device_obj;
extern const mp_obj_fun_builtin_var_t esp32_zig_load_device_obj;
extern const mp_obj_fun_builtin_var_t esp32_zig_get_device_obj;

// Public Python API
mp_obj_t esp32_zig_save_device(size_t n_args, const mp_obj_t *args);
mp_obj_t esp32_zig_load_device(size_t n_args, const mp_obj_t *args);
mp_obj_t esp32_zig_get_device(size_t n_args, const mp_obj_t *args);

// Internal helper functions
bool espz_init_device_discovery(esp32_zig_obj_t *self);
bool init_device_manager(void);
esp_err_t add_device_to_list(esp32_zig_obj_t *self, uint16_t short_addr, const uint8_t ieee_addr[8], bool initial_load_context);
esp_err_t remove_device_from_list(esp32_zig_obj_t *self, uint16_t short_addr);
esp_err_t update_device_info(esp32_zig_obj_t *self, zigbee_device_t *device);

// Functions for working with link quality
void device_update_link_quality(zigbee_device_t *device, const void *info_ptr);
uint8_t device_get_link_quality(zigbee_device_t *device);
const char* device_get_link_quality_description(zigbee_device_t *device);

#endif // MOD_ZIG_DEVICES_H
