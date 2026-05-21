#ifndef SCD40_H
#define SCD40_H

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

class SCD40 {
public:
    static const uint8_t DEFAULT_I2C_ADDRESS = 0x62;

    static const uint16_t CMD_START_PERIODIC_MEASUREMENT = 0x21B1;
    static const uint16_t CMD_READ_MEASUREMENT = 0xEC05;
    static const uint16_t CMD_STOP_PERIODIC_MEASUREMENT = 0x3F86;
    static const uint16_t CMD_GET_DATA_READY_STATUS = 0xE4B8;
    static const uint16_t CMD_SET_TEMPERATURE_OFFSET = 0x241D;
    static const uint16_t CMD_GET_TEMPERATURE_OFFSET = 0x2318;
    static const uint16_t CMD_SET_ASC_ENABLED = 0x2416;
    static const uint16_t CMD_GET_ASC_ENABLED = 0x2313;
    static const uint16_t CMD_PERFORM_FORCED_RECALIBRATION = 0x362F;
    static const uint16_t CMD_PERSIST_SETTINGS = 0x3615;
    static const uint16_t CMD_GET_SERIAL_NUMBER = 0x3682;
    static const uint16_t CMD_REINIT = 0x3646;

    static const uint16_t POWER_UP_DELAY_MS = 30;
    static const uint16_t STOP_MEASURE_DELAY_MS = 500;
    static const uint16_t FRC_DURATION_MS = 400;
    static const uint16_t COMMAND_DELAY_MS = 1;
    static const uint16_t PERSIST_SETTINGS_DELAY_MS = 800;
    static const uint16_t MEASUREMENT_INTERVAL_MS = 5000;

    enum Error : uint8_t {
        ERROR_NONE = 0,
        ERROR_I2C_WRITE = 1,
        ERROR_I2C_READ = 2,
        ERROR_CRC = 3,
        ERROR_NOT_READY = 4,
        ERROR_INVALID_PARAMETER = 5,
        ERROR_MEASUREMENT_OUT_OF_RANGE = 6,
        ERROR_FRC_FAILED = 7
    };

    explicit SCD40(TwoWire &wire = Wire, uint8_t address = DEFAULT_I2C_ADDRESS);

    // Basic operations
    bool begin();
    bool startMeasurement();
    bool stopMeasurement();

    // Read data
    bool readMeasurement(float &co2, float &temp, float &humidity);
    bool isDataReady();

    // Calibration
    bool setTemperatureOffset(float offset_degC);
    float getTemperatureOffset();
    bool enableASC(bool enable);
    bool performForcedCalibration(uint16_t target_co2_ppm);

    // Settings
    bool saveSettings();

    // Diagnostics
    uint64_t getSerialNumber();
    uint8_t getLastError();

    // Helpers
    static uint8_t calculateCRC8(const uint8_t *data, uint16_t count);
    bool getASCEnabled(bool &enabled);
    int16_t getLastFRCCorrection();

private:
    TwoWire *_wire;
    uint8_t _address;
    uint8_t _lastError;
    int16_t _lastFRCCorrection;

    void clearError();
    void setError(uint8_t error);
    bool writeCommand(uint16_t command);
    bool writeCommandWithWord(uint16_t command, uint16_t value);
    bool readWords(uint16_t command, uint16_t *words, uint8_t wordCount, uint16_t delayMs);
    bool readRaw(uint8_t *buffer, uint8_t byteCount);
    bool validateWordCRC(uint8_t msb, uint8_t lsb, uint8_t crc);
};

#endif
