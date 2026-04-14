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





#ifdef SHT3X_V2
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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * I2C Addresses (7-bit)
 * ========================================================================= */
#define SHT3X_I2C_ADDR_A             0x44u   /**< ADDR → VSS (default) */
#define SHT3X_I2C_ADDR_B             0x45u   /**< ADDR → VDD           */

/* =========================================================================
 * Single-Shot Commands  (Table 8)
 * ========================================================================= */
/* Clock stretching ENABLED */
#define SHT3X_CMD_SS_STRETCH_HIGH    0x2C06u
#define SHT3X_CMD_SS_STRETCH_MED     0x2C0Du
#define SHT3X_CMD_SS_STRETCH_LOW     0x2C10u
/* Clock stretching DISABLED */
#define SHT3X_CMD_SS_POLL_HIGH       0x2400u
#define SHT3X_CMD_SS_POLL_MED        0x240Bu
#define SHT3X_CMD_SS_POLL_LOW        0x2416u

/* =========================================================================
 * Periodic Measurement Commands  (Table 9)
 * Note: clock stretching cannot be used in periodic mode (Section 4.5).
 * ========================================================================= */
#define SHT3X_CMD_PER_0P5_HIGH       0x2032u
#define SHT3X_CMD_PER_0P5_MED        0x2024u
#define SHT3X_CMD_PER_0P5_LOW        0x202Fu
#define SHT3X_CMD_PER_1_HIGH         0x2130u
#define SHT3X_CMD_PER_1_MED          0x2126u
#define SHT3X_CMD_PER_1_LOW          0x212Du
#define SHT3X_CMD_PER_2_HIGH         0x2236u
#define SHT3X_CMD_PER_2_MED          0x2220u
#define SHT3X_CMD_PER_2_LOW          0x222Bu
#define SHT3X_CMD_PER_4_HIGH         0x2334u
#define SHT3X_CMD_PER_4_MED          0x2322u
#define SHT3X_CMD_PER_4_LOW          0x2329u
#define SHT3X_CMD_PER_10_HIGH        0x2737u
#define SHT3X_CMD_PER_10_MED         0x2721u
#define SHT3X_CMD_PER_10_LOW         0x272Au
/* Accelerated Response Time – 4 Hz  (Table 11) */
#define SHT3X_CMD_ART                0x2B32u

/* =========================================================================
 * Miscellaneous Commands
 * ========================================================================= */
#define SHT3X_CMD_FETCH_DATA         0xE000u
#define SHT3X_CMD_BREAK              0x3093u
#define SHT3X_CMD_SOFT_RESET         0x30A2u
#define SHT3X_CMD_HEATER_ON          0x306Du
#define SHT3X_CMD_HEATER_OFF         0x3066u
#define SHT3X_CMD_READ_STATUS        0xF32Du
#define SHT3X_CMD_CLEAR_STATUS       0x3041u

#define SHT3X_GENERAL_CALL_ADDR      0x00u
#define SHT3X_GENERAL_CALL_RESET     0x06u

/* =========================================================================
 * Status Register Bit Masks  (Table 17)
 * ========================================================================= */
#define SHT3X_STATUS_ALERT_PENDING   (1u << 15)
#define SHT3X_STATUS_HEATER_ON       (1u << 13)
#define SHT3X_STATUS_RH_ALERT        (1u << 11)
#define SHT3X_STATUS_T_ALERT         (1u << 10)
#define SHT3X_STATUS_RESET_DETECT    (1u <<  4)
#define SHT3X_STATUS_CMD_FAILED      (1u <<  1)
#define SHT3X_STATUS_CRC_FAILED      (1u <<  0)

/* =========================================================================
 * Timing (ms, worst-case values from Table 4)
 * ========================================================================= */
#define SHT3X_T_PU_MS                1u
#define SHT3X_T_SR_MS                1u
#define SHT3X_T_MEAS_LOW_MS          4u
#define SHT3X_T_MEAS_MED_MS          6u
#define SHT3X_T_MEAS_HIGH_MS         15u

/** Retry count in polling mode (1 ms between retries) */
#define SHT3X_POLL_RETRIES           10u

/* =========================================================================
 * CRC-8 Parameters  (Table 19)
 * ========================================================================= */
#define SHT3X_CRC_POLY               0x31u
#define SHT3X_CRC_INIT               0xFFu

/* =========================================================================
 * Public Types
 * ========================================================================= */

typedef enum {
    SHT3X_OK            =  0,
    SHT3X_ERR_I2C       = -1,
    SHT3X_ERR_CRC       = -2,
    SHT3X_ERR_TIMEOUT   = -3,
    SHT3X_ERR_PARAM     = -4,
} sht3x_err_t;

typedef enum {
    SHT3X_CLK_STRETCH_DISABLE = 0,
    SHT3X_CLK_STRETCH_ENABLE  = 1,
} sht3x_clk_stretch_t;

