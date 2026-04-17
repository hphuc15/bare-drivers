# BH1750FVI Ambient Light Sensor Driver

Platform-independent C driver for the **BH1750FVI** ambient light sensor over I2C.  
Designed with a **HAL (Hardware Abstraction Layer)** pattern — runs on any microcontroller (ESP32, STM32, AVR, …).

---

## Features

- Supports all 6 measurement modes (Continuous & One-time, H/H2/L Resolution)
- Platform-agnostic — HAL function pointers are injected at runtime
- Full lifecycle management: Power On → Reset → Measure → Power Down
- Sensitivity adjustment via MTreg register (`BH1750_SetMTreg`)
- Outputs **lux** directly as a `float`, using the full MTreg-aware datasheet formula
- Automatically re-triggers measurement for One-time modes on each read

---

## Hardware

| Parameter | Value |
|---|---|
| Interface | I2C |
| Address (ADDR = GND) | `0x23` |
| Address (ADDR = VCC) | `0x5C` |
| Supply voltage | 2.4V – 3.6V |
| Measurement range | 1 – 65535 lx |

**Basic wiring:**

```
BH1750          MCU
------          ---
VCC  ──────── 3.3V
GND  ──────── GND
SDA  ──────── SDA (+ 4.7kΩ pull-up to 3.3V)
SCL  ──────── SCL (+ 4.7kΩ pull-up to 3.3V)
ADDR ──────── GND  →  address 0x23
```

---

## File Structure

```
bh1750/
├── bh1750.h    # Public API, structs, enums
└── bh1750.c    # Implementation
```

---

## Integration

Copy `bh1750.h` and `bh1750.c` into your project and add them to your build system (CMake / Makefile / ESP-IDF component, …).  
No external dependencies beyond `<stdint.h>` and `<stddef.h>`.

---

## API

### Initialize

```c
BH1750_Status BH1750_Init(BH1750_Dev *dev, BH1750_Mode mode);
```

Powers on the sensor, clears the data register, starts measurement, and waits for the first result. MTreg is set to the factory default (69).

### Read light level

```c
BH1750_Status BH1750_ReadLux(BH1750_Dev *dev, float *lux);
```

Returns the ambient light level in **lux**, calculated using the full MTreg-aware formula.  
In One-time mode, automatically re-sends the measurement command and waits before reading. Wait time scales with the current MTreg value.

### Set sensitivity (MTreg)

```c
BH1750_Status BH1750_SetMTreg(BH1750_Dev *dev, uint8_t mtreg);
```

