// Copyright (c) 2025 Viktor Vorobjov
#include <string.h>
#include <stdlib.h>

// ESP-Zigbee headers
#include "esp_zigbee_core.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "zcl/esp_zigbee_zcl_common.h"
#include "zcl/esp_zigbee_zcl_basic.h"
#include "zcl/esp_zigbee_zcl_power_config.h"

// Zboss API headers
#include "zboss_api.h" // for zb_raw_cmd_handler

// esp-idf headers
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_timer.h"

// zig
#include "mod_zig_handlers.h"
#include "mod_zig_core.h"
#include "mod_zig_msg.h"
#include "mod_zig_devices.h"
#include "main.h"

#define HANDLERS_TAG "ZIGBEE_HANDLERS"

// Function prototypes
static void simple_desc_req_cb(esp_zb_zdp_status_t status, esp_zb_af_simple_desc_1_1_t *simple_desc, void *user_ctx);

void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_LOGI(HANDLERS_TAG, "bdb_start_top_level_commissioning_cb: Called with mode_mask: 0x%02X", mode_mask);
    esp_err_t commission_status = esp_zb_bdb_start_top_level_commissioning(mode_mask);
    
    if (commission_status == ESP_OK) {
        ESP_LOGI(HANDLERS_TAG, "bdb_start_top_level_commissioning_cb: esp_zb_bdb_start_top_level_commissioning(0x%02X) succeeded.", mode_mask);
    } else {
        // This ESP_LOGE will print detailed error information
        ESP_LOGE(HANDLERS_TAG, "bdb_start_top_level_commissioning_cb: esp_zb_bdb_start_top_level_commissioning(0x%02X) failed. Status: %d (%s)", 
                 mode_mask, commission_status, esp_err_to_name(commission_status));
        // ESP_RETURN_ON_FALSE will then log its message and handle the return for a void function.
        // We pass the already evaluated condition (commission_status == ESP_OK), which will be false here,
        // and provide a more detailed message.
        ESP_RETURN_ON_FALSE(false, , HANDLERS_TAG, "Failed to start Zigbee bdb commissioning (mode: 0x%02X, specific_error: %s)", mode_mask, esp_err_to_name(commission_status));
    }
}

// RCP error handler
void rcp_error_handler(void) {
    ESP_LOGI(HANDLERS_TAG, "ZIGBEE: RCP error occurred");
// TODO: Add RCP error handling
} 



// Function for sending message to queue with message type
void send_msg_to_micropython_queue(uint8_t msg_py, uint16_t signal_type, uint16_t src_addr, uint8_t endpoint, uint16_t cluster_id, uint8_t *data, uint8_t data_len) {
    esp32_zig_obj_t *self = (esp32_zig_obj_t *)MP_OBJ_TO_PTR(global_esp32_zig_obj_ptr);
    if (self) {

        // Log event to ESP-IDF console
        ESP_LOGI(HANDLERS_TAG, "Event->Py addr=0x%04x ep=%u cid=0x%04x len=%u sig=0x%04x", 
                 src_addr, endpoint, cluster_id, data_len, signal_type);

        zigbee_message_t msg;
        msg.msg_py = msg_py;
        msg.signal_type = signal_type;
        msg.src_addr = src_addr;
        msg.endpoint = endpoint;
        msg.cluster_id = cluster_id;
        
        // Safely copy data without overflow
        // Since data_len is uint8_t, it can't be > 255, but msg.data might be larger
        size_t max_len = sizeof(msg.data);
        if (data_len > max_len) {
            msg.data_len = max_len;
        } else {
            msg.data_len = data_len;
        }
        
        memcpy(msg.data, data, msg.data_len);
        // Try to enqueue; if queue is full, drop oldest message and retry
        if (xQueueSend(self->message_queue, &msg, 0) != pdTRUE) {
            zigbee_message_t oldest;
            xQueueReceive(self->message_queue, &oldest, 0);
            xQueueSend(self->message_queue, &msg, 0);
        }
        // Call Micropython callback
        if (self->rx_callback != mp_const_none) {
            mp_sched_schedule(self->rx_callback, mp_const_none);
        }
    } else {
        ESP_LOGE(HANDLERS_TAG, "Invalid zig_self pointer");
    }
}



// Callback for handling ZDO-Bind response
void bind_cb(esp_zb_zdp_status_t status, void *user_ctx) {
    bind_ctx_t *ctx = (bind_ctx_t*)user_ctx;
    if (status == ESP_ZB_ZDP_STATUS_SUCCESS) {
        ESP_LOGI(HANDLERS_TAG, "Bind OK device=0x%04x ep=%u cluster=0x%04x", ctx->short_addr, ctx->endpoint, ctx->cluster_id);
        
        // Configure reporting for the bound cluster
        // Configure reporting is now moved to Python via zig.configure_report()
        // Apply stored report configurations
        zigbee_device_t *dev = device_manager_get(ctx->short_addr);
        if (dev) {
            for (int j = 0; j < MAX_REPORT_CFGS; j++) {
                report_cfg_t *r = &dev->report_cfgs[j];
                if (r->in_use && r->ep == ctx->endpoint && r->cluster_id == ctx->cluster_id) {
                    esp_zb_zcl_config_report_cmd_t report_cmd = {0};
                    esp_zb_zcl_config_report_record_t rec = {0};
                    uint32_t *allocated_rc_val = NULL; // To manage allocated memory for reportable_change

                    report_cmd.zcl_basic_cmd.dst_addr_u.addr_short = ctx->short_addr;
                    report_cmd.zcl_basic_cmd.dst_endpoint = r->ep; // Use endpoint from report_cfg_t
                    report_cmd.zcl_basic_cmd.src_endpoint = ESP_ZB_GATEWAY_ENDPOINT;
                    report_cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
                    report_cmd.clusterID = r->cluster_id; // Use cluster_id from report_cfg_t
                    report_cmd.record_number = 1;
                    report_cmd.record_field = &rec;
                    // The report_cmd.direction (for the command frame) is typically ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV
                    // This is often a default or set implicitly by the SDK function if not a direct field here.
                    // Let's assume the SDK handles the command direction or it's part of zcl_basic_cmd implicitly.

                    rec.attributeID = r->attr_id; // Use attr_id from report_cfg_t

                    if (r->direction == REPORT_CFG_DIRECTION_SEND) {
                        rec.direction = ESP_ZB_ZCL_REPORT_DIRECTION_SEND; // Correct direction for the record
                        rec.attrType = r->send_cfg.attr_type;
                        rec.min_interval = r->send_cfg.min_int;
                        rec.max_interval = r->send_cfg.max_int;
                        
                        if (r->send_cfg.reportable_change_val != 0xFFFFFFFF) {
                            allocated_rc_val = malloc(sizeof(uint32_t));
                            if (allocated_rc_val) {
                                *allocated_rc_val = r->send_cfg.reportable_change_val;
                                rec.reportable_change = allocated_rc_val;
                            } else {
                                ESP_LOGE(HANDLERS_TAG, "Failed to allocate for reportable_change in bind_cb");
                                rec.reportable_change = NULL; // Or handle error more gracefully
                            }
                        } else {
                            rec.reportable_change = NULL; // No reportable change configured or discrete attribute
                        }
                    } else if (r->direction == REPORT_CFG_DIRECTION_RECV) {
                        rec.direction = ESP_ZB_ZCL_REPORT_DIRECTION_RECV; // Correct direction for the record
                        rec.timeout = r->recv_cfg.timeout_period;
                        // For RECV, other fields like attrType, min/max_interval, reportable_change are not used in the record.
                    } else {
                        ESP_LOGW(HANDLERS_TAG, "Unknown report_cfg direction: %d", r->direction);
                        if (allocated_rc_val) free(allocated_rc_val); // Should not happen here, but good practice
                        continue; // Skip this configuration
                    }
                    
                    ESP_LOGI(HANDLERS_TAG, "Auto-configuring reporting after bind: addr=0x%04x, ep=%d, cl=0x%04x, attr=0x%04x, dir=%d", 
                        ctx->short_addr, r->ep, r->cluster_id, r->attr_id, r->direction);

                    esp_zb_zcl_config_report_cmd_req(&report_cmd);

                    if (allocated_rc_val) {
                        free(allocated_rc_val);
                    }
                }
            }
        }
    } else {
        ESP_LOGW(HANDLERS_TAG, "Bind FAIL device=0x%04x ep=%u cluster=0x%04x status=%d", ctx->short_addr, ctx->endpoint, ctx->cluster_id, status);
    }
    free(ctx);
}






