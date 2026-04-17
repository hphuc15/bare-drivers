/**
 * @file    bh1750.c
 * @brief   Platform-independent driver for BH1750FVI ambient light sensor
 *
 * Communication: I2C
 * Datasheet: https://www.mouser.com/datasheet/2/348/bh1750fvi-e-186247.pdf
 */
#include "bh1750.h"
// #include <stddef.h>

#define BH1750_DELAY_H_MS 180U /* Max measurement time for H-Resolution modes */
#define BH1750_DELAY_L_MS 24U  /* Max measurement time for L-Resolution mode  */

static BH1750_Status write_cmd(BH1750_Dev *dev, uint8_t cmd)
{
    return (dev->i2c_write(dev->i2c_addr, &cmd, 1) == 0) ? BH1750_OK : BH1750_ERR_I2C_WRITE;
}

/**
 * @brief Compute the delay required after sending a measurement mode command
 *
 * Per the datasheet, measurement time scales linearly with MTreg:
 *   actual_time = base_time * mtreg / MTREG_DEFAULT
 * A +1 ms guard is added to account for rounding and I2C latency.
 *
 * @param mode   Measurement mode (determines base delay: 24 ms or 180 ms)
 * @param mtreg  Current MTreg value (31–254)
 * @return       Delay in milliseconds to wait before reading the data register
 */
static uint32_t calc_measurement_delay_ms(BH1750_Mode mode, uint8_t mtreg)
{
    uint32_t base = (mode == BH1750_MODE_CONT_L_RES ||
                     mode == BH1750_MODE_ONE_L_RES)
                        ? BH1750_DELAY_L_MS
                        : BH1750_DELAY_H_MS;

    return (uint32_t)(base * mtreg / BH1750_MTREG_DEFAULT) + 1U;
}

/**
 * @brief Convert raw 16-bit register value to lux
 *
 * Datasheet formula:
 *   H-res / L-res : lux = (raw/1.2)*(69/mtreg)
 *   H-res2        : lux = (raw/1.2)*(69/mtreg)/2
 *
 * @param raw    Raw value read from data register
 * @param mode   Current measurement mode
 * @param mtreg  Current MTreg value
 * @return       Illuminance in lux
 */
static float calc_lux(uint16_t raw, BH1750_Mode mode, uint8_t mtreg)
{
    float lux = ((float)raw / 1.2f) * ((float)BH1750_MTREG_DEFAULT / (float)mtreg);

    if (mode == BH1750_MODE_CONT_H_RES2 || mode == BH1750_MODE_ONE_H_RES2)
    {
        lux /= 2.0f;
    }

    return lux;
}

/* Public APIs */

/**
 * @brief Initialize BH1750 sensor
 * @param dev   Pointer to BH1750_Dev struct
 * @param mode  Measurement mode
 */
BH1750_Status BH1750_Init(BH1750_Dev *dev, BH1750_Mode mode)
{
    if (!dev || !dev->i2c_read || !dev->i2c_write || !dev->delay_ms)
    {
        return BH1750_ERR_NULL;
    }

    BH1750_Status err;

    /* Power On first - Reset is not accepted in Power Down mode */
    err = write_cmd(dev, BH1750_CMD_POWER_ON);
    if (err != BH1750_OK)
    {
        return err;
    }

    /* Clear previous measurement result */
    err = write_cmd(dev, BH1750_CMD_RESET);
    if (err != BH1750_OK)
    {
        return err;
    }

    /* Start measurement - sensor begins measuring immediately */
    err = write_cmd(dev, (uint8_t)mode);
    if (err != BH1750_OK)
    {
        return err;
    }
    dev->mode = mode;
    dev->mtreg = BH1750_MTREG_DEFAULT;

    /* Wait for first measurement to complete */
    uint32_t wait_ms = calc_measurement_delay_ms(dev->mode, dev->mtreg);
    dev->delay_ms(wait_ms);

    return BH1750_OK;
}

/**
 * @brief Read ambient light value
 * @param dev  Pointer to BH1750_Dev struct
 * @param lux  Output lux value
 */
BH1750_Status BH1750_ReadLux(BH1750_Dev *dev, float *lux)
{
    if (!dev || !lux)
    {
        return BH1750_ERR_NULL;
    }
    BH1750_Status err;
    /* One-time modes auto power down after measurement, re-trigger each read */
    if (dev->mode == BH1750_MODE_ONE_H_RES ||
        dev->mode == BH1750_MODE_ONE_H_RES2 ||
        dev->mode == BH1750_MODE_ONE_L_RES)
    {
        err = write_cmd(dev, (uint8_t)dev->mode);
        if (err != BH1750_OK)
        {
            return err;
        }

        dev->delay_ms(calc_measurement_delay_ms(dev->mode, dev->mtreg));
    }

    uint8_t buf[2] = {0};
    uint16_t raw;
    if (dev->i2c_read(dev->i2c_addr, buf, 2) != 0)
    {
        return BH1750_ERR_I2C_READ;
    }
    raw = (buf[0] << 8) | buf[1];

    /* Convert raw to lux value */
    *lux = calc_lux(raw, dev->mode, dev->mtreg);

    return BH1750_OK;
}

/**
 * @brief Put sensor into Power Down mode
 * @note  Call BH1750_Init() again to resume
 * @param dev  Pointer to BH1750_Dev struct
 */
BH1750_Status BH1750_PowerDown(BH1750_Dev *dev)
{
    if (!dev)
    {
        return BH1750_ERR_NULL;
    }

    return write_cmd(dev, BH1750_CMD_POWER_DOWN);
}

/**
 * @brief Set the MTreg (Measurement Time register) to adjust sensitivity
 *
 * Sensitivity scales linearly with MTreg relative to the default (69):
 *   factor = mtreg / 69
 * Useful for compensating optical filters or protective covers.
 * Values outside [BH1750_MTREG_MIN, BH1750_MTREG_MAX] are clamped.
 *
 * @param dev    Pointer to an initialised BH1750_Dev struct
 * @param mtreg  New MTreg value (31–254)
 * @return       BH1750_OK on success, error code otherwise
 */
BH1750_Status BH1750_SetMTreg(BH1750_Dev *dev, uint8_t mtreg)
{
    if (!dev)
    {
        return BH1750_ERR_NULL;
    }

    BH1750_Status err;

    if (mtreg < BH1750_MTREG_MIN)
    {
        mtreg = BH1750_MTREG_MIN;
    }
    else if (mtreg > BH1750_MTREG_MAX)
    {
        mtreg = BH1750_MTREG_MAX;
    }

    dev->mtreg = mtreg;

    uint8_t cmd;
    /* 3 steps to set mtreg follow the datasheet */

    /* Step 1: Changing High bit of MTreg */
    cmd = 0x40 | (dev->mtreg >> 5);
    err = write_cmd(dev, cmd);
    if (err != BH1750_OK)
    {
        return err;
    }

    /* Step 2: Changing Low bit of MTreg*/
    cmd = 0x60 | (dev->mtreg & 0x1F);
    err = write_cmd(dev, cmd);
    if (err != BH1750_OK)
    {
        return err;
    }
    
    /* Step 3: Input Measurement Command */
    cmd = (uint8_t)dev->mode;
    err = write_cmd(dev, cmd);
    if (err != BH1750_OK)
    {
        return err;
    }

    /* Wait for the first measurement at the new MTreg to complete */
    dev->delay_ms(calc_measurement_delay_ms(dev->mode, dev->mtreg));

    return BH1750_OK;
}