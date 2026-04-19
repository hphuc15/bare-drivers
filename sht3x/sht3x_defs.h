/**
 * @file sht3x_defs.h
 * @brief SHT3x-DIS register map, command codes, bitmasks and timing constants.
 *        Sourced from Sensirion SHT3x-DIS Datasheet, August 2016 - Version 3.
 */

#ifndef SHT3X_DEFS_H
#define SHT3X_DEFS_H

/* =========================================================================
 * I2C Addresses (Table 7)
 * ========================================================================= */

#define SHT3X_I2C_ADDR_VSS                0x44u   /* ADDR pin -> VSS (default) */
#define SHT3X_I2C_ADDR_VDD                0x45u   /* ADDR pin -> VDD           */

/* ============================================================
 * Signal Conversion Macros (Section 4.13)
 * ============================================================ */

#define SHT3X_TEMP_C(raw)    (-45.0f + 175.0f * (float)(raw) / 65535.0f)
#define SHT3X_HUMID_RH(raw)  (100.0f          * (float)(raw) / 65535.0f)

/* =========================================================================
 * Single-Shot Commands (Table 8)
 * ========================================================================= */

/* Clock stretching enabled */
#define SHT3X_CMD_SS_CS_HIGH            0x2C06u
#define SHT3X_CMD_SS_CS_MED             0x2C0Du
#define SHT3X_CMD_SS_CS_LOW             0x2C10u

/* Clock stretching disabled */
#define SHT3X_CMD_SS_HIGH               0x2400u
#define SHT3X_CMD_SS_MED                0x240Bu
#define SHT3X_CMD_SS_LOW                0x2416u

/* =========================================================================
 * Periodic Mode Commands (Table 9)
 * Naming: SHT3X_CMD_PERIOD_{mps}_{repeatability}
 * mps values: 05 = 0.5, 1, 2, 4, 10 measurements/second
 * ========================================================================= */
#define SHT3X_CMD_PERIOD_05_HIGH        0x2032u
#define SHT3X_CMD_PERIOD_05_MED         0x2024u
#define SHT3X_CMD_PERIOD_05_LOW         0x202Fu
#define SHT3X_CMD_PERIOD_1_HIGH         0x2130u
#define SHT3X_CMD_PERIOD_1_MED          0x2126u
#define SHT3X_CMD_PERIOD_1_LOW          0x212Du
#define SHT3X_CMD_PERIOD_2_HIGH         0x2236u
#define SHT3X_CMD_PERIOD_2_MED          0x2220u
#define SHT3X_CMD_PERIOD_2_LOW          0x222Bu
#define SHT3X_CMD_PERIOD_4_HIGH         0x2334u
#define SHT3X_CMD_PERIOD_4_MED          0x2322u
#define SHT3X_CMD_PERIOD_4_LOW          0x2329u
#define SHT3X_CMD_PERIOD_10_HIGH        0x2737u
#define SHT3X_CMD_PERIOD_10_MED         0x2721u
#define SHT3X_CMD_PERIOD_10_LOW         0x272Au

/* =========================================================================
 * Misc Commands
 * ========================================================================= */
#define SHT3X_CMD_FETCH_DATA            0xE000u
#define SHT3X_CMD_ART                   0x2B32u  /* Accelerated Response Time */
#define SHT3X_CMD_BREAK                 0x3093u
#define SHT3X_CMD_SOFT_RESET            0x30A2u
#define SHT3X_CMD_HEATER_ON             0x306Du
#define SHT3X_CMD_HEATER_OFF            0x3066u
#define SHT3X_CMD_READ_STATUS           0xF32Du
#define SHT3X_CMD_CLEAR_STATUS          0x3041u

/* General call reset (Table 14) */
#define SHT3X_GENERAL_CALL_ADDR         0x00u
#define SHT3X_GENERAL_CALL_RESET        0x06u

/* =========================================================================
 * Status Register Bitmasks (Table 17)
 * ========================================================================= */
#define SHT3X_SREG_ALERT_PENDING        (1u << 15)  /* At least one alert pending     */
#define SHT3X_SREG_HEATER_ON            (1u << 13)  /* Heater enabled                 */
#define SHT3X_SREG_RH_ALERT             (1u << 11)  /* RH tracking alert              */
#define SHT3X_SREG_T_ALERT              (1u << 10)  /* Temperature tracking alert     */
#define SHT3X_SREG_RESET_DETECTED       (1u <<  4)  /* Reset detected since last clear */
#define SHT3X_SREG_CMD_FAILED           (1u <<  1)  /* Last command not processed     */
#define SHT3X_SREG_CRC_WRITE_FAIL       (1u <<  0)  /* Last write CRC mismatch        */

/* =========================================================================
 * Timing Constants (ms) — safe maximums from Table 4
 * ========================================================================= */
#define SHT3X_POWERUP_MS                2u
#define SHT3X_SOFTRESET_MS              2u
#define SHT3X_MEAS_LOW_MS               4u
#define SHT3X_MEAS_MED_MS               6u
#define SHT3X_MEAS_HIGH_MS              15u
#define SHT3X_BREAK_MS                  15u

/* =========================================================================
 * CRC-8 Parameters (Table 19)
 * Polynomial: 0x31 (x^8 + x^5 + x^4 + 1), Init: 0xFF
 * ========================================================================= */
#define SHT3X_CRC_POLY                  0x31u
#define SHT3X_CRC_INIT                  0xFFu

#endif /* SHT3X_DEFS_H */