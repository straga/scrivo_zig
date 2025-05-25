"""
ZigBee Cluster Library Attribute Access Rights
Auto-generated stub file for IDE support
Generated on 2025-05-25 14:15:03
"""

from typing import Union, Optional

# Module constants/enums
ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY: int = 1  # 0x01
ESP_ZB_ZCL_ATTR_ACCESS_WRITE_ONLY: int = 2  # 0x02
ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE: int = 3  # 0x03
ESP_ZB_ZCL_ATTR_ACCESS_REPORTING: int = 4  # 0x04
ESP_ZB_ZCL_ATTR_ACCESS_SINGLETON: int = 8  # 0x08
ESP_ZB_ZCL_ATTR_ACCESS_SCENE: int = 16  # 0x10
ESP_ZB_ZCL_ATTR_MANUF_SPEC: int = 32  # 0x20
ESP_ZB_ZCL_ATTR_ACCESS_INTERNAL: int = 64  # 0x40

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

