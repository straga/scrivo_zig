
```py

# Claster: 0x0006

addr = 0xc338

# On
zig.send_command(addr, 1, 0x0006, 0x01, bytes([0x01]))

# Off
zig.send_command(addr, 1, 0x0006, 0x00, bytes([0x00]))


# Claster: 0x0008

# Brightness Decrease
zig.send_command(addr, 1, 0x0008, 0x01, bytes([0x01, 0x50, 0x00, 0x00]))

# Increase brightness
zig.send_command(addr, 1, 0x0008, 0x01, bytes([0x00, 0x50, 0x00, 0x00]))

# Stop change
zig.send_command(addr, 1, 0x0008, 0x07, bytes([0x00]))



Field Name             | Type   | Description
----------------------|--------|-------------------------
Level                 | uint8  | Brightness value (0-254) [0x64]
Transition Time       | uint16 | Transition time in 1/10 seconds (0 = instant) [0x0A, 0x00]

Example: set brightness to 100 (out of 254)

zig.send_command(
    addr=addr,
    ep=1,
    cl=0x0008,
    cmd=0x00,
    data=bytes([0x64, 0x00, 0x00])  # 0x64 = 100, transition time = 0x0000
)
    •    0x64 - brightness 100
    •    0x0000 - instant transition


to change brightness smoothly over 1 second, pass 0x000A (10 in tenths of a second):
payload=bytes([200, 0x0A, 0x00])

zig.send_command(addr=addr, ep=1, cl=0x0008, cmd=0x00, data=bytes([200, 0x0A, 0x00]))


# Read MinLevel
zig.read_attr(addr, 1, 0x0008, 0x0001)

# Read MaxLevel
zig.read_attr(addr, 1, 0x0008, 0x0002)

# Read OnOffTransitionTime
zig.read_attr(addr, 1, 0x0008, 0x0010)

FeatureMap (0xFFFD) of LEVEL_CONTROL cluster:
0x0003 = 0b0000_0000_0000_0011
This is a bit mask of supported features:
Bit 0 (1): ON/OFF = 1 (supported)
Bit 1 (1): LEVEL CONTROL = 1 (supported)
Other bits (0): extended features not supported



