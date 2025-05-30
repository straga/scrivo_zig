// Copyright (c) 2025 Viktor Vorobjov
// This file contains the functions for device management
#include <string.h>

// MicroPython headers
#include "mpconfigport.h"
#include "py/obj.h"
#include "py/objarray.h"
#include "py/binary.h"
#include "py/runtime.h"
#include "py/builtin.h"
#include "py/mperrno.h"

//Project headers
#include "main.h"
#include "mod_zig_cmd.h"
#include "mod_zig_handlers.h"
#include "device_manager.h"


#define ZIG_CMD_NAMESPACE "zig_cmd"


// recv(timeout=0, list=None)
// Non-blocking mode: if timeout==0, function will return None immediately if queue is empty.
// If timeout>0 — waits for specified time and raises OSError if timeout occurs.
static mp_obj_t esp32_zig_recv(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    enum { ARG_timeout, ARG_list };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_timeout, MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_list,    MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };

    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // Get message from queue (with support for non-blocking mode)
    zigbee_message_t msg;
    uint32_t timeout_ms = args[ARG_timeout].u_int;
    if (timeout_ms == 0) {
        // Non-blocking mode: if no messages, return None immediately
        if (xQueueReceive(self->message_queue, &msg, 0) != pdTRUE) {
            return mp_const_none;
        }
    } else {
        // Blocking mode with timeout
        if (xQueueReceive(self->message_queue, &msg, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            mp_raise_OSError(MP_ETIMEDOUT);
        }
    }

    // Create the tuple, or get the list, that will hold the return values
    mp_obj_t ret_obj = args[ARG_list].u_obj;
    mp_obj_t *items;
    if (ret_obj == mp_const_none) {
        // new tuple of 5 elements
        ret_obj = mp_obj_new_tuple(6, NULL);
        items = ((mp_obj_tuple_t *)MP_OBJ_TO_PTR(ret_obj))->items;
        // data goes into index 4
        items[5] = mp_obj_new_bytes(msg.data, msg.data_len);
    } else {
        // User should provide a list of length at least 5 to hold the values
        if (!mp_obj_is_type(ret_obj, &mp_type_list)) {
            mp_raise_TypeError(NULL);
        }
        mp_obj_list_t *list = MP_OBJ_TO_PTR(ret_obj);
        if (list->len < 6) {
            mp_raise_ValueError(NULL);
        }
        items = list->items;
        // Fifth element must be memoryview
        if (!mp_obj_is_type(items[5], &mp_type_memoryview)) {
            mp_raise_TypeError(NULL);
        }
        mp_obj_array_t *mv = MP_OBJ_TO_PTR(items[4]);
        if (!(mv->typecode == (MP_OBJ_ARRAY_TYPECODE_FLAG_RW | BYTEARRAY_TYPECODE) || (mv->typecode | 0x20) == (MP_OBJ_ARRAY_TYPECODE_FLAG_RW | 'b'))) {
            mp_raise_ValueError(NULL);
        }
        mv->len = msg.data_len;
        memcpy(mv->items, msg.data, msg.data_len);
    }
    // Fill tuple/list: [msg_py, signal_type, src_addr, endpoint, cluster_id, data]
    items[0] = MP_OBJ_NEW_SMALL_INT(msg.msg_py);
    items[1] = MP_OBJ_NEW_SMALL_INT(msg.signal_type);
    items[2] = MP_OBJ_NEW_SMALL_INT(msg.src_addr);
    items[3] = MP_OBJ_NEW_SMALL_INT(msg.endpoint);
    items[4] = MP_OBJ_NEW_SMALL_INT(msg.cluster_id);

    
    // Return the result
    return ret_obj;
}
MP_DEFINE_CONST_FUN_OBJ_KW(esp32_zig_recv_obj, 1, esp32_zig_recv);


// Method for checking if there are messages
static mp_obj_t esp32_zig_any(mp_obj_t self_in) {
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_bool(uxQueueMessagesWaiting(self->message_queue) > 0);
}
MP_DEFINE_CONST_FUN_OBJ_1(esp32_zig_any_obj, esp32_zig_any);


// Function for setting callback for receiving messages
static mp_obj_t esp32_zig_set_recv_callback(mp_obj_t self_in, mp_obj_t cb) {
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!mp_obj_is_callable(cb)) {
        mp_raise_TypeError("callback must be callable");
    }
    self->rx_callback = cb;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(esp32_zig_set_recv_callback_obj, esp32_zig_set_recv_callback);



