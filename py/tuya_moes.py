import struct

# Tuya DP Types
TUYA_DP_RAW = 0x00
TUYA_DP_BOOL = 0x01
TUYA_DP_VALUE = 0x02
TUYA_DP_STRING = 0x03
TUYA_DP_ENUM = 0x04
TUYA_DP_BITMAP = 0x05

# Tuya Commands
CMD_STATUS = 0x00
CMD_GET_DATA = 0x01
CMD_SET_DATA = 0x02
CMD_RESET = 0x03
CMD_SET_DP = 0x04
CMD_QUERY_DP = 0x05

# Moes TRV DataPoints
DP_SYSTEM_MODE = 0x01
DP_TARGET_TEMP = 0x02
DP_CURRENT_TEMP = 0x03
DP_RUNNING_STATE = 0x06
DP_WINDOW_STATE = 0x07
DP_WINDOW_DETECT = 0x08
DP_CHILD_LOCK = 0x0C
DP_BATTERY = 0x0D
DP_ALARM = 0x0E
DP_MIN_TEMP = 0x0F
DP_MAX_TEMP = 0x10
DP_SCHEDULE_MON = 0x11
DP_SCHEDULE_TUE = 0x12
DP_SCHEDULE_WED = 0x13
DP_SCHEDULE_THU = 0x14
DP_SCHEDULE_FRI = 0x15
DP_SCHEDULE_SAT = 0x16
DP_SCHEDULE_SUN = 0x17
DP_TEMP_CALIBRATION = 0x65
DP_VALVE_POSITION = 0x6C
DP_COMFORT_MODE = 0x72

#DataPoints
MOES_DP_CONFIG = {
    DP_SYSTEM_MODE: {
        'name': 'system_mode',
        'type': TUYA_DP_ENUM,
        'values': {
            0: 'off',      #(0x00)
            1: 'manual',   #0x01)
            2: 'away',     #(0x02)
            102: 'auto'    #(0x66)
        },
        'description': 'Thermostat mode',
    },
    DP_TARGET_TEMP: {
        'name': 'target_temperature',
        'type': TUYA_DP_VALUE,
        'unit': '°C',
        'divider': 10.0,
        'min': 5.0,
        'max': 35.0
    },
    DP_CURRENT_TEMP: {
        'name': 'current_temperature',
        'type': TUYA_DP_VALUE,
        'unit': '°C',
        'divider': 10.0,
        'min': 5.0,
        'max': 35.0
    },
    DP_RUNNING_STATE: {
        'name': 'running_state',
        'type': TUYA_DP_ENUM,
        'values': {
            0: 'idle',
            1: 'heat'
        }
    },
    DP_WINDOW_STATE: {
        'name': 'window_state',
        'type': TUYA_DP_ENUM,
        'values': {
            0: 'closed',
            1: 'open'
        }
    },
    DP_WINDOW_DETECT: {
        'name': 'window_detection',
        'type': TUYA_DP_ENUM,
        'values': {
            0: 'off',
            1: 'on'
        }
    },
    DP_CHILD_LOCK: {
        'name': 'child_lock',
        'type': TUYA_DP_ENUM,
        'values': {
            0: 'unlocked',
            1: 'locked'
        }
    },
    DP_BATTERY: {
        'name': 'battery',
        'type': TUYA_DP_VALUE,
        'unit': '%',
        'min': 0,
        'max': 100
    },
    DP_ALARM: {
        'name': 'alarm_switch',
        'type': TUYA_DP_ENUM,
        'values': {
            0: 'off',
            1: 'on'
        }
    },
    DP_MIN_TEMP: {
        'name': 'min_temperature',
        'type': TUYA_DP_VALUE,
        'unit': '°C',
        'divider': 10.0
    },
    DP_MAX_TEMP: {
        'name': 'max_temperature',
        'type': TUYA_DP_VALUE,
        'unit': '°C',
        'divider': 10.0
    },
    DP_SCHEDULE_MON: {
        'name': 'schedule_monday',
        'type': TUYA_DP_RAW
    },
    DP_SCHEDULE_TUE: {
        'name': 'schedule_tuesday',
        'type': TUYA_DP_RAW
    },
    DP_SCHEDULE_WED: {
        'name': 'schedule_wednesday',
        'type': TUYA_DP_RAW
    },
    DP_SCHEDULE_THU: {
        'name': 'schedule_thursday',
        'type': TUYA_DP_RAW
    },
    DP_SCHEDULE_FRI: {
        'name': 'schedule_friday',
        'type': TUYA_DP_RAW
    },
    DP_SCHEDULE_SAT: {
        'name': 'schedule_saturday',
        'type': TUYA_DP_RAW
    },
    DP_SCHEDULE_SUN: {
        'name': 'schedule_sunday',
        'type': TUYA_DP_RAW
    },
    DP_TEMP_CALIBRATION: {
        'name': 'local_temperature_calibration',
        'type': TUYA_DP_VALUE,
        'unit': ' C',
        'divider': 10.0,
        'min': -30.0,
        'max': 30.0
    },
    DP_VALVE_POSITION: {
        'name': 'position',
        'type': TUYA_DP_VALUE,
        'unit': '%',
        'divider': 10.0,
        'min': 0,
        'max': 100
    },
    DP_COMFORT_MODE: {
        'name': 'mode',
        'type': TUYA_DP_ENUM,
        'values': {
            0: 'comfort',
            1: 'eco'
        }
    }
}

