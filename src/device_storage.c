// Copyright (c) 2025 Viktor Vorobjov
// Device storage implementation - Simplified version without FreeRTOS queues
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "py/runtime.h"
#include "py/obj.h"
#include "py/mpstate.h"
#include "py/gc.h"
#include "esp_log.h"

#include "device_storage.h"
#include "device_manager.h"
#include "device_json.h"
#include "cJSON.h"

// Safety macros
#define CHECK_NULL(ptr, msg) do { \
    if (!(ptr)) { \
        ESP_LOGE(LOG_TAG, msg); \
        return mp_const_none; \
    } \
} while(0)

#define CHECK_USED(ctx, msg) do { \
    if (!(ctx)->storage_cb_obj) { \
        ESP_LOGE(LOG_TAG, msg); \
        free(ctx); \
        return mp_const_none; \
    } \
} while(0)

#define SAFE_FREE(ptr) do { \
    if (ptr) { \
        free(ptr); \
        ptr = NULL; \
    } \
} while(0)

#define LOG_CALLBACK_STATUS(obj, cb) do { \
    ESP_LOGI(LOG_TAG, "Callback status: obj=%p, cb=%p, type=%d, callable=%d, none=%d", \
             obj, cb, \
             cb ? mp_obj_get_type(cb)->name : 0, \
             mp_obj_is_callable(cb), \
             cb == mp_const_none); \
} while(0)

#define TRACE_MALLOC(size) do { \
    ESP_LOGD(LOG_TAG, "Allocating %d bytes", size); \
} while(0)

#define TRACE_FREE(ptr) do { \
    ESP_LOGD(LOG_TAG, "Freeing pointer %p", ptr); \
} while(0)

#define LOG_TAG "DEVICE_STORAGE"
#define MAX_FILENAME_LEN 32
#define MAX_SCHEDULE_RETRIES 5

// Use external global pointer declared in main.h
// This pointer is already registered as GC root in main.h
// and protects the entire zigbee object and its callbacks from garbage collection
extern mp_obj_t global_esp32_zig_obj_ptr;

// Forward declarations
static mp_obj_t do_device_save_handler(mp_obj_t short_addr_obj);
static mp_obj_t do_device_remove_handler(mp_obj_t short_addr_obj);
static mp_obj_t do_load_all_handler(mp_obj_t ctx_in);
void device_storage_update_callback(void);

// Declare function objects after forward declarations
static MP_DEFINE_CONST_FUN_OBJ_1(do_device_save_handler_obj, do_device_save_handler);
static MP_DEFINE_CONST_FUN_OBJ_1(do_device_remove_handler_obj, do_device_remove_handler);
static MP_DEFINE_CONST_FUN_OBJ_1(do_load_all_handler_obj, do_load_all_handler);

// Structure for loading all devices
typedef struct {
    mp_obj_t storage_cb_obj;
    mp_obj_t zig_obj_mp;
    mp_obj_t *files;
    size_t file_count;
    size_t current_index;
    int retry_count;
} load_all_ctx_t;

// Add structure for save event
typedef struct {
    uint16_t short_addr;
} save_event_t;

// Global queue for save events
static QueueHandle_t save_event_queue = NULL;
#define SAVE_EVENT_QUEUE_SIZE 10

// Add semaphore for device loading synchronization
static SemaphoreHandle_t device_load_complete_semaphore = NULL;

