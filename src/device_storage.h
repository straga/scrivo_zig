// Copyright (c) 2025 Viktor Vorobjov
// Device storage interface
#ifndef DEVICE_STORAGE_H
#define DEVICE_STORAGE_H

#include "esp_err.h"
#include "mod_zig_types.h"

/**
 * @brief Save device to separate JSON file
 * 
 * Creates [short_addr].json file with device information.
 * Uses scheduler for safe Python callback invocation.
 * 
 * @param self Pointer to Zigbee object
 * @param short_addr Short address of device
 * @return esp_err_t in case of success
 */
esp_err_t device_storage_save(esp32_zig_obj_t *self, uint16_t short_addr);

/**
 * @brief Load device from JSON file
 * 
 * Loads device information from [short_addr].json file.
 * Uses scheduler for safe Python callback invocation.
 * 
 * @param self Pointer to Zigbee object
 * @param short_addr Short address of device
 * @return esp_err_t in case of success
 */
esp_err_t device_storage_load(esp32_zig_obj_t *self, uint16_t short_addr);

/**
 * @brief Load all devices from JSON files
 * 
 * Gets list of all .json files through storage callback
 * and sequentially loads each device with pauses,
 * to avoid overloading MicroPython scheduler.
 * 
 * @param self Pointer to Zigbee object
 * @return esp_err_t if loading started successfully
 */
esp_err_t device_storage_load_all(esp32_zig_obj_t *self);

/**
 * @brief Delete device file
 * 
 * Deletes [short_addr].json file for specified device.
 * Uses scheduler for safe Python callback invocation.
 * 
 * @param self Pointer to Zigbee object
 * @param short_addr Short address of device
 * @return esp_err_t in case of success
 */
esp_err_t device_storage_remove(esp32_zig_obj_t *self, uint16_t short_addr);

/**
 * @brief Initialize device storage system
 * 
 * Initialize device storage subsystem.
 * Should be called once during system startup.
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t device_storage_init(void);

/**
 * @brief Deinitialize device storage system
 * 
 * Clean up device storage subsystem.
 * Should be called during system shutdown.
 */
void device_storage_deinit(void);

/**
 * @brief Set storage callback and register it in GC
 * 
 * @param cb Python callback object
 */
void device_storage_set_callback(mp_obj_t cb);

/**
 * @brief Clear storage callback and unregister from GC
 */
void device_storage_clear_callback(void);

#endif