// Callback for Active EP response
static void active_ep_cb(esp_zb_zdp_status_t status, uint8_t ep_count, uint8_t *ep_id_list, void *user_ctx) {
    uint16_t short_addr = (uint16_t)(uintptr_t)user_ctx;
    
    if (status != ESP_ZB_ZDP_STATUS_SUCCESS) {
        ESP_LOGW(HANDLERS_TAG, "Active EP request failed for device 0x%04x, status: %d", short_addr, status);
        return;
    }

    zigbee_device_t *device = device_manager_get(short_addr);
    if (!device) {
        ESP_LOGE(HANDLERS_TAG, "Device 0x%04x not found", short_addr);
        return;
    }
    
    for (int i = 0; i < ep_count && device->endpoint_count < MAX_ENDPOINTS; i++) {
        uint8_t ep = ep_id_list[i];
        int ep_index = -1;
        
// Check if such endpoint already exists
        for (int j = 0; j < device->endpoint_count; j++) {
            if (device->endpoints[j].endpoint == ep) {
                ep_index = j;
                break;
            }
        }
        
// If endpoint doesn't exist - add new one
        if (ep_index == -1) {
            ep_index = device->endpoint_count;
            device->endpoints[ep_index].endpoint = ep;
            device->endpoint_count++;
        }
        
// Request Simple Descriptor in any case
        esp_zb_zdo_simple_desc_req_param_t req = {
            .addr_of_interest = short_addr,
            .endpoint = ep
        };
// Pass short_addr and ep_index to user_ctx
        uintptr_t cb_ctx = ((uintptr_t)short_addr << 8) | (ep_index & 0xFF);
        esp_zb_zdo_simple_desc_req(&req, simple_desc_req_cb, (void*)cb_ctx);
        
        if (ep_index >= device->endpoint_count - 1) {
            ESP_LOGI(HANDLERS_TAG, "Device 0x%04x: added endpoint %d", short_addr, ep);
        } else {
            ESP_LOGI(HANDLERS_TAG, "Device 0x%04x: updating endpoint %d", short_addr, ep);
        }
    }
}


