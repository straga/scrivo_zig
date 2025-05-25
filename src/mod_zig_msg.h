// Copyright (c) 2025 Viktor Vorobjov
// mod_zig_msg.h
#ifndef MOD_ZIG_MSG_H
#define MOD_ZIG_MSG_H

#include "mod_zig_types.h"
#include "py/obj.h"

// Define message types with their string representations example for :  ZIG_MSG_<name> -> ZIG_MSG_ZB_ACTION_HANDLER
#define ZIG_MSG_TYPES \
    X(RAW,                  0, "Raw message"                    ) \
    X(SIMPLE_DESC_REQ_CB,   1, "Simple descriptor response"     ) \
    X(REPORT_ATTR_CB,       2, "Attribute report"               ) \
    X(READ_ATTR_RESP,       3, "Read attribute response"        ) \
    X(REPORT_CONFIG_RESP,   4, "Report configuration response"  ) \
    /* 5-6: reserved */ \
    X(SIGNAL_DEVICE_REBOOT, 7, "Device reboot signal"           ) \
    X(SIGNAL_FORMATION,     8, "Network formation signal"       ) \
    X(SIGNAL_DEVICE_ANNCE,  9, "Device announcement"            ) \
    /* 10-99: reserved */ \
    X(ZB_APP_SIGNAL_HANDLER,   50, "ZB app signal handler -> esp_zigbee_zdo_common.h"      ) \
    X(ACTION_DEFAULT,      100, "Default action"                ) \
    X(ZB_ACTION_HANDLER,   200, "zb_action_handler"             ) \
    X(CL_CUSTOM_CMD,             40,  "Message type for Tuya cluster commands"              ) \
    X(CL_CUSTOM_RESP,            41,  "Responses from Tuya devices"                ) \
    X(CL_CUSTOM_ATTR,            45,  "zb_action_custom"              )

// Generate enum values
#define X(name, value, desc) ZIG_MSG_##name = value,
enum {
    ZIG_MSG_TYPES
};
#undef X

// Function to get message type name
//const char* zig_msg_get_type_name(int msg_type);

// Declare the new function for MicroPython
// mp_obj_t zig_msg_get_app_signal_name_mp(mp_obj_t signal_type_in); // Not needed to be public

// Module object
extern const mp_obj_module_t zig_msg_module;

// Macro for module reference
//#define ZIG_MSG_MODULE &zig_msg_module

#endif // MOD_ZIG_MSG_H