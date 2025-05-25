#zigbee module
import ZIG
# helpers
import ZCL_CLUSTER
import ZCL_ATTR_TYPE
import ZCL_STATUS
import ZCL_PROFILE
import ZCL_DEVICE
import ZCL_ACTION_CALLBACK
import ZCL_ATTR_ACCESS
#sotorage for json devices data
import zig_storage
# Tuya Moes TRV specific parsing
import tuya_moes

import struct


def zone_status_to_list(x: int) -> list:
    """Convert IAS Zone Status bitmask to list of active status flags"""
    flags = {
        0x0001: "Alarm1",
        0x0002: "Alarm2",
        0x0004: "Tamper",
        0x0008: "Battery",
        0x0010: "SupervisionReports",
        0x0020: "RestoreReports",
        0x0040: "Trouble",
        0x0080: "ACMains",
        0x0100: "Test",
    }
    return [name for bit, name in flags.items() if x & bit]


def convert_zcl_data(data_bytes, data_type):
    """Convert ZCL data bytes to appropriate Python value based on data type"""
    if len(data_bytes) == 0:
        return None
        
    try:
        if data_type == ZCL_ATTR_TYPE.ESP_ZB_ZCL_ATTR_TYPE_8BIT:  # signed char
            return struct.unpack('<b', data_bytes)[0]  
        elif data_type == ZCL_ATTR_TYPE.ESP_ZB_ZCL_ATTR_TYPE_U8:  # unsigned char
            return struct.unpack('<B', data_bytes)[0] 
        elif data_type == ZCL_ATTR_TYPE.ESP_ZB_ZCL_ATTR_TYPE_U16:  # unsigned short
            return struct.unpack('<H', data_bytes)[0]                    
        elif data_type == ZCL_ATTR_TYPE.ESP_ZB_ZCL_ATTR_TYPE_S16:  # signed short
            return struct.unpack('<h', data_bytes)[0]
        elif data_type == ZCL_ATTR_TYPE.ESP_ZB_ZCL_ATTR_TYPE_U32:  # unsigned int
            return struct.unpack('<I', data_bytes)[0]
        elif data_type == ZCL_ATTR_TYPE.ESP_ZB_ZCL_ATTR_TYPE_S32:  # signed int
            return struct.unpack('<i', data_bytes)[0]
        # Обработка BITMAP типов как беззнаковых целых
        elif data_type == ZCL_ATTR_TYPE.ESP_ZB_ZCL_ATTR_TYPE_8BITMAP:
            return data_bytes[0]
        elif data_type == ZCL_ATTR_TYPE.ESP_ZB_ZCL_ATTR_TYPE_16BITMAP:
            return struct.unpack('<H', data_bytes)[0]
        elif data_type == ZCL_ATTR_TYPE.ESP_ZB_ZCL_ATTR_TYPE_32BITMAP:
            return struct.unpack('<I', data_bytes)[0]
        else:
            return data_bytes.hex()
    except struct.error as e:
        print(f"    Debug: struct error: {e}")
        return data_bytes.hex()
    

ZCL_CLUSTER_ATTRS = {
    0x0001: {  # Power Configuration
        0x0020: {
            "name": "Battery Voltage",
            "convert": lambda x: x / 10.0,
            "unit": "V"
        },
        0x0021: {
            "name": "Battery Percentage Remaining",
            "convert": lambda x: x / 2.0,
            "unit": "%"
        },
    },
    0x0500: {  # IAS Zone
        0x0002: {
            "name": "Zone Status",
            "convert": zone_status_to_list,
            "unit": ""
        },
    },
   
}


def parse_attribute(cluster_id, data):
    """Parse ZCL attribute data from message"""
    print(f"    Debug: parsing data={data.hex()}, cluster_id=0x{cluster_id:04X}")
    
    if len(data) < 3:
        return {"error": "Not enough data", "raw_data": data.hex()}

    # ID атрибута (2 byte, little endian)
    attribute_id = data[0] | (data[1] << 8)
    print(f"    Debug: attribute_id=0x{attribute_id:04X}")

    # Data Type (1 byte)
    data_type = data[2]
    print(f"    Debug: data_type=0x{data_type:02X}")
    
    try:
        type_name = ZCL_ATTR_TYPE.get_type(data_type)
        type_size = ZCL_ATTR_TYPE.size(data_type)
    except:
        type_name = f"UNKNOWN_TYPE_{data_type}"
        type_size = 1
    
    print(f"    Debug: type_name={type_name}, type_size={type_size}")
    

    if len(data) < 3 + type_size:
        return {
            "error": "Incomplete data", 
            "attribute_id": f"0x{attribute_id:04X}",
            "data_type": type_name,
            "raw_data": data.hex()
        }

    value_data = data[3:3 + type_size]
    print(f"    Debug: value_data={value_data.hex()}")
    

    try:
        raw_value = convert_zcl_data(value_data, data_type)
        print(f"    Debug: raw_value={raw_value}")
    except Exception as e:
        print(f"    Debug: conversion error: {e}")
        raw_value = value_data.hex()
    

    attr_info = ZCL_CLUSTER_ATTRS.get(cluster_id, {}).get(attribute_id)
    
    if attr_info:
        name = attr_info["name"]
        convert_func = attr_info.get("convert", lambda x: x)
        unit = attr_info.get("unit", "")
        try:
            converted_value = convert_func(raw_value)
        except:
            converted_value = raw_value
    else:
        name = f"Unknown Attribute 0x{attribute_id:04X}"
        converted_value = raw_value
        unit = ""

    return {
        "attribute_id": f"0x{attribute_id:04X}",
        "name": name,
        "data_type": type_name,
        "raw_value": raw_value,
        "value": converted_value,
        "unit": unit,
        "remaining_data": data[3 + type_size:].hex() if len(data) > 3 + type_size else ""
    }