static mp_obj_t esp32_zig_send_command(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    // Simplified argument check
    mp_arg_check_num(n_args, kw_args->used, 1, MP_OBJ_FUN_ARGS_MAX, true);
    
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);

    // Check if network is formed
    if (!self->config->network_formed) {
        mp_raise_msg(&mp_type_RuntimeError, "Network is not formed");
        return mp_const_none;
    }

    enum { ARG_addr, ARG_ep, ARG_cl, ARG_cmd, ARG_data, ARG_manuf_code, ARG_default_resp, ARG_data_type };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_addr,         MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_ep,           MP_ARG_REQUIRED | MP_ARG_INT },  
        { MP_QSTR_cl,           MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_cmd,          MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_data,         MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_manuf_code,   MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_default_resp, MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_data_type,    MP_ARG_INT, {.u_int = ESP_ZB_ZCL_ATTR_TYPE_SET} } // Default: 0x50U,  use for raw data send.
    };
    
    // Parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // Get command parameters
    uint16_t addr = args[ARG_addr].u_int;
    uint8_t endpoint = args[ARG_ep].u_int;
    uint16_t cluster_id = args[ARG_cl].u_int;
    uint8_t command_id = args[ARG_cmd].u_int;
    uint16_t manuf_code = args[ARG_manuf_code].u_int;
    uint8_t data_type = args[ARG_data_type].u_int;

    // Create command structure
    esp_zb_zcl_custom_cluster_cmd_t cmd_req;
    memset(&cmd_req, 0, sizeof(cmd_req));  // Clear entire structure

    // Initialize basic command info
    cmd_req.zcl_basic_cmd.dst_addr_u.addr_short = addr;
    cmd_req.zcl_basic_cmd.dst_endpoint = endpoint;
    cmd_req.zcl_basic_cmd.src_endpoint = ESP_ZB_GATEWAY_ENDPOINT;

    // Initialize command fields
    cmd_req.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
    cmd_req.profile_id = ESP_ZB_AF_HA_PROFILE_ID;
    cmd_req.cluster_id = cluster_id;
    cmd_req.custom_cmd_id = command_id;
    cmd_req.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV;
    cmd_req.dis_defalut_resp = args[ARG_default_resp].u_bool;
    cmd_req.manuf_specific = manuf_code > 0 ? 1 : 0;
    cmd_req.manuf_code = manuf_code;

    // Initialize data fields
    if (args[ARG_data].u_obj != mp_const_none) {
        mp_buffer_info_t data_info;
        mp_get_buffer_raise(args[ARG_data].u_obj, &data_info, MP_BUFFER_READ);
        
        cmd_req.data.type = data_type;  // Use specified data type
        cmd_req.data.size = data_info.len;
        cmd_req.data.value = malloc(data_info.len);
        if (cmd_req.data.value == NULL) {
            mp_raise_msg(&mp_type_MemoryError, "Failed to allocate memory for command data");
            return mp_const_none;
        }
        memcpy(cmd_req.data.value, data_info.buf, data_info.len);

        // ESP_LOGI(ZIG_CMD_NAMESPACE, "Command data: size=%d bytes, type=0x%02x", 
        //          data_info.len, data_type);
        // for (int i = 0; i < data_info.len; i++) {
        //     ESP_LOGI(ZIG_CMD_NAMESPACE, "  [%d]: 0x%02x", i, ((uint8_t*)cmd_req.data.value)[i]);
        // }
        
    } else {
        cmd_req.data.type = ESP_ZB_ZCL_ATTR_TYPE_NULL;
        cmd_req.data.value = NULL;
        cmd_req.data.size = 0;
    }

    ESP_LOGI(ZIG_CMD_NAMESPACE, "Sending command: addr=0x%04x, ep=%d, cl=0x%04x, cmd=0x%02x, data_len=%d", 
        addr, endpoint, cluster_id, command_id, cmd_req.data.size);

    // Send command
    ZB_LOCK();
    uint8_t tsn = esp_zb_zcl_custom_cluster_cmd_req(&cmd_req);
    ZB_UNLOCK();

    // Free allocated memory
    if (cmd_req.data.value != NULL) {
        free(cmd_req.data.value);
    }

    return mp_obj_new_int(tsn);
}
MP_DEFINE_CONST_FUN_OBJ_KW(esp32_zig_send_command_obj, 1, esp32_zig_send_command);


