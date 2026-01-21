# Bluetooth Subsystem Documentation

## Overview

The IR0 kernel includes a complete Bluetooth subsystem implementation following the Bluetooth Core Specification. The implementation provides Host Controller Interface (HCI) support with UART transport, device discovery, and management capabilities integrated into the kernel's unified driver system.

## Architecture

### Component Structure

```
Bluetooth Subsystem
├── HCI Core (hci_core.c/h)
│   ├── Command/Event Processing
│   ├── Device Discovery (Inquiry)
│   ├── Remote Name Resolution
│   └── Event Buffer Management
├── HCI UART Transport (hci_uart.c/h)
│   ├── UART Communication
│   ├── Packet Framing
│   └── Flow Control
├── Device Management (bt_device.c/h)
│   ├── Device Registration (/dev/bluetooth)
│   ├── User-space Interface (ioctl)
│   ├── Scan Control
│   └── Event Reporting
└── Initialization (bluetooth_init.c/h)
    ├── Driver Registration
    ├── Hardware Detection
    └── Subsystem Startup
```

### Integration Points

- **Driver Registry**: Registered as `bluetooth` driver in unified system
- **Device Filesystem**: Exposed as `/dev/bluetooth` character device
- **Serial Subsystem**: Uses kernel serial drivers for UART communication
- **Memory Management**: Integrated with kernel heap allocator
- **Logging System**: Uses kernel logging infrastructure

## Implementation Details

### HCI Core Features

#### Command Processing
- **HCI Reset**: Controller initialization and reset
- **Inquiry Commands**: Device discovery with configurable parameters
- **Remote Name Request**: Retrieve human-readable device names
- **Command Queue**: Asynchronous command processing with completion tracking

#### Event Handling
- **Event Buffer**: Circular buffer for incoming HCI events
- **Event Parsing**: Complete parsing of HCI event packets
- **Event Types Supported**:
  - Command Complete (0x0E)
  - Command Status (0x0F)
  - Inquiry Complete (0x01)
  - Inquiry Result (0x02)
  - Remote Name Request Complete (0x07)

#### Device Discovery
```c
// Start device inquiry
int hci_inquiry_start(hci_device_t *hdev, uint8_t length, uint8_t num_responses);

// Stop ongoing inquiry
int hci_inquiry_stop(hci_device_t *hdev);

// Get discovered devices
int hci_get_discovered_devices(hci_device_t *hdev, bt_device_info_t *devices, int max_devices);
```

### UART Transport Layer

#### Communication Protocol
- **Packet Types**: Command (0x01), ACL Data (0x02), Event (0x04)
- **Framing**: Standard HCI UART packet format
- **Flow Control**: Software flow control with buffer management
- **Error Handling**: Packet validation and error recovery

#### Buffer Management
- **Receive Buffer**: 1024-byte circular buffer for incoming data
- **Transmit Queue**: Asynchronous transmission with completion callbacks
- **Overflow Protection**: Buffer overflow detection and recovery

### Device Interface

#### Character Device Operations
```c
// Device operations structure
static const struct file_operations bt_fops = {
    .open = bt_device_open,
    .close = bt_device_close,
    .read = bt_device_read,
    .write = bt_device_write,
    .ioctl = bt_device_ioctl
};
```

#### IOCTL Commands
- **BT_IOCTL_SCAN_START**: Start device discovery
- **BT_IOCTL_SCAN_STOP**: Stop device discovery
- **BT_IOCTL_GET_DEVICES**: Retrieve discovered devices
- **BT_IOCTL_RESET**: Reset Bluetooth controller

#### User-space Interface
```c
// Example usage from user-space
int bt_fd = open("/dev/bluetooth", O_RDWR);
ioctl(bt_fd, BT_IOCTL_SCAN_START, "start");
// ... wait for scan completion ...
bt_device_info_t devices[10];
int count = ioctl(bt_fd, BT_IOCTL_GET_DEVICES, devices);
```

## Configuration and Setup

### Hardware Requirements
- **Bluetooth Controller**: HCI-compatible controller with UART interface
- **Serial Port**: Available COM port for UART communication
- **Power Management**: Controller power control (implementation dependent)

### Kernel Configuration
```c
// config.h - Bluetooth subsystem configuration
#define BLUETOOTH_ENABLED 1
#define BLUETOOTH_MAX_DEVICES 32
#define BLUETOOTH_EVENT_BUFFER_SIZE 2048
#define BLUETOOTH_UART_BUFFER_SIZE 1024
```

### Initialization Sequence
1. **Driver Registration**: Register with kernel driver registry
2. **Hardware Detection**: Probe for Bluetooth controller
3. **UART Setup**: Configure serial communication parameters
4. **HCI Reset**: Initialize controller to known state
5. **Device Creation**: Create `/dev/bluetooth` character device
6. **Event Processing**: Start event handling thread

## Usage Examples