def get_dp_config(dp_id):
    return MOES_DP_CONFIG.get(dp_id)

def parse_dp_value(dp_id, data_type, raw_value):
    config = get_dp_config(dp_id)
    if not config:
        return raw_value

    if config['type'] == TUYA_DP_BOOL:
        return bool(raw_value)
    
    elif config['type'] == TUYA_DP_ENUM:
        if 'values' in config:
            return config['values'].get(raw_value, 'unknown_%d' % raw_value)
        return raw_value
    
    elif config['type'] == TUYA_DP_VALUE:
        value = raw_value
        if 'divider' in config:
            value = value / config['divider']
        
        if 'min' in config and value < config['min']:
            value = config['min']
        if 'max' in config and value > config['max']:
            value = config['max']
            
        if 'unit' in config:
            return '%.1f%s' % (value, config['unit'])
        return value
    
    return raw_value

def parse_tuya_message(data):
    if len(data) < 2:
        return None
        
    # Короткие сообщения (heartbeat)
    if len(data) == 2:
        return {
            'command': data[1],
            'seq': data[0],
            'type': 'short'
        }
        
    if len(data) < 6:
        raise ValueError("Too short data for Tuya message")
    
    command = data[0]
    sequence = data[1]
    dp_id = data[2]
    data_type = data[3]
    data_len = struct.unpack('>H', data[4:6])[0]
    
    if len(data) < 6 + data_len:
        raise ValueError("Not enough data for Tuya message")
    dp_data = data[6:6+data_len]
    
  
    if data_type == TUYA_DP_RAW:
        value = dp_data
    elif data_type == TUYA_DP_BOOL:
        value = bool(dp_data[0])
    elif data_type == TUYA_DP_VALUE:
        value = struct.unpack('>i', dp_data)[0]
    elif data_type == TUYA_DP_STRING:
        value = dp_data.decode('utf-8')
    elif data_type == TUYA_DP_ENUM:
        value = dp_data[0]
    elif data_type == TUYA_DP_BITMAP:
        value = struct.unpack('>I', dp_data)[0]
    else:
        raise ValueError("Unknown data type %d" % data_type)


    config = get_dp_config(dp_id)
    name = config['name'] if config else 'unknown_dp_%d' % dp_id
    
    # Форматируем значение
    formatted = parse_dp_value(dp_id, data_type, value)

    return {
        "command": command,
        "sequence": sequence,
        "dp_id": dp_id,
        "data_type": data_type,
        "data_len": data_len,
        "raw": dp_data,
        "value": value,
        "formatted": formatted,
        "name": name
    }

