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
    
    def init(self) -> None: ...
    def get_info(self) -> Any: ...
    def reset_to_factory(self) -> None: ...
    def open_network(self) -> None: ...
    def close_network(self) -> None: ...
    def get_network_info(self) -> Any: ...
    def update_network_status(self) -> None: ...
    def scan_networks(self) -> list: ...

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
    
    def any(self) -> bool:
        """Check if any messages are available
        
        Returns:
            True if messages are available, False otherwise
        """
        ...


    def set_recv_callback(self, callback: Callable[[Any], None]) -> None:
        """Set message receive callback
        
        Args:
            callback: Function to call when message received
        """
        ...
        
    def send_command(self, addr: int, endpoint: int, cluster_id: int, 
                        cmd_id: int, data: bytes) -> None:
        """Send Zigbee command

        Args:
            addr: Target device address
            endpoint: Target endpoint
            cluster_id: Cluster ID
            cmd_id: Command ID
            data: Command data
        """
        ...
    def bind_cluster(self, addr: int, endpoint: int, cluster_id: int) -> None: ...
    def configure_report(self, addr: int, endpoint: int, cluster_id: int, attr_id: int, min_int: int, max_int: int, change: Any) -> None: ...
    def set_report_config(self, addr: int, endpoint: int, cluster_id: int, attr_id: int, config: Any) -> None: ...
    def read_attr(self, addr: int, endpoint: int, cluster_id: int, attr_id: int) -> Any: ...
    def write_attr(self, addr: int, endpoint: int, cluster_id: int, attr_id: int, value: Any) -> None: ...

    # Message type constants
    #
