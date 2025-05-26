// Copyright (c) 2025 Viktor Vorobjov
// Device storage implementation
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "device_storage.h"
#include "device_manager.h"
#include "device_json.h"
#include "cJSON.h"

#define LOG_TAG "DEVICE_STORAGE"
#define MAX_FILENAME_LEN 32
#define MAX_SAVE_QUEUE 32
#define MAX_SCHEDULE_RETRIES 5
#define SCHEDULE_RETRY_DELAY_MS 500  // Adjusted to 500 milliseconds

// Forward declarations
static mp_obj_t do_device_save_handler(mp_obj_t schedule_data_in);
static mp_obj_t do_save_retry_handler(mp_obj_t retry_data_in);
static mp_obj_t do_load_all_handler(mp_obj_t ctx_in);

// Declare function objects after forward declarations
static MP_DEFINE_CONST_FUN_OBJ_1(do_device_save_handler_obj, do_device_save_handler);
static MP_DEFINE_CONST_FUN_OBJ_1(do_save_retry_handler_obj, do_save_retry_handler);
static MP_DEFINE_CONST_FUN_OBJ_1(do_load_all_handler_obj, do_load_all_handler);

// Structure for save queue
typedef struct {
    uint16_t short_addr;  // Device address for saving
    mp_obj_t zig_obj_mp; // Python mp_obj_t reference to the main zigbee object
} device_save_data_t;

// Save queue state (simplified)
typedef struct {
    device_save_data_t items[MAX_SAVE_QUEUE];
    int count;                    // Number of devices in the queue
    uint32_t next_save_time;      // Time of next allowed save
} save_queue_t;

static save_queue_t save_queue = {
    .count = 0,
    .next_save_time = 0
};

static bool save_in_progress = false;

// Check if device is in the queue
static bool is_device_in_queue(uint16_t short_addr) {
    for (int i = 0; i < save_queue.count; i++) {
        if (save_queue.items[i].short_addr == short_addr) {
            ESP_LOGI(LOG_TAG, "Device 0x%04x found in save queue at index %d. Current save op for this addr will be skipped if one is pending.", short_addr, i);
            return true;
        }
    }
    return false;
}

// Add to queue
static bool queue_push(uint16_t short_addr, mp_obj_t zig_obj_mp_param) {
// If device is already in queue - skip
    if (is_device_in_queue(short_addr)) {
        ESP_LOGW(LOG_TAG, "Device 0x%04x already in queue, skipping", short_addr);
        return true;
    }
    
// Check queue size
    if (save_queue.count >= MAX_SAVE_QUEUE) {
        ESP_LOGW(LOG_TAG, "Save queue full, skipping save for device 0x%04x", short_addr);
        return false;
    }
    
// Add new element to the end of queue
    save_queue.items[save_queue.count].short_addr = short_addr;
    save_queue.items[save_queue.count].zig_obj_mp = zig_obj_mp_param;
    save_queue.count++;
    
    ESP_LOGI(LOG_TAG, "Device 0x%04x queued for save at position %d (queue size: %d)", 
             short_addr, save_queue.count - 1, save_queue.count);
    
    return true;
}

// Schedule next save
static void schedule_next_save(void) {
    if (save_in_progress || save_queue.count == 0) {
        if (save_in_progress) {
            ESP_LOGD(LOG_TAG, "Save already in progress, skipping schedule");
        }
        if (save_queue.count == 0) {
            ESP_LOGD(LOG_TAG, "Save queue empty, nothing to schedule");
        }
        return;
    }

// Check if it's not too early for the next save
    uint32_t current_time = mp_hal_ticks_ms();
    if (current_time < save_queue.next_save_time) {
        ESP_LOGD(LOG_TAG, "Too early for next save (current: %lu, next: %lu)", 
                 (unsigned long)current_time, (unsigned long)save_queue.next_save_time);
        return;
    }

    save_in_progress = true;
    ESP_LOGI(LOG_TAG, "Scheduling save for device 0x%04x (queue head, %d devices in queue)", 
             save_queue.items[0].short_addr, save_queue.count);

// Schedule saving through MicroPython scheduler
    if (!mp_sched_schedule((mp_obj_t)&do_device_save_handler_obj, MP_OBJ_FROM_PTR(&save_queue.items[0]))) {
        ESP_LOGE(LOG_TAG, "Failed to schedule save handler");
        save_in_progress = false;
// Next attempt will be after SCHEDULE_RETRY_DELAY_MS
        save_queue.next_save_time = current_time + SCHEDULE_RETRY_DELAY_MS;
    }
}

