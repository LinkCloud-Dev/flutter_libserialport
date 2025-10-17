package org.sigrok.flutter_libserialport

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import androidx.annotation.NonNull
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.embedding.engine.plugins.activity.ActivityAware
import io.flutter.embedding.engine.plugins.activity.ActivityPluginBinding
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugin.common.MethodChannel.MethodCallHandler
import io.flutter.plugin.common.MethodChannel.Result
import io.flutter.plugin.common.PluginRegistry.Registrar

/** FlutterLibserialportPlugin - Enhanced with USB permission management */
class FlutterLibserialportPlugin: FlutterPlugin, MethodCallHandler, ActivityAware {
  /// The MethodChannel that will handle communication between Flutter and native Android
  ///
  /// This local reference serves to register the plugin with the Flutter Engine and unregister it
  /// when the Flutter Engine is detached from the Activity
  private lateinit var channel : MethodChannel
  private lateinit var context: Context
  private var activityBinding: ActivityPluginBinding? = null
  private var usbManager: UsbManager? = null
  private var pendingIntent: PendingIntent? = null
  private var permissionResult: Result? = null

  companion object {
    private const val ACTION_USB_PERMISSION = "org.sigrok.flutter_libserialport.USB_PERMISSION"
  }

  override fun onAttachedToEngine(@NonNull flutterPluginBinding: FlutterPlugin.FlutterPluginBinding) {
    channel = MethodChannel(flutterPluginBinding.binaryMessenger, "flutter_libserialport")
    channel.setMethodCallHandler(this)
    context = flutterPluginBinding.applicationContext
    usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
  }

  override fun onMethodCall(@NonNull call: MethodCall, @NonNull result: Result) {
    when (call.method) {
      "getPlatformVersion" -> {
        result.success("Android ${android.os.Build.VERSION.RELEASE}")
      }
      "requestUsbPermission" -> {
        requestUsbPermission(call, result)
      }
      "hasUsbPermission" -> {
        hasUsbPermission(call, result)
      }
      "hasUsbHostSupport" -> {
        hasUsbHostSupport(result)
      }
      else -> {
        result.notImplemented()
      }
    }
  }

  private fun requestUsbPermission(call: MethodCall, result: Result) {
    val deviceName = call.argument<String>("deviceName")
    if (deviceName == null) {
      result.error("INVALID_ARGUMENT", "Device name is required", null)
      return
    }

    // Find USB device by matching product name and manufacturer
    val device = usbManager?.deviceList?.values?.find { usbDevice ->
      usbDevice.productName == "FT232R USB UART" && usbDevice.manufacturerName == "FTDI"
    }
    
    if (device == null) {
      result.error("DEVICE_NOT_FOUND", "FT232R USB UART device not found", null)
      return
    }

    if (usbManager?.hasPermission(device) == true) {
      result.success(true)
      return
    }

    permissionResult = result
    pendingIntent = PendingIntent.getBroadcast(
      context, 0, Intent(ACTION_USB_PERMISSION), 
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )

    val filter = IntentFilter(ACTION_USB_PERMISSION)
    context.registerReceiver(usbReceiver, filter)
    
    usbManager?.requestPermission(device, pendingIntent)
  }

  private fun hasUsbPermission(call: MethodCall, result: Result) {
    val deviceName = call.argument<String>("deviceName")
    if (deviceName == null) {
      result.error("INVALID_ARGUMENT", "Device name is required", null)
      return
    }

    // Find USB device by matching product name and manufacturer
    val device = usbManager?.deviceList?.values?.find { usbDevice ->
      usbDevice.productName == "FT232R USB UART" && usbDevice.manufacturerName == "FTDI"
    }
    
    if (device == null) {
      result.success(false)
      return
    }

    result.success(usbManager?.hasPermission(device) == true)
  }

  private fun hasUsbHostSupport(result: Result) {
    try {
      val hasUsbHost = context.packageManager.hasSystemFeature(android.content.pm.PackageManager.FEATURE_USB_HOST)
      result.success(hasUsbHost)
    } catch (e: Exception) {
      result.error("USB_HOST_CHECK_FAILED", "Failed to check USB host support: ${e.message}", null)
    }
  }

  private val usbReceiver = object : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
      if (ACTION_USB_PERMISSION == intent.action) {
        synchronized(this) {
          val device = intent.getParcelableExtra<UsbDevice>(UsbManager.EXTRA_DEVICE)
          if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
            permissionResult?.success(true)
          } else {
            permissionResult?.error("PERMISSION_DENIED", "USB permission denied", null)
          }
          permissionResult = null
        }
      }
    }
  }

  override fun onDetachedFromEngine(@NonNull binding: FlutterPlugin.FlutterPluginBinding) {
    channel.setMethodCallHandler(null)
    try {
      context.unregisterReceiver(usbReceiver)
    } catch (e: Exception) {
      // Receiver might not be registered, ignore
    }
  }

  override fun onAttachedToActivity(binding: ActivityPluginBinding) {
    activityBinding = binding
  }

  override fun onDetachedFromActivityForConfigChanges() {
    activityBinding = null
  }

  override fun onReattachedToActivityForConfigChanges(binding: ActivityPluginBinding) {
    activityBinding = binding
  }

  override fun onDetachedFromActivity() {
    activityBinding = null
  }
}
