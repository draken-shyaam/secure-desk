# Secure Desk — Smart Laptop Anti-Theft System

An Arduino-based physical security device that detects unauthorized movement or tampering on an unattended laptop, requires RFID authentication to disarm, and logs every event with a timestamp. Built as an OS + Cybersecurity course project.

## Problem Statement

Laptops left unattended in libraries, classrooms, and labs are an easy and common theft target. Once a device is physically taken, software-only protections (disk encryption, remote wipe, etc.) become irrelevant — there's no local alert, no owner verification, and no record of what happened. Secure Desk adds a physical security layer: it watches the laptop, tells authorized users apart from everyone else, and raises an alarm the moment something's wrong.

## Key Features

- **Dual tamper detection** — an SW-420 vibration sensor (interrupt-driven) catches sharp knocks, while an MPU6050 accelerometer catches slower tilts or lifts.
- **Owner authentication** — RC522 RFID required to disarm; unauthorized attempts trigger the alarm.
- **Auto-arm** — the system arms itself automatically after a period of inactivity, no manual step needed.
- **Live status display** — a 16x2 I2C LCD shows the current state at a glance (Disarmed / Armed / Auth? / Alarm).
- **Audit logging** — every arm, disarm, and alarm event is timestamped and logged over Serial as CSV.
- **Non-blocking, interrupt-driven design** — no `delay()` in the control flow; timing runs on `millis()` so sensors, display, and authentication are all handled cooperatively each loop pass.

## Hardware Components

| Component | Purpose | Module used |
|---|---|---|
| Arduino Uno R3 | Main controller | ATmega328P |
| Accelerometer | Detect movement/tilt/shock | MPU6050 (GY-521 breakout) |
| LCD | Display status/messages | 16x2 I2C LCD |
| RFID | Identify authorized users/cards | RC522 |
| Vibration sensor | Detect physical tampering/vibration | SW-420 |
| Buzzer | Audible alarm | Standard 5V active buzzer |

## Software Requirements

- [Arduino IDE](https://www.arduino.cc/en/software)
- Libraries (install via Library Manager):
  - `Wire.h` (bundled with Arduino IDE)
  - `LiquidCrystal_I2C`
  - `SPI.h` (bundled with Arduino IDE)
  - `MFRC522`
- No separate MPU6050 library needed — the sketch talks to it directly over I2C using raw register reads.

## Wiring / Pin Connections

| Component | Pin | Arduino Uno |
|---|---|---|
| SW-420 vibration | VCC / GND / OUT | 5V / GND / D2 |
| MPU6050 (GY-521) | VCC / GND / SDA / SCL | 5V / GND / A4 / A5 |
| 16x2 I2C LCD | VCC / GND / SDA / SCL | 5V / GND / A4 / A5 |
| RC522 RFID | 3.3V / GND / RST / SDA(SS) / SCK / MOSI / MISO | **3.3V** / GND / D9 / D10 / D13 / D11 / D12 |
| Buzzer | + / − | D7 / GND |

**Important:** the RC522 must be powered from the Arduino's **3.3V pin, not 5V** — it's a 3.3V-only part and 5V can damage it over time.

The LCD and MPU6050 share the same I2C bus (A4/A5) but have different addresses (`0x27` and `0x68`), so they coexist without conflict. See [`docs/WIRING.md`](docs/WIRING.md) for the shared-bus breadboard notes.

## How It Works

The system runs as a simple state machine:

```
DISARMED → ARMED → GRACE_PERIOD → DISARMED   (authenticated in time)
                                 → ALARM_ON   (grace period expires)
ALARM_ON → DISARMED                           (authenticated to clear)
```

1. **DISARMED** — idle. After a set period of inactivity, the system auto-arms and captures a baseline accelerometer reading.
2. **ARMED** — watching. Either the vibration sensor (via interrupt) or a significant drift from the accelerometer baseline flags a tamper event.
3. **GRACE_PERIOD** — a short window opens for the owner to scan their RFID tag. Authenticate in time → back to DISARMED. Time out → ALARM_ON.
4. **ALARM_ON** — buzzer sounds continuously until the owner authenticates to clear it.

Every transition is logged to Serial as `timestamp,event` for an audit trail.

## OS & Cybersecurity Concepts Demonstrated

| Concept | Where it shows up |
|---|---|
| Interrupt handling | Vibration sensor triggers a hardware interrupt (ISR); the ISR only sets a flag, real work happens in `loop()` |
| Non-blocking scheduling | No `delay()` in the control flow — sensors, LCD, and auth are checked cooperatively each pass of `loop()` via `millis()` |
| Race conditions / shared state | `tamperFlag` is `volatile` since it's written in an ISR and read in `loop()` |
| Memory-mapped I/O | MPU6050 accessed via raw I2C register reads instead of a library |
| Authentication | RFID-based access control, same principle as a login system |
| Physical security | Extends the CIA triad into the physical layer |
| Audit logging | Every event timestamped and logged, mirroring SIEM-style event logging |

## Setup & Upload

1. Wire the components per the table above.
2. Install the required libraries in the Arduino IDE (Sketch → Include Library → Manage Libraries).
3. Open `firmware/anti_theft_system.ino`.
4. Run the MFRC522 `DumpInfo` example once, scan your RFID tag, and copy the printed UID bytes into the `ownerUID[]` array in the sketch.
5. Select **Arduino Uno** as the board and the correct COM port, then upload.
6. Open the Serial Monitor (9600 baud) to watch the audit log.

## Project Photos

<!-- Drop your build photos into the /images folder and reference them here, e.g.: -->
<!-- ![Full circuit](images/full-circuit.jpg) -->
<!-- ![LCD showing ARMED state](images/lcd-armed.jpg) -->

## Future Scope

- GPS/GSM module to send an SMS alert on theft
- Cloud logging with basic log encryption
- ML-based motion classification to reduce false positives
- Bluetooth proximity lock (auto-arm when a paired phone moves away)
- Tamper-proof enclosure with its own switch

## Repo Structure

```
secure-desk/
├── firmware/
│   └── anti_theft_system.ino
├── images/
│   └── (project photos)
├── docs/
│   └── WIRING.md
├── README.md
├── LICENSE
└── .gitignore
```

## Author

**Shyaam** — B.Tech Cyber Security, Amrita Vishwa Vidyapeetham