// Structure for tracking save retry attempts
// typedef struct {
//     uint16_t short_addr;
//     mp_obj_t zig_obj_mp; // Python mp_obj_t reference to the main zigbee object
//     int retry_count;
//     char *json_str;
// } save_retry_ctx_t;

// Save handler in Python context
static mp_obj_t do_device_save_handler(mp_obj_t schedule_data_in) {
    device_save_data_t *data = MP_OBJ_TO_PTR(schedule_data_in);
    ESP_LOGI(LOG_TAG, "Save Handler: Processing device 0x%04x from queue head (zig_obj_mp: %p)", 
             data->short_addr, (void*)data->zig_obj_mp);
    
// Get current device state
    zigbee_device_t *device = device_manager_get(data->short_addr);
    if (!device) {
        ESP_LOGE(LOG_TAG, "Device 0x%04x not found in device manager for save", data->short_addr);
        goto cleanup_and_next_schedule;
    }

    ESP_LOGI(LOG_TAG, "Save Handler: Found device 0x%04x in manager, creating JSON", device->short_addr);

// Create JSON from current state
    cJSON *json = device_to_json(device);
    if (!json) {
        ESP_LOGE(LOG_TAG, "Failed to create JSON for device 0x%04x", data->short_addr);
        goto cleanup_and_next_schedule;
    }

    char *json_str_c = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    if (!json_str_c) {
        ESP_LOGE(LOG_TAG, "Failed to print JSON for device 0x%04x", data->short_addr);
        goto cleanup_and_next_schedule;
    }

    ESP_LOGI(LOG_TAG, "Save Handler: Created JSON for device 0x%04x, scheduling retry handler", data->short_addr);

    // Prepare arguments for do_save_retry_handler as a MicroPython tuple
    // Tuple: (storage_cb_obj, short_addr_obj, json_str_obj, retry_count_obj)
    mp_obj_t items[4];
    
    // Get the main zigbee object to extract the storage_cb
    esp32_zig_obj_t *self_c_obj = NULL;
    if (data->zig_obj_mp != mp_const_none) {
        self_c_obj = MP_OBJ_TO_PTR(data->zig_obj_mp);
    }

    if (!self_c_obj || self_c_obj->storage_cb == mp_const_none || !mp_obj_is_callable(self_c_obj->storage_cb)) {
        ESP_LOGE(LOG_TAG, "Save Handler: storage_cb is not valid/callable for device 0x%04x. Cannot schedule save retry.", data->short_addr);
        free(json_str_c);
        goto cleanup_and_next_schedule;
    }

    items[0] = self_c_obj->storage_cb; // Store the callback directly
    items[1] = mp_obj_new_int(data->short_addr);
    items[2] = mp_obj_new_str(json_str_c, strlen(json_str_c));
    items[3] = mp_obj_new_int(0); // Initial retry_count

    mp_obj_t scheduled_arg_tuple = mp_obj_new_tuple(4, items);
    free(json_str_c); // Free the C string after MP string is created
    
// Start first save attempt
    if (!mp_sched_schedule((mp_obj_t)&do_save_retry_handler_obj, scheduled_arg_tuple)) {
        ESP_LOGE(LOG_TAG, "Failed to schedule initial save for device 0x%04x", data->short_addr);
        // Note: scheduled_arg_tuple won't be GC'd if schedule fails and it's not rooted elsewhere,
        // but typically it's short-lived if schedule fails.
        goto cleanup_and_next_schedule; // Ensure queue processing continues
    }

cleanup_and_next_schedule:
    ESP_LOGI(LOG_TAG, "Save Handler: Cleaning up and preparing next save (removing device 0x%04x from queue head if it was processed or failed before python cb)", 
             data->short_addr);

// Shift all elements to the left
    if (save_queue.count > 1) {
        memmove(&save_queue.items[0], &save_queue.items[1], 
                (save_queue.count - 1) * sizeof(device_save_data_t));
    }
    save_queue.count--;
    
    ESP_LOGI(LOG_TAG, "Save Handler: Queue now has %d devices", save_queue.count);
    
// Set time for next allowed save
    save_queue.next_save_time = mp_hal_ticks_ms() + SCHEDULE_RETRY_DELAY_MS;
    
    save_in_progress = false;
    
// Schedule next save if there are items in the queue
    schedule_next_save();
    
    return mp_const_none;
}

