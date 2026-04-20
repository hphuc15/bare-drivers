/**
 * @file    sht3x.c
 * @brief   Bare-metal driver implementation for Sensirion SHT3x-DIS.
 * @version 1.0
 *
 * @details Datasheet: SHT3x-DIS, August 2016 – Version 3.
 */

#include "sht3x.h"
#include "sht3x_defs.h"

/* =========================================================================
 * @defgroup SHT3X_PRIVATE_DEFS Private Constants
 * @brief    Internal buffer sizing constants, not exposed in the public API.
 * @{
 * ========================================================================= */

#define SHT3X_MAX_WORDS  2u                         /**< Max data words per read transaction         */
#define SHT3X_BUF_SIZE   (SHT3X_MAX_WORDS * 3u)     /**< Raw byte buffer size: 2 bytes + 1 CRC/word  */

/** @} */ /* end group SHT3X_PRIVATE_DEFS */

/* =========================================================================
 * @defgroup SHT3X_PRIVATE Private Helper Functions
 * @brief    Internal functions not exposed through the public API.
 * @{
 * ========================================================================= */

/**
 * @brief   Compute CRC-8 checksum over a byte array.
 * @details Uses polynomial 0x31 (x^8 + x^5 + x^4 + 1) with initial value
 *          0xFF, as specified in datasheet Table 19.
 *
 * @param[in] data  Pointer to input byte array.
 * @param[in] len   Number of bytes to process.
 * @return          Computed CRC-8 value.
 */
static uint8_t _sht3x_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = SHT3X_CRC_INIT;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80u) ? ((uint8_t)(crc << 1) ^ SHT3X_CRC_POLY) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/**
 * @brief   Send a 16-bit command word over I2C (MSB first).
 *
 * @param[in] dev  Pointer to device handle.
 * @param[in] cmd  16-bit command code to transmit.
 * @return         @ref SHT3X_OK on success, @ref SHT3X_ERR_I2C on bus failure.
 */
static SHT3x_Status _sht3x_send_cmd(SHT3x_Dev *dev, uint16_t cmd)
{
    uint8_t buf[2] = {
        (uint8_t)(cmd >> 8),
        (uint8_t)(cmd & 0xFF)
    };
    return (dev->i2c_write(dev->i2c_addr, buf, 2) == 0) ? SHT3X_OK : SHT3X_ERR_I2C;
}

/**
 * @brief   Send a 16-bit command word and block for a fixed execution delay.
 * @details Combines @ref _sht3x_send_cmd with a mandatory post-command wait.
 *          The delay is skipped if the command fails.
 *
 * @param[in] dev      Pointer to device handle.
 * @param[in] cmd      16-bit command code to transmit.
 * @param[in] exec_ms  Execution time to wait after a successful send (ms).
 * @return             @ref SHT3X_OK on success, @ref SHT3X_ERR_I2C on bus failure.
 */
static SHT3x_Status _sht3x_send_cmd_with_delay(SHT3x_Dev *dev, uint16_t cmd, uint32_t exec_ms)
{
    SHT3x_Status st = _sht3x_send_cmd(dev, cmd);
    if (st == SHT3X_OK) {
        dev->delay_ms(exec_ms);
    }
    return st;
}

/**
 * @brief   Read @p n_words data words from the sensor, each followed by a
 *          CRC-8 byte, and verify integrity before storing.
 * @details Reads <tt>n_words × 3</tt> raw bytes, verifies the CRC of each
 *          16-bit word, and stores the decoded values in @p words.
 *
 * @param[in]  dev      Pointer to device handle.
 * @param[out] words    Output buffer; must hold at least @p n_words elements.
 * @param[in]  n_words  Number of words to read (1 .. @ref SHT3X_MAX_WORDS).
 * @return              @ref SHT3X_OK on success.
 * @retval  SHT3X_ERR_PARAM  if @p n_words is 0 or exceeds @ref SHT3X_MAX_WORDS.
 * @retval  SHT3X_ERR_I2C    if the I2C read transaction fails.
 * @retval  SHT3X_ERR_CRC    if any word fails its CRC check.
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
 * @brief   Resolve the single-shot command code for the requested
 *          repeatability and clock-stretching configuration.
 *
 * @param[in] rep  Repeatability level (@ref SHT3x_Repeatability).
 * @param[in] cs   @c true to select the clock-stretching variant.
 * @return         16-bit command code corresponding to the requested settings.
 */
