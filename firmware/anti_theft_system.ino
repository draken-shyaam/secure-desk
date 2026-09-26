/*
  Smart Laptop Anti-Theft System (v3)
  Arduino Uno R3 | MPU6050 (accelerometer, tilt/shock) +
  SW-420 (vibration/interrupt) + MFRC522 RFID (owner auth) +
  16x2 I2C LCD + buzzer

  State machine:
    DISARMED -> ARMED -> GRACE_PERIOD -> DISARMED   (authenticated in time)
                                       -> ALARM_ON   (grace period expires)
    ALARM_ON -> DISARMED                             (authenticated to clear)

  Component set:
    - Ultrasonic sensor, keypad, and vibration motor removed (not in the
      final component list).
    - MPU6050 accelerometer is a second, independent tamper source
      alongside the SW-420 vibration sensor. Both feed the same
      tamperFlag, so either one can trip GRACE_PERIOD.
    - No separate arm button -- system auto-arms after inactivity only.
      (Easy to add back if you want a manual override.)

  Design notes (the OS/security talking points for the report):
    - vibrationISR() only sets a flag: the real work happens in loop(),
      not the ISR -- standard interrupt-handling practice.
    - No delay() anywhere in the control flow, including the short
      "tamper detected" feedback pulse -- timing uses millis(), so
      sensors/LCD/auth/buzzer are cooperatively scheduled each pass of
      loop() instead of blocking each other.
    - tamperFlag is volatile because it's written in an ISR and read in
      loop() -- textbook shared-state/race-condition example.
    - MPU6050 access is raw I2C register reads/writes (no extra library),
      a nice concrete example of memory-mapped I/O for the memory
      management section of your report.
    - Every state transition calls logEvent() -> CSV over Serial
      (timestamp,event), i.e. the audit log.
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>

// ---------- Pin map ----------
#define VIBRATION_PIN   2      // SW-420 OUT -- must be interrupt-capable (2 or 3 on Uno)
#define BUZZER_PIN      7
#define RFID_SS_PIN     10
#define RFID_RST_PIN    9
// RFID also uses the fixed SPI pins: MOSI 11, MISO 12, SCK 13
// IMPORTANT: RC522 VCC must go to the Arduino's 3.3V pin, NOT 5V --
// the module's logic/power is 3.3V-only and 5V can damage it.
// LCD + MPU6050 share the fixed I2C pins: SDA A4, SCL A5

// ---------- MPU6050 ----------
#define MPU_ADDR        0x68   // default address (AD0 tied low)
#define MPU_PWR_MGMT_1  0x6B
#define MPU_ACCEL_XOUT_H 0x3B

// ---------- Tunables ----------
const unsigned long INACTIVITY_TIMEOUT = 10000UL; // 10 s -> auto-arm (demo-friendly value)
const unsigned long GRACE_PERIOD_MS    = 10000UL; // 10 s to authenticate
const unsigned long FEEDBACK_PULSE_MS  = 200UL;   // short buzz on tamper detected
const long ACCEL_DELTA_THRESHOLD       = 4000L;   // raw LSB units -- tune via Serial monitor

// Replace with your tag's real UID. Run the MFRC522 "DumpInfo" example
// once, scan your card, and copy the printed bytes in here.
byte ownerUID[4] = {0x65, 0x84, 0x76, 0x06};

LiquidCrystal_I2C lcd(0x27, 16, 2); // change to 0x3F if 0x27 doesn't work
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);

enum SystemState { DISARMED, ARMED, GRACE_PERIOD, ALARM_ON };
SystemState currentState = DISARMED;

volatile bool tamperFlag = false;   // set by ISR or accel check, cleared in loop()
unsigned long lastActivityTime = 0;
unsigned long graceStartTime   = 0;
unsigned long feedbackEndTime  = 0; // non-blocking "tamper detected" pulse end time

int16_t baselineX = 0, baselineY = 0, baselineZ = 0; // captured when arming

// ---------- ISR: minimal, just flag the event ----------
void vibrationISR() {
  tamperFlag = true;
}

void setup() {
  Serial.begin(9600);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(VIBRATION_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(VIBRATION_PIN), vibrationISR, RISING);

  Wire.begin();
  mpuInit();

  lcd.init();
  lcd.backlight();

  SPI.begin();
  rfid.PCD_Init();

  lastActivityTime = millis();
  logEvent("SYSTEM_BOOT");
}

void loop() {
  checkSensors();
  runStateMachine();
  updateFeedbackPulse();
  updateLCD();
}

// ---------- MPU6050 (raw register access, no extra library) ----------
void mpuInit() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_PWR_MGMT_1);
  Wire.write(0);              // wake the sensor up (it starts in sleep mode)
  Wire.endTransmission(true);
}

// Reads the 3 accelerometer axes into the given pointers.
void mpuReadAccel(int16_t *ax, int16_t *ay, int16_t *az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_ACCEL_XOUT_H);
  Wire.endTransmission(false); // repeated start, keep the bus held
  Wire.requestFrom(MPU_ADDR, 6, true);
  *ax = (Wire.read() << 8) | Wire.read();
  *ay = (Wire.read() << 8) | Wire.read();
  *az = (Wire.read() << 8) | Wire.read();
}

void captureBaseline() {
  mpuReadAccel(&baselineX, &baselineY, &baselineZ);
}

// ---------- Sensors ----------
void checkSensors() {
  if (currentState != ARMED) return;

  int16_t ax, ay, az;
  mpuReadAccel(&ax, &ay, &az);

  long dx = (long)ax - baselineX; if (dx < 0) dx = -dx;
  long dy = (long)ay - baselineY; if (dy < 0) dy = -dy;
  long dz = (long)az - baselineZ; if (dz < 0) dz = -dz;
  long delta = dx + dy + dz;

  if (delta > ACCEL_DELTA_THRESHOLD) {
    tamperFlag = true; // treat a tilt/shock the same as a vibration hit
  }
}

// ---------- RFID authentication ----------
bool authenticate() {
  if (!rfid.PICC_IsNewCardPresent()) return false;
  if (!rfid.PICC_ReadCardSerial()) return false;

  Serial.print("Scanned UID: ");
  for (byte i = 0; i < 4; i++) {
    Serial.print(rfid.uid.uidByte[i]);
    Serial.print(" ");
  }
  Serial.println();

  bool match = (memcmp(rfid.uid.uidByte, ownerUID, 4) == 0);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  return match;
}

// ---------- Non-blocking alarm (toggles buzzer) ----------
void triggerAlarm() {
  static unsigned long lastToggle = 0;
  static bool onState = false;
  if (millis() - lastToggle >= 300) {
    lastToggle = millis();
    onState = !onState;
    digitalWrite(BUZZER_PIN, onState ? HIGH : LOW);
  }
}

// Short one-shot buzz fired the moment tamper is first detected, so the
// owner gets feedback even before the alarm state kicks in. Non-blocking:
// just sets an end time, updateFeedbackPulse() clears the pin once it passes.
void startFeedbackPulse() {
  digitalWrite(BUZZER_PIN, HIGH);
  feedbackEndTime = millis() + FEEDBACK_PULSE_MS;
}

void updateFeedbackPulse() {
  if (feedbackEndTime != 0 && millis() >= feedbackEndTime) {
    digitalWrite(BUZZER_PIN, LOW);
    feedbackEndTime = 0;
  }
}

// ---------- Core state machine ----------
void runStateMachine() {
  switch (currentState) {

    case DISARMED:
      digitalWrite(BUZZER_PIN, LOW);
      if (millis() - lastActivityTime >= INACTIVITY_TIMEOUT) {
        currentState = ARMED;
        captureBaseline();       // "remember" how the laptop is sitting right now
        logEvent("AUTO_ARM");
      }
      break;

    case ARMED:
      if (tamperFlag) {
        tamperFlag = false;
        currentState = GRACE_PERIOD;
        graceStartTime = millis();
        startFeedbackPulse();
        logEvent("TAMPER_DETECTED");
      }
      break;

    case GRACE_PERIOD:
      if (authenticate()) {
        currentState = DISARMED;
        lastActivityTime = millis();
        logEvent("AUTHORIZED_ACCESS");
      } else if (millis() - graceStartTime >= GRACE_PERIOD_MS) {
        currentState = ALARM_ON;
        logEvent("UNAUTHORIZED_ATTEMPT");
      }
      break;

    case ALARM_ON:
      triggerAlarm();
      if (authenticate()) {
        currentState = DISARMED;
        lastActivityTime = millis();
        digitalWrite(BUZZER_PIN, LOW);
        logEvent("ALARM_CLEARED");
      }
      break;
  }
}

// ---------- LCD (only redraws on state change -> no flicker) ----------
void updateLCD() {
  static SystemState lastShown;
  static bool firstRun = true;
  if (currentState == lastShown && !firstRun) return;
  firstRun = false;
  lastShown = currentState;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Laptop Security");
  lcd.setCursor(0, 1);
  switch (currentState) {
    case DISARMED:     lcd.print("State: DISARMED"); break;
    case ARMED:        lcd.print("State: ARMED");    break;
    case GRACE_PERIOD: lcd.print("State: AUTH?");    break;
    case ALARM_ON:     lcd.print("State: ALARM!!");  break;
  }
}

// ---------- Audit log (Serial CSV: millis,event) ----------
// Swap the Serial.print calls for EEPROM.put()/EEPROM.get() with a
// rotating index if you need standalone (no-PC) logging instead.
void logEvent(const char* event) {
  Serial.print(millis());
  Serial.print(",");
  Serial.println(event);
}
