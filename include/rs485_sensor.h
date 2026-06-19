#pragma once 
#include <Arduino.h>


//! ── RS485 Modbus RTU Sensor Driver ───────────────────────────────────────────
//
// Hardware (IES-WI-C6A, see schematic):
//   MAX3485 DI  <- GPIO16 (TXD0 pad, driven as UART1 via GPIO matrix)
//   MAX3485 RO  -> GPIO17 (RXD0 pad, driven as UART1 via GPIO matrix)
//   MAX3485 DE+RE (RS485_FC) <- GPIO14   HIGH = transmit, LOW = receive
//
// Driver selection is bound to the existing device sensor type:
//   1 = Environment (SCD40/LDR only — RS485 idle, UART not initialised)
//   2 = Soil        -> Halisense Soil 7-in-1   (4800,N,8,1, addr 1, regs 0x0000..0x0006)
//   3 = Mineral     -> CWT-OYS-PHEC Water pH/EC (9600,N,8,1, addr 1, regs 0x0000..0x0002)
//
// Only ONE sensor is ever connected at a time (single-drop bus).

//! ── Water pH/EC shared state (sensor type 3) ────────────────────────────────
extern float waterPh;            // pH            (raw / 100)
extern float waterEc;            // uS/cm         (raw)
extern float waterTemp;          // degC          (raw / 10)
extern bool  waterOK;            // last poll cycle succeeded
extern bool  alertWaterPh;       // outside [threshWaterPhLow, threshWaterPhHigh]
extern bool  alertWaterEc;       // above threshWaterEcHigh

//! ── Soil 7-in-1 shared state (sensor type 2) ────────────────────────────────
extern float    soilMoist;       // %RH           (raw / 10)
extern float    soilTemp;        // degC          (raw / 10, signed)
extern float    soilEc;          // uS/cm         (raw)
extern float    soilPh;          // pH            (raw / 10)
extern uint16_t soilN;           // mg/kg         (raw)
extern uint16_t soilP;           // mg/kg         (raw)
extern uint16_t soilK;           // mg/kg         (raw)
extern bool     soilOK;          // last poll cycle succeeded
extern bool     alertSoilMoist;  // outside [threshSoilMoistLow, threshSoilMoistHigh]
extern bool     alertSoilEc;     // above threshSoilEcHigh
extern bool     alertSoilPh;     // outside [threshSoilPhLow, threshSoilPhHigh]

//! ── Diagnostics ──────────────────────────────────────────────────────────────
extern uint32_t rs485PollCount;  // total poll attempts since boot
extern uint32_t rs485FailCount;  // total failed polls since boot (timeout/CRC/invalid)

//! ── API ──────────────────────────────────────────────────────────────────────
void    rs485SensorInit();              // call once in setup() — reads type from NVS
void    rs485SensorRead();              // call in loop() — internally throttled (5s)
void    rs485ApplySensorType(uint8_t t);// call after web UI changes type (1/2/3)
uint8_t rs485ActiveType();              // currently active type
const char* rs485StatusLabel();         // "idle" / "ok" / "no response" — for UI/logs
