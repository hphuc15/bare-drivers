# bare-drivers

Lightweight, platform-independent drivers for embedded peripherals.
Designed to work on any microcontroller without vendor lock-in.

---

## Drivers

| Driver            | Device    | Protocol | Description          |
| ----------------- | --------- | -------- | -------------------- |
| [bh1750](bh1750/) | BH1750FVI | I2C      | Ambient light sensor |

---

## Repository Structure

```id="yq5p7h"
bare-drivers/
├── bh1750/
│   ├── bh1750.c
│   ├── include/
│   │   └── bh1750.h
│   └── README.md
└── README.md
```

---

## Design Philosophy

Each driver:
- Has no dependency on any HAL or SDK
- Uses function pointers for I2C/SPI/UART abstraction
- Is fully standalone and portable
- Brings its own platform layer per target

---

## Usage

1. Copy the driver into your project
2. Implement the platform layer
3. Initialize and use

See each driver's README for detailed instructions.

---

## Get a Single Driver

If you only need one driver (e.g. `bh1750`), you can clone only that folder using Git sparse-checkout:

```bash id="f4lqk9"
git clone --filter=blob:none --no-checkout https://github.com/hphuc15/bare-drivers
cd ./bare-drivers

git sparse-checkout init --cone
git sparse-checkout set bh1750

git checkout
```

This will download only the selected driver instead of the entire repository.

---

## Notes

- Drivers are independent → safe to use individually
- No shared core or hidden dependencies
- Suitable for bare-metal, RTOS, or any embedded platform

---

## License

MIT