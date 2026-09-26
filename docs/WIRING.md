# Wiring Notes

## Pin Connections

| Component | Pin | Arduino Uno |
|---|---|---|
| SW-420 vibration | VCC / GND / OUT | 5V / GND / D2 |
| MPU6050 (GY-521) | VCC / GND / SDA / SCL | 5V / GND / A4 / A5 |
| 16x2 I2C LCD | VCC / GND / SDA / SCL | 5V / GND / A4 / A5 |
| RC522 RFID | 3.3V / GND / RST / SDA(SS) / SCK / MOSI / MISO | **3.3V** / GND / D9 / D10 / D13 / D11 / D12 |
| Buzzer | + / − | D7 / GND |

## Hardware Safety Notes

- **RC522 power:** connect VCC to the Arduino's **3.3V** pin, not 5V. The module's logic and power are 3.3V-only; running it at 5V risks damaging it over time.
- **GY-521 (MPU6050) power:** this breakout has an onboard voltage regulator, so 5V into VCC is fine. If you're using a bare MPU6050 chip with no breakout PCB, use 3.3V instead.
- **Buzzer:** a standard 5V active buzzer module or small piezo buzzer draws low enough current to connect directly to a digital pin — no driver transistor needed.

## Sharing the I2C Bus (LCD + MPU6050)

The LCD and MPU6050 both need SDA (A4) and SCL (A5), but each Arduino pin only cleanly takes one wire. Two ways to share the bus:

**Option 1 — breadboard bus row:**
1. Pick two empty breadboard rows — one for SDA, one for SCL.
2. Run one wire from Arduino A4 into the SDA row, and one wire from Arduino A5 into the SCL row.
3. Jumper both the LCD's SDA pin and the MPU6050's SDA pin into the SDA row. Do the same for SCL.
4. Tie all three GNDs (Arduino, LCD, MPU6050) together the same way, or use the breadboard's GND rail.

**Option 2 — daisy-chain:**
- Arduino A4 → LCD SDA pin → a second wire from that same LCD SDA pin → MPU6050 SDA pin.
- Same for A5 → LCD SCL → MPU6050 SCL.

Both devices can share the bus safely since they use different I2C addresses — the LCD is typically `0x27` (some boards use `0x3F`) and the MPU6050 defaults to `0x68`.
