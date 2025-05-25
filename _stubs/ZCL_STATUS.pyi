"""
ZigBee Cluster Library Status Codes
Auto-generated stub file for IDE support
Generated on 2025-05-25 14:15:03
"""

from typing import Union, Optional

# Module constants/enums
ESP_ZB_ZCL_STATUS_SUCCESS: int = 0  # 0x00
ESP_ZB_ZCL_STATUS_FAIL: int = 1  # 0x01
ESP_ZB_ZCL_STATUS_NOT_AUTHORIZED: int = 126  # 0x7E
ESP_ZB_ZCL_STATUS_MALFORMED_CMD: int = 128  # 0x80
ESP_ZB_ZCL_STATUS_UNSUP_CLUST_CMD: int = 129  # 0x81
ESP_ZB_ZCL_STATUS_UNSUP_GEN_CMD: int = 130  # 0x82
ESP_ZB_ZCL_STATUS_UNSUP_MANUF_CLUST_CMD: int = 131  # 0x83
ESP_ZB_ZCL_STATUS_UNSUP_MANUF_GEN_CMD: int = 132  # 0x84
ESP_ZB_ZCL_STATUS_INVALID_FIELD: int = 133  # 0x85
ESP_ZB_ZCL_STATUS_UNSUP_ATTRIB: int = 134  # 0x86
ESP_ZB_ZCL_STATUS_INVALID_VALUE: int = 135  # 0x87
ESP_ZB_ZCL_STATUS_READ_ONLY: int = 136  # 0x88
ESP_ZB_ZCL_STATUS_INSUFF_SPACE: int = 137  # 0x89
ESP_ZB_ZCL_STATUS_DUPE_EXISTS: int = 138  # 0x8a
ESP_ZB_ZCL_STATUS_NOT_FOUND: int = 139  # 0x8b
ESP_ZB_ZCL_STATUS_UNREPORTABLE_ATTRIB: int = 140  # 0x8c
ESP_ZB_ZCL_STATUS_INVALID_TYPE: int = 141  # 0x8d
ESP_ZB_ZCL_STATUS_WRITE_ONLY: int = 143  # 0x8f
ESP_ZB_ZCL_STATUS_INCONSISTENT: int = 146  # 0x92
ESP_ZB_ZCL_STATUS_ACTION_DENIED: int = 147  # 0x93
ESP_ZB_ZCL_STATUS_TIMEOUT: int = 148  # 0x94
ESP_ZB_ZCL_STATUS_ABORT: int = 149  # 0x95
ESP_ZB_ZCL_STATUS_INVALID_IMAGE: int = 150  # 0x96
ESP_ZB_ZCL_STATUS_WAIT_FOR_DATA: int = 151  # 0x97
ESP_ZB_ZCL_STATUS_NO_IMAGE_AVAILABLE: int = 152  # 0x98
ESP_ZB_ZCL_STATUS_REQUIRE_MORE_IMAGE: int = 153  # 0x99
ESP_ZB_ZCL_STATUS_NOTIFICATION_PENDING: int = 154  # 0x9A
ESP_ZB_ZCL_STATUS_HW_FAIL: int = 192  # 0xc0
ESP_ZB_ZCL_STATUS_SW_FAIL: int = 193  # 0xc1
ESP_ZB_ZCL_STATUS_CALIB_ERR: int = 194  # 0xc2
ESP_ZB_ZCL_STATUS_UNSUP_CLUST: int = 195  # 0xc3
ESP_ZB_ZCL_STATUS_LIMIT_REACHED: int = 196  # 0xc4

# Module functions
def get_type(value: int) -> str:
    """
    Get the string representation of a constant value.
    
    Args:
        value: Integer value to lookup
    
    Returns:
        String name of the constant
    """
    ...

def size(value: int) -> int:
    """
    Get the size in bytes for a data type.
    
    Args:
        value: Data type value
    
    Returns:
        Size in bytes
    """
    ...

