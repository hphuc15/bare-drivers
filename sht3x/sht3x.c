/**
 * @file    sht3x.c
 * @brief   Bare-metal driver implementation for Sensirion SHT3x-DIS
 * @version 2.0
 *
 * Datasheet: SHT3x-DIS, August 2016 – Version 3
 */
#include "sht3x.h"
#include "sht3x_defs.h"

#define CRC8_POLY  0x31u
#define CRC8_INIT  0xFFu

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
 * @return     SHT3X_OK, or SHT3X_ERR_I2C on bus failure.
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
    SHT3x_Status st = _sht3x_send_cmd(dev, cmd);
    if (st == SHT3X_OK) {
        dev->delay_ms(exec_ms);
    }
    return st;
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
        case SHT3X_REPEAT_HIGH:{
            return SHT3X_EXEC_MEAS_HIGH_MS;
        }
        case SHT3X_REPEAT_MEDIUM:{
            return SHT3X_EXEC_MEAS_MEDIUM_MS;
        }
        default:{
            return SHT3X_EXEC_MEAS_LOW_MS;
        }
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
    if (ret != SHT3X_OK){
        return ret;
    }
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
    if (!dev){
        return SHT3X_ERR_PARAM;
    }
    if (!dev->initialized){
        return SHT3X_ERR_NOT_INIT;
    }
    SHT3x_Status ret = _sht3x_send_cmd(dev, enable ? SHT3X_CMD_HEATER_EN : SHT3X_CMD_HEATER_DIS);
    dev->delay_ms(SHT3X_EXEC_HEATER_MS);
    return ret;
}

SHT3x_Status SHT3x_ReadStatus(SHT3x_Dev *dev, uint16_t *status)
{
    if (!dev || !status){
        return SHT3X_ERR_PARAM;
    }
    if (!dev->initialized){
        return SHT3X_ERR_NOT_INIT;
    }

    SHT3x_Status ret = _sht3x_send_cmd(dev, SHT3X_CMD_READ_STATUS);
    if (ret != SHT3X_OK){
        return ret;
    }
    dev->delay_ms(SHT3X_EXEC_READ_STATUS_MS);

    uint16_t words[1];
    ret = _sht3x_read_words(dev, words, 1u);
    if (ret != SHT3X_OK){
        return ret;
    }

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