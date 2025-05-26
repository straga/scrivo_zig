// Copyright (c) 2025 Viktor Vorobjov
// Device manager interface
#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include "esp_err.h"
#include "py/obj.h" // For mp_obj_t
#include "mod_zig_types.h" // Include the centralized type definitions

/**
 * @brief Initialize device manager
 * 
 * @return esp_err_t in case of success
 */
esp_err_t device_manager_init(void);

/**
 * @brief Add new device
 * 
 * @param short_addr Short address of device
 * @param ieee_addr IEEE address of device
 * @return esp_err_t in case of success
 */
esp_err_t device_manager_add(uint16_t new_short_addr, const uint8_t ieee_addr[8], mp_obj_t zig_obj_mp, bool initial_load_context);

/**
 * @brief Delete device
 * 
 * @param short_addr Short address of device
 * @return esp_err_t in case of success
 */
esp_err_t device_manager_remove(uint16_t short_addr);

/**
 * @brief Update device information
 * 
 * @param device Pointer to structure with new data
 * @return esp_err_t in case of success
 */
esp_err_t device_manager_update(const zigbee_device_t *device);

/**
 * @brief Get device information
 * 
 * @param short_addr Short address of device
 * @return zigbee_device_t* Pointer to data structure or NULL
 */
zigbee_device_t* device_manager_get(uint16_t short_addr);

/**
 * @brief Check device availability
 * 
 * @param short_addr Short address of device
 * @return true if device is available
 */
bool device_manager_is_available(uint16_t short_addr);

/**
 * @brief Update timestamp of last contact with device
 * 
 * @param short_addr Short address of device
 */
void device_manager_update_timestamp(uint16_t short_addr);

/**
 * @brief Get list of all devices
 * 
 * @param count Pointer for returning device count
 * @return zigbee_device_t* Pointer to device array
 */
zigbee_device_t* device_manager_get_list(size_t *count);

#endif // DEVICE_MANAGER_H
