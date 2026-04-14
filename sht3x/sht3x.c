#include "sht3x.h"

#define CRC8_POLY  0x31u
#define CRC8_INIT  0xFFu

/* ============================================================
 * Signal Conversion Macros (Section 4.13)
 * ============================================================ */

#define SHT3X_TEMP_C(raw)    (-45.0f + 175.0f * (float)(raw) / 65535.0f)
#define SHT3X_HUMID_RH(raw)  (100.0f           * (float)(raw) / 65535.0f)

/* ============================================================
 * Command Codes
 * ============================================================ */

/* Single Shot - Clock Stretching Enabled (Section 4.3, Table 8) */
#define SHT3X_CMD_SINGLESHOT_CS_HIGH     0x2C06u
#define SHT3X_CMD_SINGLESHOT_CS_MEDIUM   0x2C0Du
#define SHT3X_CMD_SINGLESHOT_CS_LOW      0x2C10u

/* Single Shot - No Clock Stretching */
#define SHT3X_CMD_SINGLESHOT_HIGH        0x2400u
#define SHT3X_CMD_SINGLESHOT_MEDIUM      0x240Bu
#define SHT3X_CMD_SINGLESHOT_LOW         0x2416u

/* Periodic (Section 4.5, Table 9) */
#define SHT3X_CMD_PERIODIC_0_5_HIGH    0x2032u
#define SHT3X_CMD_PERIODIC_0_5_MEDIUM  0x2024u
#define SHT3X_CMD_PERIODIC_0_5_LOW     0x202Fu
#define SHT3X_CMD_PERIODIC_1_HIGH      0x2130u
#define SHT3X_CMD_PERIODIC_1_MEDIUM    0x2126u
#define SHT3X_CMD_PERIODIC_1_LOW       0x212Du
#define SHT3X_CMD_PERIODIC_2_HIGH      0x2236u
#define SHT3X_CMD_PERIODIC_2_MEDIUM    0x2220u
#define SHT3X_CMD_PERIODIC_2_LOW       0x222Bu
#define SHT3X_CMD_PERIODIC_4_HIGH      0x2334u
#define SHT3X_CMD_PERIODIC_4_MEDIUM    0x2322u
#define SHT3X_CMD_PERIODIC_4_LOW       0x2329u
#define SHT3X_CMD_PERIODIC_10_HIGH     0x2737u
#define SHT3X_CMD_PERIODIC_10_MEDIUM   0x2721u
#define SHT3X_CMD_PERIODIC_10_LOW      0x272Au

/* Utility Commands */
#define SHT3X_CMD_FETCH_DATA    0xE000u
#define SHT3X_CMD_BREAK         0x3093u
#define SHT3X_CMD_SOFT_RESET    0x30A2u
#define SHT3X_CMD_HEATER_EN     0x306Du
#define SHT3X_CMD_HEATER_DIS    0x3066u
#define SHT3X_CMD_READ_STATUS   0xF32Du
#define SHT3X_CMD_CLEAR_STATUS  0x3041u

/* ============================================================
 * Execution Time (ms) - Section 2.2, Table 4
 * ============================================================ */
#define SHT3X_EXEC_POWERUP_MS       1
#define SHT3X_EXEC_SOFT_RESET_MS    2
#define SHT3X_EXEC_MEAS_LOW_MS      4
#define SHT3X_EXEC_MEAS_MEDIUM_MS   6
#define SHT3X_EXEC_MEAS_HIGH_MS     15
#define SHT3X_EXEC_READ_STATUS_MS   1
#define SHT3X_EXEC_HEATER_MS        1
#define SHT3X_EXEC_BREAK_MS         15  /* = tMEAS,h max, safe cho mọi repeatability */

/* FIX #1: max read buffer = 2 words × 3 bytes (data + CRC) = 6 bytes.
   Replaced VLA with a fixed-size array to avoid stack issues and
   undefined behaviour when n_words == 0. */
#define SHT3X_MAX_WORDS  2u
#define SHT3X_BUF_SIZE   (SHT3X_MAX_WORDS * 3u)

/* ============================================================
 * Private helpers
 * ============================================================ */

