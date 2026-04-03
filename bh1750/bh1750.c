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

    uint8_t cmd;
    /* Power On first - Reset is not accepted in Power Down mode */
    cmd = BH1750_CMD_POWER_ON;
    if (dev->i2c_write(dev->i2c_addr, &cmd, 1) != 0)
    {
        return BH1750_ERR_I2C_WRITE;
    }
    /* Clear previous measurement result */
    cmd = BH1750_CMD_RESET;
    if (dev->i2c_write(dev->i2c_addr, &cmd, 1) != 0)
    {
        return BH1750_ERR_I2C_WRITE;
    }
    /* Start measurement - sensor begins measuring immediately */
    cmd = (uint8_t)mode;
    if (dev->i2c_write(dev->i2c_addr, &cmd, 1) != 0)
    {
        return BH1750_ERR_I2C_WRITE;
    }
    dev->mode = mode;
    /* Wait for first measurement to complete */
    uint32_t wait_ms = (mode == BH1750_MODE_CONT_L_RES ||
                        mode == BH1750_MODE_ONE_L_RES)
                           ? BH1750_DELAY_L_MS
                           : BH1750_DELAY_H_MS;
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
    /* One-time modes auto power down after measurement, re-trigger each read */
    if (dev->mode == BH1750_MODE_ONE_H_RES ||
        dev->mode == BH1750_MODE_ONE_H_RES2 ||
        dev->mode == BH1750_MODE_ONE_L_RES)
    {
        uint8_t cmd = (uint8_t)dev->mode;
        if (dev->i2c_write(dev->i2c_addr, &cmd, 1) != 0)
        {
            return BH1750_ERR_I2C_WRITE;
        }
        uint32_t wait_ms = (dev->mode == BH1750_MODE_ONE_L_RES) ? BH1750_DELAY_L_MS : BH1750_DELAY_H_MS;
        dev->delay_ms(wait_ms);
    }

    uint8_t buf[2] = {0};
    if (dev->i2c_read(dev->i2c_addr, buf, 2) != 0)
    {
        return BH1750_ERR_I2C_READ;
    }
    /* lux = raw / 1.2 (measurement accuracy factor) */
    uint16_t raw = (buf[0] << 8) | buf[1];
    *lux = raw / 1.2f;

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

    uint8_t cmd = BH1750_CMD_POWER_DOWN;
    return (dev->i2c_write(dev->i2c_addr, &cmd, 1) == 0) ? BH1750_OK : BH1750_ERR_I2C_WRITE;
}