// Callback for Simple Descriptor response
static void simple_desc_req_cb(esp_zb_zdp_status_t status, esp_zb_af_simple_desc_1_1_t *simple_desc, void *user_ctx) {
    if (status != ESP_ZB_ZDP_STATUS_SUCCESS || simple_desc == NULL) {
        return;
    }
    uintptr_t ctx = (uintptr_t)user_ctx;
    uint16_t short_addr = (uint16_t)(ctx >> 8);
    int ep_index = (int)(ctx & 0xFF);
    
    zigbee_device_t *device = device_manager_get(short_addr);
    if (!device) {
        ESP_LOGE(HANDLERS_TAG, "Device 0x%04x not found", short_addr);
        return;
    }
    zigbee_endpoint_t *ep_rec = &device->endpoints[ep_index];

    ep_rec->profile_id     = simple_desc->app_profile_id;
    ep_rec->device_id      = simple_desc->app_device_id;
    ep_rec->cluster_count  = simple_desc->app_input_cluster_count + simple_desc->app_output_cluster_count;

// Limit number of clusters by array size
    size_t max_clusters = sizeof(ep_rec->cluster_list) / sizeof(ep_rec->cluster_list[0]);
    if (ep_rec->cluster_count > max_clusters) {
        ESP_LOGW(HANDLERS_TAG, "Truncating cluster_count from %u to %u", ep_rec->cluster_count, (unsigned)max_clusters);
        ep_rec->cluster_count = max_clusters;
    }
    for (int i = 0; i < ep_rec->cluster_count; i++) {
        ep_rec->cluster_list[i] = simple_desc->app_cluster_list[i];
    }

// Check if Basic and Power Config clusters are in the device
    bool has_basic = false;
    bool has_power_config = false;
    for (int i = 0; i < ep_rec->cluster_count; i++) {
        if (ep_rec->cluster_list[i] == ESP_ZB_ZCL_CLUSTER_ID_BASIC) {
            has_basic = true;
        } else if (ep_rec->cluster_list[i] == ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG) {
            has_power_config = true;
        }
    }

    if (has_basic || has_power_config) {
        ESP_LOGI(HANDLERS_TAG, "Found Basic/Power clusters on device 0x%04x, endpoint %d", 
                 device->short_addr, simple_desc->endpoint);
            
// Allocate memory for attributes array
        uint16_t *attr_field = malloc(6 * sizeof(uint16_t));
        uint8_t tsn = 0;
        
        if (attr_field == NULL) {
            ESP_LOGE(HANDLERS_TAG, "Failed to allocate memory for attr_field");
            return;
        }

// Form request for all attributes
        uint8_t attr_count = 0;
        if (has_basic) {
            attr_field[attr_count++] = ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID;     // 0x0004
            attr_field[attr_count++] = ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID;      // 0x0005
            attr_field[attr_count++] = ESP_ZB_ZCL_ATTR_BASIC_APPLICATION_VERSION_ID;   // 0x0001
            attr_field[attr_count++] = ESP_ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID;         // 0x0007

            esp_zb_zcl_read_attr_cmd_t read_basic_cmd = {
                .zcl_basic_cmd = {
                    .dst_addr_u.addr_short = device->short_addr,
                    .dst_endpoint = simple_desc->endpoint,
                    .src_endpoint = ESP_ZB_GATEWAY_ENDPOINT,
                },
                .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
                .clusterID = ESP_ZB_ZCL_CLUSTER_ID_BASIC,
                .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
                .attr_number = attr_count,
                .attr_field = attr_field
            };

            tsn = esp_zb_zcl_read_attr_cmd_req(&read_basic_cmd);
            ESP_LOGI(HANDLERS_TAG, "Basic cluster info request sent, tsn=%d", tsn);
        }

        if (has_power_config) {
            attr_count = 0;
            attr_field[attr_count++] = ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID;           // 0x0020
            attr_field[attr_count++] = ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID; // 0x0021

            esp_zb_zcl_read_attr_cmd_t read_power_cmd = {
                .zcl_basic_cmd = {
                    .dst_addr_u.addr_short = device->short_addr,
                    .dst_endpoint = simple_desc->endpoint,
                    .src_endpoint = ESP_ZB_GATEWAY_ENDPOINT,
                },
                .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
                .clusterID = ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
                .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
                .attr_number = attr_count,
                .attr_field = attr_field
            };

            tsn = esp_zb_zcl_read_attr_cmd_req(&read_power_cmd);
            ESP_LOGI(HANDLERS_TAG, "Power Config cluster info request sent, tsn=%d", tsn);
        }
        
        free(attr_field);
    }

// Iterate through all input clusters (server) of device
    for (int i = 0; i < (int)simple_desc->app_input_cluster_count; i++) {
        uint16_t cluster_id = simple_desc->app_cluster_list[i];
        // Send bind only if cluster is configured through Python report_cfg
        bool should_bind = false;
        for (int j = 0; j < MAX_REPORT_CFGS; j++) {
            report_cfg_t *r = &device->report_cfgs[j];
            if (r->in_use && r->ep == simple_desc->endpoint && r->cluster_id == cluster_id) {
                should_bind = true;
                break;
            }
        }
        if (!should_bind) {
            continue;
        }       
         esp_zb_zdo_bind_req_param_t bind_req;
// Initialize structure
        memset(&bind_req, 0, sizeof(bind_req));
// Source address (IEEE) for binding
        memcpy(bind_req.src_address, device->ieee_addr, sizeof(esp_zb_ieee_addr_t));
        bind_req.cluster_id    = cluster_id;
        bind_req.src_endp      = simple_desc->endpoint;
        bind_req.dst_addr_mode = ESP_ZB_ZDO_BIND_DST_ADDR_MODE_64_BIT_EXTENDED;
// Destination IEEE address (coordinator)
        {
            esp_zb_ieee_addr_t coord_ieee;
            esp_zb_get_long_address(coord_ieee);
            memcpy(bind_req.dst_address_u.addr_long, coord_ieee, sizeof(esp_zb_ieee_addr_t));
        }
        bind_req.dst_endp      = ESP_ZB_GATEWAY_ENDPOINT;
        // Address where to send the ZDO request
        bind_req.req_dst_addr  = device->short_addr;
// Context for callback
        bind_ctx_t *bctx = malloc(sizeof(bind_ctx_t));
        bctx->short_addr = device->short_addr;
        bctx->endpoint   = simple_desc->endpoint;
        bctx->cluster_id = cluster_id;
        esp_zb_zdo_device_bind_req(&bind_req, bind_cb, bctx);
        ESP_LOGI(HANDLERS_TAG, "Bind req sent to dev=0x%04x ep=%u cluster=0x%04x", device->short_addr, simple_desc->endpoint, cluster_id);
        }

        ESP_LOGI(HANDLERS_TAG, "cluster_count: %d", ep_rec->cluster_count);
        // Send simple descriptor info to MicroPython: ep, count, profile, device, clusters list

        // uint8_t buf[70];
        // size_t pos = 0;
        // buf[pos++] = ep_rec->endpoint;
        // buf[pos++] = (uint8_t)ep_rec->cluster_count;
        // buf[pos++] = ep_rec->profile_id & 0xFF;
        // buf[pos++] = (ep_rec->profile_id >> 8) & 0xFF;
        // buf[pos++] = ep_rec->device_id & 0xFF;
        // buf[pos++] = (ep_rec->device_id >> 8) & 0xFF;
        // for (int i = 0; i < ep_rec->cluster_count; i++) {
        //     buf[pos++] = ep_rec->cluster_list[i] & 0xFF;
        //     buf[pos++] = (ep_rec->cluster_list[i] >> 8) & 0xFF;
        // }

        //Not need now, because we store all info in device_manager and json file
        //send_msg_to_micropython_queue(ZIG_MSG_SIMPLE_DESC_REQ_CB, , device->short_addr, 0xFD, 0xFFFD, buf, pos);

// Save device after initialization of all endpoints and clusters
        ESP_LOGI(HANDLERS_TAG, "Device 0x%04x: endpoints and clusters initialized", device->short_addr);
        device_storage_save((esp32_zig_obj_t *)MP_OBJ_TO_PTR(global_esp32_zig_obj_ptr), device->short_addr);
    
}