/**
 * @brief Compute CRC-8 checksum (poly 0x31, init 0xFF).
 *
 * @param data  Pointer to input bytes.
 * @param len   Number of bytes to process.
 * @return      Computed CRC-8 value.
 */
static uint8_t _sht3x_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = CRC8_INIT;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80u) ? ((uint8_t)(crc << 1) ^ CRC8_POLY) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/**
 * @brief Send a 16-bit command word over I2C (MSB first).
 *
 * @param dev  Device handle.
 * @param cmd  Command code.
 * @return     @ref SHT3X_OK, or @ref SHT3X_ERR_I2C on bus failure.
 */
static SHT3x_Status _sht3x_send_cmd(SHT3x_Dev *dev, uint16_t cmd)
{
    uint8_t buf[2] = {
        (uint8_t)(cmd >> 8),
        (uint8_t)(cmd & 0xFF)
    };
    return (dev->i2c_write(dev->i2c_addr, buf, 2) == 0) ? SHT3X_OK : SHT3X_ERR_I2C;
}

static SHT3x_Status _sht3x_send_cmd_with_delay(SHT3x_Dev *dev, uint16_t cmd, uint32_t exec_ms){

}

/**
 * @brief Read @p n_words data words, each followed by a CRC byte.
 *
 * Reads (n_words × 3) bytes from the sensor and verifies the CRC of
 * each word before storing the decoded value in @p words.
 *
 * @param dev      Device handle.
 * @param words    Output buffer; must hold at least @p n_words elements.
 * @param n_words  Number of words to read (1 .. @ref SHT3X_MAX_WORDS).
 * @return         @ref SHT3X_OK, @ref SHT3X_ERR_I2C, @ref SHT3X_ERR_CRC,
 *                 or @ref SHT3X_ERR_PARAM if @p n_words is out of range.
 */
static SHT3x_Status _sht3x_read_words(SHT3x_Dev *dev, uint16_t *words, size_t n_words)
{
    if (n_words == 0u || n_words > SHT3X_MAX_WORDS) {
        return SHT3X_ERR_PARAM;
    }

    uint8_t buf[SHT3X_BUF_SIZE];

    if (dev->i2c_read(dev->i2c_addr, buf, n_words * 3u) != 0) {
        return SHT3X_ERR_I2C;
    }

    for (size_t i = 0; i < n_words; i++) {
        uint8_t *p = &buf[i * 3u];
        if (_sht3x_crc8(p, 2) != p[2]) {
            return SHT3X_ERR_CRC;
        }
        words[i] = ((uint16_t)p[0] << 8) | p[1];
    }

    return SHT3X_OK;
}

/**
 * @brief Select the single-shot command code for the given repeatability
 *        and clock-stretching setting.
 *
 * @param rep  Repeatability level.
 * @param cs   @c true to use clock-stretching variant.
 * @return     Corresponding command code.
 */
static uint16_t _sht3x_singleshot_cmd(SHT3x_Repeatability rep, bool cs)
{
    if (cs) {
        switch (rep) {
            case SHT3X_REPEAT_HIGH:   { return SHT3X_CMD_SINGLESHOT_CS_HIGH; }
            case SHT3X_REPEAT_MEDIUM: { return SHT3X_CMD_SINGLESHOT_CS_MEDIUM; }
            default:                  { return SHT3X_CMD_SINGLESHOT_CS_LOW; }
        }
    } else {
        switch (rep) {
            case SHT3X_REPEAT_HIGH:   { return SHT3X_CMD_SINGLESHOT_HIGH; }
            case SHT3X_REPEAT_MEDIUM: { return SHT3X_CMD_SINGLESHOT_MEDIUM; }
            default:                  { return SHT3X_CMD_SINGLESHOT_LOW; }
        }
    }
}

/**
 * @brief Select the periodic-mode command code for the given MPS and
 *        repeatability setting.
 *
 * @param mps  Measurement rate.
 * @param rep  Repeatability level.
 * @return     Corresponding command code.
 */
