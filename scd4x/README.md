# SCD4x Bare-Metal Driver

Bare-metal C driver for Sensirion SCD4x CO2 sensors (SCD40, SCD41, SCD43).
Based on **Datasheet v1.7 – April 2025**. Platform-independent via HAL function pointer injection.

---

## Supported Sensors

| Model | Periodic | Low-Power Periodic | Single-Shot | Power Down/Wake Up |
|-------|----------|--------------------|-------------|-------------------|
| SCD40 | Yes | Yes | No | No |
| SCD41 | Yes | Yes | Yes | Yes |
| SCD43 | Yes | Yes | Yes | Yes |

---

## Files

```
scd4x.h        - Public API, types, constants
scd4x.c        - Driver implementation
scd4x_def.h    - Register and constant definitions
```

---

## Quick Start

### 1. Implement HAL callbacks

```c
/* ESP32 example (esp-idf) */
static int my_i2c_write(uint8_t addr, const uint8_t *buf, size_t len) {
    i2c_master_dev_handle_t h = find_handle(addr);
    return (h && i2c_master_transmit(h, buf, len, 100) == ESP_OK) ? 0 : -1;
}

static int my_i2c_read(uint8_t addr, uint8_t *buf, size_t len) {
    i2c_master_dev_handle_t h = find_handle(addr);
    return (h && i2c_master_receive(h, buf, len, 100) == ESP_OK) ? 0 : -1;
}

static void my_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

```

> **Note:** Pass 7-bit I2C address (`0x62`). The HAL must handle the R/W bit shift (e.g. `addr << 1` for STM32).

### 2. Initialize device

```c
SCD4x_Dev dev = {
    .i2c_addr  = SCD4X_I2C_ADDR,   /* 0x62 */
    .i2c_write = my_i2c_write,
    .i2c_read  = my_i2c_read,
    .delay_ms  = my_delay_ms,
    .mode      = SCD4X_MODE_PERIODIC,
};

SCD4x_Status st = SCD4x_Init(&dev);
if (st != SCD4X_OK) { /* handle error */ }
```

### 3. Read measurements

```c
/* Start periodic measurement (one sample every 5s) */
SCD4x_StartMeasurement(&dev, SCD4X_MODE_PERIODIC);

while (1) {
    bool ready = false;
    SCD4x_GetDataReadyStatus(&dev, &ready);
    if (!ready) { my_delay_ms(100); continue; }

    SCD4x_Measurement meas;
    if (SCD4x_ReadMeasurement(&dev, &meas) == SCD4X_OK) {
        printf("CO2: %u ppm  Temp: %.2f C  RH: %.2f %%\n",
               meas.co2_ppm, meas.temperature_c, meas.humidity_rh);
    }
    my_delay_ms(5000);
}
```

---

## API Reference

### Initialization

```c
SCD4x_Status SCD4x_Init(SCD4x_Dev *dev);
```

Waits for power-up (30ms), then verifies communication by reading the serial number.

---

### Measurement Modes

```c
SCD4x_Status SCD4x_StartMeasurement(SCD4x_Dev *dev, SCD4x_Mode mode);
SCD4x_Status SCD4x_Stop(SCD4x_Dev *dev);
SCD4x_Status SCD4x_ReadMeasurement(SCD4x_Dev *dev, SCD4x_Measurement *out);
SCD4x_Status SCD4x_GetDataReadyStatus(SCD4x_Dev *dev, bool *ready);
```

| Mode | Interval | Command |
|------|----------|---------|
| `SCD4X_MODE_PERIODIC` | 5s | `start_periodic_measurement` |
| `SCD4X_MODE_PERIODIC_LOW_POWER` | 30s | `start_low_power_periodic_measurement` |
| `SCD4X_MODE_SINGLE_SHOT` | on-demand | `measure_single_shot` (SCD41/43 only) |

**Measurement data:**

```c
typedef struct {
    uint16_t co2_ppm;       /* CO2 concentration in ppm */
    float    temperature_c; /* Temperature in °C        */
    float    humidity_rh;   /* Relative humidity in %   */
} SCD4x_Measurement;
```

---

### On-Chip Signal Compensation (Section 3.7)

> Sensor must be **idle** (stopped) before calling these. The driver handles stop/resume automatically.
> Exception: ambient pressure can be updated while measurement is running.

