# Binding Table

This command retrieves the ZDO binding table from a Zigbee device via the Python API.

## Python API

```python
>>> zig.get_binding_table(addr=device_addr, start_index=0)
```

- **addr**: 16-bit short network address of the target device (integer).
- **start_index**: Binding table entry index from which to start retrieval (default `0`).

## Output

Each binding table entry is logged to the ESP-IDF console and sent as a raw message to MicroPython. Log format:

```
I (<timestamp>) ZIGBEE_HANDLERS: Binding table response for 0x<short_addr>: total=<total>, count=<count>
I (<timestamp>) ZIGBEE_HANDLERS: Binding record: <src_ieee> ep=<src_ep> cluster=0x<cluster_id> -> <dst_ieee> ep=<dst_ep>
```

### Example

```sh
>>> zig.get_binding_table(addr=device_addr, start_index=0)
>>> I (951606) ZIGBEE_HANDLERS: Binding table response for 0x21e7: total=2, count=2
I (951606) ZIGBEE_HANDLERS: Binding record: 9d:06:63:fe:ff:de:c7:b0 ep=1 cluster=0x0500 -> 63:39:c0:fe:ff:b7:31:48 ep=1
I (951616) ZIGBEE_HANDLERS: Binding record: 9d:06:63:fe:ff:de:c7:b0 ep=1 cluster=0x0001 -> 63:39:c0:fe:ff:b7:31:48 ep=1
```

## Cluster Binding

Use the Python `bind_cluster` method to establish a binding between two Zigbee endpoints (for example, a button -> a light).

```python
# Default: bind to the gateway (G2W) on endpoint 1
zig.bind_cluster(
    addr=src_addr,    # button short address
    ep=src_ep,        # button endpoint
    cl=cluster_id     # cluster ID (e.g., 0x0006 for On/Off)
)
```

Default parameters:
- **dst_addr=0** (binds to coordinator/gateway IEEE)
- **dst_ep=1** (gateway endpoint)

To bind directly button -> light:
```python
zig.bind_cluster(
    addr=0x1234,      # button
    ep=1,             # its EP
    cl=0x0006,        # On/Off cluster
    dst_addr=0x5678,  # light short address
    dst_ep=1          # light endpoint
)
```

- **dst_addr**: destination short network address (0 = gateway)
- **dst_ep**: destination endpoint (default 1)