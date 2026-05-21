#include <Wire.h>
#include <SCD40.h>

SCD40 scd40;

// Determine this after the sensor is installed in its final enclosure and has
// reached thermal equilibrium.
const float REFERENCE_TEMPERATURE_C = 24.0;

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  Wire.begin();
  Wire.setClock(400000);

  Serial.println("SCD40 Temperature Offset Example");

  if (!scd40.begin()) {
    Serial.print("SCD40 not found. Error: ");
    Serial.println(scd40.getLastError());
    while (1) delay(1000);
  }

  // Settings must be changed while the sensor is idle.
  float previousOffset = scd40.getTemperatureOffset();
  if (isnan(previousOffset)) {
    Serial.print("Could not read previous offset. Error: ");
    Serial.println(scd40.getLastError());
    while (1) delay(1000);
  }

  Serial.print("Previous temperature offset: ");
  Serial.print(previousOffset, 2);
  Serial.println(" °C");

  // Start a temporary measurement to observe the sensor's reported temperature.
  scd40.startMeasurement();
  delay(SCD40::MEASUREMENT_INTERVAL_MS);

  float co2, sensorTemperature, humidity;
  if (scd40.isDataReady() && scd40.readMeasurement(co2, sensorTemperature, humidity)) {
    Serial.print("Sensor temperature before correction: ");
    Serial.print(sensorTemperature, 2);
    Serial.println(" °C");
  } else {
    Serial.println("Could not read temporary measurement; using example offset calculation anyway.");
    sensorTemperature = REFERENCE_TEMPERATURE_C + previousOffset;
  }

  scd40.stopMeasurement();

  // Datasheet formula:
  // T_offset_actual = T_sensor - T_reference + T_offset_previous
  float newOffset = sensorTemperature - REFERENCE_TEMPERATURE_C + previousOffset;

  Serial.print("Calculated new offset: ");
  Serial.print(newOffset, 2);
  Serial.println(" °C");

  if (!scd40.setTemperatureOffset(newOffset)) {
    Serial.print("Could not set temperature offset. Error: ");
    Serial.println(scd40.getLastError());
    while (1) delay(1000);
  }

  // Persist only when the value is final. EEPROM write endurance is limited.
  if (!scd40.saveSettings()) {
    Serial.print("Could not persist settings. Error: ");
    Serial.println(scd40.getLastError());
    while (1) delay(1000);
  }

  Serial.println("Temperature offset saved. Restarting periodic measurement.");
  scd40.startMeasurement();
}

void loop() {
  delay(SCD40::MEASUREMENT_INTERVAL_MS);

  float co2, temperature, humidity;
  if (scd40.isDataReady() && scd40.readMeasurement(co2, temperature, humidity)) {
    Serial.print("CO2: "); Serial.print(co2, 0); Serial.print(" ppm, ");
    Serial.print("T: "); Serial.print(temperature, 2); Serial.print(" °C, ");
    Serial.print("RH: "); Serial.print(humidity, 2); Serial.println(" %RH");
  }
}
