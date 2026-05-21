#include "SCD40.h"

SCD40::SCD40(TwoWire &wire, uint8_t address)
    : _wire(&wire), _address(address), _lastError(ERROR_NONE), _lastFRCCorrection(0) {}

bool SCD40::begin() {
    clearError();
    delay(POWER_UP_DELAY_MS);

    // The sensor may still be in periodic measurement mode after MCU reset.
    // get_serial_number is only valid in idle mode, so stop first.
    writeCommand(CMD_STOP_PERIODIC_MEASUREMENT);
    delay(STOP_MEASURE_DELAY_MS);

    // Ignore any stop error, because the sensor may already have been idle.
    clearError();

    uint64_t serial = getSerialNumber();
    return (_lastError == ERROR_NONE && serial != 0ULL);
}

bool SCD40::startMeasurement() {
    return writeCommand(CMD_START_PERIODIC_MEASUREMENT);
}

bool SCD40::stopMeasurement() {
    if (!writeCommand(CMD_STOP_PERIODIC_MEASUREMENT)) {
        return false;
    }
    delay(STOP_MEASURE_DELAY_MS);
    return true;
}

bool SCD40::readMeasurement(float &co2, float &temp, float &humidity) {
    uint16_t words[3] = {0, 0, 0};

    if (!readWords(CMD_READ_MEASUREMENT, words, 3, COMMAND_DELAY_MS)) {
        return false;
    }

    co2 = static_cast<float>(words[0]);
    temp = -45.0f + (175.0f * static_cast<float>(words[1]) / 65535.0f);
    humidity = 100.0f * static_cast<float>(words[2]) / 65535.0f;

    Serial.printf("[SCD40 LIB DEBUG] rawCO2=%u rawT=%u rawH=%u -> CO2=%.1f T=%.2f H=%.2f\n",
              words[0], words[1], words[2], co2, temp, humidity);

    // CRC has already been validated by readWords().
    // Do not reject valid sensor output here; let the application decide
    // whether a value is usable for its product logic.
    if (words[0] > 40000 || isnan(temp) || isnan(humidity)) {
        setError(ERROR_MEASUREMENT_OUT_OF_RANGE);
        return false;
    }

    clearError();
    return true;
}

bool SCD40::isDataReady() {
    uint16_t status = 0;
    if (!readWords(CMD_GET_DATA_READY_STATUS, &status, 1, COMMAND_DELAY_MS)) {
        return false;
    }

    bool ready = (status & 0x07FF) != 0;
    if (!ready) {
        setError(ERROR_NOT_READY);
    } else {
        clearError();
    }
    return ready;
}

bool SCD40::setTemperatureOffset(float offset_degC) {
    if (offset_degC < 0.0f || offset_degC > 20.0f || isnan(offset_degC)) {
        setError(ERROR_INVALID_PARAMETER);
        return false;
    }

    uint16_t raw = static_cast<uint16_t>((offset_degC * 65535.0f / 175.0f) + 0.5f);
    return writeCommandWithWord(CMD_SET_TEMPERATURE_OFFSET, raw);
}

float SCD40::getTemperatureOffset() {
    uint16_t raw = 0;
    if (!readWords(CMD_GET_TEMPERATURE_OFFSET, &raw, 1, COMMAND_DELAY_MS)) {
        return NAN;
    }

    clearError();
    return static_cast<float>(raw) * 175.0f / 65535.0f;
}

bool SCD40::enableASC(bool enable) {
    return writeCommandWithWord(CMD_SET_ASC_ENABLED, enable ? 1 : 0);
}

bool SCD40::getASCEnabled(bool &enabled) {
    uint16_t raw = 0;
    if (!readWords(CMD_GET_ASC_ENABLED, &raw, 1, COMMAND_DELAY_MS)) {
        return false;
    }

    enabled = (raw != 0);
    clearError();
    return true;
}

bool SCD40::performForcedCalibration(uint16_t target_co2_ppm) {
    if (target_co2_ppm < 400 || target_co2_ppm > 2000) {
        setError(ERROR_INVALID_PARAMETER);
        return false;
    }

    if (!writeCommandWithWord(CMD_PERFORM_FORCED_RECALIBRATION, target_co2_ppm)) {
        return false;
    }

    delay(FRC_DURATION_MS);

    uint8_t buffer[3] = {0, 0, 0};
    if (!readRaw(buffer, 3)) {
        return false;
    }

    if (!validateWordCRC(buffer[0], buffer[1], buffer[2])) {
        setError(ERROR_CRC);
        return false;
    }

    uint16_t raw = (static_cast<uint16_t>(buffer[0]) << 8) | buffer[1];
    if (raw == 0xFFFF) {
        setError(ERROR_FRC_FAILED);
        return false;
    }

    _lastFRCCorrection = static_cast<int16_t>(static_cast<int32_t>(raw) - 0x8000);
    clearError();
    return true;
}

