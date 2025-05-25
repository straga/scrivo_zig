// Copyright (c) 2025 Viktor Vorobjov
#ifndef MOD_ZIG_NET_H
#define MOD_ZIG_NET_H

#include "mod_zig_types.h"
#include "esp_zigbee_type.h"
#include "esp_zigbee_core.h"
#include "esp_err.h"

#include "py/obj.h"
#include "py/runtime.h"

// Network management function objects
extern const mp_obj_fun_builtin_var_t esp32_zig_open_network_obj;
extern const mp_obj_fun_builtin_fixed_t esp32_zig_close_network_obj;
extern const mp_obj_fun_builtin_fixed_t esp32_zig_get_network_info_obj;
extern const mp_obj_fun_builtin_fixed_t esp32_zig_update_network_status_obj;
extern const mp_obj_fun_builtin_fixed_t esp32_zig_scan_networks_obj;
extern const mp_obj_fun_builtin_fixed_t esp32_zig_start_network_obj;

// Direct access to network functions for internal use
mp_obj_t esp32_zig_update_network_status(mp_obj_t self_in);
mp_obj_t esp32_zig_get_network_info(mp_obj_t self_in);

#endif // MOD_ZIG_NET_H