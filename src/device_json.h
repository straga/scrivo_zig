// Copyright (c) 2025 Viktor Vorobjov
#ifndef DEVICE_JSON_H
#define DEVICE_JSON_H

#include "esp_err.h"
#include "mod_zig_types.h"
#include "cJSON.h"

/**
 * @brief Serialize device to JSON
 * 
 * @param device Pointer to device structure
 * @return cJSON* JSON object or NULL in case of error
 */
cJSON* device_to_json(const zigbee_device_t *device);

/**
 * @brief Deserialize device from JSON
 * 
 * @param json JSON object with device data
 * @param device Pointer to structure for filling
 * @param zig_obj_mp mp_obj_t representing the main zigbee object, for operations like adding to device_manager
 * @return esp_err_t in case of success
 */
esp_err_t device_from_json(const cJSON *json, zigbee_device_t *device, mp_obj_t zig_obj_mp);

#endif
