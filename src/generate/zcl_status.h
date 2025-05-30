/*
 * ZigBee Cluster Library Status Codes
 * Based on esp_zb_zcl_status_t enum type
 * Generated by generate_separate_modules.py on 2025-05-25 11:05:08
 */

#ifndef MICROPYTHON_ZCL_STATUS_H
#define MICROPYTHON_ZCL_STATUS_H

#include "py/runtime.h"
#include "py/obj.h"

// Module registration
extern const mp_obj_module_t mp_module_ZCL_STATUS;

// Function declarations - module level functions
mp_obj_t mod_zigbee_status_get_type(mp_obj_t value_in);
mp_obj_t mod_zigbee_status_size(mp_obj_t value_in);

#endif // MICROPYTHON_ZCL_STATUS_H