static uint16_t _sht3x_singleshot_cmd(SHT3x_Repeatability rep, bool cs)
{
    if (cs) {
        switch (rep) {
            case SHT3X_REPEAT_HIGH:   return SHT3X_CMD_SINGLESHOT_CS_HIGH;
            case SHT3X_REPEAT_MEDIUM: return SHT3X_CMD_SINGLESHOT_CS_MED;
            default:                  return SHT3X_CMD_SINGLESHOT_CS_LOW;
        }
    } else {
        switch (rep) {
            case SHT3X_REPEAT_HIGH:   return SHT3X_CMD_SINGLESHOT_HIGH;
            case SHT3X_REPEAT_MEDIUM: return SHT3X_CMD_SINGLESHOT_MED;
            default:                  return SHT3X_CMD_SINGLESHOT_LOW;
        }
    }
}

/**
 * @brief   Resolve the periodic-mode command code for the requested
 *          measurement rate and repeatability.
 *
 * @param[in] mps  Measurement rate (@ref SHT3x_MPS).
 * @param[in] rep  Repeatability level (@ref SHT3x_Repeatability).
 * @return         16-bit command code corresponding to the requested settings.
 */
static uint16_t _sht3x_periodic_cmd(SHT3x_MPS mps, SHT3x_Repeatability rep)
{
    switch (mps) {
        case SHT3X_MPS_05:
            switch (rep) {
                case SHT3X_REPEAT_HIGH:   return SHT3X_CMD_PERIODIC_05_HIGH;
                case SHT3X_REPEAT_MEDIUM: return SHT3X_CMD_PERIODIC_05_MED;
                default:                  return SHT3X_CMD_PERIODIC_05_LOW;
            }
        case SHT3X_MPS_1:
            switch (rep) {
                case SHT3X_REPEAT_HIGH:   return SHT3X_CMD_PERIODIC_1_HIGH;
                case SHT3X_REPEAT_MEDIUM: return SHT3X_CMD_PERIODIC_1_MED;
                default:                  return SHT3X_CMD_PERIODIC_1_LOW;
            }
        case SHT3X_MPS_2:
            switch (rep) {
                case SHT3X_REPEAT_HIGH:   return SHT3X_CMD_PERIODIC_2_HIGH;
                case SHT3X_REPEAT_MEDIUM: return SHT3X_CMD_PERIODIC_2_MED;
                default:                  return SHT3X_CMD_PERIODIC_2_LOW;
            }
        case SHT3X_MPS_4:
            switch (rep) {
                case SHT3X_REPEAT_HIGH:   return SHT3X_CMD_PERIODIC_4_HIGH;
                case SHT3X_REPEAT_MEDIUM: return SHT3X_CMD_PERIODIC_4_MED;
                default:                  return SHT3X_CMD_PERIODIC_4_LOW;
            }
        default: /* SHT3X_MPS_10 */
            switch (rep) {
                case SHT3X_REPEAT_HIGH:   return SHT3X_CMD_PERIODIC_10_HIGH;
                case SHT3X_REPEAT_MEDIUM: return SHT3X_CMD_PERIODIC_10_MED;
                default:                  return SHT3X_CMD_PERIODIC_10_LOW;
            }
    }
}

/**
 * @brief   Return the required post-trigger measurement wait time for
 *          no-clock-stretch single-shot mode.
 *
 * @param[in] rep  Repeatability level (@ref SHT3x_Repeatability).
 * @return         Required delay in milliseconds.
 */
static uint32_t _sht3x_meas_delay_ms(SHT3x_Repeatability rep)
{
    switch (rep) {
        case SHT3X_REPEAT_HIGH:   return SHT3X_EXEC_MEAS_HIGH_MS;
        case SHT3X_REPEAT_MEDIUM: return SHT3X_EXEC_MEAS_MED_MS;
        default:                  return SHT3X_EXEC_MEAS_LOW_MS;
    }
}

