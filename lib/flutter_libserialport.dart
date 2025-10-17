export 'package:libserialport/libserialport.dart';

import 'package:flutter/services.dart';
import 'package:libserialport/libserialport.dart';

/// USB Permission Manager
/// Handles USB device permission requests and automatic detection on Android platform
class UsbPermissionManager {
  static const MethodChannel _channel = MethodChannel('flutter_libserialport');
  
  // Cache detection results to avoid repeated detection
  static bool? _isNewMachine;
  static bool? _hasUsbHostSupport;

  /// Automatically detect machine type and permission requirements
  static Future<MachineType> detectMachineType() async {
    if (_isNewMachine != null) {
      return _isNewMachine! ? MachineType.newMachine : MachineType.oldMachine;
    }

    try {
      // 1. First try to enumerate ports directly (old machine way)
      List<String> ports = SerialPort.availablePorts;
      
      // 2. Check if there are USB devices
      bool hasUsbDevices = ports.any((port) => 
          port.startsWith('/dev/ttyUSB') || port.startsWith('/dev/ttyACM'));
      
      if (!hasUsbDevices) {
        _isNewMachine = false;
        return MachineType.oldMachine;
      }

      // 3. Try to get USB device details (this will trigger sysfs access)
      bool canAccessSysfs = true;
      for (String port in ports) {
        if (port.startsWith('/dev/ttyUSB') || port.startsWith('/dev/ttyACM')) {
          try {
            SerialPort serialPort = SerialPort(port);
            // Try to get device information, this will access sysfs
            String? description = serialPort.description;
            String? manufacturer = serialPort.manufacturer;
            // If we can get information, it means we can access sysfs
            if (description != null || manufacturer != null) {
              canAccessSysfs = true;
              break;
            }
          } catch (e) {
            // If permission error occurs, it means it's a new machine
            if (e.toString().contains('Permission denied') || 
                e.toString().contains('sysfs') ||
                e.toString().contains('avc: denied')) {
              canAccessSysfs = false;
              break;
            }
          }
        }
      }

      _isNewMachine = !canAccessSysfs;
      return _isNewMachine! ? MachineType.newMachine : MachineType.oldMachine;
      
    } catch (e) {
      print('Failed to detect machine type: $e');
      // Default to new machine, requires USB permission
      _isNewMachine = true;
      return MachineType.newMachine;
    }
  }

  /// Smart handle USB device permissions
  static Future<bool> smartHandleUsbPermission(String deviceName) async {
    try {
      // 1. Detect machine type
      MachineType machineType = await detectMachineType();
      
      if (machineType == MachineType.oldMachine) {
        print('Detected old machine, no USB permission needed');
        return true;
      }

      // 2. New machine needs USB permission
      print('Detected new machine, USB permission required');
      
      // Check if permission already exists
      bool hasPermission = await hasUsbPermission(deviceName);
      if (hasPermission) {
        return true;
      }

      // Request permission
      return await requestUsbPermission(deviceName);
      
    } catch (e) {
      print('Failed to smart handle USB permission: $e');
      return false;
    }
  }

  /// Request USB device permission
  static Future<bool> requestUsbPermission(String deviceName) async {
    try {
      final result = await _channel.invokeMethod('requestUsbPermission', {
        'deviceName': deviceName,
      });
      return result == true;
    } on PlatformException catch (e) {
      print('USB permission request failed: ${e.message}');
      return false;
    }
  }

  /// Check if USB device permission already exists
  static Future<bool> hasUsbPermission(String deviceName) async {
    try {
      final result = await _channel.invokeMethod('hasUsbPermission', {
        'deviceName': deviceName,
      });
      return result == true;
    } on PlatformException catch (e) {
      print('Failed to check USB permission: ${e.message}');
      return false;
    }
  }

  /// Reset detection cache (for testing or forced re-detection)
  static void resetDetectionCache() {
    _isNewMachine = null;
    _hasUsbHostSupport = null;
  }
}

/// Machine type enumeration
enum MachineType {
  /// Old machine - can directly access sysfs, no USB permission needed
  oldMachine,
  /// New machine - requires USB permission to access USB devices
  newMachine,
}

/// Smart Serial Manager
/// Automatically handles compatibility between old and new machines, no need to manually determine machine type
class SmartSerialManager {
  /// Get available serial port list (automatically handles permissions)
  static Future<List<String>> getAvailablePorts() async {
    try {
      // 1. First try to get port list directly
      List<String> ports = SerialPort.availablePorts;
      
      // 2. Detect machine type
      MachineType machineType = await UsbPermissionManager.detectMachineType();
      
      if (machineType == MachineType.oldMachine) {
        print('Old machine mode: returning port list directly');
        return ports;
      }

      // 3. New machine mode: filter out USB devices with permissions
      List<String> accessiblePorts = [];
      for (String port in ports) {
        if (port.startsWith('/dev/ttyUSB') || port.startsWith('/dev/ttyACM')) {
          // USB devices need permission check
          bool hasPermission = await UsbPermissionManager.hasUsbPermission(port);
          if (hasPermission) {
            accessiblePorts.add(port);
          } else {
            print('USB device $port has no permission, skipping');
          }
        } else {
          // Non-USB devices add directly
          accessiblePorts.add(port);
        }
      }
      
      print('New machine mode: returning port list with permissions');
      return accessiblePorts;
      
    } catch (e) {
      print('Failed to get available ports: $e');
      return [];
    }
  }

  /// Smart open serial port (automatically handles permissions)
  static Future<SerialPort?> smartOpenPort(String portName, {SerialPortConfig? config}) async {
    try {
      // 1. Check if it's a USB device
      bool isUsbDevice = portName.startsWith('/dev/ttyUSB') || portName.startsWith('/dev/ttyACM');
      
      if (isUsbDevice) {
        // 2. USB devices need smart permission handling
        bool hasPermission = await UsbPermissionManager.smartHandleUsbPermission(portName);
        if (!hasPermission) {
          print('Unable to get USB device permission: $portName');
          return null;
        }
      }

      // 3. Create and open serial port
      SerialPort serialPort = SerialPort(portName);
      bool success = serialPort.openReadWrite();
      
      if (!success) {
        print('Failed to open serial port: $portName, error: ${SerialPort.lastError}');
        return null;
      }

      // 4. Apply configuration
      if (config != null) {
        serialPort.config = config;
      }

      print('Successfully opened serial port: $portName');
      return serialPort;
      
    } catch (e) {
      print('Failed to smart open serial port: $e');
      return null;
    }
  }

  /// Smart close serial port
  static void smartClosePort(SerialPort? serialPort) {
    if (serialPort != null) {
      try {
        serialPort.close();
        print('Serial port closed: ${serialPort.name}');
      } catch (e) {
        print('Failed to close serial port: $e');
      }
    }
  }
}