// Gateway app signal handler
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct) {
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    uint16_t coord_short = 0;
    
    ESP_LOGI(HANDLERS_TAG, "HANDLER: ID: %d - %s", sig_type, esp_err_to_name(err_status));


    switch (sig_type) {
        case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP: {
            ESP_LOGI(HANDLERS_TAG, "CASE: Init Zigbee stack");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
            break;
        }

        case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
            ESP_LOGI(HANDLERS_TAG, "CASE: NEW device first start");
            /* FALLTHROUGH */
            
        case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT: {
            if (err_status == ESP_OK) {
                ESP_LOGI(HANDLERS_TAG, "CASE: Device started in %s mode", esp_zb_bdb_is_factory_new() ? "NEW" : "REBBOT ");

                // Get coordinator IEEE address
                esp_zb_ieee_addr_t coord_ieee;
                esp_zb_get_long_address(coord_ieee);
                
                // Get object from global pointer
                esp32_zig_obj_t *zb_obj = (esp32_zig_obj_t *)MP_OBJ_TO_PTR(global_esp32_zig_obj_ptr);
                if (!zb_obj) {
                    ESP_LOGE(HANDLERS_TAG, "Failed to get zigbee object from global pointer");
                    break;
                }
                
                // Add or update coordinator in device manager
                esp_err_t err = device_manager_add(0x0000, coord_ieee, MP_OBJ_FROM_PTR(zb_obj));
                if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                    ESP_LOGE(HANDLERS_TAG, "Failed to add/update coordinator in device manager: %s", esp_err_to_name(err));
                } else {
                    // Update coordinator info
                    zigbee_device_t *coordinator = device_manager_get(0x0000);
                    if (coordinator) {
                        coordinator->active = true;
                        coordinator->last_seen = esp_timer_get_time() / 1000;
                        memcpy(coordinator->ieee_addr, coord_ieee, sizeof(esp_zb_ieee_addr_t));
                        // Don't save yet - wait for manufacturer info
                    }
                }

                if (esp_zb_bdb_is_factory_new()) {
                    esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
                } else {
                    ESP_LOGI(HANDLERS_TAG, "Device restarted in existing network mode. Network steering will be initiated by ESP_ZB_BDB_SIGNAL_FORMATION if applicable.");
                }
            } else {
                ESP_LOGI(HANDLERS_TAG, "CASE: Error initializing Zigbee stack (status: %s)", esp_err_to_name(err_status));
            }
            break;
        }

        case ESP_ZB_BDB_SIGNAL_FORMATION: {
            ESP_LOGI(HANDLERS_TAG, "CASE: Network formation");

            if (err_status == ESP_OK) {
                esp_zb_ieee_addr_t extended_pan_id;
                esp_zb_get_extended_pan_id(extended_pan_id);
                ESP_LOGI(HANDLERS_TAG, "CASE: Network formation completed on channel %d, extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
                    esp_zb_get_current_channel(),
                    extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                    extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0]);

                esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                     ESP_ZB_BDB_MODE_NETWORK_STEERING, 100);
            } else {
                ESP_LOGI(HANDLERS_TAG, "CASE: Network formation error, status: %s, retrying...", esp_err_to_name(err_status));
                esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, 
                                     ESP_ZB_BDB_MODE_NETWORK_FORMATION, 1000);
            }
            break;
        }
        
        case ESP_ZB_BDB_SIGNAL_STEERING: {
            if (err_status == ESP_OK) {
                ESP_LOGI(HANDLERS_TAG, "CASE: Network management started");
            } else {
                ESP_LOGI(HANDLERS_TAG, "CASE: Network management error, status: %s", esp_err_to_name(err_status));
            }
            break;
        }
            
        case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE: {
            esp_zb_zdo_signal_device_annce_params_t *dev_annce_params = 
                (esp_zb_zdo_signal_device_annce_params_t *)esp_zb_app_signal_get_params(signal_struct->p_app_signal);
            char temp_ieee_str[24];
            zigbee_format_ieee_addr_to_str(dev_annce_params->ieee_addr, temp_ieee_str, sizeof(temp_ieee_str));
            ESP_LOGI(HANDLERS_TAG, "New device announcement: 0x%04x (IEEE: %s)", dev_annce_params->device_short_addr, temp_ieee_str);

            // Get object from global pointer
            esp32_zig_obj_t *zb_obj = (esp32_zig_obj_t *)MP_OBJ_TO_PTR(global_esp32_zig_obj_ptr);
            if (!zb_obj) {
                ESP_LOGE(HANDLERS_TAG, "Failed to get zigbee object from global pointer");
                break;
            }

            // Find or add device using device manager
            esp_err_t err = device_manager_add(dev_annce_params->device_short_addr, dev_annce_params->ieee_addr, MP_OBJ_FROM_PTR(zb_obj));
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(HANDLERS_TAG, "ZIGBEE: Failed to add/update device 0x%04x in manager, error %s. Continuing with EP discovery.", 
                         dev_annce_params->device_short_addr, esp_err_to_name(err));
            }

            // Get device - this should now reliably get the device, even if it re-joined
            zigbee_device_t *device = device_manager_get(dev_annce_params->device_short_addr);
            if (!device) {
                ESP_LOGE(HANDLERS_TAG, "ZIGBEE: Device 0x%04x not found in manager after add/update attempt. Cannot proceed with EP discovery.", dev_annce_params->device_short_addr);
                break;
            }
            
            // Request active endpoints for update 
            esp_zb_zdo_active_ep_req_param_t active_ep_req = {
                .addr_of_interest = dev_annce_params->device_short_addr
            };
            esp_zb_zdo_active_ep_req(&active_ep_req, active_ep_cb, (void*)(uintptr_t)dev_annce_params->device_short_addr);
            
            ESP_LOGI(HANDLERS_TAG, "ZIGBEE: Device request Active EP for device: 0x%04x", dev_annce_params->device_short_addr);
            ESP_LOGI(HANDLERS_TAG, "ZIGBEE: Device added/updated: 0x%04x", device->short_addr);
            break;
        }
            
        case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS: {
            ESP_LOGI(HANDLERS_TAG, "CASE: Permit join status");
            if (err_status == ESP_OK) {
                uint8_t *permit_duration = (uint8_t *)esp_zb_app_signal_get_params(signal_struct->p_app_signal);
                if (*permit_duration) {
                    ESP_LOGI(HANDLERS_TAG, "CASE: Network is open for new devices for %d seconds", *permit_duration);
                } else {
                    ESP_LOGI(HANDLERS_TAG, "CASE: Network is closed, new devices are not allowed to connect");
                }
            }
            break;
        }

        case ESP_ZB_ZDO_SIGNAL_PRODUCTION_CONFIG_READY: {
            ESP_LOGI(HANDLERS_TAG, "CASE: Production configuration ready");
            if (err_status == ESP_OK) {
                app_production_config_t *prod_cfg = 
                    (app_production_config_t *)esp_zb_app_signal_get_params(signal_struct->p_app_signal);
                
// Add log to check from which device signal came
                ESP_LOGI(HANDLERS_TAG, "Production config signal from device with manuf_code: 0x%x", prod_cfg->manuf_code);
                
                if (prod_cfg->version == ZB_PRODUCTION_CONFIG_CURRENT_VERSION) {
                    ESP_LOGI(HANDLERS_TAG, "CASE: Manufacturer code: 0x%x, manufacturer name: %s", 
                            prod_cfg->manuf_code, prod_cfg->manuf_name);
                    esp_zb_set_node_descriptor_manufacturer_code(prod_cfg->manuf_code);
                    
                    // Since this is for the gateway (coordinator) itself, update coordinator device info (0x0000)
                    zigbee_device_t *coordinator = device_manager_get(0x0000);
                    if (coordinator) {
                        // Update the manufacturer information for the coordinator
                        coordinator->manufacturer_code = prod_cfg->manuf_code;
                        strncpy(coordinator->manufacturer_name, prod_cfg->manuf_name,
                                sizeof(coordinator->manufacturer_name) - 1);
                        coordinator->manufacturer_name[sizeof(coordinator->manufacturer_name) - 1] = '\0';
                        coordinator->prod_config_version = prod_cfg->version;
                        
                        // Now save coordinator with all information
                        if (esp_zb_is_started()) {
                            device_storage_save((esp32_zig_obj_t *)MP_OBJ_TO_PTR(global_esp32_zig_obj_ptr), 0x0000);
                        }
                    }
                }
            } else {
                ESP_LOGI(HANDLERS_TAG, "CASE: Production configuration is missing");
            }
            break;
        }

        case ESP_ZB_NLME_STATUS_INDICATION: {
            uint8_t status = *(uint8_t *)esp_zb_app_signal_get_params(p_sg_p);
            switch (status) {
                case ZB_NWK_COMMAND_STATUS_BAD_KEY_SEQUENCE_NUMBER:
                    ESP_LOGW(HANDLERS_TAG, "Bad key sequence number");
                    break;
                case ZB_NWK_COMMAND_STATUS_NO_ROUTE_AVAILABLE:
                    ESP_LOGW(HANDLERS_TAG, "No route available");
                    break;
                case ZB_NWK_COMMAND_STATUS_TREE_LINK_FAILURE:
                    ESP_LOGW(HANDLERS_TAG, "Tree link failure");
                    break;
                case ZB_NWK_COMMAND_STATUS_NONE_TREE_LINK_FAILURE:
                    ESP_LOGW(HANDLERS_TAG, "None-tree link failure");
                    break;
                case ZB_NWK_COMMAND_STATUS_LOW_BATTERY_LEVEL:
                    ESP_LOGW(HANDLERS_TAG, "Low battery level");
                    break;
                case ZB_NWK_COMMAND_STATUS_NO_ROUTING_CAPACITY:
                    ESP_LOGW(HANDLERS_TAG, "No routing capacity");
                    break;
                case ZB_NWK_COMMAND_STATUS_NO_INDIRECT_CAPACITY:
                    ESP_LOGW(HANDLERS_TAG, "No indirect capacity");
                    break;
                case ZB_NWK_COMMAND_STATUS_INDIRECT_TRANSACTION_EXPIRY:
                    ESP_LOGW(HANDLERS_TAG, "Indirect transaction expiry");
                    break;
                case ZB_NWK_COMMAND_STATUS_TARGET_DEVICE_UNAVAILABLE:
                    ESP_LOGW(HANDLERS_TAG, "Target device unavailable");
                    break;
                case ZB_NWK_COMMAND_STATUS_TARGET_ADDRESS_UNALLOCATED:
                    ESP_LOGW(HANDLERS_TAG, "Target address unallocated");
                    break;
                case ZB_NWK_COMMAND_STATUS_PARENT_LINK_FAILURE:
                    ESP_LOGW(HANDLERS_TAG, "Parent link failure");
                    break;
                case ZB_NWK_COMMAND_STATUS_VALIDATE_ROUTE:
                    ESP_LOGW(HANDLERS_TAG, "Validate route");
                    break;
                case ZB_NWK_COMMAND_STATUS_SOURCE_ROUTE_FAILURE:
                    ESP_LOGW(HANDLERS_TAG, "Source route failure");
                    break;
                case ZB_NWK_COMMAND_STATUS_MANY_TO_ONE_ROUTE_FAILURE:
                    ESP_LOGW(HANDLERS_TAG, "Many-to-one route failure");
                    break;
                case ZB_NWK_COMMAND_STATUS_ADDRESS_CONFLICT:
                    ESP_LOGW(HANDLERS_TAG, "Address conflict");
                    break;
                case ZB_NWK_COMMAND_STATUS_VERIFY_ADDRESS:
                    ESP_LOGW(HANDLERS_TAG, "Verify address");
                    break;
                case ZB_NWK_COMMAND_STATUS_PAN_IDENTIFIER_UPDATE:
                    ESP_LOGW(HANDLERS_TAG, "Pan ID update");
                    break;
                case ZB_NWK_COMMAND_STATUS_NETWORK_ADDRESS_UPDATE:
                    ESP_LOGW(HANDLERS_TAG, "Network address update");
                    break;
                case ZB_NWK_COMMAND_STATUS_BAD_FRAME_COUNTER:
                    ESP_LOGW(HANDLERS_TAG, "Bad frame counter");
                    break;
                case ZB_NWK_COMMAND_STATUS_UNKNOWN_COMMAND:
                    ESP_LOGW(HANDLERS_TAG, "Command received is not known");
                    break;
                default:
                    ESP_LOGW(HANDLERS_TAG, "Unknown network status: 0x%02x", status);
                    break;
            }
            printf("%s, status: 0x%x ", esp_zb_zdo_signal_to_string(sig_type), status);
            break;
        }
            
        case ESP_ZB_ZDO_SIGNAL_DEVICE_UPDATE: {
            esp_zb_zdo_signal_device_update_params_t *update_params = 
                (esp_zb_zdo_signal_device_update_params_t *)esp_zb_app_signal_get_params(signal_struct->p_app_signal);
            
            char ieee_from_signal_str[24];
            zigbee_format_ieee_addr_to_str(update_params->long_addr, ieee_from_signal_str, sizeof(ieee_from_signal_str));

            ESP_LOGI(HANDLERS_TAG, "Device update signal: short_addr=0x%04x, IEEE=%s, status=%d",
                     update_params->short_addr, ieee_from_signal_str, update_params->status);

            zigbee_device_t *device = device_manager_get(update_params->short_addr);
            if (!device) {
                ESP_LOGW(HANDLERS_TAG, "Device not found by short_addr=0x%04x for device update signal. IEEE from signal was %s. Attempting to add and interview.", 
                         update_params->short_addr, ieee_from_signal_str);
                
                // Get object from global pointer
                esp32_zig_obj_t *zb_obj = (esp32_zig_obj_t *)MP_OBJ_TO_PTR(global_esp32_zig_obj_ptr);
                if (!zb_obj) {
                    ESP_LOGE(HANDLERS_TAG, "Failed to get zigbee object from global pointer");
                    break;
                }

                // Attempt to add this device to the manager as it's clearly communicating
                esp_err_t add_err = device_manager_add(update_params->short_addr, update_params->long_addr, MP_OBJ_FROM_PTR(zb_obj));
                if (add_err == ESP_OK || add_err == ESP_ERR_INVALID_STATE) { // ESP_ERR_INVALID_STATE might mean it was already added by a concurrent event or handled conflict
                    ESP_LOGI(HANDLERS_TAG, "Successfully added device 0x%04x (IEEE: %s) from Device Update signal. Will interview.", 
                             update_params->short_addr, ieee_from_signal_str);
                    
                    // Request active endpoints for update 
                    esp_zb_zdo_active_ep_req_param_t active_ep_req = {
                        .addr_of_interest = update_params->short_addr
                    };
                    esp_zb_zdo_active_ep_req(&active_ep_req, active_ep_cb, (void*)(uintptr_t)update_params->short_addr);
                    
                    ESP_LOGI(HANDLERS_TAG, "ZIGBEE: Device request Active EP for device: 0x%04x", update_params->short_addr);
                } else {
                    ESP_LOGE(HANDLERS_TAG, "Failed to add device 0x%04x (IEEE: %s) from Device Update signal. Error: %s. Cannot interview.", 
                             update_params->short_addr, ieee_from_signal_str, esp_err_to_name(add_err));
                }
            } else {
                ESP_LOGI(HANDLERS_TAG, "ZIGBEE: Device update for known device: short=0x%04x (signal IEEE=%s, stored IEEE=%s), signal_status=%d",
                         device->short_addr, ieee_from_signal_str, device->ieee_addr_str, update_params->status);
                
                // Important: Check if IEEE from signal matches the stored IEEE for this short_addr
                if (memcmp(device->ieee_addr, update_params->long_addr, sizeof(esp_zb_ieee_addr_t)) != 0) {
                    ESP_LOGW(HANDLERS_TAG, "IEEE MISMATCH for short_addr 0x%04x! Signal reports IEEE %s, but manager has %s.",
                             device->short_addr, ieee_from_signal_str, device->ieee_addr_str);
                    // Here additional logic may be needed to resolve the conflict,
                    // for example, update IEEE in device_manager or mark the device as suspicious.
                    // For now, just log.
                }

                // Update device metrics
                device->active = true;
                device_manager_update_timestamp(update_params->short_addr);
                
                // Save device after update
                device_storage_save((esp32_zig_obj_t *)MP_OBJ_TO_PTR(global_esp32_zig_obj_ptr), device->short_addr);
            }
            break;
        }

        default: {
            ESP_LOGI(HANDLERS_TAG, "CASE: ZDO signal: %s (0x%x), status: %s", 
                     esp_zb_zdo_signal_to_string(sig_type), sig_type, esp_err_to_name(err_status));

            ESP_LOGI(HANDLERS_TAG, "HANDLER: Signal struct params: %p", signal_struct->p_app_signal);
            ESP_LOGI(HANDLERS_TAG, "HANDLER: Signal struct params value: %lu", (unsigned long)*signal_struct->p_app_signal);
            ESP_LOGI(HANDLERS_TAG, "HANDLER: Signal struct params value (hex): 0x%08lx", (unsigned long)*signal_struct->p_app_signal);


            coord_short = esp_zb_get_short_address();
            send_msg_to_micropython_queue(
                ZIG_MSG_ZB_APP_SIGNAL_HANDLER,       // msg_py
                sig_type,                            // signal_type
                coord_short,
                0xFE,                                // special endpoint for network event
                0xFFFE,                              // special cluster_id for network formed
                (uint8_t*)&err_status,               // data
                sizeof(err_status)                   // data_len
            );


            break;
        }
    }

    ESP_LOGI(HANDLERS_TAG, " ");
}

esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;
    ESP_LOGI(HANDLERS_TAG, "ALL Receive Zigbee action(0x%x) callback", callback_id);

    switch (callback_id) {


    case ESP_ZB_CORE_CMD_DEFAULT_RESP_CB_ID: {
        const esp_zb_zcl_cmd_default_resp_message_t *resp = (esp_zb_zcl_cmd_default_resp_message_t *)message;
        
        // Create buffer for sending data to MicroPython
        // Format: status(1) + command_id(1)
        uint8_t data[3];
        data[0] = resp->status_code; // 0x00 - success, 0x01 - failure , other codes esp_zb_zcl_status_t
        data[1] = resp->resp_to_cmd;


        if (resp->status_code == ESP_ZB_ZCL_STATUS_SUCCESS) {
            // Log success message

            ESP_LOGI(HANDLERS_TAG, "Command ID 0x%x to device 0x%04x succeeded in cluster 0x%04x",
                resp->resp_to_cmd,
                resp->info.src_address.u.short_addr,
                resp->info.cluster
            );
        } else {
            // Log failure message
            ESP_LOGW(HANDLERS_TAG, "Command ID 0x%x to device 0x%04x failed with status 0x%x",
                resp->resp_to_cmd,
                resp->info.src_address.u.short_addr,
                resp->status_code
            );
        }


        // Send command execution result to MicroPython
        send_msg_to_micropython_queue(
            ZIG_MSG_ZB_ACTION_HANDLER,
            ESP_ZB_CORE_CMD_DEFAULT_RESP_CB_ID,
            resp->info.src_address.u.short_addr,
            resp->info.src_endpoint,
            resp->info.cluster,
            data,
            sizeof(data)
        );

        break;
    }


    case ESP_ZB_CORE_REPORT_ATTR_CB_ID: {                       //         = 0x2000,   /*!< Attribute Report, refer to esp_zb_zcl_report_attr_message_t */
        const esp_zb_zcl_report_attr_message_t *report_msg = (esp_zb_zcl_report_attr_message_t *)message;

        // Send full attribute data: ID (2 bytes), type (1 byte), payload
        uint16_t attr_id = report_msg->attribute.id;
        uint8_t  attr_type = report_msg->attribute.data.type;
        uint16_t payload_len = report_msg->attribute.data.size;
        void    *value_ptr = report_msg->attribute.data.value;
        size_t buf_len = 2 + 1 + payload_len;
        uint8_t *buf = malloc(buf_len);
        if (buf) {
            buf[0] = attr_id & 0xFF;
            buf[1] = (attr_id >> 8) & 0xFF;
            buf[2] = attr_type;
            if (value_ptr && payload_len) {
                memcpy(buf + 3, value_ptr, payload_len);
            }

            send_msg_to_micropython_queue(
                ZIG_MSG_ZB_ACTION_HANDLER,
                ESP_ZB_CORE_REPORT_ATTR_CB_ID,
                report_msg->src_address.u.short_addr,
                report_msg->src_endpoint,
                report_msg->cluster,
                buf,
                buf_len
            );

            free(buf);
        }
        break;
    }
    case ESP_ZB_CORE_CMD_READ_ATTR_RESP_CB_ID: {
        const esp_zb_zcl_cmd_read_attr_resp_message_t *read_msg = (esp_zb_zcl_cmd_read_attr_resp_message_t *)message;
        
        if (read_msg->info.status == ESP_ZB_ZCL_STATUS_SUCCESS) {
            esp_zb_zcl_read_attr_resp_variable_t *variable = read_msg->variables;
            
            // Process responses from both Basic and Power Configuration clusters
            if (read_msg->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_BASIC ||
                read_msg->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG) {
                uint16_t short_addr = read_msg->info.src_address.u.short_addr;
                
// Get device through device manager
                zigbee_device_t *device = device_manager_get(short_addr);                // If device is found, process attributes
                if (device) {
                    // Update device timestamp instead of LQI/RSSI
                    device_manager_update_timestamp(short_addr);

                    esp_zb_zcl_read_attr_resp_variable_t *current = variable;
                    
                    while (current) {
                        switch (current->attribute.id) {
                            case ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID:
                                if (current->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING && 
                                    current->attribute.data.value && 
                                    current->attribute.data.size < sizeof(device->manufacturer_name)) {
                                    memcpy(device->manufacturer_name, current->attribute.data.value, current->attribute.data.size);
                                    device->manufacturer_name[current->attribute.data.size] = '\0';
                                }
                                break;

                            case ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID:
                                if (current->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING && 
                                    current->attribute.data.value) {
                                    size_t copy_size = current->attribute.data.size;
                                    if (copy_size >= sizeof(device->device_name)) {
                                        copy_size = sizeof(device->device_name) - 1;
                                    }
                                    memcpy(device->device_name, current->attribute.data.value, copy_size);
                                    device->device_name[copy_size] = '\0';
                                }
                                break;

                            case ESP_ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID:
                                if (current->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U8 && 
                                    current->attribute.data.value) {
                                    device->power_source = *(uint8_t*)current->attribute.data.value;
                                }
                                break;

                            case ESP_ZB_ZCL_ATTR_BASIC_APPLICATION_VERSION_ID:
                                if (current->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U8 && 
                                    current->attribute.data.value) {
                                    device->firmware_version = *(uint8_t*)current->attribute.data.value;
                                }
                                break;

                            case ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID:
                            case ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_PERCENTAGE_REMAINING_ID:
                                if (current->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_U8 && 
                                    current->attribute.data.value) {
                                    if (current->attribute.id == ESP_ZB_ZCL_ATTR_POWER_CONFIG_BATTERY_VOLTAGE_ID) {
                                        device->battery_voltage = *(uint8_t*)current->attribute.data.value;
                                    } else {
                                        uint8_t percentage = *(uint8_t*)current->attribute.data.value;
                                        if (percentage > 100) {
                                            percentage -= 100;
                                        }
                                        device->battery_percentage = percentage;
                                    }
                                }
                                break;
                        }
                        current = current->next;
                    }

// Save device after receiving all attributes
                    if (device->manufacturer_name[0] != '\0' && device->device_name[0] != '\0') {
                        ESP_LOGI(HANDLERS_TAG, "Device 0x%04x: got all required attributes", device->short_addr);
                        device_storage_save((esp32_zig_obj_t *)MP_OBJ_TO_PTR(global_esp32_zig_obj_ptr), device->short_addr);
                    }
                }
            }
            
// Standard processing for all attributes
            while (variable) {
                // Send full attribute value (ID, type, payload)
                uint16_t attr_id = variable->attribute.id;
                uint8_t attr_type = variable->attribute.data.type;
                uint16_t payload_len = variable->attribute.data.size;
                void *value_ptr = variable->attribute.data.value;
                // Buffer for ID(2) + type(1) + payload_len
                size_t buf_len = 2 + 1 + payload_len;
                uint8_t *buf = malloc(buf_len);
                if (buf) {
                    buf[0] = attr_id & 0xFF;
                    buf[1] = (attr_id >> 8) & 0xFF;
                    buf[2] = attr_type;
                    if (value_ptr && payload_len) {
                        memcpy(buf + 3, value_ptr, payload_len);
                    }

                    send_msg_to_micropython_queue(
                        ZIG_MSG_ZB_APP_SIGNAL_HANDLER,
                        ESP_ZB_CORE_CMD_READ_ATTR_RESP_CB_ID,
                        read_msg->info.src_address.u.short_addr,
                        read_msg->info.src_endpoint,
                        read_msg->info.cluster,
                        buf,
                        buf_len
                    );

                    free(buf);
                }
                variable = variable->next;
            }
        }
        break;
    }
    case ESP_ZB_CORE_CMD_REPORT_CONFIG_RESP_CB_ID: {
        const esp_zb_zcl_cmd_config_report_resp_message_t *config_msg = (esp_zb_zcl_cmd_config_report_resp_message_t *)message;
        
        if (config_msg->info.status == ESP_ZB_ZCL_STATUS_SUCCESS) {
            // Send report configuration information
            uint8_t data[2] = {
                config_msg->info.status,
                config_msg->variables ? config_msg->variables->attribute_id & 0xFF : 0
            };

            send_msg_to_micropython_queue(
                ZIG_MSG_ZB_APP_SIGNAL_HANDLER,
                ESP_ZB_CORE_CMD_REPORT_CONFIG_RESP_CB_ID,
                config_msg->info.src_address.u.short_addr,
                config_msg->info.src_endpoint,
                config_msg->info.cluster,
                data,
                sizeof(data)
            );
        }
        break;
    }
    case ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID: {
        const esp_zb_zcl_custom_cluster_command_message_t *custom_msg = 
            (esp_zb_zcl_custom_cluster_command_message_t *)message;
        
        if (!custom_msg) {
            return ESP_ERR_INVALID_ARG;
        }

        // Send all custom cluster commands to Python side
        send_msg_to_micropython_queue(
            ZIG_MSG_ZB_APP_SIGNAL_HANDLER,
            ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID,
            custom_msg->info.src_address.u.short_addr,
            custom_msg->info.src_endpoint,
            custom_msg->info.cluster,
            (uint8_t *)custom_msg->data.value,
            custom_msg->data.size
        );
        
        return ESP_OK;
    }

    case ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_RESP_CB_ID: {
        const esp_zb_zcl_custom_cluster_command_message_t *resp_msg = 
            (esp_zb_zcl_custom_cluster_command_message_t *)message;

        if (!resp_msg) {
            return ESP_ERR_INVALID_ARG;
        }

        // Forward all custom cluster responses to Python for processing
        send_msg_to_micropython_queue(
            ZIG_MSG_ZB_APP_SIGNAL_HANDLER,
            ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_RESP_CB_ID,
            resp_msg->info.src_address.u.short_addr,
            resp_msg->info.src_endpoint,
            resp_msg->info.cluster,
            (uint8_t *)resp_msg->data.value,
            resp_msg->data.size
        );
        
        break;
    }

    default:
        ESP_LOGW(HANDLERS_TAG, "Default Zigbee action(0x%x) callback", callback_id);

        send_msg_to_micropython_queue(
            ZIG_MSG_ZB_APP_SIGNAL_HANDLER,
            callback_id,
            0,
            0,
            0,
            (uint8_t*)&ret,
            sizeof(ret)
        );
        break;
    }
    return ret;
}




