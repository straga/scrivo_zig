// Copyright (c) 2025 Viktor Vorobjov
#include "py/runtime.h"
#include "py/obj.h"
#include <string.h>  // Added header for strlen
#include "mod_zig_msg.h"
#include "zdo/esp_zigbee_zdo_command.h" // For esp_zb_zdo_signal_to_string
// esp_zigbee_zdo_common.h is included by esp_zigbee_zdo_command.h, so esp_zb_app_signal_type_t is available

// Get message type name
const char* zig_msg_get_type_name(int msg_type) {
    switch (msg_type) {
        #define X(name, value, desc) case ZIG_MSG_##name: return #name;
        ZIG_MSG_TYPES
        #undef X
        default: return "UNKNOWN";
    }
}

// Method to get message type name (for MicroPython)
static mp_obj_t zig_msg_get_type_name_mp(mp_obj_t value_in) {
    int value = mp_obj_get_int(value_in);
    const char* name = zig_msg_get_type_name(value);
    return mp_obj_new_str(name, strlen(name));
}
static MP_DEFINE_CONST_FUN_OBJ_1(zig_msg_get_type_name_obj, zig_msg_get_type_name_mp);

// New function to get app signal name
static mp_obj_t zig_msg_get_app_signal_name_mp(mp_obj_t signal_type_in) {
    esp_zb_app_signal_type_t signal_type = (esp_zb_app_signal_type_t)mp_obj_get_int(signal_type_in);
    const char *signal_name_str = esp_zb_zdo_signal_to_string(signal_type);
    if (signal_name_str == NULL) {
        // Handle cases where the signal type might not have a string representation
        char fallback_buf[30];
        snprintf(fallback_buf, sizeof(fallback_buf), "UNKNOWN_SIGNAL_%d", (int)signal_type);
        return mp_obj_new_str(fallback_buf, strlen(fallback_buf));
    }
    return mp_obj_new_str(signal_name_str, strlen(signal_name_str));
}
static MP_DEFINE_CONST_FUN_OBJ_1(zig_msg_get_app_signal_name_obj, zig_msg_get_app_signal_name_mp);

// Module globals table
static const mp_rom_map_elem_t zig_msg_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_ZIG_MSG) },

    // Message types
    #define X(name, value, desc) { MP_ROM_QSTR(MP_QSTR_##name), MP_ROM_INT(ZIG_MSG_##name) },
    ZIG_MSG_TYPES
    #undef X

    // Methods
    { MP_ROM_QSTR(MP_QSTR_get_type_name), MP_ROM_PTR(&zig_msg_get_type_name_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_app_signal_name), MP_ROM_PTR(&zig_msg_get_app_signal_name_obj) },
};

// Fixed variable name
static MP_DEFINE_CONST_DICT(zig_msg_globals, zig_msg_globals_table);

// Module definition
const mp_obj_module_t zig_msg_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&zig_msg_globals,
};

// // Module registration
// MP_REGISTER_MODULE(MP_QSTR_ZIG_MSG, zig_msg_module);