Adjusts sensor sensitivity by changing the internal Measurement Time register. See [Sensitivity Adjustment](#sensitivity-adjustment-mtreg) for details.  
Recommended for use with **Continuous mode** — see note below.

### Power down

```c
BH1750_Status BH1750_PowerDown(BH1750_Dev *dev);
```

Places the sensor in Power Down state (low power). MTreg resets to default (69) on the next `BH1750_Init()`.  
Call `BH1750_Init()` again to resume operation.

---

## Measurement Modes

| Enum | Opcode | Resolution | Measurement time | Notes |
|---|---|---|---|---|
| `BH1750_MODE_CONT_H_RES` | `0x10` | 1 lx | ~180 ms | Continuous |
| `BH1750_MODE_CONT_H_RES2` | `0x11` | 0.5 lx | ~180 ms | Continuous |
| `BH1750_MODE_CONT_L_RES` | `0x13` | 4 lx | ~24 ms | Continuous, fast |
| `BH1750_MODE_ONE_H_RES` | `0x20` | 1 lx | ~180 ms | One-time |
| `BH1750_MODE_ONE_H_RES2` | `0x21` | 0.5 lx | ~180 ms | One-time |
| `BH1750_MODE_ONE_L_RES` | `0x23` | 4 lx | ~24 ms | One-time |

> **Continuous mode**: the sensor measures continuously; simply read the result register at any time.  
> **One-time mode**: the sensor takes one measurement then powers down automatically. The driver re-triggers it on every call to `BH1750_ReadLux()`.

---

## Return Codes

| Value | Meaning |
|---|---|
| `BH1750_OK` (0) | Success |
| `BH1750_ERR_NULL` (-1) | NULL pointer passed |
| `BH1750_ERR_I2C_WRITE` (-2) | I2C write failed |
| `BH1750_ERR_I2C_READ` (-3) | I2C read failed |

---

## Sensitivity Adjustment (MTreg)

The BH1750FVI allows sensitivity tuning via the **MTreg** (Measurement Time register). Increasing MTreg extends the internal integration window, raising sensitivity proportionally. This is useful when a protective cover or optical filter attenuates the incoming light.

| MTreg value | Sensitivity factor | Min detectable (H-res) |
|---|---|---|
| 31 (min) | × 0.45 | ~1.85 lx/count |
| **69 (default)** | **× 1.0** | **~0.83 lx/count** |
| 254 (max) | × 3.68 | ~0.23 lx/count |

**Example — compensating a 50% transmittance filter:**

```c
BH1750_Init(&sensor, BH1750_MODE_CONT_H_RES);

// Filter blocks 50% of light → double the sensitivity
BH1750_SetMTreg(&sensor, 138); // 69 * 2
```

> **Note:** Measurement time scales linearly with MTreg. At MTreg = 138, H-res measurement time increases from ~180 ms to ~360 ms. The driver accounts for this automatically in both `BH1750_SetMTreg()` and `BH1750_ReadLux()`.

> **Recommended mode:** The datasheet illustrates MTreg adjustment using **Continuous mode**. In One-time mode, every `BH1750_ReadLux()` call triggers a full measurement cycle, so a high MTreg value directly adds latency on every read.

---

## Lux Calculation

Per the BH1750FVI datasheet, lux is calculated as follows:

```
H-res / L-res:  lux = raw / 1.2 × (69 / MTreg)
H-res2:         lux = raw / 1.2 × (69 / MTreg) / 2
```

At the default MTreg (69), the correction factor `69 / 69 = 1` drops out, reducing the formula to `raw / 1.2`. When MTreg is changed via `BH1750_SetMTreg()`, the driver applies the full formula automatically — no changes needed at the call site.

---

## Usage Examples

### ESP32 (ESP-IDF)

```c
#include "bh1750.h"
#include "driver/i2c_master.h"

/* --- HAL functions --- */

static int hal_i2c_write(uint8_t addr, const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return (ret == ESP_OK) ? 0 : -1;
}

static int hal_i2c_read(uint8_t addr, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return (ret == ESP_OK) ? 0 : -1;
}

static void hal_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* --- Initialize and use --- */

void app_main(void)
{
    BH1750_Dev sensor = {
        .i2c_addr  = BH1750_I2C_ADDR_LOW,
        .i2c_write = hal_i2c_write,
        .i2c_read  = hal_i2c_read,
        .delay_ms  = hal_delay_ms,
    };

    if (BH1750_Init(&sensor, BH1750_MODE_CONT_H_RES) != BH1750_OK) {
        // Handle initialization error
        return;
    }

    // Optional: increase sensitivity for a protective cover with 50% transmittance
    BH1750_SetMTreg(&sensor, 138);

    float lux = 0.0f;
    while (1) {
        if (BH1750_ReadLux(&sensor, &lux) == BH1750_OK) {
            printf("Light: %.1f lx\n", lux);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

### STM32 (HAL)

> **Note:** STM32 HAL expects a shifted I2C address (`addr << 1`). Apply the shift inside your `hal_i2c_write` / `hal_i2c_read` functions, **not** in the driver.

```c
static int hal_i2c_write(uint8_t addr, const uint8_t *data, size_t len)
{
    return HAL_I2C_Master_Transmit(&hi2c1, addr << 1,
                                   (uint8_t *)data, len, 100) == HAL_OK ? 0 : -1;
}

static int hal_i2c_read(uint8_t addr, uint8_t *data, size_t len)
{
    return HAL_I2C_Master_Receive(&hi2c1, addr << 1,
                                  data, len, 100) == HAL_OK ? 0 : -1;
}

static void hal_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}
```

---

## Reference

- [BH1750FVI Datasheet (Rohm)](https://www.mouser.com/datasheet/2/348/bh1750fvi-e-186247.pdf)