static uint16_t _sht3x_periodic_cmd(SHT3x_MPS mps, SHT3x_Repeatability rep)
{
    switch (mps) {
        case SHT3X_MPS_0_5:
            switch (rep) {
                case SHT3X_REPEAT_HIGH:   { return SHT3X_CMD_PERIODIC_0_5_HIGH; }
                case SHT3X_REPEAT_MEDIUM: { return SHT3X_CMD_PERIODIC_0_5_MEDIUM; }
                default:                  { return SHT3X_CMD_PERIODIC_0_5_LOW; }
            }
        case SHT3X_MPS_1:
            switch (rep) {
                case SHT3X_REPEAT_HIGH:   { return SHT3X_CMD_PERIODIC_1_HIGH; }
                case SHT3X_REPEAT_MEDIUM: { return SHT3X_CMD_PERIODIC_1_MEDIUM; }
                default:                  { return SHT3X_CMD_PERIODIC_1_LOW; }
            }
        case SHT3X_MPS_2:
            switch (rep) {
                case SHT3X_REPEAT_HIGH:   { return SHT3X_CMD_PERIODIC_2_HIGH; }
                case SHT3X_REPEAT_MEDIUM: { return SHT3X_CMD_PERIODIC_2_MEDIUM; }
                default:                  { return SHT3X_CMD_PERIODIC_2_LOW; }
            }
        case SHT3X_MPS_4:
            switch (rep) {
                case SHT3X_REPEAT_HIGH:   { return SHT3X_CMD_PERIODIC_4_HIGH; }
                case SHT3X_REPEAT_MEDIUM: { return SHT3X_CMD_PERIODIC_4_MEDIUM; }
                default:                  { return SHT3X_CMD_PERIODIC_4_LOW; }
            }
        default: /* SHT3X_MPS_10 */
            switch (rep) {
                case SHT3X_REPEAT_HIGH:   { return SHT3X_CMD_PERIODIC_10_HIGH; }
                case SHT3X_REPEAT_MEDIUM: { return SHT3X_CMD_PERIODIC_10_MEDIUM; }
                default:                  { return SHT3X_CMD_PERIODIC_10_LOW; }
            }
    }
}

/**
 * @brief Return the required measurement wait time for a given
 *        repeatability level (no-clock-stretch single-shot only).
 *
 * @param rep  Repeatability level.
 * @return     Delay in milliseconds.
 */
static uint32_t _sht3x_meas_delay_ms(SHT3x_Repeatability rep)
{
    switch (rep) {
        case SHT3X_REPEAT_HIGH:   { return SHT3X_EXEC_MEAS_HIGH_MS; }
        case SHT3X_REPEAT_MEDIUM: { return SHT3X_EXEC_MEAS_MEDIUM_MS; }
        default:                  { return SHT3X_EXEC_MEAS_LOW_MS; }
    }
}

/**
 * @brief Validate all fields of a device handle before use.
 *
 * Checks that function pointers are non-NULL, the I2C address is
 * one of the two valid values, and that mode / repeatability / MPS
 * enumerators are within their legal ranges.
 *
 * @param dev  Device handle to validate.
 * @return     @c true if the handle is fully valid, @c false otherwise.
 */
static bool _sht3x_is_valid_device(SHT3x_Dev *dev)
{
    if (!dev || !dev->i2c_write || !dev->i2c_read || !dev->delay_ms){
        return false;
    }
    if (dev->i2c_addr != SHT3X_I2C_ADDR_DEFAULT && dev->i2c_addr != SHT3X_I2C_ADDR_ALT){
        return false;
    }

    if (dev->mode != SHT3X_MODE_SINGLE_SHOT && dev->mode != SHT3X_MODE_PERIODIC){
        return false;
    }
    if (dev->repeatability != SHT3X_REPEAT_LOW && 
        dev->repeatability != SHT3X_REPEAT_MEDIUM && 
        dev->repeatability != SHT3X_REPEAT_HIGH){
        return false;
    }
    if (dev->mode == SHT3X_MODE_PERIODIC && dev->mps > SHT3X_MPS_10){
        return false;
    }
    return true;
}

/* ============================================================
 * Public API
 * ============================================================ */