static mp_obj_t esp32_zig_bind_cluster(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_addr, ARG_ep, ARG_cl, ARG_dst_addr, ARG_dst_ep };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_addr,     MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_ep,       MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_cl,       MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_dst_addr, MP_ARG_INT,                  {.u_int = 0} },
        { MP_QSTR_dst_ep,   MP_ARG_INT,                  {.u_int = ESP_ZB_GATEWAY_ENDPOINT} },
    };

    // Parse arguments
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // Extract argument values
    uint16_t addr = args[ARG_addr].u_int;
    uint8_t ep = args[ARG_ep].u_int;
    uint16_t cluster = args[ARG_cl].u_int;
    uint16_t dst_short = args[ARG_dst_addr].u_int;
    uint8_t dst_ep = args[ARG_dst_ep].u_int;

    // Validate arguments
    if (addr == 0) {
        mp_raise_ValueError("Device address cannot be 0");
    }
    if (ep == 0 || ep > 254) {
        mp_raise_ValueError("Endpoint must be between 1 and 254");
    }

    // Get device by short address using device manager
    zigbee_device_t *device = device_manager_get(addr);
    if (!device) {
        mp_raise_msg_varg(&mp_type_ValueError,
                          "Device 0x%04x not found", addr);
    }

    // Prepare ZDO Bind request
    esp_zb_zdo_bind_req_param_t bind_req = {0};
    
    // Copy IEEE address from device
    memcpy(bind_req.src_address, device->ieee_addr, sizeof(bind_req.src_address));
    
    // Set bind parameters
    bind_req.cluster_id = cluster;
    bind_req.src_endp = ep;
    bind_req.dst_addr_mode = ESP_ZB_ZDO_BIND_DST_ADDR_MODE_64_BIT_EXTENDED;

    // example of binding to coordinator
    // esp_zb_get_long_address(bind_req.dst_address_u.addr_long);
    // bind_req.dst_endp = ESP_ZB_GATEWAY_ENDPOINT;
    
    // Set destination IEEE address and endpoint for device-to-device binding
    if (dst_short != 0) {
        zigbee_device_t *dst_dev = device_manager_get(dst_short);
        if (!dst_dev) {
            mp_raise_msg_varg(&mp_type_ValueError, "Destination device 0x%04x not found", dst_short);
        }
        memcpy(bind_req.dst_address_u.addr_long, dst_dev->ieee_addr, sizeof(bind_req.dst_address_u.addr_long));
    } else {
        // fallback to coordinator
        esp_zb_get_long_address(bind_req.dst_address_u.addr_long);
    }
    bind_req.dst_endp = dst_ep;
    bind_req.req_dst_addr = addr;
    
    // Allocate bind context for callback
    bind_ctx_t *bctx = malloc(sizeof(bind_ctx_t));
    if (bctx == NULL) {
        mp_raise_msg(&mp_type_MemoryError, "Failed to allocate bind context");
    }
    
    // Initialize bind context
    *bctx = (bind_ctx_t){ 
        .short_addr = addr,
        .endpoint = ep,
        .cluster_id = cluster 
    };

    ESP_LOGI(ZIG_CMD_NAMESPACE, "Binding cluster: src=0x%04x ep=%d cluster=0x%04x -> dst=0x%04x ep=%d", 
             addr, ep, cluster, dst_short ? dst_short : 0x0000, dst_ep);
                          
    // Send bind request
    ZB_LOCK();
    esp_zb_zdo_device_bind_req(&bind_req, bind_cb, bctx);
    ZB_UNLOCK();

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(esp32_zig_bind_cluster_obj, 1, esp32_zig_bind_cluster);


