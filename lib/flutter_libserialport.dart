export 'package:libserialport/libserialport.dart';

import 'package:flutter/services.dart';
import 'package:libserialport/libserialport.dart';

/// USB Permission Manager
/// Handles USB device permission requests for Android platform
class UsbPermissionManager {
  static const MethodChannel _channel = MethodChannel('flutter_libserialport');

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

  /// Check if device has USB host support
  static Future<bool> hasUsbHostSupport() async {
    try {
      final result = await _channel.invokeMethod('hasUsbHostSupport');
      return result == true;
    } on PlatformException catch (e) {
      print('Failed to check USB host support: ${e.message}');
      return false;
    }
  }
}
