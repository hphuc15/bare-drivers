/**
 * @file    sht3x.h
 * @brief   Bare-metal driver for Sensirion SHT3x-DIS humidity & temperature sensor
 * @version 2.0
 *
 * Datasheet: SHT3x-DIS, August 2016 – Version 3
 *
 * Single-shot mode supports two variants (Section 4.3 / 4.4):
 *
 *   CLOCK STRETCHING ENABLED  (SHT3X_CLK_STRETCH_ENABLE):
 *     Master sends the measurement command, then immediately issues a
 *     repeated-START + read header.  The sensor ACKs the address and pulls
 *     SCL low until the measurement is complete, then releases SCL and sends
 *     the 6 data bytes.  No explicit delay is needed in firmware.
 *     Requires HAL support: sht3x_hal_i2c_read_stretch().
 *
 *   CLOCK STRETCHING DISABLED (SHT3X_CLK_STRETCH_DISABLE):
 *     Master sends the measurement command, waits the worst-case measurement
 *     time, then issues a read header.  If the sensor is still busy it NACKs;
 *     the driver retries up to SHT3X_POLL_RETRIES times.
 *     Works on any I2C master (hardware or bit-bang).
 */
#ifndef SHT3X_H
#define SHT3X_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return code */
typedef enum {
    SHT3X_OK           =  0,  /* Operation successful       */
    SHT3X_ERR_I2C      = -1,  /* I2C communication error    */
    SHT3X_ERR_CRC      = -2,  /* CRC validation failed      */
    SHT3X_ERR_PARAM    = -3,  /* Invalid parameter          */
    SHT3X_ERR_NOT_INIT = -4,  /* Driver not initialized     */
} SHT3x_Status;

/* I2C address */
#define SHT3X_I2C_ADDR_DEFAULT  0x44u   /* ADDR -> VSS */
#define SHT3X_I2C_ADDR_ALT      0x45u   /* ADDR -> VDD */

/* Measurement precision level */
typedef enum {
    SHT3X_REPEAT_LOW    = 0,  /* Low repeatability (fast, lower accuracy) */
    SHT3X_REPEAT_MEDIUM = 1,  /* Medium repeatability */
    SHT3X_REPEAT_HIGH   = 2,  /* High repeatability (slower, higher accuracy) */
} SHT3x_Repeatability;

/* Sensor operating mode */
typedef enum {
    SHT3X_MODE_SINGLE_SHOT = 0,  /* Single-shot measurement */
    SHT3X_MODE_PERIODIC    = 1,  /* Continuous periodic measurement */
} SHT3x_Mode;

/* Measurement rate (Measurements Per Second) used in periodic mode */
typedef enum {
    SHT3X_MPS_0_5 = 0,  /* 0.5 measurements per second */
    SHT3X_MPS_1   = 1,  /* 1 measurement per second */
    SHT3X_MPS_2   = 2,  /* 2 measurements per second */
    SHT3X_MPS_4   = 3,  /* 4 measurements per second */
    SHT3X_MPS_10  = 4,  /* 10 measurements per second */
} SHT3x_MPS;

/* HAL */
typedef int  (*SHT3x_I2C_Write_Fn)(uint8_t addr, const uint8_t *data, size_t len);
typedef int  (*SHT3x_I2C_Read_Fn) (uint8_t addr, uint8_t *data, size_t len);
typedef void (*SHT3x_Delay_Ms_Fn) (uint32_t ms);

/* Device handle */
typedef struct {
    uint8_t             i2c_addr;          /* I2C device address */
    SHT3x_Mode          mode;              /* Measurement mode */
    SHT3x_Repeatability repeatability;     /* Precision setting */
    SHT3x_MPS           mps;               /* Measurement rate (periodic only) */
    bool                clock_stretch;     /* Clock stretching enable (single-shot only).
                                              When true, no software delay is needed after
                                              the measurement command — the sensor holds
                                              SCL low until data is ready. */

    bool                initialized;       /* Initialization state flag */
    bool                periodic_running;  /* Periodic mode active flag */

    SHT3x_I2C_Write_Fn  i2c_write;         /* Platform I2C write callback */
    SHT3x_I2C_Read_Fn   i2c_read;          /* Platform I2C read callback */
    SHT3x_Delay_Ms_Fn   delay_ms;          /* Platform delay callback */
} SHT3x_Dev;

/* Measurement result container */
typedef struct {
    float temperature_c;  /* Temperature in °C */
    float humidity_rh;    /* Relative humidity in %RH */
} SHT3x_Data;

/* Sensor status register bit masks (section 4.11) */
#define SHT3X_SREG_ALERT_PENDING   (1u << 15)
#define SHT3X_SREG_HEATER_ON       (1u << 13)
#define SHT3X_SREG_RH_ALERT        (1u << 11)
#define SHT3X_SREG_T_ALERT         (1u << 10)
#define SHT3X_SREG_RESET_DETECTED  (1u <<  4)
#define SHT3X_SREG_CMD_FAILED      (1u <<  1)
#define SHT3X_SREG_CRC_FAILED      (1u <<  0)

/* Public APIs */

SHT3x_Status SHT3x_Init(SHT3x_Dev *dev);
SHT3x_Status SHT3x_Read(SHT3x_Dev *dev, SHT3x_Data *out);
SHT3x_Status SHT3x_Deinit(SHT3x_Dev *dev);
SHT3x_Status SHT3x_HeaterEnable(SHT3x_Dev *dev, bool enable);
SHT3x_Status SHT3x_ReadStatus(SHT3x_Dev *dev, uint16_t *status);
SHT3x_Status SHT3x_ClearStatus(SHT3x_Dev *dev);

#ifdef __cplusplus
}
#endif

#endif /* SHT3X_H */