/**
 * @brief   Validate all fields of a device handle before use.
 * @details Checks that: HAL function pointers are non-NULL; the I2C address
 *          is one of the two valid values; and the mode, repeatability, and
 *          MPS enumerators are within their legal ranges.
 *
 * @param[in] dev  Pointer to the device handle to validate.
 * @return         @c true if the handle is fully valid, @c false otherwise.
 */
static bool _sht3x_is_valid_device(SHT3x_Dev *dev)
{
    if (!dev || !dev->i2c_write || !dev->i2c_read || !dev->delay_ms) {
        return false;
    }
    if (dev->i2c_addr != SHT3X_I2C_ADDR_DEFAULT && dev->i2c_addr != SHT3X_I2C_ADDR_ALT) {
        return false;
    }
    if (dev->mode != SHT3X_MODE_SINGLE_SHOT && dev->mode != SHT3X_MODE_PERIODIC) {
        return false;
    }
    if (dev->repeatability != SHT3X_REPEAT_LOW &&
        dev->repeatability != SHT3X_REPEAT_MEDIUM &&
        dev->repeatability != SHT3X_REPEAT_HIGH) {
        return false;
    }
    if (dev->mode == SHT3X_MODE_PERIODIC && dev->mps > SHT3X_MPS_10) {
        return false;
    }
    return true;
}

/** @} */ /* end group SHT3X_PRIVATE */

/* =========================================================================
 * @defgroup SHT3X_PUBLIC Public API
 * @brief    Functions exposed through sht3x.h for application use.
 * @{
 * ========================================================================= */