SHT3x_Status SHT3x_Init(SHT3x_Dev *dev)
{
    if (!_sht3x_is_valid_device(dev)) {
        return SHT3X_ERR_PARAM;
    }
    SHT3x_Status st;

    if (dev->initialized && dev->periodic_running) {
        (void)_sht3x_send_cmd_with_delay(dev, SHT3X_CMD_BREAK, SHT3X_EXEC_BREAK_MS);
        dev->periodic_running = false;
    }
    dev->initialized = false;
    dev->delay_ms(SHT3X_EXEC_POWERUP_MS);

    st = _sht3x_send_cmd_with_delay(dev, SHT3X_CMD_SOFT_RESET, SHT3X_EXEC_SOFT_RESET_MS);
    if (st != SHT3X_OK){
        return st;
    }

    switch (dev->mode) {
        case SHT3X_MODE_PERIODIC: {
            st = _sht3x_send_cmd(dev, _sht3x_periodic_cmd(dev->mps, dev->repeatability));
            dev->periodic_running = (st == SHT3X_OK);
            break;
        }
        case SHT3X_MODE_SINGLE_SHOT: {
            /* No start command needed */
            st = SHT3X_OK;
            break;
        }
        default: {
            return SHT3X_ERR_PARAM;
        }
    }

    if (st == SHT3X_OK) {
        dev->initialized = true;
    }
    return st;
}

SHT3x_Status SHT3x_Read(SHT3x_Dev *dev, SHT3x_Data *out)
{
    if (!dev || !out) {
        return SHT3X_ERR_PARAM;
    }
    if (!dev->initialized) {
        return SHT3X_ERR_NOT_INIT;
    }

    SHT3x_Status ret;

    switch (dev->mode) {
        case SHT3X_MODE_SINGLE_SHOT: {
            ret = _sht3x_send_cmd(dev, _sht3x_singleshot_cmd(dev->repeatability, dev->clock_stretch));
            if (ret != SHT3X_OK) {
                return ret;
            }

            if (!dev->clock_stretch) {
                dev->delay_ms(_sht3x_meas_delay_ms(dev->repeatability));
            }
            break;
        }
        case SHT3X_MODE_PERIODIC: {
            if (!dev->periodic_running) {
                return SHT3X_ERR_NOT_INIT;
            }

            ret = _sht3x_send_cmd(dev, SHT3X_CMD_FETCH_DATA);
            if (ret != SHT3X_OK) {
                return ret;
            }
            break;
        }
        default: {
            return SHT3X_ERR_PARAM;
        }
    }

    uint16_t words[SHT3X_MAX_WORDS]; /* [0]=Temperature, [1]=Humidity */
    ret = _sht3x_read_words(dev, words, 2u);
    if (ret != SHT3X_OK) return ret;

    out->temperature_c = SHT3X_TEMP_C(words[0]);
    out->humidity_rh   = SHT3X_HUMID_RH(words[1]);

    return SHT3X_OK;
}

SHT3x_Status SHT3x_Deinit(SHT3x_Dev *dev)
{
    if (!dev) {
        return SHT3X_ERR_PARAM;
    }

    SHT3x_Status ret = SHT3X_OK;

    /* FIX #3: check mode as well as periodic_running to decide whether a
       BREAK command is needed, making the condition self-consistent. */
    if (dev->initialized
        && dev->mode == SHT3X_MODE_PERIODIC
        && dev->periodic_running)
    {
        ret = _sht3x_send_cmd(dev, SHT3X_CMD_BREAK);
        dev->delay_ms(SHT3X_EXEC_BREAK_MS);
    }

    dev->initialized      = false;
    dev->periodic_running = false;
    return ret;
}

SHT3x_Status SHT3x_HeaterEnable(SHT3x_Dev *dev, bool enable)
{
    if (!dev) return SHT3X_ERR_PARAM;
    if (!dev->initialized) return SHT3X_ERR_NOT_INIT;

    SHT3x_Status ret = _sht3x_send_cmd(dev, enable ? SHT3X_CMD_HEATER_EN : SHT3X_CMD_HEATER_DIS);
    dev->delay_ms(SHT3X_EXEC_HEATER_MS);
    return ret;
}