// Python API: configure reporting for a bound cluster
static mp_obj_t esp32_zig_configure_report(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    enum {
        ARG_addr, ARG_ep, ARG_cl, ARG_attr, ARG_direction, // Common args
        ARG_attr_type, ARG_min_int, ARG_max_int, ARG_reportable_change, // For SEND direction
        ARG_timeout // For RECV direction
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_addr,     MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_ep,       MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_cl,       MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} }, // cluster_id
        { MP_QSTR_attr,     MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} }, // attr_id
        { MP_QSTR_direction, MP_ARG_INT, {.u_int = REPORT_CFG_DIRECTION_SEND} }, // Default to SEND
        // Args for SEND direction (conditionally required/optional)
        { MP_QSTR_attr_type, MP_ARG_INT, {.u_int = 0} }, // Will be checked if direction is SEND
        { MP_QSTR_min_int, MP_ARG_INT, {.u_int = 300} },
        { MP_QSTR_max_int, MP_ARG_INT, {.u_int = 3600} },
        { MP_QSTR_reportable_change, MP_ARG_INT, {.u_int = -1} }, // -1 signifies not set by user for SEND
        // Arg for RECV direction (conditionally required)
        { MP_QSTR_timeout,  MP_ARG_INT, {.u_int = 0xFFFF} }, // Marker for not set for RECV
    };

    mp_arg_val_t vals[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, vals);

    uint16_t addr_val = vals[ARG_addr].u_int;
    uint8_t ep_val = vals[ARG_ep].u_int;
    uint16_t cluster_id_val = vals[ARG_cl].u_int;
    uint16_t attribute_id_val = vals[ARG_attr].u_int;
    uint8_t direction_val = (uint8_t)vals[ARG_direction].u_int;

    esp_zb_zcl_config_report_cmd_t report_cmd;
    esp_zb_zcl_config_report_record_t record;
    memset(&report_cmd, 0, sizeof(report_cmd));
    memset(&record, 0, sizeof(record));

    report_cmd.zcl_basic_cmd.dst_addr_u.addr_short = addr_val;
    report_cmd.zcl_basic_cmd.dst_endpoint = ep_val;
    report_cmd.zcl_basic_cmd.src_endpoint = ESP_ZB_GATEWAY_ENDPOINT; // Assuming this is the gateway's endpoint
    report_cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
    report_cmd.clusterID = cluster_id_val;
    report_cmd.record_number = 1;
    report_cmd.record_field = &record; 
    // report_cmd.direction is for the command itself, usually TO_SRV, this is implicitly set or default

    record.attributeID = attribute_id_val;
    record.direction = direction_val; // This is esp_zb_zcl_report_direction_t

    uint32_t *allocated_reportable_change_val = NULL;

    if (direction_val == REPORT_CFG_DIRECTION_SEND) {
        // Check if attr_type was actually passed using mp_map_lookup
        mp_map_elem_t *elem_attr_type = mp_map_lookup(kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_attr_type), MP_MAP_LOOKUP);
        if (elem_attr_type == NULL) { // attr_type was not provided
             mp_raise_ValueError("attr_type is required for SEND direction");
        }
        // Access fields of esp_zb_zcl_config_report_record_t directly for SEND case
        record.attrType = (uint8_t)vals[ARG_attr_type].u_int;
        record.min_interval = (uint16_t)vals[ARG_min_int].u_int;
        record.max_interval = (uint16_t)vals[ARG_max_int].u_int;

        int reportable_change_input = vals[ARG_reportable_change].u_int;

        // Always allocate memory for SEND direction / always allocate memory for SEND direction
        allocated_reportable_change_val = malloc(sizeof(uint32_t));
        if (allocated_reportable_change_val == NULL) {
            mp_raise_msg(&mp_type_MemoryError, "Failed to allocate for reportable_change");
        }
        
        if (reportable_change_input != -1) {
            // User specified value
            *allocated_reportable_change_val = (uint32_t)reportable_change_input;
        } else {
            // User did not specify - use 0 (any change) or 0xFFFFFFFF (only by time)
            *allocated_reportable_change_val = 0xFFFFFFFF; // Только по времени
        }
        record.reportable_change = allocated_reportable_change_val;


    } else if (direction_val == REPORT_CFG_DIRECTION_RECV) {
        // Check if timeout was actually passed or is still marker
        if (vals[ARG_timeout].u_int == 0xFFFF) { 
             mp_raise_ValueError("timeout is required for RECV direction");
        }
        // Access field of esp_zb_zcl_config_report_record_t directly for RECV case
        record.timeout = (uint16_t)vals[ARG_timeout].u_int;
    } else {
        mp_raise_ValueError("Invalid direction value");
    }

    uint8_t tsn;
    ZB_LOCK();
    tsn = esp_zb_zcl_config_report_cmd_req(&report_cmd);
    ZB_UNLOCK();

    if (allocated_reportable_change_val != NULL) {
        free(allocated_reportable_change_val);
    }

    return mp_obj_new_int(tsn);
}
MP_DEFINE_CONST_FUN_OBJ_KW(esp32_zig_configure_report_obj, 1, esp32_zig_configure_report);



