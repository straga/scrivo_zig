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
#define SCHEDULE_RETRY_DELAY_MS 5000  // Increase to 5 seconds - enough to collect all attributes

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
    mp_obj_t callback;    // Python callback for saving
} device_save_data_t;

// Save queue state (simplified)
typedef struct {
    device_save_data_t items[MAX_SAVE_QUEUE];
    int count;                    // Number of devices in the queue
    uint32_t next_save_time;      // Time of next allowed save
} save_queue_t;

static save_queue_t save_queue = {
    .count = 0
};

static bool save_in_progress = false;

// Check if device is in the queue
static bool is_device_in_queue(uint16_t short_addr) {
    for (int i = 0; i < save_queue.count; i++) {
        if (save_queue.items[i].short_addr == short_addr) {
            return true;
        }
    }
    return false;
}

// Add to queue
static bool queue_push(uint16_t short_addr, mp_obj_t callback) {
// If device is already in queue - skip
    if (is_device_in_queue(short_addr)) {
        ESP_LOGD(LOG_TAG, "Skipping save for device 0x%04x - already in queue", short_addr);
        return true;
    }
    
// Check queue size
    if (save_queue.count >= MAX_SAVE_QUEUE) {
        ESP_LOGW(LOG_TAG, "Save queue full, skipping save for device 0x%04x", short_addr);
        return false;
    }
    
// Add new element to the end of queue
    save_queue.items[save_queue.count].short_addr = short_addr;
    save_queue.items[save_queue.count].callback = callback;
    save_queue.count++;
    
    ESP_LOGI(LOG_TAG, "Device 0x%04x queued for save (queue size: %d)", short_addr, save_queue.count);
    
    return true;
}

// Schedule next save
static void schedule_next_save(void) {
    if (save_in_progress || save_queue.count == 0) {
        return;
    }

// Check if it's not too early for the next save
    uint32_t current_time = mp_hal_ticks_ms();
    if (current_time < save_queue.next_save_time) {
        return;
    }

    save_in_progress = true;

// Schedule saving through MicroPython scheduler
    if (!mp_sched_schedule((mp_obj_t)&do_device_save_handler_obj, MP_OBJ_FROM_PTR(&save_queue.items[0]))) {
        ESP_LOGE(LOG_TAG, "Failed to schedule save handler");
        save_in_progress = false;
// Next attempt will be after SCHEDULE_RETRY_DELAY_MS
        save_queue.next_save_time = current_time + SCHEDULE_RETRY_DELAY_MS;
    }
}

// Structure for tracking save retry attempts
typedef struct {
    uint16_t short_addr;
    mp_obj_t callback;
    int retry_count;
    char *json_str;
} save_retry_ctx_t;

// Save handler in Python context
static mp_obj_t do_device_save_handler(mp_obj_t schedule_data_in) {
    device_save_data_t *data = MP_OBJ_TO_PTR(schedule_data_in);
    
// Get current device state
    zigbee_device_t *device = device_manager_get(data->short_addr);
    if (!device) {
        ESP_LOGE(LOG_TAG, "Device 0x%04x not found for save", data->short_addr);
        goto cleanup;
    }

// Create JSON from current state
    cJSON *json = device_to_json(device);
    if (!json) {
        ESP_LOGE(LOG_TAG, "Failed to create JSON for device 0x%04x", data->short_addr);
        goto cleanup;
    }

    char *json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    if (!json_str) {
        ESP_LOGE(LOG_TAG, "Failed to print JSON for device 0x%04x", data->short_addr);
        goto cleanup;
    }

// Create retry context
    save_retry_ctx_t *retry_ctx = malloc(sizeof(save_retry_ctx_t));
    if (!retry_ctx) {
        ESP_LOGE(LOG_TAG, "Failed to allocate retry context");
        free(json_str);
        goto cleanup;
    }
    
    retry_ctx->short_addr = data->short_addr;
    retry_ctx->callback = data->callback;
    retry_ctx->retry_count = 0;
    retry_ctx->json_str = json_str;
    
// Start first save attempt
    if (!mp_sched_schedule((mp_obj_t)&do_save_retry_handler_obj, MP_OBJ_FROM_PTR(retry_ctx))) {
        ESP_LOGE(LOG_TAG, "Failed to schedule initial save for device 0x%04x", data->short_addr);
        free(json_str);
        free(retry_ctx);
    }

cleanup:
// Shift all elements to the left
    if (save_queue.count > 1) {
        memmove(&save_queue.items[0], &save_queue.items[1], 
                (save_queue.count - 1) * sizeof(device_save_data_t));
    }
    save_queue.count--;
    
// Set time for next allowed save
    save_queue.next_save_time = mp_hal_ticks_ms() + SCHEDULE_RETRY_DELAY_MS;
    
    save_in_progress = false;
    
// Schedule next save if there are items in the queue
    schedule_next_save();
    
    return mp_const_none;
}

