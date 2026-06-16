# BlePressureSensor

Arduino / PlatformIO library for reading BLE pressure sensors over NimBLE on ESP32.

Features:

- Connect to a sensor by MAC address and read a custom GATT pressure characteristic
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
  Serial.printf("Pressure: %.2f psi (raw %u)\n", reading.psi, reading.raw);
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

You can call `BlePressureSensor::rawToPsi()` directly for unit tests without BLE hardware.

## License

MIT