SHT3x_Status SHT3x_ReadStatus(SHT3x_Dev *dev, uint16_t *status)
{
    if (!dev || !status) return SHT3X_ERR_PARAM;
    if (!dev->initialized) return SHT3X_ERR_NOT_INIT;

    SHT3x_Status ret = _sht3x_send_cmd(dev, SHT3X_CMD_READ_STATUS);
    if (ret != SHT3X_OK) return ret;
    dev->delay_ms(SHT3X_EXEC_READ_STATUS_MS);

    uint16_t words[1];
    ret = _sht3x_read_words(dev, words, 1u);
    if (ret != SHT3X_OK) return ret;

    *status = words[0];
    return SHT3X_OK;
}

SHT3x_Status SHT3x_ClearStatus(SHT3x_Dev *dev)
{
    if (!dev) {
        return SHT3X_ERR_PARAM;
    }
    if (!dev->initialized) {
        return SHT3X_ERR_NOT_INIT;
    }
    return _sht3x_send_cmd(dev, SHT3X_CMD_CLEAR_STATUS);
}


#ifdef SHT3X_V2

/**
 * @file    sht3x.c
 * @brief   Bare-metal driver implementation for Sensirion SHT3x-DIS
 * @version 2.0
 *
 * Datasheet: SHT3x-DIS, August 2016 – Version 3
 */

#include "sht3x.h"

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/**
 * @brief  Transmit a 16-bit command (MSB first) to the sensor.
 */
static sht3x_err_t _sht3x_send_cmd(const sht3x_t *dev, uint16_t cmd)
{
    uint8_t buf[2] = {
        (uint8_t)(cmd >> 8),
        (uint8_t)(cmd & 0xFFu)
    };
    return (sht3x_hal_i2c_write(dev->i2c_addr, buf, 2) == 0)
           ? SHT3X_OK : SHT3X_ERR_I2C;
}

/**
 * @brief  Decode and CRC-verify 6 raw bytes into sht3x_raw_t.
 *
 * Byte layout (Section 4.4):
 *   [T_MSB][T_LSB][T_CRC][RH_MSB][RH_LSB][RH_CRC]
 */
static sht3x_err_t sht3x_parse_raw(const uint8_t *buf, sht3x_raw_t *raw)
{
    if (sht3x_crc8(&buf[0], 2) != buf[2]) return SHT3X_ERR_CRC;
    if (sht3x_crc8(&buf[3], 2) != buf[5]) return SHT3X_ERR_CRC;

    raw->raw_temperature = ((uint16_t)buf[0] << 8) | buf[1];
    raw->raw_humidity    = ((uint16_t)buf[3] << 8) | buf[4];
    return SHT3X_OK;
}

/**
 * @brief  Worst-case measurement delay for a repeatability level (ms).
 */
static uint32_t sht3x_meas_delay(sht3x_repeat_t r)
{
    switch (r) {
        case SHT3X_REPEAT_HIGH:   return SHT3X_T_MEAS_HIGH_MS;
        case SHT3X_REPEAT_MEDIUM: return SHT3X_T_MEAS_MED_MS;
        default:                  return SHT3X_T_MEAS_LOW_MS;
    }
}

/**
 * @brief  Select the correct no-stretch single-shot command.
 */
static uint16_t sht3x_ss_poll_cmd(sht3x_repeat_t r)
{
    switch (r) {
        case SHT3X_REPEAT_HIGH:   return SHT3X_CMD_SS_POLL_HIGH;
        case SHT3X_REPEAT_MEDIUM: return SHT3X_CMD_SS_POLL_MED;
        default:                  return SHT3X_CMD_SS_POLL_LOW;
    }
}

/**
 * @brief  Select the correct clock-stretch single-shot command.
 */
static uint16_t sht3x_ss_stretch_cmd(sht3x_repeat_t r)
{
    switch (r) {
        case SHT3X_REPEAT_HIGH:   return SHT3X_CMD_SS_STRETCH_HIGH;
        case SHT3X_REPEAT_MEDIUM: return SHT3X_CMD_SS_STRETCH_MED;
        default:                  return SHT3X_CMD_SS_STRETCH_LOW;
    }
}

/* =========================================================================
 * CRC-8
 * Polynomial : 0x31  (x^8 + x^5 + x^4 + 1)
 * Init       : 0xFF
 * No input/output reflection, final XOR = 0x00
 * Verification: CRC({0xBE, 0xEF}) == 0x92  (Table 19)
 * ========================================================================= */