/**
 * @brief   Initialize the SHT3x device and start the selected operating mode.
 * @details Performs the following sequence:
 *          -# Validates the device handle with @ref _sht3x_is_valid_device.
 *          -# Issues a BREAK command if the device was previously running in
 *             periodic mode, then waits @ref SHT3X_EXEC_BREAK_MS.
 *          -# Waits for power-up (@ref SHT3X_EXEC_POWERUP_MS).
 *          -# Sends a soft reset and waits @ref SHT3X_EXEC_SOFT_RESET_MS.
 *          -# For @ref SHT3X_MODE_PERIODIC, sends the appropriate periodic
 *             command and sets @c dev->periodic_running.
 *          -# For @ref SHT3X_MODE_SINGLE_SHOT, no start command is required.
 *          -# Sets @c dev->initialized = @c true on success.
 *
 * @param[in,out] dev  Pointer to a fully populated @ref SHT3x_Dev handle.
 * @return             @ref SHT3X_OK on success.
 * @retval  SHT3X_ERR_PARAM  if the handle fails validation.
 * @retval  SHT3X_ERR_I2C    if any I2C transaction fails.
 */
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
    if (st != SHT3X_OK) {
        return st;
    }

    switch (dev->mode) {
        case SHT3X_MODE_PERIODIC: {
            st = _sht3x_send_cmd(dev, _sht3x_periodic_cmd(dev->mps, dev->repeatability));
            dev->periodic_running = (st == SHT3X_OK);
            break;
        }
        case SHT3X_MODE_SINGLE_SHOT: {
            st = SHT3X_OK; /* No start command required */
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

/**
 * @brief   Trigger or fetch a temperature and humidity measurement.
 * @details Behaviour depends on the configured operating mode:
 *          - **Single-shot:** sends the appropriate measurement command, then
 *            waits for the conversion delay if clock-stretching is disabled.
 *          - **Periodic:** sends FETCH_DATA to retrieve the last completed
 *            measurement from the sensor's internal buffer.
 *          In both cases, two data words are read and CRC-verified before
 *          populating @p out.
 *
 * @param[in]  dev  Pointer to an initialised @ref SHT3x_Dev handle.
 * @param[out] out  Pointer to a @ref SHT3x_Data struct to receive the result.
 * @return          @ref SHT3X_OK on success.
 * @retval  SHT3X_ERR_PARAM     if @p dev or @p out is NULL.
 * @retval  SHT3X_ERR_NOT_INIT  if the device has not been initialised, or
 *                               periodic mode is not running.
 * @retval  SHT3X_ERR_I2C       if any I2C transaction fails.
 * @retval  SHT3X_ERR_CRC       if data integrity verification fails.
 */
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

    uint16_t words[SHT3X_MAX_WORDS]; /* [0] = Temperature raw, [1] = Humidity raw */
    ret = _sht3x_read_words(dev, words, 2u);
    if (ret != SHT3X_OK) {
        return ret;
    }

    out->temperature_c = SHT3X_TEMP_C(words[0]);
    out->humidity_rh   = SHT3X_HUMID_RH(words[1]);

    return SHT3X_OK;
}

/**
 * @brief   Stop periodic measurement and reset the device state.
 * @details If the device is initialised and running in periodic mode, sends
 *          a BREAK command and waits @ref SHT3X_EXEC_BREAK_MS before clearing
 *          internal flags. Safe to call even if the device is not initialised.
 *
 * @param[in,out] dev  Pointer to the @ref SHT3x_Dev handle to de-initialise.
 * @return             @ref SHT3X_OK on success.
 * @retval  SHT3X_ERR_PARAM  if @p dev is NULL.
 * @retval  SHT3X_ERR_I2C    if the BREAK command fails.
 */
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

/**
 * @brief   Enable or disable the on-chip heater.
 * @details The heater is intended for testing and diagnostic purposes only.
 *
 * @param[in,out] dev     Pointer to an initialised @ref SHT3x_Dev handle.
 * @param[in]     enable  @c true to enable the heater, @c false to disable it.
 * @return                @ref SHT3X_OK on success.
 * @retval  SHT3X_ERR_PARAM     if @p dev is NULL.
 * @retval  SHT3X_ERR_NOT_INIT  if the device has not been initialised.
 * @retval  SHT3X_ERR_I2C       if the I2C command fails.
 */
SHT3x_Status SHT3x_HeaterEnable(SHT3x_Dev *dev, bool enable)
{
    if (!dev) {
        return SHT3X_ERR_PARAM;
    }
    if (!dev->initialized) {
        return SHT3X_ERR_NOT_INIT;
    }
    return _sht3x_send_cmd(dev, enable ? SHT3X_CMD_HEATER_ON : SHT3X_CMD_HEATER_OFF);
}

/**
 * @brief   Read the 16-bit device status register.
 * @details Sends @ref SHT3X_CMD_READ_STATUS, then reads one word with CRC
 *          verification. The raw register value is returned in @p status;
 *          use the @c SHT3X_SREG_* bitmasks from @ref sht3x_defs.h to decode
 *          individual flag bits.
 *
 * @param[in]  dev     Pointer to an initialised @ref SHT3x_Dev handle.
 * @param[out] status  Pointer to receive the 16-bit status register value.
 * @return             @ref SHT3X_OK on success.
 * @retval  SHT3X_ERR_PARAM     if @p dev or @p status is NULL.
 * @retval  SHT3X_ERR_NOT_INIT  if the device has not been initialised.
 * @retval  SHT3X_ERR_I2C       if any I2C transaction fails.
 * @retval  SHT3X_ERR_CRC       if the response fails CRC verification.
 */
SHT3x_Status SHT3x_ReadStatus(SHT3x_Dev *dev, uint16_t *status)
{
    if (!dev || !status) {
        return SHT3X_ERR_PARAM;
    }
    if (!dev->initialized) {
        return SHT3X_ERR_NOT_INIT;
    }

    SHT3x_Status ret = _sht3x_send_cmd(dev, SHT3X_CMD_READ_STATUS);
    if (ret != SHT3X_OK) {
        return ret;
    }

    uint16_t words[1];
    ret = _sht3x_read_words(dev, words, 1u);
    if (ret != SHT3X_OK) {
        return ret;
    }

    *status = words[0];
    return SHT3X_OK;
}

/**
 * @brief   Clear all alert and error flags in the device status register.
 * @details Sends @ref SHT3X_CMD_CLEAR_STATUS. No read-back is performed;
 *          call @ref SHT3x_ReadStatus to verify if needed.
 *
 * @param[in,out] dev  Pointer to an initialised @ref SHT3x_Dev handle.
 * @return             @ref SHT3X_OK on success.
 * @retval  SHT3X_ERR_PARAM     if @p dev is NULL.
 * @retval  SHT3X_ERR_NOT_INIT  if the device has not been initialised.
 * @retval  SHT3X_ERR_I2C       if the I2C command fails.
 */
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

/** @} */ /* end group SHT3X_PUBLIC */