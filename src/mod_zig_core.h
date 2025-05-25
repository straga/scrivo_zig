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

// Other core functions
extern const mp_obj_fun_builtin_fixed_t esp32_zig_reset_to_factory_obj;

#endif // MOD_ZIG_CORE_H
