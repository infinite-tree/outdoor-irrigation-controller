# BlePressureSensor

Arduino / PlatformIO library for reading BLE pressure sensors over NimBLE on ESP32.

Designed for [CirrusSense TDWLB-LC Low Cost Wireless Pressure Transducers](https://transducersdirect.com/products/pressure-transducers/wireless-pressure-transducers/cirrussense-tdwlb-lc-low-cost-wireless-pressure-transducer/). Tested with the **TDWLB-LC0250034** (0–250 psi range).

Features:

- Connect to a sensor by MAC address and read a custom GATT pressure characteristic
- Per-read retries and forced disconnect between attempts
- `resetStack()` to deinit/reinit NimBLE after repeated failures
- CirrusSense TDWLB-LC: signed int16 **big-endian**, `psi = raw / 10` (0xFFF4 → −1.2 psi vacuum). Negative values are not clamped; a drifted zero is reported as-is.
- Interpret pressure as a 16-bit little-endian raw value with configurable linear scaling
- Read battery level from the standard Bluetooth SIG Battery Service (`0x180F`) and Battery Level characteristic (`0x2A19`)

## Dependencies

- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino)

## Install

### PlatformIO

Copy this folder into your project's `lib/` directory and add NimBLE-Arduino to the project `lib_deps`:

```ini
lib_deps =
  h2zero/NimBLE-Arduino@^2.1.0
```

### Arduino IDE

Copy the `BlePressureSensor` folder into your sketchbook `libraries` directory.

## Usage

```cpp
#include <BlePressureSensor.h>

BlePressureSensor sensor;

void setup() {
  BlePressureConfig config;
  config.device_id = "AA:BB:CC:DD:EE:FF";
  config.service_uuid = "0000xxxx-0000-1000-8000-00805f9b34fb";
  config.characteristic_uuid = "0000yyyy-0000-1000-8000-00805f9b34fb";
  config.raw_floor = 2000;
  config.raw_ref_low = 19000;
  config.psi_ref_low = 34.0f;
  config.raw_ref_high = 26000;
  config.psi_ref_high = 37.0f;

  sensor.begin(config);
}

void loop() {
  BlePressureReading reading = sensor.read();
  if (reading.ok) {
    Serial.printf("Pressure: %d psi (raw %u)\n", (int)reading.psi, reading.raw);
    if (reading.battery_valid) {
      Serial.printf("Battery: %d%%\n", reading.battery_pct);
    }
  } else {
    Serial.println(reading.error);
  }
  delay(5000);
}
```

## Scaling

Raw values below `raw_floor` report `0` psi. Between the two reference points:

`psi = psi_ref_low + (raw - raw_ref_low) * (psi_ref_high - psi_ref_low) / (raw_ref_high - raw_ref_low)`

CirrusSense tenths mode (`scale_tenths`) is linear through vacuum: `psi = (int16)raw / raw_tenths_divisor`. The high clamp (`psi_max`) still applies; the low end is not clamped so zero-drift below 0 psi remains visible.

You can call `BlePressureSensor::rawToPsi()` directly for unit tests without BLE hardware.

## License

MIT