esp_err_t device_storage_save(esp32_zig_obj_t *self, uint16_t short_addr) {
    if (!self->storage_cb || self->storage_cb == mp_const_none) {
        ESP_LOGW(LOG_TAG, "No storage callback");
        return ESP_ERR_INVALID_STATE;
    }

// Get device
    zigbee_device_t *device = device_manager_get(short_addr);
    if (!device) {
        ESP_LOGE(LOG_TAG, "Device not found: 0x%04x", short_addr);
        return ESP_ERR_NOT_FOUND;
    }

// Add to save queue
    if (!queue_push(short_addr, self->storage_cb)) {
        return ESP_ERR_NO_MEM;
    }

// Schedule next save
    schedule_next_save();
    
    return ESP_OK;
}

// Structure for loading all devices
typedef struct {
    mp_obj_t callback;
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
        mp_obj_t file_list = mp_call_function_n_kw(ctx->callback, 1, 0, args);

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
        mp_obj_t json_str = mp_call_function_n_kw(ctx->callback, 2, 0, load_args);

        if (json_str != mp_const_none) {
            const char *json_data = mp_obj_str_get_str(json_str);
            cJSON *json = cJSON_Parse(json_data);
            
            if (json) {
                zigbee_device_t device = {0};
                if (device_from_json(json, &device) == ESP_OK) {
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

    ctx->callback = self->storage_cb;
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
static mp_obj_t do_save_retry_handler(mp_obj_t retry_data_in) {
    save_retry_ctx_t *ctx = MP_OBJ_TO_PTR(retry_data_in);
    bool success = false;
    
// Generate filename
    char filename[16];
    snprintf(filename, sizeof(filename), "%04x.json", ctx->short_addr);
    
// Create arguments for callback invocation
    mp_obj_t save_cmd = mp_obj_new_str("save", 4);
    mp_obj_t filename_obj = mp_obj_new_str(filename, strlen(filename));
    mp_obj_t json_str_obj = mp_obj_new_str(ctx->json_str, strlen(ctx->json_str));
    
    mp_obj_t args[3] = {save_cmd, filename_obj, json_str_obj};
    mp_obj_t result = mp_call_function_n_kw(ctx->callback, 3, 0, args);
    
    if (result != mp_const_none) {
        ESP_LOGI(LOG_TAG, "Successfully saved device 0x%04x on retry %d", 
                 ctx->short_addr, ctx->retry_count);
        success = true;
    } else {
        ctx->retry_count++;
        if (ctx->retry_count < 3) {
            ESP_LOGW(LOG_TAG, "Save failed for 0x%04x, scheduling retry %d/3", 
                    ctx->short_addr, ctx->retry_count + 1);
// Schedule next attempt through scheduler
            if (!mp_sched_schedule((mp_obj_t)&do_save_retry_handler_obj, 
                               MP_OBJ_FROM_PTR(ctx))) {
                ESP_LOGE(LOG_TAG, "Failed to schedule retry for device 0x%04x", ctx->short_addr);
                success = false;
            } else {
// Exit without freeing context, it will be used in the next attempt
                return mp_const_none;
            }
        } else {
            ESP_LOGE(LOG_TAG, "Failed to save 0x%04x after 3 retries", ctx->short_addr);
            success = false;
        }
    }
    
// Clear resources if no longer needed
    if (!success || ctx->retry_count >= 3) {
        free(ctx->json_str);
        free(ctx);
    }
    
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
    esp_err_t err = device_from_json(json, &device);
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

