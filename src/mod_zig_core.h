// Copyright (c) 2025 Viktor Vorobjov
/*
 * mod_zig_core.h - Core functionality for Zigbee module
 *
 * This file contains the core functionality and type definitions for the Zigbee module.
 */

#ifndef MOD_ZIG_CORE_H
#define MOD_ZIG_CORE_H

#include "mod_zig_types.h"

// Declare global variables for access from other modules
extern esp_zb_cluster_list_t *cluster_list;
extern esp32_zig_obj_t esp32_zig_obj; // Global Zigbee object instance
extern esp32_zig_obj_t *zb_obj;       // Global pointer to the Zigbee object instance

// Other core functions
extern const mp_obj_fun_builtin_fixed_t esp32_zig_reset_to_factory_obj;
void esp_zb_app_init(esp32_zig_obj_t *self_in); // Declaration for esp_zb_app_init

#endif // MOD_ZIG_CORE_H
