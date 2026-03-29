/**
 * @file  bh1750.h
 * @brief BH1750FVI ambient light sensor driver
 */
#ifndef BH1750_H
#define BH1750_H

#include <stdint.h>

/* I2C slave address (7-bit), determined by ADDR pin */
#define BH1750_I2C_ADDR_LOW 0x23  /* ADDR = GND */
#define BH1750_I2C_ADDR_HIGH 0x5C /* ADDR = VCC */

/* Opcodes */
#define BH1750_CMD_POWER_DOWN 0x00
#define BH1750_CMD_POWER_ON 0x01
#define BH1750_CMD_RESET 0x07 /* Clears data register, requires Power On state */

/** Measurement mode */
typedef enum
{
    BH1750_MODE_CONT_H_RES = 0x10,  /* Continuous, 1 lx resolution,   ~180ms */
    BH1750_MODE_CONT_H_RES2 = 0x11, /* Continuous, 0.5 lx resolution, ~180ms */
    BH1750_MODE_CONT_L_RES = 0x13,  /* Continuous, 4 lx resolution,   ~24ms  */
    BH1750_MODE_ONE_H_RES = 0x20,   /* One-time,   1 lx resolution,   ~180ms */
    BH1750_MODE_ONE_H_RES2 = 0x21,  /* One-time,   0.5 lx resolution, ~180ms */
    BH1750_MODE_ONE_L_RES = 0x23,   /* One-time,   4 lx resolution,   ~24ms  */
} BH1750_Mode;

/** Return status */
typedef enum
{
    BH1750_OK = 0,
    BH1750_ERROR
} BH1750_Status;

/**
 * @brief Device handle
 * @note  Populate i2c_write, i2c_read, delay_ms, and address before calling BH1750_Init()
 * @note  mode is set internally by BH1750_Init(), do not modify manually
 * @note  Platform i2c_write/read must handle R/W bit shift if required by HAL (e.g. addr << 1 for STM32)
 */
typedef struct
{
    uint8_t address;  /* 7-bit I2C address: BH1750_ADDR_LOW or BH1750_ADDR_HIGH */
    BH1750_Mode mode; /* Set by BH1750_Init(), do not modify manually */

    BH1750_Status (*i2c_write)(uint8_t addr, uint8_t *data, uint16_t len);
    BH1750_Status (*i2c_read)(uint8_t addr, uint8_t *data, uint16_t len);
    void (*delay_ms)(uint32_t ms);
} BH1750_Dev;

BH1750_Status BH1750_Init(BH1750_Dev *dev, BH1750_Mode mode);
BH1750_Status BH1750_ReadLux(BH1750_Dev *dev, float *lux);
BH1750_Status BH1750_PowerDown(BH1750_Dev *dev);

#endif /* BH1750_H */