uint8_t sht3x_crc8(const uint8_t *buf, uint8_t len)
{
    uint8_t crc = SHT3X_CRC_INIT;
    uint8_t i, b;
    for (i = 0; i < len; i++) {
        crc ^= buf[i];
        for (b = 0; b < 8; b++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ SHT3X_CRC_POLY)
                                : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* =========================================================================
 * Unit Conversion  (Section 4.13)
 *
 *   T[°C] = -45 + 175 × S_T  / (2^16 − 1)
 *   T[°F] = -49 + 315 × S_T  / (2^16 − 1)
 *   RH    = 100 × S_RH / (2^16 − 1)
 * ========================================================================= */
void sht3x_convert(const sht3x_raw_t *raw, sht3x_data_t *data)
{
    const float inv = 1.0f / 65535.0f;
    data->temperature_c = -45.0f + 175.0f * (float)raw->raw_temperature * inv;
    data->temperature_f = -49.0f + 315.0f * (float)raw->raw_temperature * inv;
    data->humidity_rh   = 100.0f          * (float)raw->raw_humidity    * inv;
}

/* =========================================================================
 * Initialisation
 * ========================================================================= */
sht3x_err_t sht3x_init(sht3x_t *dev, uint8_t i2c_addr)
{
    if (!dev) return SHT3X_ERR_PARAM;
    if (i2c_addr != SHT3X_I2C_ADDR_A &&
        i2c_addr != SHT3X_I2C_ADDR_B) return SHT3X_ERR_PARAM;

    dev->i2c_addr      = i2c_addr;
    dev->periodic_mode = false;

    sht3x_hal_delay_ms(SHT3X_T_PU_MS);

    /* Probe by reading the status register */
    uint16_t status;
    return sht3x_read_status(dev, &status);
}

/* =========================================================================
 * Single-Shot – POLLING (clock stretching DISABLED)
 *
 * Timing diagram (Section 4.4, no-stretch path):
 *
 *  Master: [CMD MSB][CMD LSB]  ···delay···  [ADDR+R]
 *  Sensor:                                  [ACK] [T_MSB][T_LSB][T_CRC]
 *                                                  [RH_MSB][RH_LSB][RH_CRC]
 *
 * If the sensor is still converting when the master sends ADDR+R it will
 * NACK the address byte.  The driver retries up to SHT3X_POLL_RETRIES
 * times with a 1 ms interval before giving up.
 * ========================================================================= */
sht3x_err_t sht3x_single_shot_poll_raw(sht3x_t       *dev,
                                        sht3x_repeat_t repeat,
                                        sht3x_raw_t   *raw)
{
    uint8_t     buf[6];
    uint8_t     retries;
    sht3x_err_t err;

    if (!dev || !raw) return SHT3X_ERR_PARAM;

    /* 1. Send measurement command (no clock stretching) */
    err = _sht3x_send_cmd(dev, sht3x_ss_poll_cmd(repeat));
    if (err != SHT3X_OK) return err;

    /* 2. Wait worst-case measurement time for chosen repeatability */
    sht3x_hal_delay_ms(sht3x_meas_delay(repeat));

    /*
     * 3. Poll: attempt to read; if sensor NACKs (data not ready yet)
     *    wait 1 ms and retry.
     */
    for (retries = 0; retries < SHT3X_POLL_RETRIES; retries++) {
        if (sht3x_hal_i2c_read(dev->i2c_addr, buf, 6) == 0) {
            /* Read succeeded – verify CRC and unpack */
            return sht3x_parse_raw(buf, raw);
        }
        sht3x_hal_delay_ms(1u);
    }

    return SHT3X_ERR_TIMEOUT;
}

/* =========================================================================
 * Single-Shot – CLOCK STRETCHING ENABLED
 *
 * Timing diagram (Section 4.4, clock-stretch path):
 *
 *  Master: [CMD MSB][CMD LSB]  [ADDR+R]  ←SCL held low by sensor→  [data clocked out]
 *  Sensor:                     [ACK]     ···measuring···  [release SCL]
 *                                        [T_MSB][T_LSB][T_CRC][RH_MSB][RH_LSB][RH_CRC]
 *
 * The master issues the read header immediately after the command STOP
 * (or repeated-START).  The sensor ACKs, then pulls SCL low (stretches)
 * until measurement completes, then releases SCL and clocks out data.
 *
 * The HAL function sht3x_hal_i2c_read_stretch() must handle SCL being
 * held low for up to SHT3X_T_MEAS_HIGH_MS (15 ms).
 * ========================================================================= */
sht3x_err_t sht3x_single_shot_stretch_raw(sht3x_t       *dev,
                                           sht3x_repeat_t repeat,
                                           sht3x_raw_t   *raw)
{
    uint8_t     buf[6];
    sht3x_err_t err;

    if (!dev || !raw) return SHT3X_ERR_PARAM;

    /* 1. Send clock-stretch measurement command */
    err = _sht3x_send_cmd(dev, sht3x_ss_stretch_cmd(repeat));
    if (err != SHT3X_OK) return err;

    /*
     * 2. Immediately read back.  The HAL must block until SCL is released
     *    by the sensor (up to SHT3X_T_MEAS_HIGH_MS).
     *    No explicit delay here – timing is managed by the sensor itself.
     */
    if (sht3x_hal_i2c_read_stretch(dev->i2c_addr, buf, 6) != 0) {
        return SHT3X_ERR_TIMEOUT;   /* HAL timed out waiting for SCL release */
    }

    /* 3. Verify CRC and unpack */
    return sht3x_parse_raw(buf, raw);
}

/* =========================================================================
 * Single-Shot – convenience wrappers (converted output)
 * ========================================================================= */
sht3x_err_t sht3x_single_shot_poll(sht3x_t       *dev,
                                    sht3x_repeat_t repeat,
                                    sht3x_data_t  *data)
{
    sht3x_raw_t raw;
    sht3x_err_t err;
    if (!data) return SHT3X_ERR_PARAM;
    err = sht3x_single_shot_poll_raw(dev, repeat, &raw);
    if (err == SHT3X_OK) sht3x_convert(&raw, data);
    return err;
}

sht3x_err_t sht3x_single_shot_stretch(sht3x_t       *dev,
                                       sht3x_repeat_t repeat,
                                       sht3x_data_t  *data)
{
    sht3x_raw_t raw;
    sht3x_err_t err;
    if (!data) return SHT3X_ERR_PARAM;
    err = sht3x_single_shot_stretch_raw(dev, repeat, &raw);
    if (err == SHT3X_OK) sht3x_convert(&raw, data);
    return err;
}

/** Unified entry point – dispatches based on stretch parameter */
sht3x_err_t sht3x_single_shot(sht3x_t            *dev,
                               sht3x_repeat_t      repeat,
                               sht3x_clk_stretch_t stretch,
                               sht3x_data_t       *data)
{
    return (stretch == SHT3X_CLK_STRETCH_ENABLE)
           ? sht3x_single_shot_stretch(dev, repeat, data)
           : sht3x_single_shot_poll   (dev, repeat, data);
}

/* =========================================================================
 * Periodic Mode
 * ========================================================================= */
sht3x_err_t sht3x_start_periodic(sht3x_t *dev, uint16_t cmd)
{
    sht3x_err_t err;
    if (!dev) return SHT3X_ERR_PARAM;

    /*
     * If already in periodic mode, issue Break first so the sensor can
     * accept the new command cleanly (Section 4.8).
     */
    if (dev->periodic_mode) {
        err = sht3x_stop_periodic(dev);
        if (err != SHT3X_OK) return err;
    }

    err = _sht3x_send_cmd(dev, cmd);
    if (err == SHT3X_OK) dev->periodic_mode = true;
    return err;
}

sht3x_err_t sht3x_fetch_periodic(sht3x_t *dev, sht3x_data_t *data)
{
    /*
     * Periodic fetch flow (Section 4.6):
     *   Master: [FETCH_DATA MSB][FETCH_DATA LSB]  [ADDR+R]
     *   Sensor: if data present  → ACK + 6 bytes
     *           if data absent   → NACK (communication stops)
     *
     * Clock stretching is NOT available in periodic mode.
     */
    uint8_t     buf[6];
    sht3x_raw_t raw;
    sht3x_err_t err;

    if (!dev || !data) return SHT3X_ERR_PARAM;
    if (!dev->periodic_mode) return SHT3X_ERR_PARAM;

    err = _sht3x_send_cmd(dev, SHT3X_CMD_FETCH_DATA);
    if (err != SHT3X_OK) return err;

    /*
     * If no measurement data is ready the sensor NACKs the read header
     * → HAL returns non-zero → we surface this as TIMEOUT.
     */
    if (sht3x_hal_i2c_read(dev->i2c_addr, buf, 6) != 0) {
        return SHT3X_ERR_TIMEOUT;
    }

    err = sht3x_parse_raw(buf, &raw);
    if (err == SHT3X_OK) sht3x_convert(&raw, data);
    return err;
}

sht3x_err_t sht3x_stop_periodic(sht3x_t *dev)
{
    sht3x_err_t err;
    if (!dev) return SHT3X_ERR_PARAM;

    err = _sht3x_send_cmd(dev, SHT3X_CMD_BREAK);
    if (err == SHT3X_OK) {
        /*
         * Sensor finishes its ongoing measurement before entering idle.
         * Worst-case = high-repeatability measurement time (Section 4.8).
         */
        sht3x_hal_delay_ms(SHT3X_T_MEAS_HIGH_MS);
        dev->periodic_mode = false;
    }
    return err;
}

/* =========================================================================
 * Reset
 * ========================================================================= */
sht3x_err_t sht3x_soft_reset(sht3x_t *dev)
{
    sht3x_err_t err;
    if (!dev) return SHT3X_ERR_PARAM;

    err = _sht3x_send_cmd(dev, SHT3X_CMD_SOFT_RESET);
    if (err != SHT3X_OK) return err;

    dev->periodic_mode = false;
    sht3x_hal_delay_ms(SHT3X_T_SR_MS);
    return SHT3X_OK;
}

sht3x_err_t sht3x_general_call_reset(sht3x_t *dev)
{
    /*
     * Two-byte sequence to the reserved general-call address 0x00:
     *   byte 1 = 0x06 (reset command)
     * Resets ALL devices on the bus that support general call (Table 14).
     */
    uint8_t payload[1] = { SHT3X_GENERAL_CALL_RESET };
    if (!dev) return SHT3X_ERR_PARAM;

    if (sht3x_hal_i2c_write(SHT3X_GENERAL_CALL_ADDR, payload, 1) != 0) {
        return SHT3X_ERR_I2C;
    }
    dev->periodic_mode = false;
    sht3x_hal_delay_ms(SHT3X_T_SR_MS);
    return SHT3X_OK;
}

/* =========================================================================
 * Status Register
 * ========================================================================= */
sht3x_err_t sht3x_read_status(sht3x_t *dev, uint16_t *status)
{
    /*
     * Response: [STATUS_MSB][STATUS_LSB][CRC]  (Table 16)
     */
    uint8_t     buf[3];
    sht3x_err_t err;

    if (!dev || !status) return SHT3X_ERR_PARAM;

    err = _sht3x_send_cmd(dev, SHT3X_CMD_READ_STATUS);
    if (err != SHT3X_OK) return err;

    if (sht3x_hal_i2c_read(dev->i2c_addr, buf, 3) != 0) return SHT3X_ERR_I2C;
    if (sht3x_crc8(&buf[0], 2) != buf[2])              return SHT3X_ERR_CRC;

    *status = ((uint16_t)buf[0] << 8) | buf[1];
    return SHT3X_OK;
}

sht3x_err_t sht3x_clear_status(sht3x_t *dev)
{
    if (!dev) return SHT3X_ERR_PARAM;
    return _sht3x_send_cmd(dev, SHT3X_CMD_CLEAR_STATUS);
}

/* =========================================================================
 * Heater
 * ========================================================================= */
sht3x_err_t sht3x_set_heater(sht3x_t *dev, bool enable)
{
    if (!dev) return SHT3X_ERR_PARAM;
    return _sht3x_send_cmd(dev, enable ? SHT3X_CMD_HEATER_ON
                                      : SHT3X_CMD_HEATER_OFF);
}

#endif