bool zb_raw_cmd_handler(uint8_t bufid)
{
    ESP_LOGI(HANDLERS_TAG, "RAW command handler, bufid: %d", bufid);
    zb_zcl_parsed_hdr_t *cmd_info = ZB_BUF_GET_PARAM(bufid, zb_zcl_parsed_hdr_t);

    // Get raw payload
    uint8_t payload_len = zb_buf_len(bufid);
    uint8_t *payload = zb_buf_begin(bufid);

    // Calculate total buffer size needed:
    // Header info (12 bytes) + payload
    // Header: 
    // - cmd_id (1)
    // - cmd_direction (1)
    // - seq_number (1)
    // - is_common_command (1)
    // - disable_default_response (1)
    // - is_manuf_specific (1)
    // - manuf_specific (2)
    // - profile_id (2)
    // - cluster_id (2)
    size_t total_len = 12 + payload_len;
    uint8_t *data = malloc(total_len);
    
    if (data) {
        size_t pos = 0;
        
        // Pack header information
        data[pos++] = cmd_info->cmd_id;
        data[pos++] = cmd_info->cmd_direction;
        data[pos++] = cmd_info->seq_number;
        data[pos++] = cmd_info->is_common_command;
        data[pos++] = cmd_info->disable_default_response;
        data[pos++] = cmd_info->is_manuf_specific;
        data[pos++] = cmd_info->manuf_specific & 0xFF;
        data[pos++] = (cmd_info->manuf_specific >> 8) & 0xFF;
        data[pos++] = cmd_info->profile_id & 0xFF;
        data[pos++] = (cmd_info->profile_id >> 8) & 0xFF;
        data[pos++] = cmd_info->cluster_id & 0xFF;
        data[pos++] = (cmd_info->cluster_id >> 8) & 0xFF;

        // Copy payload
        if (payload_len > 0) {
            memcpy(data + pos, payload, payload_len);
        }

        // Send message to MicroPython queue
        send_msg_to_micropython_queue(
            ZIG_MSG_RAW,
            0,
            cmd_info->addr_data.common_data.source.u.short_addr,
            cmd_info->addr_data.common_data.src_endpoint,
            cmd_info->cluster_id,
            data,
            total_len
        );

        free(data);
    }

    // Process command as usual
    zb_zcl_send_default_handler(bufid, cmd_info, ZB_ZCL_STATUS_SUCCESS);
    return true;
}