// Python API: set preconfigured report parameters for a device
static mp_obj_t esp32_zig_set_report_config(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {

    enum {
        ARG_addr, ARG_ep, ARG_cl, ARG_attr, ARG_direction, // Common args
        ARG_attr_type, ARG_min_int, ARG_max_int, ARG_reportable_change, // For SEND direction
        ARG_timeout // For RECV direction
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_addr,                 MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_ep,                   MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_cl,                   MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_attr,                 MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },

        { MP_QSTR_direction,            MP_ARG_INT, {.u_int = REPORT_CFG_DIRECTION_SEND} }, // Default to SEND
        // Args for SEND direction (conditionally required)
        { MP_QSTR_attr_type,            MP_ARG_INT, {.u_int = 0} },          // Made optional here, will check dependency later
        { MP_QSTR_min_int,              MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_max_int,              MP_ARG_INT, {.u_int = 30} },
        { MP_QSTR_reportable_change,    MP_ARG_INT, {.u_int = 0xFFFFFFFF} }, // Marker for not set
        // Arg for RECV direction (conditionally required)
        { MP_QSTR_timeout,              MP_ARG_INT, {.u_int = 0xFFFF} },    // Marker for not set / default for RECV
    };

    mp_arg_val_t vals[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, vals);

    uint16_t addr_val      = vals[ARG_addr].u_int;
    uint8_t  ep_val        = vals[ARG_ep].u_int;
    uint16_t cl_val        = vals[ARG_cl].u_int;
    uint16_t attr_id_val   = vals[ARG_attr].u_int;
    uint8_t  direction_val = (uint8_t)vals[ARG_direction].u_int;

    zigbee_device_t *dev = device_manager_get(addr_val);
    if (!dev) {
        mp_raise_msg_varg(&mp_type_ValueError, "Device 0x%04x not found", addr_val);
    }
    
    report_cfg_t *r_cfg = NULL;
    for (int j = 0; j < MAX_REPORT_CFGS; j++) {
        if (!dev->report_cfgs[j].in_use) {
            r_cfg = &dev->report_cfgs[j];
            break;
        }
    }

    if (!r_cfg) {
        mp_raise_msg(&mp_type_RuntimeError, "No free report slots");
        return mp_const_none; // Should not reach here due to raise
    }

    r_cfg->in_use      = true;
    r_cfg->direction   = direction_val;
    r_cfg->ep          = ep_val;
    r_cfg->cluster_id  = cl_val;
    r_cfg->attr_id     = attr_id_val;

    if (direction_val == REPORT_CFG_DIRECTION_SEND) {
        // attr_type is required for SEND direction
        mp_map_elem_t *elem_attr_type = mp_map_lookup(kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_attr_type), MP_MAP_LOOKUP);
        if (elem_attr_type == NULL) { // Check if attr_type was actually passed
             mp_raise_ValueError("attr_type is required for SEND direction");
        }
        r_cfg->send_cfg.attr_type = (uint8_t)vals[ARG_attr_type].u_int;
        r_cfg->send_cfg.min_int = (uint16_t)vals[ARG_min_int].u_int;
        r_cfg->send_cfg.max_int = (uint16_t)vals[ARG_max_int].u_int;
        r_cfg->send_cfg.reportable_change_val = (uint32_t)vals[ARG_reportable_change].u_int;
    } else if (direction_val == REPORT_CFG_DIRECTION_RECV) {
        // timeout is required for RECV direction
        if (vals[ARG_timeout].u_int == 0xFFFF) { // Check if timeout was actually passed or is still marker
             mp_raise_ValueError("timeout is required for RECV direction");
        }
        r_cfg->recv_cfg.timeout_period = (uint16_t)vals[ARG_timeout].u_int;
    } else {
        mp_raise_ValueError("Invalid direction value");
    }

    return mp_const_true;
}
MP_DEFINE_CONST_FUN_OBJ_KW(esp32_zig_set_report_config_obj, 1, esp32_zig_set_report_config);


