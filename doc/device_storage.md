# Device Management

## Device Storage

Each device is saved in a separate JSON file named `[short_addr].json`, where short_addr is the device's short address in hex format.

### Storage Structure

```
/flash/devices/     # Base directory for storage
  1234.json        # Device with address 0x1234
  5678.json        # Device with address 0x5678
  ...
```
