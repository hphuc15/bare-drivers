# bare-drivers

Lightweight, platform-independent drivers for embedded peripherals.
Works on any microcontroller.

## Drivers

| Driver | Device | Protocol | Description |
|--------|--------|----------|-------------|
| [bh1750](bh1750/) | BH1750FVI | I2C | Ambient light sensor |

## Design Philosophy

Each driver:
- Has no dependency on any HAL or SDK
- Uses function pointers for I2C/SPI/UART abstraction
- Brings its own platform layer per target

## Usage

1. Copy the driver folder into your project
2. Implement the platform layer
3. Initialize and use

See each driver's README for details.

## License

MIT