```c
/* Temperature offset — corrects RH and T output (not CO2) */
SCD4x_Status SCD4x_SetTemperatureOffset(SCD4x_Dev *dev, float offset_c);  /* 0.0 – 20.0 °C */
SCD4x_Status SCD4x_GetTemperatureOffset(SCD4x_Dev *dev, float *offset_c);

/* Sensor altitude — compensates CO2 for ambient pressure via altitude */
SCD4x_Status SCD4x_SetSensorAltitude(SCD4x_Dev *dev, uint16_t altitude_m); /* 0 – 3000 m */
SCD4x_Status SCD4x_GetSensorAltitude(SCD4x_Dev *dev, uint16_t *altitude_m);

/* Ambient pressure — higher accuracy than altitude; can be called during measurement */
SCD4x_Status SCD4x_SetAmbientPressure(SCD4x_Dev *dev, uint32_t pressure_pa); /* 70000 – 120000 Pa */
SCD4x_Status SCD4x_GetAmbientPressure(SCD4x_Dev *dev, uint32_t *pressure_pa);
```

> Use **either** altitude or ambient pressure compensation, not both simultaneously.

---

### Field Calibration (Section 3.8)

```c
/* Forced Recalibration — requires sensor warm-up (3+ min) in known CO2 concentration */
/* correction_ppm is optional output (pass NULL if not needed) */
SCD4x_Status SCD4x_PerformForcedRecalibration(SCD4x_Dev *dev, uint16_t target_ppm, int16_t *correction_ppm);

/* Auto Self-Calibration (ASC) */
SCD4x_Status SCD4x_SetAutoSelfCalibEnabled(SCD4x_Dev *dev, bool enabled);
SCD4x_Status SCD4x_GetAutoSelfCalibEnabled(SCD4x_Dev *dev, bool *enabled);
SCD4x_Status SCD4x_SetAutoSelfCalibTarget(SCD4x_Dev *dev, uint16_t target_ppm);
SCD4x_Status SCD4x_GetAutoSelfCalibTarget(SCD4x_Dev *dev, uint16_t *target_ppm);
```

**ASC periods (SCD41/SCD43 only) — must be multiples of 4:**

```c
SCD4x_Status SCD4x_SetASCInitialPeriod(SCD4x_Dev *dev, uint16_t period_h);  /* default: 44h  */
SCD4x_Status SCD4x_GetASCInitialPeriod(SCD4x_Dev *dev, uint16_t *period_h);
SCD4x_Status SCD4x_SetASCStandardPeriod(SCD4x_Dev *dev, uint16_t period_h); /* default: 156h */
SCD4x_Status SCD4x_GetASCStandardPeriod(SCD4x_Dev *dev, uint16_t *period_h);
```

---

### Advanced Features (Section 3.10)

```c
/* Persist settings to EEPROM (survives power cycle) — max 2000 write cycles */
SCD4x_Status SCD4x_PersistSettings(SCD4x_Dev *dev);

/* Read 48-bit serial number */
SCD4x_Status SCD4x_GetSerialNumber(SCD4x_Dev *dev, uint64_t *serial);

/* Self-test — takes ~10s; ok=true means sensor passed */
SCD4x_Status SCD4x_PerformSelfTest(SCD4x_Dev *dev, bool *ok);

/* Factory reset — restores all settings to factory defaults */
SCD4x_Status SCD4x_PerformFactoryReset(SCD4x_Dev *dev);

/* Reinit — reloads settings from EEPROM without power cycle */
SCD4x_Status SCD4x_Reinit(SCD4x_Dev *dev);

/* Sensor variant — bits[15:12] of response word */
SCD4x_Status SCD4x_GetSensorVariant(SCD4x_Dev *dev, uint16_t *variant);
```

**Variant values:**

```c
SCD4X_VARIANT_SCD40  /* 0x0000 */
SCD4X_VARIANT_SCD41  /* 0x1000 */
SCD4X_VARIANT_SCD43  /* 0x5000 */
```

---

### Single-Shot Mode (SCD41/SCD43 only — Section 3.11)

```c
/* Full measurement: CO2 + T + RH (5s) */
SCD4x_Status SCD4x_MeasureSingleShot(SCD4x_Dev *dev);

/* Temperature and humidity only, no CO2 (50ms) */
SCD4x_Status SCD4x_MeasureSingleShotRHTOnly(SCD4x_Dev *dev);

/* Power down — sensor draws <1µA; call WakeUp before next command */
SCD4x_Status SCD4x_PowerDown(SCD4x_Dev *dev);

/* Wake up — sensor does NOT ACK this command (expected behavior) */
/* Readiness is verified internally by reading the serial number */
SCD4x_Status SCD4x_WakeUp(SCD4x_Dev *dev);
```