// Initialize event queue
esp_err_t device_storage_init(void) {
    // Initialize queue if not already initialized
    if (save_event_queue == NULL) {
        save_event_queue = xQueueCreate(SAVE_EVENT_QUEUE_SIZE, sizeof(save_event_t));
        if (save_event_queue == NULL) {
            ESP_LOGE(LOG_TAG, "Failed to create save event queue");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(LOG_TAG, "Save queue initialized with size %d", SAVE_EVENT_QUEUE_SIZE);
    } else {
        ESP_LOGW(LOG_TAG, "Save queue already initialized");
    }

    // Initialize semaphore if not already initialized
    if (device_load_complete_semaphore == NULL) {
        device_load_complete_semaphore = xSemaphoreCreateBinary();
        if (device_load_complete_semaphore == NULL) {
            ESP_LOGE(LOG_TAG, "Failed to create device load semaphore");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(LOG_TAG, "Device load semaphore initialized");
    }

    return ESP_OK;
}

// Free event queue
void device_storage_deinit(void) {
    if (save_event_queue) {
        // Clear queue before deletion
        save_event_t item;
        while (xQueueReceive(save_event_queue, &item, 0) == pdTRUE) {
            ESP_LOGD(LOG_TAG, "Clearing device 0x%04x from queue", item.short_addr);
        }
        
        vQueueDelete(save_event_queue);
        save_event_queue = NULL;
        ESP_LOGI(LOG_TAG, "Save queue deinitialized");
    }

    if (device_load_complete_semaphore) {
        vSemaphoreDelete(device_load_complete_semaphore);
        device_load_complete_semaphore = NULL;
        ESP_LOGI(LOG_TAG, "Device load semaphore deinitialized");
    }
}

// Queue management functions
static bool queue_contains_device(uint16_t short_addr) {
    if (!save_event_queue) {
        return false;
    }

    // Create temporary queue for checking
    QueueHandle_t temp_queue = xQueueCreate(SAVE_EVENT_QUEUE_SIZE, sizeof(save_event_t));
    if (!temp_queue) {
        ESP_LOGE(LOG_TAG, "Failed to create temporary queue");
        return false;
    }

    bool found = false;
    save_event_t item;

    // Check all items in the queue
    while (xQueueReceive(save_event_queue, &item, 0) == pdTRUE) {
        if (item.short_addr == short_addr) {
            found = true;
        }
        xQueueSend(temp_queue, &item, 0);
    }

    // Restore original queue
    while (xQueueReceive(temp_queue, &item, 0) == pdTRUE) {
        xQueueSend(save_event_queue, &item, 0);
    }

    vQueueDelete(temp_queue);
    return found;
}

static bool queue_push_simple(uint16_t short_addr) {
    if (!save_event_queue) {
        ESP_LOGE(LOG_TAG, "Save queue not initialized");
        return false;
    }

    // Check if device is already in queue
    if (queue_contains_device(short_addr)) {
        ESP_LOGD(LOG_TAG, "Device 0x%04x already in save queue", short_addr);
        return true;
    }

    // Create new save event
    save_event_t event = {
        .short_addr = short_addr
    };

    // Add to queue
    if (xQueueSend(save_event_queue, &event, 0) != pdTRUE) {
        ESP_LOGE(LOG_TAG, "Failed to add device 0x%04x to save queue", short_addr);
        return false;
    }

    ESP_LOGD(LOG_TAG, "Device 0x%04x added to save queue", short_addr);
    return true;
}

// Modified save function to use queue
esp_err_t device_storage_save(esp32_zig_obj_t *self, uint16_t short_addr) {
    // Check input parameters
    if (!self) {
        ESP_LOGE(LOG_TAG, "Invalid self pointer");
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize queue if not initialized
    if (save_event_queue == NULL) {
        esp_err_t err = device_storage_init();
        if (err != ESP_OK) {
            ESP_LOGE(LOG_TAG, "Failed to initialize save queue");
            return err;
        }
    }

    // Get object through global pointer
    esp32_zig_obj_t *zig_self = (esp32_zig_obj_t *)MP_OBJ_TO_PTR(global_esp32_zig_obj_ptr);
    if (!zig_self) {
        ESP_LOGE(LOG_TAG, "Invalid zig_self pointer");
        return ESP_ERR_INVALID_STATE;
    }

    if (!zig_self->storage_cb || zig_self->storage_cb == mp_const_none) {
        ESP_LOGW(LOG_TAG, "No storage callback");
        return ESP_ERR_INVALID_STATE;
    }

    // Check if device exists
    zigbee_device_t *device = device_manager_get(short_addr);
    if (!device) {
        ESP_LOGE(LOG_TAG, "Device not found: 0x%04x", short_addr);
        return ESP_ERR_NOT_FOUND;
    }

    // Add to save queue
    if (!queue_push_simple(short_addr)) {
        ESP_LOGE(LOG_TAG, "Failed to queue device 0x%04x for saving", short_addr);
        return ESP_ERR_NO_MEM;
    }

    // Create object for scheduler
    mp_obj_t short_addr_obj = mp_obj_new_int(short_addr);
    if (!short_addr_obj) {
        ESP_LOGE(LOG_TAG, "Failed to create short_addr object");
        return ESP_ERR_NO_MEM;
    }

    // Schedule save operation
    if (!mp_sched_schedule((mp_obj_t)&do_device_save_handler_obj, short_addr_obj)) {
        ESP_LOGE(LOG_TAG, "Failed to schedule save handler for device 0x%04x", short_addr);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGD(LOG_TAG, "Device 0x%04x queued for saving", short_addr);
    return ESP_OK;
}

// Save event handler
static mp_obj_t do_device_save_handler(mp_obj_t short_addr_obj) {
    // Check input parameter
    if (short_addr_obj == mp_const_none) {
        ESP_LOGE(LOG_TAG, "Invalid short_addr_obj");
        return mp_const_none;
    }

    // Update pointer to current callback location
    device_storage_update_callback();

    // Safely get short_addr
    mp_int_t short_addr;
    if (!mp_obj_get_int_maybe(short_addr_obj, &short_addr)) {
        ESP_LOGE(LOG_TAG, "Invalid short_addr format");
        return mp_const_none;
    }

    // Get object through global pointer
    esp32_zig_obj_t *zig_self = (esp32_zig_obj_t *)MP_OBJ_TO_PTR(global_esp32_zig_obj_ptr);
    if (!zig_self) {
        ESP_LOGE(LOG_TAG, "Invalid zig_self pointer");
        return mp_const_none;
    }

    // Check callback
    if (!zig_self->storage_cb || zig_self->storage_cb == mp_const_none) {
        ESP_LOGE(LOG_TAG, "No storage callback");
        return mp_const_none;
    }

    // Get device
    zigbee_device_t *dev = device_manager_get((uint16_t)short_addr);
    if (!dev) {
        ESP_LOGW(LOG_TAG, "Device 0x%04x not found", (uint16_t)short_addr);
        return mp_const_none;
    }

    // Create JSON
    cJSON *json = device_to_json(dev);
    if (!json) {
        ESP_LOGE(LOG_TAG, "Failed to create JSON for device 0x%04x", (uint16_t)short_addr);
        return mp_const_none;
    }

    char *json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    if (!json_str) {
        ESP_LOGE(LOG_TAG, "Failed to print JSON for device 0x%04x", (uint16_t)short_addr);
        return mp_const_none;
    }

    // Create filename
    char filename[16];
    snprintf(filename, sizeof(filename), "%04x.json", (uint16_t)short_addr);

    // Create arguments for callback
    mp_obj_t args[3] = {
        mp_obj_new_str("save", 4),
        mp_obj_new_str(filename, strlen(filename)),
        mp_obj_new_str(json_str, strlen(json_str))
    };

    // Call callback
    mp_obj_t result = mp_call_function_n_kw(zig_self->storage_cb, 3, 0, args);
    SAFE_FREE(json_str);

    if (result == mp_const_none) {
        ESP_LOGW(LOG_TAG, "Storage callback returned None for device 0x%04x", (uint16_t)short_addr);
    }

    return mp_const_none;
}

// Function to update callback pointer to current location
void device_storage_update_callback(void) {
    // Get object from global pointer
    esp32_zig_obj_t *zig_self = (esp32_zig_obj_t *)MP_OBJ_TO_PTR(global_esp32_zig_obj_ptr);
    if (!zig_self) {
        ESP_LOGE(LOG_TAG, "Invalid zig_self pointer");
        return;
    }

    // Check if callback exists and is valid
    if (zig_self->storage_cb && zig_self->storage_cb != mp_const_none) {
        ESP_LOGD(LOG_TAG, "Callback is valid: %p", zig_self->storage_cb);
    } else {
        ESP_LOGW(LOG_TAG, "No valid callback in object");
    }
}

// Function to set callback
void device_storage_set_callback(mp_obj_t cb) {
    if (cb != mp_const_none) {
        esp32_zig_obj_t *zig_self = (esp32_zig_obj_t *)MP_OBJ_TO_PTR(global_esp32_zig_obj_ptr);
        if (zig_self) {
            zig_self->storage_cb = cb;
        }
    }
}

// Single handler for loading all devices
static mp_obj_t do_load_all_handler(mp_obj_t ctx_in) {
    // Check input parameter
    if (ctx_in == mp_const_none) {
        ESP_LOGE(LOG_TAG, "Invalid context: None");
        return mp_const_none;
    }

    load_all_ctx_t *ctx = MP_OBJ_TO_PTR(ctx_in);
    if (!ctx) {
        ESP_LOGE(LOG_TAG, "Invalid context pointer");
        return mp_const_none;
    }

    if (!ctx->storage_cb_obj) {
        ESP_LOGE(LOG_TAG, "Context already used");
        SAFE_FREE(ctx);
        return mp_const_none;
    }
    
    // Update pointer to current callback location
    device_storage_update_callback();
    
    bool success = false;
    cJSON *json = NULL;
    mp_obj_t *files = NULL;
    size_t file_count = 0;
    
    // Get file list on first run
    if (ctx->current_index == 0 && ctx->retry_count == 0) {
        mp_obj_t list_cmd = mp_obj_new_str("list", 4);
        mp_obj_t args[1] = {list_cmd};
        mp_obj_t file_list = mp_call_function_n_kw(ctx->storage_cb_obj, 1, 0, args);

        if (file_list == mp_const_none) {
            ESP_LOGE(LOG_TAG, "Failed to get file list");
            ctx->storage_cb_obj = ctx->zig_obj_mp = NULL;
            SAFE_FREE(ctx);
            return mp_const_none;
        }

        mp_obj_get_array(file_list, &file_count, &files);
        if (file_count == 0) {
            ESP_LOGD(LOG_TAG, "No files to load");
            ctx->storage_cb_obj = ctx->zig_obj_mp = NULL;
            SAFE_FREE(ctx);
            return mp_const_none;
        }

        // Save the list of files in the context
        ctx->files = files;
        ctx->file_count = file_count;
    }

    // If there is a file to load
    if (ctx->current_index < ctx->file_count) {
        // Add GC protection for the file object
        mp_obj_t file_obj = ctx->files[ctx->current_index];
        if (!MP_OBJ_IS_STR(file_obj)) {
            ESP_LOGE(LOG_TAG, "Invalid file object type at index %d", ctx->current_index);
            goto next_file;
        }
        
        const char *filename = mp_obj_str_get_str(file_obj);
        if (!filename) {
            ESP_LOGE(LOG_TAG, "Invalid filename at index %d", ctx->current_index);
            goto next_file;
        }

        uint16_t short_addr;
        if (sscanf(filename, "%04hx.json", &short_addr) != 1) {
            ESP_LOGW(LOG_TAG, "Invalid filename format: %s", filename);
            goto next_file;
        }

        // Load device
        mp_obj_t load_cmd = mp_obj_new_str("load", 4);
        mp_obj_t fname = mp_obj_new_str(filename, strlen(filename));
        if (!load_cmd || !fname) {
            ESP_LOGE(LOG_TAG, "Failed to create command objects");
            goto next_file;
        }
        
        mp_obj_t load_args[2] = {load_cmd, fname};
        mp_obj_t json_str = mp_call_function_n_kw(ctx->storage_cb_obj, 2, 0, load_args);

        if (json_str != mp_const_none) {
            if (!MP_OBJ_IS_STR(json_str)) {
                ESP_LOGE(LOG_TAG, "Invalid JSON string type for device 0x%04x", short_addr);
                goto next_file;
            }
            
            const char *json_data = mp_obj_str_get_str(json_str);
            if (!json_data) {
                ESP_LOGE(LOG_TAG, "Invalid JSON data for device 0x%04x", short_addr);
                goto next_file;
            }

            json = cJSON_Parse(json_data);
            if (!json) {
                ESP_LOGE(LOG_TAG, "Failed to parse JSON for device 0x%04x", short_addr);
                goto next_file;
            }
            
            zigbee_device_t device = {0};
            if (device_from_json(json, &device, ctx->zig_obj_mp) == ESP_OK) {
                device_manager_add_new_device(device.short_addr, device.ieee_addr, ctx->zig_obj_mp);
                device_manager_update(&device);
                success = true;
                ESP_LOGD(LOG_TAG, "Loaded device 0x%04x from %s", short_addr, filename);
            }
            cJSON_Delete(json);
        }

        if (!success) {
            ctx->retry_count++;
            if (ctx->retry_count < MAX_SCHEDULE_RETRIES) {
                ESP_LOGW(LOG_TAG, "Load failed for %s, retry %d/%d", filename, ctx->retry_count, MAX_SCHEDULE_RETRIES);
                if (!mp_sched_schedule((mp_obj_t)&do_load_all_handler_obj, MP_OBJ_FROM_PTR(ctx))) {
                    ESP_LOGE(LOG_TAG, "Failed to schedule retry for %s", filename);
                    ctx->storage_cb_obj = ctx->zig_obj_mp = NULL;
                    SAFE_FREE(ctx);
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
            if (!mp_sched_schedule((mp_obj_t)&do_load_all_handler_obj, MP_OBJ_FROM_PTR(ctx))) {
                ESP_LOGE(LOG_TAG, "Failed to schedule next file load");
                ctx->storage_cb_obj = ctx->zig_obj_mp = NULL;
                SAFE_FREE(ctx);
                return mp_const_none;
            }
            return mp_const_none;
        }
    }

    ESP_LOGD(LOG_TAG, "Load all completed");
    // Signal that loading is complete and delete semaphore
    if (device_load_complete_semaphore) {
        xSemaphoreGive(device_load_complete_semaphore);
        ESP_LOGD(LOG_TAG, "Device load complete semaphore given");
        vSemaphoreDelete(device_load_complete_semaphore);
        device_load_complete_semaphore = NULL;
        ESP_LOGD(LOG_TAG, "Device load semaphore deleted");
    }

    // Clear the context
    ctx->storage_cb_obj = ctx->zig_obj_mp = NULL;
    SAFE_FREE(ctx);
    return mp_const_none;
}

esp_err_t device_storage_load_all(esp32_zig_obj_t *self) {
    // Check input parameters
    if (!self) {
        ESP_LOGE(LOG_TAG, "Invalid self pointer");
        return ESP_ERR_INVALID_ARG;
    }

    if (!self->storage_cb || self->storage_cb == mp_const_none) {
        ESP_LOGW(LOG_TAG, "No storage callback");
        return ESP_ERR_INVALID_STATE;
    }

    // Initialize semaphore if not already initialized
    if (device_load_complete_semaphore == NULL) {
        device_load_complete_semaphore = xSemaphoreCreateBinary();
        if (device_load_complete_semaphore == NULL) {
            ESP_LOGE(LOG_TAG, "Failed to create device load semaphore");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(LOG_TAG, "Device load semaphore initialized");
    }

    // Allocate memory for context
    TRACE_MALLOC(sizeof(load_all_ctx_t));
    load_all_ctx_t *ctx = malloc(sizeof(load_all_ctx_t));
    if (!ctx) {
        ESP_LOGE(LOG_TAG, "Failed to allocate context");
        return ESP_ERR_NO_MEM;
    }

    // Initialize context
    memset(ctx, 0, sizeof(load_all_ctx_t));
    ctx->storage_cb_obj = self->storage_cb;
    ctx->zig_obj_mp = MP_OBJ_FROM_PTR(self);
    ctx->files = NULL;
    ctx->file_count = 0;
    ctx->current_index = 0;
    ctx->retry_count = 0;

    // Schedule loading
    if (!mp_sched_schedule((mp_obj_t)&do_load_all_handler_obj, MP_OBJ_FROM_PTR(ctx))) {
        ESP_LOGE(LOG_TAG, "Failed to schedule load all");
        SAFE_FREE(ctx);
        return ESP_FAIL;
    }

    ESP_LOGD(LOG_TAG, "Load all scheduled");
    return ESP_OK;
}

// Deletion handler in Python context
static mp_obj_t do_device_remove_handler(mp_obj_t short_addr_obj) {
    // Check input parameter
    if (short_addr_obj == mp_const_none) {
        ESP_LOGE(LOG_TAG, "Invalid short_addr_obj: None");
        return mp_const_none;
    }

    // Update pointer to current callback location
    //device_storage_update_callback();

    // Safely get short_addr
    mp_int_t short_addr;
    if (!mp_obj_get_int_maybe(short_addr_obj, &short_addr)) {
        ESP_LOGE(LOG_TAG, "Failed to convert short_addr_obj to integer");
        return mp_const_none;
    }
    
    // Get callback directly from global zigbee object
    esp32_zig_obj_t *zig_self = (esp32_zig_obj_t *)MP_OBJ_TO_PTR(global_esp32_zig_obj_ptr);
    if (!zig_self) {
        ESP_LOGE(LOG_TAG, "Invalid zig_self pointer");
        return mp_const_none;
    }
    
    if (!zig_self->storage_cb || zig_self->storage_cb == mp_const_none) {
        ESP_LOGE(LOG_TAG, "No valid storage callback for remove");
        return mp_const_none;
    }
    
    char filename[MAX_FILENAME_LEN];
    snprintf(filename, sizeof(filename), "%04hx.json", (uint16_t)short_addr);
    
    mp_obj_t remove_cmd = mp_obj_new_str("remove", 6);
    mp_obj_t filename_obj = mp_obj_new_str(filename, strlen(filename));
    
    mp_obj_t args[2] = {remove_cmd, filename_obj};
    mp_obj_t result = mp_call_function_n_kw(zig_self->storage_cb, 2, 0, args);
    
    if (result == mp_const_none) {
        ESP_LOGW(LOG_TAG, "Remove callback returned None for device 0x%04x", (uint16_t)short_addr);
    } else {
        ESP_LOGD(LOG_TAG, "Device 0x%04x removed from storage", (uint16_t)short_addr);
    }
    
    return mp_const_none;
}

esp_err_t device_storage_remove(esp32_zig_obj_t *self, uint16_t short_addr) {
    // Check input parameters
    if (!self) {
        ESP_LOGE(LOG_TAG, "Invalid self pointer");
        return ESP_ERR_INVALID_ARG;
    }

    if (!self->storage_cb || self->storage_cb == mp_const_none) {
        ESP_LOGW(LOG_TAG, "No storage callback");
        return ESP_ERR_INVALID_STATE;
    }

    // Schedule execution in Python context with just short_addr
    mp_obj_t short_addr_obj = mp_obj_new_int(short_addr);
    if (!short_addr_obj) {
        ESP_LOGE(LOG_TAG, "Failed to create short_addr object");
        return ESP_ERR_NO_MEM;
    }

    if (!mp_sched_schedule(MP_OBJ_FROM_PTR(&do_device_remove_handler_obj), short_addr_obj)) {
        ESP_LOGE(LOG_TAG, "Failed to schedule remove operation");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGD(LOG_TAG, "Remove scheduled for device 0x%04x", short_addr);
    return ESP_OK;
}

// Function to load a single device from storage callback
esp_err_t device_storage_load(esp32_zig_obj_t *self, uint16_t short_addr) {
    // Check input parameters
    if (!self) {
        ESP_LOGE(LOG_TAG, "Invalid self pointer");
        return ESP_ERR_INVALID_ARG;
    }

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
    if (!load_cmd || !fname) {
        ESP_LOGE(LOG_TAG, "Failed to create command objects");
        return ESP_ERR_NO_MEM;
    }

    mp_obj_t args[2] = {load_cmd, fname};
    mp_obj_t json_str = mp_call_function_n_kw(self->storage_cb, 2, 0, args);

    if (json_str == mp_const_none) {
        ESP_LOGE(LOG_TAG, "Failed to load device 0x%04x", short_addr);
        return ESP_ERR_NOT_FOUND;
    }

    // Parse JSON
    const char *json_data = mp_obj_str_get_str(json_str);
    if (!json_data) {
        ESP_LOGE(LOG_TAG, "Invalid JSON data for device 0x%04x", short_addr);
        return ESP_ERR_INVALID_STATE;
    }

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
    ESP_LOGD(LOG_TAG, "Device 0x%04x loaded successfully", short_addr);
    return ESP_OK;
}

// Function to wait for device loading to complete
esp_err_t device_storage_wait_load_complete(TickType_t timeout) {
    if (!device_load_complete_semaphore) {
        ESP_LOGE(LOG_TAG, "Device load semaphore not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(device_load_complete_semaphore, timeout) != pdTRUE) {
        ESP_LOGW(LOG_TAG, "Timeout waiting for device load to complete");
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGD(LOG_TAG, "Device load complete semaphore taken");
    return ESP_OK;
}


