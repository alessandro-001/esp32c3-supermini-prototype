#include <Wire.h>
#include <SCD40.h>

SCD40 scd40;

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  Wire.begin();
  Wire.setClock(400000);  // SCD40 supports standard and fast-mode I2C.

  Serial.println("SCD40 Basic Reading Example");
  Serial.println("Use 10k pull-up resistors on SDA/SCL if your board does not already include pull-ups.");

  if (!scd40.begin()) {
    Serial.print("SCD40 not found. Error: ");
    Serial.println(scd40.getLastError());
    while (1) delay(1000);
  }

  Serial.print("Serial number: 0x");
  Serial.println((uint32_t)(scd40.getSerialNumber() & 0xFFFFFFFF), HEX);

  // ASC is enabled by default. Keep it enabled for long-term accuracy when the
  // sensor is exposed to fresh air around 400 ppm for at least 3 minutes weekly.
  if (!scd40.startMeasurement()) {
    Serial.print("Could not start measurement. Error: ");
    Serial.println(scd40.getLastError());
    while (1) delay(1000);
  }
}

void loop() {
  static unsigned long lastRead = 0;

  if (millis() - lastRead < SCD40::MEASUREMENT_INTERVAL_MS) {
    return;
  }
  lastRead = millis();

  if (!scd40.isDataReady()) {
    Serial.print("Data not ready or communication error. Error: ");
    Serial.println(scd40.getLastError());
    return;
  }

  float co2, temperature, humidity;
  if (scd40.readMeasurement(co2, temperature, humidity)) {
    Serial.print("CO2: ");
    Serial.print(co2, 0);
    Serial.print(" ppm, Temperature: ");
    Serial.print(temperature, 2);
    Serial.print(" °C, Humidity: ");
    Serial.print(humidity, 2);
    Serial.println(" %RH");
  } else {
    Serial.print("Read failed or value outside SCD40 specified accuracy range. Error: ");
    Serial.println(scd40.getLastError());
  }
}
