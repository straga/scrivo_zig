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
    // Fill tuple/list: [signal_type, src_addr, endpoint, cluster_id, data]
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





static mp_obj_t esp32_zig_bind_cluster(size_t n_args, const mp_obj_t *args) {
    // args: (self, addr, endpoint, cluster_id)
    uint16_t addr      = mp_obj_get_int(args[1]);
    uint8_t  ep        = mp_obj_get_int(args[2]);
    uint16_t cluster   = mp_obj_get_int(args[3]);

    // 1) ZDO Bind
    esp_zb_zdo_bind_req_param_t bind_req = {0};
    // Get device by short address using device manager
    zigbee_device_t *device = device_manager_get(addr);
    if (!device) {
        mp_raise_msg_varg(&mp_type_ValueError,
                          "Device 0x%04x not found", addr);
    }
    
    // Copy IEEE address
    memcpy(bind_req.src_address, device->ieee_addr, sizeof(bind_req.src_address));
    
    bind_req.cluster_id    = cluster;
    bind_req.src_endp      = ep;
    bind_req.dst_addr_mode = ESP_ZB_ZDO_BIND_DST_ADDR_MODE_64_BIT_EXTENDED;
    esp_zb_get_long_address(bind_req.dst_address_u.addr_long);
    bind_req.dst_endp      = ESP_ZB_GATEWAY_ENDPOINT;
    bind_req.req_dst_addr  = addr;
    
    bind_ctx_t *bctx = malloc(sizeof(bind_ctx_t));
    if (bctx == NULL) {
        mp_raise_msg(&mp_type_MemoryError, "Failed to allocate bind context");
    }
    
    *bctx = (bind_ctx_t){ .short_addr = addr,
                          .endpoint = ep,
                          .cluster_id = cluster };
                          
    ZB_LOCK();
    esp_zb_zdo_device_bind_req(&bind_req, bind_cb, bctx);
    ZB_UNLOCK();

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp32_zig_bind_cluster_obj, 4, 4, esp32_zig_bind_cluster);


// Python API: configure reporting for a bound cluster
static mp_obj_t esp32_zig_configure_report(size_t n_args, const mp_obj_t *args) {
    // args: (self, addr, endpoint, cluster_id, attribute_id, attr_type, [min_int, max_int])
    uint16_t addr = mp_obj_get_int(args[1]);
    uint8_t ep = mp_obj_get_int(args[2]);
    uint16_t cluster = mp_obj_get_int(args[3]);
    uint16_t attr_id = mp_obj_get_int(args[4]);
    uint8_t attr_type = mp_obj_get_int(args[5]);
    // default reporting intervals
    uint16_t min_int = 0;
    uint16_t max_int = 30;
    if (n_args > 6) {
        min_int = mp_obj_get_int(args[6]);
    }
    if (n_args > 7) {
        max_int = mp_obj_get_int(args[7]);
    }
    bool report_change = true;
    esp_zb_zcl_config_report_cmd_t report_cmd;
    esp_zb_zcl_config_report_record_t record;
    memset(&report_cmd, 0, sizeof(report_cmd));
    report_cmd.zcl_basic_cmd.dst_addr_u.addr_short = addr;
    report_cmd.zcl_basic_cmd.dst_endpoint = ep;
    report_cmd.zcl_basic_cmd.src_endpoint = ESP_ZB_GATEWAY_ENDPOINT;
    report_cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
    report_cmd.clusterID = cluster;
    report_cmd.record_number = 1;
    record.min_interval = min_int;
    record.max_interval = max_int;
    record.reportable_change = &report_change;
    record.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV;
    record.attributeID = attr_id;
    record.attrType = attr_type;
    report_cmd.record_field = &record;
    ZB_LOCK();
    esp_zb_zcl_config_report_cmd_req(&report_cmd);
    ZB_UNLOCK();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp32_zig_configure_report_obj, 6, 8, esp32_zig_configure_report);



// Python API: set preconfigured report parameters for a device
static mp_obj_t esp32_zig_set_report_config(size_t n_args, const mp_obj_t *args) {
    // args: (self, addr, ep, cluster, attr_id, attr_type, [min_interval, [max_interval]])
    uint16_t addr      = mp_obj_get_int(args[1]);
    uint8_t  ep        = mp_obj_get_int(args[2]);
    uint16_t cluster   = mp_obj_get_int(args[3]);
    uint16_t attr_id   = mp_obj_get_int(args[4]);
    uint8_t  attr_type = mp_obj_get_int(args[5]);
    // default intervals
    uint16_t min_int = 0;
    uint16_t max_int = 30;
    if (n_args > 6) {
        min_int = mp_obj_get_int(args[6]);
    }
    if (n_args > 7) {
        max_int = mp_obj_get_int(args[7]);
    }
    // Get device using device manager
    zigbee_device_t *dev = device_manager_get(addr);
    if (!dev) {
        mp_raise_msg_varg(&mp_type_ValueError, "Device 0x%04x not found", addr);
    }
    
    // Find free slot
    for (int j = 0; j < MAX_REPORT_CFGS; j++) {
        report_cfg_t *r = &dev->report_cfgs[j];
        if (!r->in_use) {
            r->in_use      = true;
            r->ep          = ep;
            r->cluster_id  = cluster;
            r->attr_id     = attr_id;
            r->attr_type   = attr_type;
            r->min_int     = min_int;
            r->max_int     = max_int;
            return mp_const_true;
        }
    }
    mp_raise_msg(&mp_type_RuntimeError, "No free report slots");
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp32_zig_set_report_config_obj, 6, 8, esp32_zig_set_report_config);


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
// Acquire lock before calling Zigbee API
    ZB_LOCK();
    tsn = esp_zb_zcl_read_attr_cmd_req(&read_req);
    ZB_UNLOCK();
    free(attr_field);
    if (tsn == 0) {
        mp_raise_msg(&mp_type_RuntimeError, "Failed to send read_attr");
    }
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
    uint8_t tsn = esp_zb_zcl_write_attr_cmd_req(&cmd);
    ZB_UNLOCK();

    if (tsn == 0) {
        mp_raise_msg(&mp_type_RuntimeError, "Failed to send write_attr");
    }
    
    return mp_obj_new_int(tsn);
}

MP_DEFINE_CONST_FUN_OBJ_KW(esp32_zig_read_attr_obj, 1, esp32_zig_read_attr);
MP_DEFINE_CONST_FUN_OBJ_KW(esp32_zig_write_attr_obj, 1, esp32_zig_write_attr);