typedef enum {
    SHT3X_REPEAT_LOW    = 0,
    SHT3X_REPEAT_MEDIUM = 1,
    SHT3X_REPEAT_HIGH   = 2,
} sht3x_repeat_t;

typedef struct {
    float temperature_c;
    float temperature_f;
    float humidity_rh;
} sht3x_data_t;

typedef struct {
    uint16_t raw_temperature;
    uint16_t raw_humidity;
} sht3x_raw_t;

typedef struct {
    uint8_t  i2c_addr;
    bool     periodic_mode;
} sht3x_t;

/* =========================================================================
 * API – Initialisation
 * ========================================================================= */
sht3x_err_t sht3x_init(sht3x_t *dev, uint8_t i2c_addr);

/* =========================================================================
 * API – Single-Shot, raw output
 * ========================================================================= */

/** Polling (no clock stretch) – works on any I2C master. */
sht3x_err_t sht3x_single_shot_poll_raw(sht3x_t       *dev,
                                        sht3x_repeat_t repeat,
                                        sht3x_raw_t   *raw);

/** Clock-stretch – requires sht3x_hal_i2c_read_stretch(). */
sht3x_err_t sht3x_single_shot_stretch_raw(sht3x_t       *dev,
                                           sht3x_repeat_t repeat,
                                           sht3x_raw_t   *raw);

/* =========================================================================
 * API – Single-Shot, converted output (convenience wrappers)
 * ========================================================================= */
sht3x_err_t sht3x_single_shot_poll   (sht3x_t *dev, sht3x_repeat_t repeat,
                                       sht3x_data_t *data);
sht3x_err_t sht3x_single_shot_stretch(sht3x_t *dev, sht3x_repeat_t repeat,
                                       sht3x_data_t *data);

/** Unified API – selects mode via @p stretch parameter. */
sht3x_err_t sht3x_single_shot(sht3x_t *dev, sht3x_repeat_t repeat,
                               sht3x_clk_stretch_t stretch,
                               sht3x_data_t *data);

/* =========================================================================
 * API – Periodic Mode
 * ========================================================================= */
sht3x_err_t sht3x_start_periodic(sht3x_t *dev, uint16_t cmd);
sht3x_err_t sht3x_fetch_periodic (sht3x_t *dev, sht3x_data_t *data);
sht3x_err_t sht3x_stop_periodic  (sht3x_t *dev);

/* =========================================================================
 * API – Reset / Status / Heater
 * ========================================================================= */
sht3x_err_t sht3x_soft_reset         (sht3x_t *dev);
sht3x_err_t sht3x_general_call_reset (sht3x_t *dev);
sht3x_err_t sht3x_read_status        (sht3x_t *dev, uint16_t *status);
sht3x_err_t sht3x_clear_status       (sht3x_t *dev);
sht3x_err_t sht3x_set_heater         (sht3x_t *dev, bool enable);

/* =========================================================================
 * API – Helpers
 * ========================================================================= */
void    sht3x_convert(const sht3x_raw_t *raw, sht3x_data_t *data);
uint8_t sht3x_crc8   (const uint8_t *buf, uint8_t len);

/* =========================================================================
 * HAL Callbacks – implement for your platform
 * All return 0 on success, non-zero on error (NACK, bus error, timeout).
 * ========================================================================= */

/** Standard write: START – ADDR+W – buf[0..len-1] – STOP */
extern int  sht3x_hal_i2c_write(uint8_t addr,
                                 const uint8_t *buf, uint8_t len);

/**
 * Standard read: START – ADDR+R – buf[0..len-1] – STOP
 * Must return non-zero immediately if sensor NACKs the address
 * (used by the polling path to detect "not ready").
 */
extern int  sht3x_hal_i2c_read(uint8_t addr,
                                uint8_t *buf, uint8_t len);

/**
 * Clock-stretch-aware read.
 *
 * Same as sht3x_hal_i2c_read() but the implementation must tolerate the
 * sensor holding SCL low for up to SHT3X_T_MEAS_HIGH_MS (15 ms) after
 * the address ACK before releasing it and sending data.
 *
 * Platform notes:
 *   STM32 HAL  – use HAL_I2C_Master_Receive() with timeout >= 20 ms;
 *                clock stretching is handled transparently by hardware.
 *   AVR TWI    – increase TWBR / prescaler timeout, or loop on TWSR.
 *   Bit-bang   – after ACKing the address, poll SCL pin HIGH before
 *                beginning to clock data bits.
 *
 * If your platform cannot support clock stretching, implement this as a
 * stub returning -1 and use the poll variants exclusively.
 */
extern int  sht3x_hal_i2c_read_stretch(uint8_t addr,
                                        uint8_t *buf, uint8_t len);

/** Block for at least `ms` milliseconds. */
extern void sht3x_hal_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* SHT3X_H */

#endif