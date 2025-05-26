

IKEA E2202

## Method 1: set_report_config + bind_cluster (Recommended)

```py

import ZIG  
zig=ZIG()

device_addr=0x9f54

zig.set_report_config(
    addr=device_addr,
    ep=1,
    cl=0x0500,
    attr=0x0002,
    attr_type=0x19,
    min_int=1,
    max_int=60,
)
zig.bind_cluster(addr=device_addr, ep=1, cl=0x0500)



# Battery Voltage Monitoring
zig.set_report_config(
    addr=device_addr,
    ep=1,
    cl=0x0001,         # Power Configuration cluster
    attr=0x0020,       # BatteryVoltage attribute
    attr_type=0x20,    # uint8 (voltage in 100mV units)
    min_int=3600,      # Minimum 1 hour interval
    max_int=21600,     # Maximum 6 hours interval
    reportable_change=1 # Report on 0.1V change
)

# Battery Percentage Monitoring
zig.set_report_config(
    addr=device_addr,
    ep=1,
    cl=0x0001,         # Power Configuration cluster
    attr=0x0021,       # BatteryPercentageRemaining
    attr_type=0x20,    # uint8 (percentage * 2, so 100% = 200)
    min_int=3600,      # Minimum 1 hour interval
    max_int=21600,     # Maximum 6 hours interval
    reportable_change=4 # Report on 2% change (4 units = 2%)
)

# Bind Power Configuration cluster
zig.bind_cluster(addr=device_addr, ep=1, cl=0x0001)


```