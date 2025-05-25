"""
Main Zigbee module for MicroPython
Auto-generated stub file for IDE support
"""

from typing import Union, Optional, Callable, Tuple, Any

class MSG:
    """Zigbee message types"""
    # Message type constants
    ZB_APP_SIGNAL_HANDLER: int
    ZB_ACTION_HANDLER: int
    RAW: int
    CL_CUSTOM_CMD: int
    REPORT_ATTR_CB: int
    
    @staticmethod
    def get_type_name(msg_type: int) -> str:
        """Get message type name"""
        ...
        
    @staticmethod
    def get_app_signal_name(signal_type: int) -> str:
        """Get app signal name"""
        ...

class ZIG:
    """Main Zigbee class"""
    
    MSG = MSG
    
    def __init__(self, start: bool = True, storage: Optional[Any] = None):
        """Initialize Zigbee module
        
        Args:
            start: Whether to start immediately
            storage: Storage handler for device data
        """
        ...
        
    def start_network(self) -> None:
        """Start Zigbee network"""
        ...
        
    def recv(self) -> Optional[Tuple[int, int, int, int, int, bytes]]:
        """Receive Zigbee message
        
        Returns:
            Tuple of (msg_type, signal_type, src_addr, endpoint, cluster_id, data)
            or None if no message available
        """
        ...
        
    def set_recv_callback(self, callback: Callable[[Any], None]) -> None:
        """Set message receive callback
        
        Args:
            callback: Function to call when message received
        """
        ...
        
    def send_raw_command(self, addr: int, endpoint: int, cluster_id: int, 
                        cmd_id: int, data: bytes) -> None:
        """Send raw Zigbee command
        
        Args:
            addr: Target device address
            endpoint: Target endpoint
            cluster_id: Cluster ID
            cmd_id: Command ID
            data: Command data
        """
        ...
