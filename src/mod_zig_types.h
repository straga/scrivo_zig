// Copyright (c) 2025 Viktor Vorobjov
#ifndef MOD_ZIG_TYPES_H
#define MOD_ZIG_TYPES_H

#include "py/obj.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_zigbee_type.h"

// Configuration structure for Zigbee module
typedef struct _esp32_zig_config_t {
    char general[32];        // General configuration name
    uint32_t bitrate;        // UART bitrate
    uint8_t rcp_reset_pin;   // RCP reset pin
    uint8_t rcp_boot_pin;    // RCP boot pin
    uint8_t uart_port;       // UART port number
    uint8_t uart_rx_pin;     // UART RX pin
    uint8_t uart_tx_pin;     // UART TX pin
    bool network_formed;     // Network formation status
    uint16_t pan_id;         // PAN ID for the network
    uint8_t channel;         // Channel number
} esp32_zig_config_t;

// Forward declaration of main structure
typedef struct _esp32_zig_obj_t esp32_zig_obj_t;

// Main Zigbee object structure
typedef struct _esp32_zig_obj_t {
    mp_obj_base_t base;                // Base object header for MicroPython
    esp32_zig_config_t *config;        // Pointer to Zigbee configuration settings
    mp_obj_t rx_callback;              // Callback for incoming messages 
    mp_obj_t tx_callback;              // Callback for transmission completion
    TaskHandle_t irq_handler;          // FreeRTOS task handle for RCP event processing
    TaskHandle_t gateway_task;         // FreeRTOS task handle for Zigbee gateway main loop
    QueueHandle_t message_queue;       // Queue for delivering Zigbee messages to MicroPython
    mp_obj_t storage_cb;               // Callback for saving devices to storage
} esp32_zig_obj_t;

// RCP version structure
typedef struct {
    uint8_t version;
    uint8_t capabilities;
    uint8_t ncp_version[20];
} rcp_version_t;

// Production configuration structure
typedef struct {
    uint8_t version;
    uint16_t manuf_code;
    char manuf_name[32];
} app_production_config_t;

// Maximum configurations
#define MAX_DEVICES 32
#define MAX_ENDPOINTS 40       // Unified value
#define MAX_CLUSTERS 16        // Unified value
#define MAX_REPORT_CFGS 16

#define MAX_DEVICE_NAME_LEN 32
#define MAX_MANUFACTURER_NAME_LEN 32
#define MAX_MODEL_LEN 32

// Forward declaration if esp_zigbee_zcl_common.h is not included here
// Alternatively, include the necessary header
// #include "zcl/esp_zigbee_zcl_common.h" // Assuming this path is correct for esp_zb_zcl_report_direction_t

// Define constants for direction if the enum itself is not directly used or available.
#define REPORT_CFG_DIRECTION_SEND 0x00
#define REPORT_CFG_DIRECTION_RECV 0x01

// Structure for attribute reporting configuration
typedef struct {
    bool in_use;            // Whether this configuration slot is in use
    uint8_t direction;      // Direction of reporting: 0x00 for SEND, 0x01 for RECV
                            // Corresponds to esp_zb_zcl_report_direction_t
    uint8_t ep;             // Endpoint
    uint16_t cluster_id;    // Cluster ID
    uint16_t attr_id;       // Attribute ID

    union {
        struct {                                    // Configuration for sending reports (direction == REPORT_CFG_DIRECTION_SEND)
            uint8_t attr_type;                      // Attribute type
            uint16_t min_int;                       // Minimum reporting interval
            uint16_t max_int;                       // Maximum reporting interval
            uint32_t reportable_change_val;         // Reportable change value, 0xFFFFFFFF if not used/discrete
        } send_cfg;

        struct {                                    // Configuration for receiving reports (direction == REPORT_CFG_DIRECTION_RECV)
            uint16_t timeout_period;                // Timeout period for receiving reports
        } recv_cfg;
    };
} report_cfg_t;

// Structure for storing information about the endpoint
typedef struct {
    uint8_t endpoint;              // Endpoint number
    uint16_t profile_id;           // Profile ID
    uint16_t device_id;            // Device ID
    uint16_t cluster_list[MAX_CLUSTERS]; // List of supported clusters
    uint8_t cluster_count;         // Number of clusters
} zigbee_endpoint_t;

// Structure for storing information about a Zigbee device
typedef struct {
    uint16_t short_addr;                            // Short address of the device
    uint8_t ieee_addr[8];                           // IEEE address as byte array
    char ieee_addr_str[24];                         // Formatted IEEE address string (e.g., "XX:XX:XX:XX:XX:XX:XX:XX\0")
    uint8_t endpoint_count;                         // Number of endpoints
    zigbee_endpoint_t endpoints[MAX_ENDPOINTS];     // Array of endpoints
    report_cfg_t report_cfgs[MAX_REPORT_CFGS];      // Array of report configurations
    uint32_t last_seen;                             // Last seen timestamp
    char manufacturer_name[MAX_MANUFACTURER_NAME_LEN]; // Manufacturer name
    char model[MAX_MODEL_LEN];                                 // Model name
    char device_name[MAX_DEVICE_NAME_LEN];                           // Device name
    bool active;                                    // Device active status
    uint8_t firmware_version;                       // Firmware version
    uint8_t power_source;                           // Power source
    uint8_t battery_voltage;                        // Battery voltage
    uint8_t battery_percentage;                     // Battery percentage
    uint16_t manufacturer_code;                     // Manufacturer code
    uint8_t prod_config_version;                    // Production config version
    uint8_t last_lqi;                               // Link Quality Indicator (0-255)
    int8_t last_rssi;                               // Received Signal Strength Indicator (dBm)
} zigbee_device_t;

// Structure for managing a list of Zigbee devices
typedef struct {
    zigbee_device_t devices[MAX_DEVICES];  // Array of devices
    uint8_t device_count;                  // Number of devices in the list
} zigbee_device_list_t;

// Structure for Zigbee messages
typedef struct {
    uint16_t msg_py;         // Micropython Message type
    uint16_t signal_type;    // Signal type
    uint16_t src_addr;      // Source address
    uint8_t endpoint;       // Endpoint
    uint16_t cluster_id;    // Cluster ID
    uint8_t data[256];      // Message data
    uint8_t data_len;       // Data length
} zigbee_message_t;

// Structure for bind context
typedef struct {
    uint16_t short_addr;    // Short address of the device
    uint8_t endpoint;       // Endpoint number
    uint16_t cluster_id;    // Cluster ID
} bind_ctx_t;

#endif // MOD_ZIG_TYPES_H