def handle_raw_command(data):

    cmd_id = data[0]
    cmd_direction = data[1]
    seq_number = data[2]
    is_common_command = bool(data[3])
    disable_default_response = bool(data[4])
    is_manuf_specific = bool(data[5])
    manuf_specific = data[6] | (data[7] << 8)
    profile_id = data[8] | (data[9] << 8)
    cluster_id = data[10] | (data[11] << 8)

    # Last remaining data is the payload
    payload = data[12:]

    print(f"RAW Command:")
    print(f"  cmd_id: 0x{cmd_id:02x}")
    print(f"  direction: {'server->client' if cmd_direction else 'client->server'}")
    print(f"  sequence: {seq_number}")
    print(f"  common_command: {is_common_command}")
    print(f"  disable_response: {disable_default_response}")
    print(f"  manuf_specific: {is_manuf_specific}")
    if is_manuf_specific:
        print(f"  manuf_code: 0x{manuf_specific:04x}")
    print(f"  profile_id: 0x{profile_id:04x}")
    print(f"  cluster_id: 0x{cluster_id:04x}")

    print('payload: {}'.format(' '.join('{:02x}'.format(x) for x in payload)))


def on_msg(any):
    #print("Processing messages...")
    try:
        while True:  # Process all available messages
            # Try to receive a message without blocking
            result = zig.recv()
            if result is None:
                break  # No more messages available, exit the loop

            print(f"Received a message: {result}")
            # Get message data
            msg_py, signal_type, src, ep, cid, data = result
            msg_py_name = ZIG.MSG.get_type_name(msg_py)
            
            cluster = ZCL_CLUSTER.get_type(cid)
            
            print(f"From: 0x{src:04X}, Endpoint: {ep}, Cluster: {cluster} (0x{cid:04X})")
            print(f"Message: {msg_py_name} ({msg_py})")
            if data:
                print(f"  data: {data.hex()}")

            if msg_py == ZIG.MSG.ZB_APP_SIGNAL_HANDLER: 
                signal_type_name = ZIG.MSG.get_app_signal_name(signal_type)
                print("  App Signal Details:")
                print(f"    Signal Type: {signal_type_name} ({signal_type})")

            elif msg_py == ZIG.MSG.ZB_ACTION_HANDLER:
                action_name = ZCL_ACTION_CALLBACK.get_type(signal_type)
                print("  Action Handler Details:")
                print(f"    Action Type: {action_name} ({signal_type})")
                
                # Parse attribute data for SET_ATTR_VALUE callbacks
                if signal_type == ZCL_ACTION_CALLBACK.ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID and data:
                    print("  Attribute Update Details:")
                    try:
                        result = parse_attribute(cid, data)
                        print(f"    Debug: parse_attribute returned: {result}")
                        if result:
                            print(f"    Attribute ID: {result.get('attribute_id', 'N/A')}")
                            print(f"    Attribute Name: {result.get('name', 'N/A')}")
                            print(f"    Data Type: {result.get('data_type', 'N/A')}")
                            print(f"    Raw Value: {result.get('raw_value', 'N/A')}")
                            print(f"    Converted Value: {result.get('value', 'N/A')}")
                            print(f"    Unit: {result.get('unit', 'N/A')}")
                    except Exception as e:
                        print(f"      Exception in parse_attribute: {e}")


            if msg_py == ZIG.MSG.RAW:
                handle_raw_command(data)

            elif msg_py == ZIG.MSG.CL_CUSTOM_CMD:
                print("Custom action received")
                tuya_dp_data = tuya_moes.parse_tuya_message(data)
                print(f"    TRV: {tuya_dp_data}")

            print("-" * 50)

    except Exception as e:
        print(f"Error in on_msg: {e}")


storage = zig_storage.ZigbeeStorage()
zig = ZIG(start=False, storage=storage.storage_handler)
zig.set_recv_callback(on_msg)
zig.start_network()