bool SCD40::saveSettings() {
    if (!writeCommand(CMD_PERSIST_SETTINGS)) {
        return false;
    }
    delay(PERSIST_SETTINGS_DELAY_MS);
    return true;
}

uint64_t SCD40::getSerialNumber() {
    uint16_t words[3] = {0, 0, 0};
    if (!readWords(CMD_GET_SERIAL_NUMBER, words, 3, COMMAND_DELAY_MS)) {
        return 0ULL;
    }

    uint64_t serial = 0ULL;
    serial |= (static_cast<uint64_t>(words[0]) << 32);
    serial |= (static_cast<uint64_t>(words[1]) << 16);
    serial |= static_cast<uint64_t>(words[2]);
    clearError();
    return serial;
}

uint8_t SCD40::getLastError() {
    return _lastError;
}

int16_t SCD40::getLastFRCCorrection() {
    return _lastFRCCorrection;
}

uint8_t SCD40::calculateCRC8(const uint8_t *data, uint16_t count) {
    uint8_t crc = 0xFF;
    for (uint16_t i = 0; i < count; i++) {
        crc ^= data[i];
        for (uint8_t bit = 8; bit > 0; --bit) {
            if (crc & 0x80) {
                crc = static_cast<uint8_t>((crc << 1) ^ 0x31);
            } else {
                crc = static_cast<uint8_t>(crc << 1);
            }
        }
    }
    return crc;
}

void SCD40::clearError() {
    _lastError = ERROR_NONE;
}

void SCD40::setError(uint8_t error) {
    _lastError = error;
}

bool SCD40::writeCommand(uint16_t command) {
    _wire->beginTransmission(_address);
    _wire->write(static_cast<uint8_t>(command >> 8));
    _wire->write(static_cast<uint8_t>(command & 0xFF));
    uint8_t result = _wire->endTransmission();

    if (result != 0) {
        setError(ERROR_I2C_WRITE);
        return false;
    }

    clearError();
    return true;
}

bool SCD40::writeCommandWithWord(uint16_t command, uint16_t value) {
    uint8_t wordBytes[2] = {
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value & 0xFF)
    };
    uint8_t crc = calculateCRC8(wordBytes, 2);

    _wire->beginTransmission(_address);
    _wire->write(static_cast<uint8_t>(command >> 8));
    _wire->write(static_cast<uint8_t>(command & 0xFF));
    _wire->write(wordBytes[0]);
    _wire->write(wordBytes[1]);
    _wire->write(crc);
    uint8_t result = _wire->endTransmission();

    if (result != 0) {
        setError(ERROR_I2C_WRITE);
        return false;
    }

    clearError();
    return true;
}

bool SCD40::readWords(uint16_t command, uint16_t *words, uint8_t wordCount, uint16_t delayMs) {
    if (!writeCommand(command)) {
        return false;
    }

    if (delayMs > 0) {
        delay(delayMs);
    }

    uint8_t byteCount = wordCount * 3;
    uint8_t buffer[9];
    if (byteCount > sizeof(buffer)) {
        setError(ERROR_INVALID_PARAMETER);
        return false;
    }

    if (!readRaw(buffer, byteCount)) {
        return false;
    }

    for (uint8_t i = 0; i < wordCount; i++) {
        uint8_t msb = buffer[i * 3];
        uint8_t lsb = buffer[i * 3 + 1];
        uint8_t crc = buffer[i * 3 + 2];

        if (!validateWordCRC(msb, lsb, crc)) {
            setError(ERROR_CRC);
            return false;
        }

        words[i] = (static_cast<uint16_t>(msb) << 8) | lsb;
    }

    clearError();
    return true;
}

bool SCD40::readRaw(uint8_t *buffer, uint8_t byteCount) {
    uint8_t received = _wire->requestFrom(_address, byteCount);
    if (received != byteCount) {
        setError(ERROR_I2C_READ);
        while (_wire->available()) {
            _wire->read();
        }
        return false;
    }

    for (uint8_t i = 0; i < byteCount; i++) {
        if (!_wire->available()) {
            setError(ERROR_I2C_READ);
            return false;
        }
        buffer[i] = static_cast<uint8_t>(_wire->read());
    }

    return true;
}

bool SCD40::validateWordCRC(uint8_t msb, uint8_t lsb, uint8_t crc) {
    uint8_t data[2] = {msb, lsb};
    return calculateCRC8(data, 2) == crc;
}