esp_err_t device_storage_save(esp32_zig_obj_t *self, uint16_t short_addr) {

    ESP_LOGI(LOG_TAG, "=== SAVE CALLED === device_storage_save called for device 0x%04x", short_addr);

    if (!self->storage_cb || self->storage_cb == mp_const_none) {
        ESP_LOGW(LOG_TAG, "No storage callback");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(LOG_TAG, "device_storage_save called for device 0x%04x", short_addr);

// Get device
    zigbee_device_t *device = device_manager_get(short_addr);
    if (!device) {
        ESP_LOGE(LOG_TAG, "Device not found: 0x%04x", short_addr);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(LOG_TAG, "Device 0x%04x found in manager, adding to save queue", short_addr);

// Add to save queue
    if (!queue_push(short_addr, MP_OBJ_FROM_PTR(self))) {
        return ESP_ERR_NO_MEM;
    }

// Schedule next save
    schedule_next_save();
    
    return ESP_OK;
}

// Structure for loading all devices
typedef struct {
    mp_obj_t storage_cb_obj; // Renamed from callback to be more specific
    mp_obj_t zig_obj_mp;     // Added: mp_obj_t for the main zigbee object
    mp_obj_t *files;
    size_t file_count;
    size_t current_index;
    int retry_count;
} load_all_ctx_t;

// Single handler for loading all devices
static mp_obj_t do_load_all_handler(mp_obj_t ctx_in) {
    load_all_ctx_t *ctx = MP_OBJ_TO_PTR(ctx_in);
    bool success = false;
    
// Get file list on first run
    if (ctx->current_index == 0 && ctx->retry_count == 0) {
        mp_obj_t list_cmd = mp_obj_new_str("list", 4);
        mp_obj_t args[1] = {list_cmd};
        mp_obj_t file_list = mp_call_function_n_kw(ctx->storage_cb_obj, 1, 0, args);

        if (file_list == mp_const_none) {
            ESP_LOGE(LOG_TAG, "Failed to get file list");
            free(ctx);
            return mp_const_none;
        }

        mp_obj_get_array(file_list, &ctx->file_count, &ctx->files);
        if (ctx->file_count == 0) {
            ESP_LOGI(LOG_TAG, "No devices to load");
            free(ctx);
            return mp_const_none;
        }
    }

// If there is a file to load
    if (ctx->current_index < ctx->file_count) {
        const char *filename = mp_obj_str_get_str(ctx->files[ctx->current_index]);
        uint16_t short_addr;
        if (sscanf(filename, "%04hx.json", &short_addr) != 1) {
            ESP_LOGW(LOG_TAG, "Invalid filename format: %s", filename);
            goto next_file;
        }

// Load device
        mp_obj_t load_cmd = mp_obj_new_str("load", 4);
        mp_obj_t fname = mp_obj_new_str(filename, strlen(filename));
        mp_obj_t load_args[2] = {load_cmd, fname};
        mp_obj_t json_str = mp_call_function_n_kw(ctx->storage_cb_obj, 2, 0, load_args);

        if (json_str != mp_const_none) {
            const char *json_data = mp_obj_str_get_str(json_str);
            cJSON *json = cJSON_Parse(json_data);
            
            if (json) {
                zigbee_device_t device = {0};
                if (device_from_json(json, &device, ctx->zig_obj_mp) == ESP_OK) {
                    device_manager_update(&device);
                    success = true;
                }
                cJSON_Delete(json);
            }
        }

        if (!success) {
            ctx->retry_count++;
            if (ctx->retry_count < MAX_SCHEDULE_RETRIES) {
                ESP_LOGW(LOG_TAG, "Load failed for %s, retry %d/%d", filename, ctx->retry_count, MAX_SCHEDULE_RETRIES);
                if (!mp_sched_schedule((mp_obj_t)&do_load_all_handler_obj, 
                                   MP_OBJ_FROM_PTR(ctx))) {
                    ESP_LOGE(LOG_TAG, "Failed to schedule retry for %s", filename);
                    free(ctx);
                    return mp_const_none;
                }
                return mp_const_none;
            }
            ESP_LOGE(LOG_TAG, "Failed to load %s after %d retries", filename, MAX_SCHEDULE_RETRIES);
        }

next_file:
// Move to next file
        ctx->current_index++;
        ctx->retry_count = 0;

// If there are more files, schedule loading the next one
        if (ctx->current_index < ctx->file_count) {
            if (!mp_sched_schedule((mp_obj_t)&do_load_all_handler_obj, 
                                 MP_OBJ_FROM_PTR(ctx))) {
                ESP_LOGE(LOG_TAG, "Failed to schedule next file load");
                free(ctx);
                return mp_const_none;
            }
            return mp_const_none;
        }
    }

    ESP_LOGI(LOG_TAG, "Completed loading all devices");


    ctx->storage_cb_obj = ctx->zig_obj_mp = NULL;
    free(ctx);
    return mp_const_none;
}

esp_err_t device_storage_load_all(esp32_zig_obj_t *self) {
    if (!self->storage_cb || self->storage_cb == mp_const_none) {
        ESP_LOGW(LOG_TAG, "No storage callback");
        return ESP_ERR_INVALID_STATE;
    }

    load_all_ctx_t *ctx = malloc(sizeof(load_all_ctx_t));
    if (!ctx) {
        ESP_LOGE(LOG_TAG, "Failed to allocate context");
        return ESP_ERR_NO_MEM;
    }

    ctx->storage_cb_obj = self->storage_cb;
    ctx->zig_obj_mp = MP_OBJ_FROM_PTR(self);
    ctx->files = NULL;
    ctx->file_count = 0;
    ctx->current_index = 0;
    ctx->retry_count = 0;

    if (!mp_sched_schedule((mp_obj_t)&do_load_all_handler_obj, MP_OBJ_FROM_PTR(ctx))) {
        ESP_LOGE(LOG_TAG, "Failed to schedule load all");
        free(ctx);
        return ESP_FAIL;
    }

    ESP_LOGI(LOG_TAG, "Load all devices scheduled");
    return ESP_OK;
}

// Structure for data transfer during deletion
typedef struct {
    char *filename;
    mp_obj_t callback;
} device_remove_data_t;

// Deletion handler in Python context
static mp_obj_t do_device_remove_handler(mp_obj_t schedule_data_in) {
    device_remove_data_t *data = MP_OBJ_TO_PTR(schedule_data_in);
    
    mp_obj_t remove_cmd = mp_obj_new_str("remove", 6);
    mp_obj_t filename = mp_obj_new_str(data->filename, strlen(data->filename));
    
    mp_obj_t args[2] = {remove_cmd, filename};
    mp_call_function_n_kw(data->callback, 2, 0, args);

    free(data->filename);
    free(data);
    
    return mp_const_none;
}

static MP_DEFINE_CONST_FUN_OBJ_1(do_device_remove_handler_obj, do_device_remove_handler);

esp_err_t device_storage_remove(esp32_zig_obj_t *self, uint16_t short_addr) {
    if (!self->storage_cb || self->storage_cb == mp_const_none) {
        ESP_LOGW(LOG_TAG, "No storage callback");
        return ESP_ERR_INVALID_STATE;
    }

// Generate filename
    char *filename = malloc(MAX_FILENAME_LEN);
    if (!filename) {
        ESP_LOGE(LOG_TAG, "Failed to allocate filename");
        return ESP_ERR_NO_MEM;
    }
    snprintf(filename, MAX_FILENAME_LEN, "%04hx.json", short_addr);  // Use %04hx for unified format

// Prepare data for scheduler
    device_remove_data_t *data = malloc(sizeof(device_remove_data_t));
    if (!data) {
        ESP_LOGE(LOG_TAG, "Failed to allocate remove data");
        free(filename);
        return ESP_ERR_NO_MEM;
    }

    data->filename = filename;
    data->callback = self->storage_cb;

// Schedule execution in Python context
    mp_obj_t handler = MP_OBJ_FROM_PTR(&do_device_remove_handler_obj);
    mp_obj_t args = MP_OBJ_FROM_PTR(data);
    
    if (!mp_sched_schedule(handler, args)) {
        ESP_LOGE(LOG_TAG, "Failed to schedule remove operation");
        free(filename);
        free(data);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

// Save retry handler
static mp_obj_t do_save_retry_handler(mp_obj_t arg_tuple_in) {
    // Unpack the tuple: (storage_cb_obj, short_addr_obj, json_str_obj, retry_count_obj)
    if (!mp_obj_is_type(arg_tuple_in, &mp_type_tuple)) {
        ESP_LOGE(LOG_TAG, "Argument to do_save_retry_handler is not a tuple");
        return mp_const_none;
    }
    mp_obj_tuple_t *arg_tuple_ptr = MP_OBJ_TO_PTR(arg_tuple_in);
    size_t items_len = arg_tuple_ptr->len;
    mp_obj_t *items = arg_tuple_ptr->items;

    if (items_len != 4) {
        ESP_LOGE(LOG_TAG, "Invalid argument tuple length in do_save_retry_handler");
        return mp_const_none;
    }

    mp_obj_t storage_cb_obj = items[0];
    uint16_t short_addr = mp_obj_get_int(items[1]);
    mp_obj_t json_obj_str = items[2];
    int retry_count = mp_obj_get_int(items[3]);
    
    ESP_LOGI(LOG_TAG, "Retry Handler: Processing save attempt %d for device 0x%04x", 
             retry_count + 1, short_addr);
    
    if (!storage_cb_obj || storage_cb_obj == mp_const_none || !mp_obj_is_callable(storage_cb_obj)) {
        ESP_LOGE(LOG_TAG, "Retry Handler: storage_cb_obj is not valid/callable for device 0x%04x.", short_addr);
        // No cleanup_and_next_schedule here as this is a scheduled task for a specific save.
        // The main queue processing is independent.
        return mp_const_none;
    }
        
// Generate filename
    char filename[16];
    snprintf(filename, sizeof(filename), "%04x.json", short_addr);
    
    ESP_LOGI(LOG_TAG, "Retry Handler: Calling Python storage callback for device 0x%04x (filename: %s)", 
             short_addr, filename);
    
// Create arguments for callback invocation
    mp_obj_t save_cmd = mp_obj_new_str("save", 4);
    mp_obj_t filename_obj = mp_obj_new_str(filename, strlen(filename));
    
    mp_obj_t args_for_cb[3] = {save_cmd, filename_obj, json_obj_str};

    mp_obj_t result = mp_call_function_n_kw(storage_cb_obj, 3, 0, args_for_cb); // Call storage_cb_obj directly
    
    if (result != mp_const_none) {
        ESP_LOGI(LOG_TAG, "Successfully saved device 0x%04x on attempt %d", 
                 short_addr, retry_count); // retry_count is 0-indexed
    } else {
        retry_count++;
        if (retry_count < 3) {
            ESP_LOGW(LOG_TAG, "Save failed for 0x%04x, scheduling retry %d/3", 
                    short_addr, retry_count + 1); // User-facing retry_count is 1-indexed

            // Prepare new tuple for next retry
            mp_obj_t next_retry_items[4];
            next_retry_items[0] = storage_cb_obj; // Pass the callback object itself for the next retry
            next_retry_items[1] = items[1]; // short_addr_obj
            next_retry_items[2] = json_obj_str;
            next_retry_items[3] = mp_obj_new_int(retry_count);
            mp_obj_t next_retry_arg_tuple = mp_obj_new_tuple(4, next_retry_items);

// Schedule next attempt through scheduler
            if (!mp_sched_schedule((mp_obj_t)&do_save_retry_handler_obj, 
                               next_retry_arg_tuple)) {
                ESP_LOGE(LOG_TAG, "Failed to schedule retry for device 0x%04x", short_addr);
                // If scheduling fails, the tuple 'next_retry_arg_tuple' might not be rooted.
                // However, the original 'arg_tuple_in' and its contents are still valid for this call.
            } else {
// Exit without freeing context, it will be used in the next attempt
                return mp_const_none; 
            }
        } else {
            ESP_LOGE(LOG_TAG, "Failed to save 0x%04x after 3 retries", short_addr);
        }
    }
    
// No explicit free needed for tuple items as they are mp_obj_t and GC managed.
// The tuple 'arg_tuple_in' itself will be handled by GC after this function returns if not re-scheduled.
    
    return mp_const_none;
}

esp_err_t device_storage_load(esp32_zig_obj_t *self, uint16_t short_addr) {
    if (!self->storage_cb || self->storage_cb == mp_const_none) {
        ESP_LOGW(LOG_TAG, "No storage callback");
        return ESP_ERR_INVALID_STATE;
    }

// Generate filename
    char filename[MAX_FILENAME_LEN];
    snprintf(filename, sizeof(filename), "%04hx.json", short_addr);

// Load file through callback
    mp_obj_t load_cmd = mp_obj_new_str("load", 4);
    mp_obj_t fname = mp_obj_new_str(filename, strlen(filename));
    mp_obj_t args[2] = {load_cmd, fname};
    mp_obj_t json_str = mp_call_function_n_kw(self->storage_cb, 2, 0, args);

    if (json_str == mp_const_none) {
        ESP_LOGE(LOG_TAG, "Failed to load device 0x%04x", short_addr);
        return ESP_ERR_NOT_FOUND;
    }

// Parse JSON
    const char *json_data = mp_obj_str_get_str(json_str);
    cJSON *json = cJSON_Parse(json_data);
    if (!json) {
        ESP_LOGE(LOG_TAG, "Failed to parse JSON for device 0x%04x", short_addr);
        return ESP_ERR_INVALID_STATE;
    }

// Create temporary device and fill it with data
    zigbee_device_t device = {0};
    esp_err_t err = device_from_json(json, &device, MP_OBJ_FROM_PTR(self));
    cJSON_Delete(json);

    if (err != ESP_OK) {
        ESP_LOGE(LOG_TAG, "Failed to parse device data for 0x%04x: %s", 
                 short_addr, esp_err_to_name(err));
        return err;
    }

// Update device in manager
    device_manager_update(&device);
    ESP_LOGI(LOG_TAG, "Successfully loaded device 0x%04x", short_addr);
    
    return ESP_OK;
}