### Device Discovery
```c
// Kernel-space device discovery
hci_device_t *hdev = get_hci_device();
int result = hci_inquiry_start(hdev, 10, 8); // 10 seconds, max 8 devices
if (result == 0) {
    // Wait for inquiry completion
    while (hdev->scanning) {
        schedule(); // Yield to other processes
    }
    
    // Get discovered devices
    bt_device_info_t devices[8];
    int count = hci_get_discovered_devices(hdev, devices, 8);
    
    for (int i = 0; i < count; i++) {
        LOG_INFO_FMT("BT", "Device %d: %02X:%02X:%02X:%02X:%02X:%02X (%s)",
                     i, devices[i].bdaddr[0], devices[i].bdaddr[1],
                     devices[i].bdaddr[2], devices[i].bdaddr[3],
                     devices[i].bdaddr[4], devices[i].bdaddr[5],
                     devices[i].name);
    }
}
```

### Shell Commands
The debug shell includes Bluetooth management commands:

```bash
# Start Bluetooth scan
bt_scan start

# Stop Bluetooth scan  
bt_scan stop

# List discovered devices
bt_devices

# Reset Bluetooth controller
bt_reset
```

## Error Handling and Diagnostics

### Error Codes
- **-EBUSY**: Operation already in progress
- **-ENODEV**: No Bluetooth controller found
- **-ETIMEDOUT**: Command timeout
- **-EIO**: Communication error
- **-ENOMEM**: Memory allocation failure

### Diagnostic Features
- **Kernel Logging**: Comprehensive logging with configurable levels
- **Event Tracing**: Complete HCI event logging for debugging
- **Buffer Statistics**: Monitor buffer usage and overflow conditions
- **Command Tracking**: Track command completion and timeouts

### Debug Output
```c
// Enable Bluetooth debugging
#define DEBUG_BLUETOOTH 1

// Example debug output
[BT] HCI Reset command sent
[BT] Command Complete event received (opcode: 0x0C03)
[BT] Inquiry started (length: 10s, max_responses: 8)
[BT] Inquiry Result: Device 00:1A:2B:3C:4D:5E (Class: 0x240404)
[BT] Remote Name: "Device Name"
[BT] Inquiry Complete (status: 0x00, devices: 3)
```

## Performance Characteristics

### Memory Usage
- **Static Memory**: ~4KB for core data structures
- **Dynamic Memory**: ~2KB per discovered device
- **Buffer Memory**: ~3KB for UART and event buffers
- **Total Footprint**: ~10KB typical, ~50KB maximum

### Timing Characteristics
- **Command Latency**: <10ms typical for local commands
- **Inquiry Duration**: 1.28s to 61.44s (configurable)
- **Event Processing**: <1ms per event
- **UART Throughput**: Up to 115.2 Kbps (hardware dependent)

### Scalability
- **Maximum Devices**: 32 discovered devices (configurable)
- **Concurrent Operations**: Single inquiry, multiple name requests
- **Event Queue**: 128 events maximum (configurable)

## Security Considerations

### Access Control
- **Device Permissions**: `/dev/bluetooth` requires appropriate permissions
- **Privilege Separation**: User-space access through well-defined interface
- **Buffer Validation**: All user-space data validated before processing

### Privacy Protection
- **Address Filtering**: Support for device address filtering
- **Inquiry Control**: User-controlled device discovery
- **Data Sanitization**: Clear sensitive data on device close

## Future Enhancements

### Planned Features
- **L2CAP Protocol**: Logical Link Control and Adaptation Protocol
- **SDP Support**: Service Discovery Protocol implementation
- **Profile Support**: A2DP, HID, SPP profile implementations
- **Security Manager**: Pairing and encryption support
- **Power Management**: Advanced power saving features

### API Extensions
- **Connection Management**: Establish and manage ACL connections
- **Service Discovery**: Discover and connect to Bluetooth services
- **Profile APIs**: High-level APIs for common Bluetooth profiles
- **Event Callbacks**: Asynchronous event notification system

## Troubleshooting

### Common Issues

#### Controller Not Detected
```bash
# Check if controller is present
dmesg | grep -i bluetooth
# Expected: [BLUETOOTH] Controller detected on COM1
```

#### Communication Errors
```bash
# Check UART configuration
cat /proc/serial
# Verify baud rate and flow control settings
```

#### Discovery Failures
```bash
# Reset controller
echo "reset" > /dev/bluetooth
# Restart discovery
echo "start" > /dev/bluetooth
```

### Performance Tuning
- **Buffer Sizes**: Increase buffer sizes for high-throughput scenarios
- **Inquiry Parameters**: Adjust inquiry length and response count
- **Event Processing**: Optimize event handler for specific use cases

## Integration with IR0 Kernel

The Bluetooth subsystem demonstrates several key aspects of IR0's architecture:

1. **Multi-language Support**: Core implementation in C with potential for C++/Rust extensions
2. **Unified Driver Model**: Seamless integration with kernel driver registry
3. **VFS Integration**: Standard Unix device model through `/dev/bluetooth`
4. **Memory Management**: Proper use of kernel heap allocator with error handling
5. **Interrupt Handling**: Integration with serial interrupt system
6. **Modular Design**: Clean separation between transport, protocol, and interface layers

This implementation showcases IR0's capability to support complex, real-world hardware subsystems while maintaining the kernel's architectural principles and design goals.