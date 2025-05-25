// Copyright (c) 2025 Viktor Vorobjov
#ifndef MOD_ZIG_CUSTOM_H
#define MOD_ZIG_CUSTOM_H

#include "esp_zigbee_core.h"
#include "mod_zig_msg.h"

// Custom cluster identifiers
#define CUSTOM_CLUSTER_CLI_ID         0xEF00


// Initialize custom cluster handlers
esp_err_t custom_clusters_init(void);


#endif // MOD_ZIG_CUSTOM_H