---

### Status Codes

```c
SCD4X_OK            =  0   /* Success                          */
SCD4X_ERR_I2C       = -1   /* I2C bus error                    */
SCD4X_ERR_CRC       = -2   /* CRC mismatch on received data    */
SCD4X_ERR_FRC       = -3   /* Forced recalibration failed      */
SCD4X_ERR_NOT_READY = -4   /* Data not ready                   */
SCD4X_ERR_PARAM     = -5   /* Invalid parameter or NULL ptr    */
```

---

## Usage Patterns

### Periodic measurement with ambient pressure update

```c
SCD4x_Init(&dev);
SCD4x_SetTemperatureOffset(&dev, 4.0f);  /* design-in offset */
SCD4x_StartMeasurement(&dev, SCD4X_MODE_PERIODIC);

while (1) {
    /* Update ambient pressure from external sensor (barometer) */
    uint32_t pressure_pa = barometer_read_pa();
    SCD4x_SetAmbientPressure(&dev, pressure_pa);  /* safe during measurement */

    bool ready = false;
    SCD4x_GetDataReadyStatus(&dev, &ready);
    if (ready) {
        SCD4x_Measurement meas;
        SCD4x_ReadMeasurement(&dev, &meas);
        /* process meas */
    }
    my_delay_ms(1000);
}
```

### Low-power single-shot (SCD41/SCD43)

```c
SCD4x_Init(&dev);

while (1) {
    SCD4x_MeasureSingleShot(&dev);
    my_delay_ms(5000);  /* wait for measurement */

    SCD4x_Measurement meas;
    SCD4x_ReadMeasurement(&dev, &meas);
    /* process meas */

    SCD4x_PowerDown(&dev);
    my_delay_ms(60000);  /* sleep 60s */

    SCD4x_WakeUp(&dev);
    my_delay_ms(30);
}
```

### Persist settings after configuration

```c
SCD4x_Init(&dev);
SCD4x_SetTemperatureOffset(&dev, 3.5f);
SCD4x_SetSensorAltitude(&dev, 100);
SCD4x_SetAutoSelfCalibEnabled(&dev, true);
SCD4x_SetAutoSelfCalibTarget(&dev, 400);
SCD4x_PersistSettings(&dev);   /* save to EEPROM */
/* Settings now survive power cycle */
```

---

## Important Notes

**Stop required before settings:** Sensor must be idle before get/set of temperature offset, altitude, ASC parameters. The driver calls `stop_periodic_measurement` and resumes automatically — except for ambient pressure which can be updated during measurement.

**ASC requires outdoor exposure:** Auto Self-Calibration assumes the sensor is exposed to 400 ppm CO2 (fresh outdoor air) at least once during the initial period. In purely indoor deployments, disable ASC and use Forced Recalibration instead.

**FRC warm-up:** Forced Recalibration requires at least 3 minutes of continuous periodic measurement before issuing the FRC command.

**Persist write cycles:** EEPROM supports a maximum of 2000 write cycles. Do not call `PersistSettings` in a loop.

**WakeUp no-ACK:** The `wake_up` command intentionally receives no ACK from the sensor. The driver ignores the resulting I2C error and verifies readiness by reading the serial number instead.

**Single-shot / Power-down:** Only available on SCD41 and SCD43. Calling these functions on an SCD40 will still transmit the command; behavior is undefined per datasheet.

---

## Limits & Defaults

| Parameter | Min | Max | Default |
|-----------|-----|-----|---------|
| Temperature offset | 0.0 °C | 20.0 °C | 4.0 °C |
| Sensor altitude | 0 m | 3000 m | 0 m |
| Ambient pressure | 70,000 Pa | 120,000 Pa | 101,300 Pa |
| ASC target | — | — | 400 ppm |
| ASC initial period | multiples of 4 | — | 44 h |
| ASC standard period | multiples of 4 | — | 156 h |

---

## CRC

SCD4x uses CRC-8 with polynomial `0x31` and initial value `0xFF`. The driver computes and verifies CRC automatically on all read/write operations.

```
Polynomial: x^8 + x^5 + x^4 + 1 (0x31)
Init:       0xFF
Input/output reflection: none
```