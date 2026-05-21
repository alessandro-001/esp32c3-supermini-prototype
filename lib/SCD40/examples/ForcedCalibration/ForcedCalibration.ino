#include <Wire.h>
#include <SCD40.h>

SCD40 scd40;

// Set this to the known stable CO2 reference concentration in ppm.
// For outdoor fresh air, use a calibrated reference if possible rather than assuming exactly 400 ppm.
const uint16_t KNOWN_CO2_REFERENCE_PPM = 420;

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  Wire.begin();
  Wire.setClock(400000);

  Serial.println("SCD40 Forced Recalibration Example");
  Serial.println("Place the sensor in a stable, homogeneous CO2 environment first.");

  if (!scd40.begin()) {
    Serial.print("SCD40 not found. Error: ");
    Serial.println(scd40.getLastError());
    while (1) delay(1000);
  }

  // Run in the normal operation mode for at least 3 minutes before FRC.
  Serial.println("Warming/running sensor for 3 minutes before FRC...");
  scd40.startMeasurement();
  delay(180000UL);

  if (!scd40.stopMeasurement()) {
    Serial.print("Could not stop measurement. Error: ");
    Serial.println(scd40.getLastError());
    while (1) delay(1000);
  }

  Serial.print("Performing FRC with target ");
  Serial.print(KNOWN_CO2_REFERENCE_PPM);
  Serial.println(" ppm...");

  if (scd40.performForcedCalibration(KNOWN_CO2_REFERENCE_PPM)) {
    Serial.print("FRC succeeded. Correction: ");
    Serial.print(scd40.getLastFRCCorrection());
    Serial.println(" ppm");
  } else {
    Serial.print("FRC failed. Error: ");
    Serial.println(scd40.getLastError());
  }

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