// Modify attribute reading function
static mp_obj_t esp32_zig_read_attr(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    // Simplified argument check
    mp_arg_check_num(n_args, kw_args->used, 1, MP_OBJ_FUN_ARGS_MAX, true);
    
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);

    // Check if network is formed
    if (!self->config->network_formed) {
        mp_raise_msg(&mp_type_RuntimeError, "Network is not formed");
        return mp_const_none;
    }

    enum { ARG_addr, ARG_ep, ARG_cluster, ARG_attr_id };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_addr,    MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_ep,      MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_cluster, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_attr_id, MP_ARG_REQUIRED | MP_ARG_INT },
    };

    // Parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    uint16_t addr = args[ARG_addr].u_int;
    uint8_t endpoint = args[ARG_ep].u_int;
    uint16_t cluster_id = args[ARG_cluster].u_int;
    uint16_t attr_id = args[ARG_attr_id].u_int;
    uint16_t *attr_field = malloc(sizeof(uint16_t));
    *attr_field = attr_id;

    esp_zb_zcl_read_attr_cmd_t read_req = {
        .zcl_basic_cmd = { .dst_addr_u = { .addr_short = addr }, .dst_endpoint = endpoint, .src_endpoint = ESP_ZB_GATEWAY_ENDPOINT },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .clusterID = cluster_id,
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
        .attr_number = 1,
        .attr_field = attr_field
    };
    uint8_t tsn;
    ZB_LOCK();
    tsn = esp_zb_zcl_read_attr_cmd_req(&read_req);
    ZB_UNLOCK();
    free(attr_field);

    return mp_obj_new_int(tsn);
}

// Modify attribute writing function
static mp_obj_t esp32_zig_write_attr(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    // Simplified argument check
    mp_arg_check_num(n_args, kw_args->used, 1, MP_OBJ_FUN_ARGS_MAX, true);
    
    esp32_zig_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);

    // Check if network is formed
    if (!self->config->network_formed) {
        mp_raise_msg(&mp_type_RuntimeError, "Network is not formed");
        return mp_const_none;
    }

    enum { ARG_addr, ARG_ep, ARG_cluster, ARG_attr_id, ARG_attr_type, ARG_value };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_addr,      MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_ep,        MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_cluster,   MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_attr_id,   MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_attr_type, MP_ARG_REQUIRED | MP_ARG_INT },
        { MP_QSTR_value,     MP_ARG_REQUIRED | MP_ARG_OBJ },
    };

    // Parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    uint16_t addr = args[ARG_addr].u_int;
    uint8_t endpoint = args[ARG_ep].u_int;
    uint16_t cluster = args[ARG_cluster].u_int;
    uint16_t attr_id = args[ARG_attr_id].u_int;
    uint8_t attr_type = args[ARG_attr_type].u_int;
    mp_obj_t value = args[ARG_value].u_obj;

    // Get data buffer
    mp_buffer_info_t buf_info;
    mp_get_buffer_raise(value, &buf_info, MP_BUFFER_READ);
    
    // Prepare attribute structure
    esp_zb_zcl_attribute_t attr = {
        .id = attr_id,
        .data = {
            .type = attr_type,
            .size = buf_info.len,
            .value = buf_info.buf
        }
    };

    // Prepare command structure
    esp_zb_zcl_write_attr_cmd_t cmd = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = addr,
            .dst_endpoint = endpoint,
            .src_endpoint = ESP_ZB_GATEWAY_ENDPOINT,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .clusterID = cluster,
        .dis_defalut_resp = 0,
        .manuf_code = 0,
        .attr_number = 1,
        .attr_field = &attr
    };

    // Send command
    ZB_LOCK();
    uint8_t tsn_write = esp_zb_zcl_write_attr_cmd_req(&cmd);
    ZB_UNLOCK();

    return mp_obj_new_int(tsn_write);
}

MP_DEFINE_CONST_FUN_OBJ_KW(esp32_zig_read_attr_obj, 1, esp32_zig_read_attr);
MP_DEFINE_CONST_FUN_OBJ_KW(esp32_zig_write_attr_obj, 1, esp32_zig_write_attr);



// Python API: request binding table from a device
static mp_obj_t esp32_zig_get_binding_table(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_addr, ARG_start_index };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_addr,         MP_ARG_REQUIRED  | MP_ARG_INT },
        { MP_QSTR_start_index,  MP_ARG_INT,      {.u_int = 0} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args,
                     MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    uint16_t addr        = (uint16_t)args[ARG_addr].u_int;
    uint8_t  start_index = (uint8_t) args[ARG_start_index].u_int;

    esp_zb_zdo_mgmt_bind_param_t req = {
        .start_index = start_index,
        .dst_addr    = addr,
    };

    ZB_LOCK();
    esp_zb_zdo_binding_table_req(&req, binding_table_cb, (void*)(uintptr_t)addr);
    ZB_UNLOCK();

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(esp32_zig_get_binding_table_obj, 1, esp32_zig_get_binding_table);




