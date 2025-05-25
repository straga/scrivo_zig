// Copyright (c) 2025 Viktor Vorobjov
#ifndef MOD_ZIG_HANDLERS_H
#define MOD_ZIG_HANDLERS_H

#include "mod_zig_types.h"
#include "esp_zigbee_type.h"
#include "esp_zigbee_core.h"
#include "esp_err.h"

// Handler for signals from Zigbee stack
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct);

// RCP error handler
void rcp_error_handler(void);

// Handler for unprocessed Zigbee commands
bool zb_raw_cmd_handler(uint8_t bufid);

// ZCL action handler
esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message);

// Callback for bind operations
void bind_cb(esp_zb_zdp_status_t status, void *user_ctx);

void send_msg_to_micropython_queue(uint8_t msg_py, uint16_t signal_type, uint16_t src_addr, uint8_t endpoint, 
                                 uint16_t cluster_id, uint8_t *data, uint8_t data_len);

#endif /* MOD_ZIG_HANDLERS_H */