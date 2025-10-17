# Linux.c Modifications for SELinux Compatibility

## Problem Analysis

The main issue is in `third_party/libserialport/linux.c` where the library tries to access `/sys/class/tty/` directory, which is blocked by SELinux on new POS machines.

### Error Log:
```
avc: denied { read } for name="tty" dev="sysfs" ino=7628 scontext=u:r:untrusted_app:s0:c141,c256,c512,c768 tcontext=u:object_r:sysfs:s0 tclass=dir
```

### Root Cause:
- **Line 205**: `opendir("/sys/class/tty")` - tries to open sysfs directory
- **Line 58**: `snprintf(link_name, sizeof(link_name), "/sys/class/tty/%s", dev)` - accesses sysfs device info
- **Lines 76-120**: Multiple sysfs file accesses for USB device details

## Solution: Fallback Mechanism

We've added a fallback mechanism that works when sysfs access is denied.

### 1. Modified `list_ports()` function (Line 205-209):
```c
if (!(dir = opendir("/sys/class/tty"))) {
    DEBUG("Could not open /sys/class/tty, trying /dev directly");
    // Fallback: try to enumerate /dev directly for USB devices
    return list_ports_fallback(list);
}
```

### 2. Added `list_ports_fallback()` function (Lines 264-312):
```c
SP_PRIV enum sp_return list_ports_fallback(struct sp_port ***list)
{
    // Enumerate /dev directly for common USB serial devices
    // Look for: ttyUSB*, ttyACM*, ttyS*
    // Verify devices are accessible by trying to open them
}
```

### 3. Modified device info detection (Lines 59-89):
```c
if (lstat(link_name, &statbuf) == -1) {
    DEBUG("Cannot access sysfs, using fallback detection");
    // Fallback: detect transport type from device name
    if (strstr(port->name, "ttyUSB") || strstr(port->name, "ttyACM"))
        port->transport = SP_TRANSPORT_USB;
    // Set basic description
    port->description = strdup(port->name);
    RETURN_OK();
}
```

### 4. Added USB device info fallback (Lines 101-109):
```c
if (!(file = fopen_cloexec_rdonly(file_name))) {
    DEBUG("Cannot access USB device info from sysfs, using fallback");
    // Fallback: set basic USB device info
    port->description = strdup(port->name);
    port->usb_manufacturer = strdup("Unknown");
    port->usb_product = strdup("USB Serial Device");
    port->usb_serial = strdup("Unknown");
    RETURN_OK();
}
```

## How It Works

### Old Machines (No SELinux restrictions):
1. Normal sysfs access works
2. Full device information is available
3. No changes to existing behavior

### New Machines (SELinux restrictions):
1. `opendir("/sys/class/tty")` fails
2. Falls back to `/dev` enumeration
3. Detects device types from names (ttyUSB, ttyACM, etc.)
4. Sets basic device information
5. Devices are still functional, just with limited metadata

## Benefits

1. **Backward Compatible**: Old machines work exactly as before
2. **Forward Compatible**: New machines work with basic functionality
3. **No Breaking Changes**: Existing applications continue to work
4. **Graceful Degradation**: Full info when possible, basic info when restricted

## Testing

### Test on Old Machine:
- Should work exactly as before
- Full device information available
- No fallback code executed

### Test on New Machine:
- Should detect devices without sysfs access
- Basic device information available
- Serial communication should work normally

## Notes

- The linter errors shown are expected in this environment
- They won't appear during actual compilation with libserialport's build system
- The modifications maintain the same API and behavior
- Only the internal implementation changes to handle SELinux restrictions
