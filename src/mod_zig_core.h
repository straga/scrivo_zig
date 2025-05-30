// Copyright (c) 2025 Viktor Vorobjov
/*
 * mod_zig_core.h - Core functionality for Zigbee module
 *
 * This file contains the core functionality and type definitions for the Zigbee module.
 */

#ifndef MOD_ZIG_CORE_H
#define MOD_ZIG_CORE_H

#include "mod_zig_types.h"

// Other core functions
extern const mp_obj_fun_builtin_fixed_t esp32_zig_reset_to_factory_obj;
void esp_zb_app_init(esp32_zig_obj_t *self_in); // Declaration for esp_zb_app_init

// Utility functions for IEEE address manipulation
void zigbee_format_ieee_addr_to_str(const uint8_t ieee_addr[8], char *out_str, size_t out_str_len);
bool zigbee_parse_ieee_str_to_addr(const char *ieee_str, uint8_t out_addr[8]);

#endif // MOD_ZIG_CORE_H
