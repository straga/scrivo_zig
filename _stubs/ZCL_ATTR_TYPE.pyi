"""
ZigBee Cluster Library Attribute Types
Auto-generated stub file for IDE support
Generated on 2025-05-25 14:15:03
"""

from typing import Union, Optional

# Module constants/enums
ESP_ZB_ZCL_ATTR_TYPE_NULL: int = 0  # 0x00
ESP_ZB_ZCL_ATTR_TYPE_8BIT: int = 8  # 0x08
ESP_ZB_ZCL_ATTR_TYPE_16BIT: int = 9  # 0x09
ESP_ZB_ZCL_ATTR_TYPE_24BIT: int = 10  # 0x0a
ESP_ZB_ZCL_ATTR_TYPE_32BIT: int = 11  # 0x0b
ESP_ZB_ZCL_ATTR_TYPE_40BIT: int = 12  # 0x0c
ESP_ZB_ZCL_ATTR_TYPE_48BIT: int = 13  # 0x0d
ESP_ZB_ZCL_ATTR_TYPE_56BIT: int = 14  # 0x0e
ESP_ZB_ZCL_ATTR_TYPE_64BIT: int = 15  # 0x0f
ESP_ZB_ZCL_ATTR_TYPE_BOOL: int = 16  # 0x10
ESP_ZB_ZCL_ATTR_TYPE_8BITMAP: int = 24  # 0x18
ESP_ZB_ZCL_ATTR_TYPE_16BITMAP: int = 25  # 0x19
ESP_ZB_ZCL_ATTR_TYPE_24BITMAP: int = 26  # 0x1a
ESP_ZB_ZCL_ATTR_TYPE_32BITMAP: int = 27  # 0x1b
ESP_ZB_ZCL_ATTR_TYPE_40BITMAP: int = 28  # 0x1c
ESP_ZB_ZCL_ATTR_TYPE_48BITMAP: int = 29  # 0x1d
ESP_ZB_ZCL_ATTR_TYPE_56BITMAP: int = 30  # 0x1e
ESP_ZB_ZCL_ATTR_TYPE_64BITMAP: int = 31  # 0x1f
ESP_ZB_ZCL_ATTR_TYPE_U8: int = 32  # 0x20
ESP_ZB_ZCL_ATTR_TYPE_U16: int = 33  # 0x21
ESP_ZB_ZCL_ATTR_TYPE_U24: int = 34  # 0x22
ESP_ZB_ZCL_ATTR_TYPE_U32: int = 35  # 0x23
ESP_ZB_ZCL_ATTR_TYPE_U40: int = 36  # 0x24
ESP_ZB_ZCL_ATTR_TYPE_U48: int = 37  # 0x25
ESP_ZB_ZCL_ATTR_TYPE_U56: int = 38  # 0x26
ESP_ZB_ZCL_ATTR_TYPE_U64: int = 39  # 0x27
ESP_ZB_ZCL_ATTR_TYPE_S8: int = 40  # 0x28
ESP_ZB_ZCL_ATTR_TYPE_S16: int = 41  # 0x29
ESP_ZB_ZCL_ATTR_TYPE_S24: int = 42  # 0x2a
ESP_ZB_ZCL_ATTR_TYPE_S32: int = 43  # 0x2b
ESP_ZB_ZCL_ATTR_TYPE_S40: int = 44  # 0x2c
ESP_ZB_ZCL_ATTR_TYPE_S48: int = 45  # 0x2d
ESP_ZB_ZCL_ATTR_TYPE_S56: int = 46  # 0x2e
ESP_ZB_ZCL_ATTR_TYPE_S64: int = 47  # 0x2f
ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM: int = 48  # 0x30
ESP_ZB_ZCL_ATTR_TYPE_16BIT_ENUM: int = 49  # 0x31
ESP_ZB_ZCL_ATTR_TYPE_SEMI: int = 56  # 0x38
ESP_ZB_ZCL_ATTR_TYPE_SINGLE: int = 57  # 0x39
ESP_ZB_ZCL_ATTR_TYPE_DOUBLE: int = 58  # 0x3a
ESP_ZB_ZCL_ATTR_TYPE_OCTET_STRING: int = 65  # 0x41
ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING: int = 66  # 0x42
ESP_ZB_ZCL_ATTR_TYPE_LONG_OCTET_STRING: int = 67  # 0x43
ESP_ZB_ZCL_ATTR_TYPE_LONG_CHAR_STRING: int = 68  # 0x44
ESP_ZB_ZCL_ATTR_TYPE_ARRAY: int = 72  # 0x48
ESP_ZB_ZCL_ATTR_TYPE_16BIT_ARRAY: int = 73  # 0x49
ESP_ZB_ZCL_ATTR_TYPE_32BIT_ARRAY: int = 74  # 0x4a
ESP_ZB_ZCL_ATTR_TYPE_STRUCTURE: int = 76  # 0x4c
ESP_ZB_ZCL_ATTR_TYPE_SET: int = 80  # 0x50
ESP_ZB_ZCL_ATTR_TYPE_BAG: int = 81  # 0x51
ESP_ZB_ZCL_ATTR_TYPE_TIME_OF_DAY: int = 224  # 0xe0
ESP_ZB_ZCL_ATTR_TYPE_DATE: int = 225  # 0xe1
ESP_ZB_ZCL_ATTR_TYPE_UTC_TIME: int = 226  # 0xe2
ESP_ZB_ZCL_ATTR_TYPE_CLUSTER_ID: int = 232  # 0xe8
ESP_ZB_ZCL_ATTR_TYPE_ATTRIBUTE_ID: int = 233  # 0xe9
ESP_ZB_ZCL_ATTR_TYPE_BACNET_OID: int = 234  # 0xea
ESP_ZB_ZCL_ATTR_TYPE_IEEE_ADDR: int = 240  # 0xf0
ESP_ZB_ZCL_ATTR_TYPE_128_BIT_KEY: int = 241  # 0xf1
ESP_ZB_ZCL_ATTR_TYPE_INVALID: int = 255  # 0xff

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