// Callback for ZDO binding table response
void binding_table_cb(const esp_zb_zdo_binding_table_info_t *table_info, void *user_ctx) {
    uint16_t short_addr = (uint16_t)(uintptr_t)user_ctx;
    ESP_LOGI(HANDLERS_TAG, "Binding table response for 0x%04x: total=%d, count=%d",
             short_addr, table_info->total, table_info->count);
    // Iterate through all records
    esp_zb_zdo_binding_table_record_t *rec = table_info->record;
    while (rec) {
        // Format source and destination addresses
        char src_str[24];
        char dst_str[32];
        zigbee_format_ieee_addr_to_str(rec->src_address, src_str, sizeof(src_str));
        if (rec->dst_addr_mode == ESP_ZB_ZDO_BIND_DST_ADDR_MODE_64_BIT_EXTENDED) {
            zigbee_format_ieee_addr_to_str(rec->dst_address.addr_long, dst_str, sizeof(dst_str));
        } else {
            snprintf(dst_str, sizeof(dst_str), "short=0x%04x", rec->dst_address.addr_short);
        }
        // Build record string
        char record_str[128];
        snprintf(record_str, sizeof(record_str),
            "%s ep=%u cluster=0x%04x -> %s ep=%u",
            src_str, rec->src_endp, rec->cluster_id, dst_str, rec->dst_endp);
        ESP_LOGI(HANDLERS_TAG, "Binding record: %s", record_str);
        rec = rec->next;
    }
}

