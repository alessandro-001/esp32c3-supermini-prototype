# SCD40 Arduino Library

Professional Arduino/C++ library for the Sensirion **SCD40** CO2 sensor. It reads **CO2**, **temperature**, and **relative humidity** using CRC-validated I2C communication and the SCD40's 5-second periodic measurement mode.

## Features

- Fixed SCD40 I2C address: `0x62`
- CRC-8 validation on every 16-bit word read from the sensor
- CRC-8 generation on every 16-bit word written to the sensor
- Periodic measurement mode with 5-second read interval
- Data-ready polling
- Temperature offset configuration
- Automatic self-calibration (ASC) enable/disable
- Forced recalibration (FRC)
- EEPROM persistence for settings
- Serial number readback for sensor presence checking
- Error reporting via `getLastError()`

## Hardware wiring

| SCD40 pin | Arduino / MCU |
|---|---|
| VDD | 3.3 V or 5 V |
| VDDH | Same supply as VDD |
| GND | GND |
| SDA | SDA |
| SCL | SCL |

Use pull-up resistors on SDA and SCL. **10 kΩ** is a good starting value for many short-bus designs, but final values should consider bus capacitance and clock speed.

## Quick start

```cpp
#include <Wire.h>
#include <SCD40.h>

SCD40 scd40;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(400000);

  if (!scd40.begin()) {
    Serial.println("SCD40 not found");
    while (1) delay(1000);
  }

  scd40.startMeasurement();
}

void loop() {
  delay(SCD40::MEASUREMENT_INTERVAL_MS);

  float co2, temperature, humidity;
  if (scd40.isDataReady() && scd40.readMeasurement(co2, temperature, humidity)) {
    Serial.print("CO2: "); Serial.print(co2, 0); Serial.print(" ppm, ");
    Serial.print("T: "); Serial.print(temperature, 2); Serial.print(" C, ");
    Serial.print("RH: "); Serial.print(humidity, 2); Serial.println(" %");
  } else {
    Serial.print("SCD40 error: ");
    Serial.println(scd40.getLastError());
  }
}
```

## Measurement notes

The SCD40 periodic measurement signal update interval is **5 seconds**. Do not read faster than this interval. Use `isDataReady()` before calling `readMeasurement()` to avoid reading when the sensor has no new sample available.

`readMeasurement()` converts raw words as:

```cpp
CO2_ppm = word0;
Temperature_C = -45 + 175 * word1 / 65535;
Humidity_RH = 100 * word2 / 65535;
```

This library flags measurements outside the SCD40 specified accuracy envelope with `ERROR_MEASUREMENT_OUT_OF_RANGE`. The CO2 output range of the device is wider than the SCD40 specified accuracy range, but for maximum accuracy this library treats 400-2000 ppm as the expected SCD40 range.

## Temperature offset

Temperature offset improves the temperature and humidity output after the sensor is integrated into the final enclosure. It does **not** improve CO2 accuracy directly.

Recommended workflow:

1. Install the SCD40 in the final enclosure.
2. Run it in the same measurement mode used by the application.
3. Wait until the device reaches thermal equilibrium.
4. Measure the real ambient temperature with a reference thermometer.
5. Read the current offset with `getTemperatureOffset()`.
6. Calculate:

```cpp
T_offset_actual = T_sensor - T_reference + T_offset_previous;
```

7. Set it with `setTemperatureOffset(offset)`.
8. Persist it with `saveSettings()` only after you are sure the value is correct.

Recommended offset range: **0-20 °C**.

## ASC vs FRC

### Automatic self-calibration (ASC)

ASC is enabled by default and is recommended for normal indoor-air applications when the sensor is exposed to fresh air around 400 ppm for at least 3 minutes weekly. Keep ASC enabled when that condition is true:

```cpp
scd40.enableASC(true);
scd40.saveSettings();
```

### Forced recalibration (FRC)

Use FRC only when you have a known stable CO2 reference concentration and ASC is not suitable. Procedure:

1. Run the sensor in normal measurement mode for at least 3 minutes in a stable, homogeneous CO2 environment.
2. Stop measurement with `stopMeasurement()`.
3. Run `performForcedCalibration(referencePpm)`.
4. Restart periodic measurement.

## Error codes

| Code | Name | Meaning |
|---:|---|---|
| 0 | `ERROR_NONE` | No error |
| 1 | `ERROR_I2C_WRITE` | I2C write or ACK failure |
| 2 | `ERROR_I2C_READ` | I2C read length failure |
| 3 | `ERROR_CRC` | CRC mismatch; data rejected |
| 4 | `ERROR_NOT_READY` | No new sample ready |
| 5 | `ERROR_INVALID_PARAMETER` | Invalid function input |
| 6 | `ERROR_MEASUREMENT_OUT_OF_RANGE` | Measurement outside configured accuracy envelope |
| 7 | `ERROR_FRC_FAILED` | Sensor returned FRC failure value |

## Included examples

- `BasicReading` - simple CO2, temperature, and humidity output
- `WithCalibration` - temperature offset calculation and persistence
- `ForcedCalibration` - manual forced recalibration flow

## Important implementation details

- The sensor is idle after power-up; `begin()` waits 30 ms and verifies presence with `getSerialNumber()`.
- `stopMeasurement()` waits 500 ms before returning.
- `saveSettings()` waits 800 ms. Use it sparingly because EEPROM write endurance is finite.
- CRC-8 polynomial: `0x31`; initialization: `0xFF`.
- All commands and words are sent MSB first.
