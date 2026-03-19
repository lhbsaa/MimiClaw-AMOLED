# Troubleshooting Guide

This guide helps resolve common issues with MimiClaw-AMOLED.

## Display Issues

### Screen Remains Blank After Boot

**Symptoms**: No display output, screen stays black

**Possible Causes & Solutions**:

1. **Wrong USB Port**
   - T-Display-S3 has two USB-C ports
   - Use the **USB** port (native USB Serial/JTAG), not COM port
   - Try the other USB-C port

2. **Display Initialization Failed**
   - Check serial output for "Failed to init display" message
   - Verify GPIO connections (5, 6, 7, 9, 17, 18, 47, 48)

3. **Backlight Off**
   - GPIO 38 controls backlight
   - Check if backlight PWM is configured

4. **Display Sleep Mode**
   - The display may be in sleep mode
   - Send wake command (0x11) to RM67162

### Display Shows Garbled Output

**Symptoms**: Random pixels, incorrect colors, shifted image

**Possible Causes & Solutions**:

1. **SPI Clock Too High**
   - Reduce SPI clock speed in `display_manager.c`
   - Change from 40MHz to 20MHz or lower

2. **Frame Buffer Corruption**
   - Check PSRAM integrity
   - Verify frame buffer allocation succeeded

3. **Wrong Color Format**
   - Ensure RGB565 format is used consistently
   - Check color endianness

### "txdata transfer > hardware max supported len" Error

**Cause**: SPI transfer size exceeds hardware limit

**Solution**: Already fixed in current version with chunked transfers (16,384 pixels max per transfer)

### Display Flickering or Tearing

**Causes & Solutions**:

1. **No VSync**
   - RM67162 doesn't expose VSync signal
   - Consider implementing double buffering for smooth animations

2. **Slow Frame Updates**
   - Optimize frame buffer drawing
   - Reduce number of `gui_flush()` calls

## WiFi Issues

### WiFi Connection Timeout

**Symptoms**: "WiFi connection timeout" message

**Solutions**:

1. **Check Credentials**
   ```
   mimi> set_wifi <ssid> <password>
   mimi> wifi_status
   ```

2. **Signal Strength**
   - Move closer to router
   - Check for interference
   ```
   mimi> wifi_scan
   ```

3. **Proxy Issues**
   - Clear proxy if not needed
   ```
   mimi> clear_proxy
   ```

### WiFi Disconnects Frequently

**Solutions**:
- Check power supply stability
- Reduce WiFi transmit power in sdkconfig
- Check for antenna issues

## Memory Issues

### Heap Exhaustion

**Symptoms**: Crashes, "Failed to alloc" messages

**Diagnostic Commands**:
```
mimi> heap_info
```

**Solutions**:

1. **Check PSRAM**
   - Ensure PSRAM is enabled: `CONFIG_SPIRAM=y`
   - Frame buffer should use PSRAM

2. **Reduce Buffer Sizes**
   - Lower display resolution if needed
   - Reduce task stack sizes

3. **Memory Leaks**
   - Check for unfreed allocations
   - Review task stack usage

### Frame Buffer Allocation Failed

**Error Message**: "Failed to alloc framebuffer (257280 bytes)"

**Solutions**:
1. Verify PSRAM is working
2. Check sdkconfig for `CONFIG_SPIRAM=y`
3. Reduce frame buffer size (not recommended)

## Telegram Issues

### Bot Not Responding

**Diagnostic Steps**:

1. **Check Token**
   ```
   mimi> set_tg_token <your_token>
   mimi> config_show
   ```

2. **Check Network**
   - Ensure WiFi is connected
   - Test with proxy if in restricted region

3. **Check Logs**
   - Monitor serial output for Telegram errors
   - Look for "TG" status in display

### Messages Not Displaying on Screen

**Causes**:
- Wrong page selected
- Message handling not triggered

**Solutions**:
- Press Boot button to navigate to Message page
- Check `ui_add_message()` is being called

## Battery Issues

### Battery Percentage Shows 0%

**Causes**:

1. **ADC Not Initialized**
   - Check serial output for "Battery ADC initialized"
   - Verify GPIO 4 is connected

2. **Voltage Too Low**
   - Actual battery voltage below 3.3V
   - Charge the battery

3. **Voltage Divider Issue**
   - Verify 2:1 divider ratio
   - Check ADC calibration

### Battery Percentage Incorrect

**Solutions**:

1. **Calibrate Voltage Range**
   - Adjust `MIMI_BATT_MV_EMPTY` and `MIMI_BATT_MV_FULL` in `mimi_config.h`
   - Measure actual battery voltage and compare

2. **Non-Linear Discharge**
   - Current implementation uses linear mapping
   - Consider implementing lookup table for accurate readings

## Build Issues

### Compilation Errors

**Common Errors**:

1. **"Cannot find esp_adc"**
   - Add `esp_adc` to REQUIRES in CMakeLists.txt

2. **"PSRAM not found"**
   - Check sdkconfig for `CONFIG_SPIRAM=y`
   - Verify PSRAM configuration matches hardware

3. **"sdkconfig out of date"**
   ```
   idf.py reconfigure
   ```

### Flash Errors

**Solutions**:

1. **"Failed to connect"**
   - Hold BOOT button while connecting
   - Try lower baud rate: `idf.py -p PORT -b 115200 flash`

2. **"Writing at 0x0" fails**
   - Erase flash completely: `idf.py erase-flash`
   - Rebuild and flash

## CLI Issues

### CLI Not Responding

**Causes**:

1. **Wrong Port**
   - Connect to UART (COM) port, not USB (JTAG)
   - Check device manager for correct port

2. **Wrong Baud Rate**
   - Use 115200 baud

3. **Line Ending**
   - Ensure terminal sends CR+LF or LF

### Commands Not Recognized

**Solutions**:
- Type `help` to see available commands
- Check command syntax (use underscore, not space for some commands)

## Performance Issues

### Slow Response Time

**Causes**:

1. **Network Latency**
   - Check WiFi signal
   - Consider using proxy

2. **LLM API Delay**
   - Normal for cloud LLM services
   - Consider using local Ollama for faster response

3. **Display Rendering**
   - Full screen updates are slow
   - Optimize by updating only changed regions

### System Freezes

**Diagnostic Steps**:

1. Enable watchdog timer
2. Check for infinite loops
3. Monitor stack usage with `heap_info`

## Debug Tips

### Enable Verbose Logging

In `menuconfig`:
```
Component config → Log output → Default log verbosity → Debug
```

### Serial Monitor

```bash
# Windows
idf.py -p COMx monitor

# Linux/Mac
idf.py -p /dev/ttyUSB0 monitor
```

### Memory Debug

```c
ESP_LOGI(TAG, "Free heap: %d", esp_get_free_heap_size());
ESP_LOGI(TAG, "Free PSRAM: %d", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
```

## Getting Help

1. Check [GitHub Issues](https://github.com/lhbsaa/MimiClaw-AMOLED/issues)
2. Review serial output logs
3. Consult [Hardware Reference](HARDWARE.md) and [Display Driver](DISPLAY_DRIVER.md) docs
