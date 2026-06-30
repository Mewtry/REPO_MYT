#include <Arduino.h>
#include <PinChangeInterrupt.h>
#include "SoftwareSerial.h"
#include <Wire.h>
#include <avr/pgmspace.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace board {
constexpr uint32_t CONSOLE_BAUD = 115200;
constexpr uint32_t RS485_BAUD = 9600;
constexpr uint32_t I2C_CLOCK_HZ = 100000;

constexpr uint8_t PIN_ENCODER_A = 2;
constexpr uint8_t PIN_ENCODER_B = 3;
constexpr uint8_t PIN_ENCODER_SW = 4;
constexpr uint8_t PIN_RX_MODBUS = 5;
constexpr uint8_t PIN_ZERO_MAG = 6;
constexpr uint8_t PIN_INT_GPIO = 7;
constexpr uint8_t PIN_INT_KEYPAD = 8;
constexpr uint8_t PIN_TCS_OUT = 9;
constexpr uint8_t PIN_CC_IN1 = 10;
constexpr uint8_t PIN_CC_IN2 = 11;
constexpr uint8_t PIN_TX_MODBUS = 12;
constexpr uint8_t PIN_DIR_MODBUS = 13;
constexpr uint8_t PIN_STEP = A0;
constexpr uint8_t PIN_DIR = A1;
constexpr uint8_t PIN_TCS_S2 = A2;
constexpr uint8_t PIN_TCS_S3 = A3;
constexpr uint8_t PIN_I2C_SDA = A4;
constexpr uint8_t PIN_I2C_SCL = A5;

constexpr uint8_t LCD_I2C_ADDRESS = 0x27;
constexpr uint8_t LCD_COLUMNS = 20;
constexpr uint8_t LCD_ROWS = 4;
constexpr uint32_t LCD_DEFAULT_INTERVAL_MS = 1000;
constexpr uint8_t PCF8574_ADDR_GND = 0x20;
constexpr uint8_t PCF8574A_ADDR_GND = 0x38;

constexpr uint8_t DC_MOTOR_DEFAULT_DUTY = 180;
constexpr uint32_t DC_MOTOR_DEFAULT_RAMP_MS = 2000;
constexpr uint32_t DC_MOTOR_DEFAULT_HOLD_MS = 1000;
constexpr uint32_t DC_MOTOR_DEFAULT_PAUSE_MS = 1000;
constexpr uint16_t TCS_SAMPLE_MS = 20;
constexpr uint16_t TCS_TIMER1_PRESCALER = 8;
constexpr uint32_t TCS_TIMER1_HZ = F_CPU / TCS_TIMER1_PRESCALER;
constexpr uint16_t TCS_SAMPLE_TICKS =
    static_cast<uint16_t>((TCS_TIMER1_HZ / 1000UL) * TCS_SAMPLE_MS);
constexpr uint32_t TCS_DEFAULT_STREAM_MS = 1000;
constexpr uint32_t TCS_MIN_STREAM_MS = 250;
constexpr uint32_t TCS_MAX_STREAM_MS = 60000;
constexpr uint8_t TCS_CALIBRATION_SAMPLES = 4;
static_assert((TCS_TIMER1_HZ / 1000UL) * TCS_SAMPLE_MS <= 0xFFFFUL,
              "Janela TCS excede a capacidade do Timer1");
} // namespace board

struct Signal {
  PGM_P name;
  uint8_t pin;
};

const char NAME_CC_IN1[] PROGMEM = "CC_IN1";
const char NAME_CC_IN2[] PROGMEM = "CC_IN2";
const char NAME_DIR_MODBUS[] PROGMEM = "DIR_MODBUS";
const char NAME_STEP[] PROGMEM = "STEP";
const char NAME_DIR[] PROGMEM = "DIR";
const char NAME_TCS_S2[] PROGMEM = "TCS_S2";
const char NAME_TCS_S3[] PROGMEM = "TCS_S3";

const char NAME_ENCODER_A[] PROGMEM = "ENCODER_A";
const char NAME_ENCODER_B[] PROGMEM = "ENCODER_B";
const char NAME_ENCODER_SW[] PROGMEM = "ENCODER_SW";
const char NAME_ZERO_MAG[] PROGMEM = "ZERO_MAG";
const char NAME_INT_GPIO[] PROGMEM = "INT_GPIO";
const char NAME_INT_KEYPAD[] PROGMEM = "INT_KEYPAD";
const char NAME_TCS_OUT[] PROGMEM = "TCS_OUT";

const Signal OUTPUTS[] PROGMEM = {
    {NAME_CC_IN1, board::PIN_CC_IN1},
    {NAME_CC_IN2, board::PIN_CC_IN2},
    {NAME_DIR_MODBUS, board::PIN_DIR_MODBUS},
    {NAME_STEP, board::PIN_STEP},
    {NAME_DIR, board::PIN_DIR},
    {NAME_TCS_S2, board::PIN_TCS_S2},
    {NAME_TCS_S3, board::PIN_TCS_S3},
};

const Signal INPUTS[] PROGMEM = {
    {NAME_ENCODER_A, board::PIN_ENCODER_A},
    {NAME_ENCODER_B, board::PIN_ENCODER_B},
    {NAME_ENCODER_SW, board::PIN_ENCODER_SW},
    {NAME_ZERO_MAG, board::PIN_ZERO_MAG},
    {NAME_INT_GPIO, board::PIN_INT_GPIO},
    {NAME_INT_KEYPAD, board::PIN_INT_KEYPAD},
    {NAME_TCS_OUT, board::PIN_TCS_OUT},
};

constexpr uint8_t OUTPUT_COUNT = sizeof(OUTPUTS) / sizeof(OUTPUTS[0]);
constexpr uint8_t INPUT_COUNT = sizeof(INPUTS) / sizeof(INPUTS[0]);
constexpr uint8_t MAX_I2C_MESSAGE_BYTES = 12;

SoftwareSerial Rs485Serial(board::PIN_RX_MODBUS, board::PIN_TX_MODBUS);
hd44780_I2Cexp lcd(board::LCD_I2C_ADDRESS, I2Cexp_BOARD_YWROBOT);

bool outputLevels[OUTPUT_COUNT] = {};
uint8_t lastInputLevels[INPUT_COUNT] = {};
uint32_t inputEdges[INPUT_COUNT] = {};

bool watchChanges = false;
bool periodicStatus = false;
uint32_t periodicStatusMs = 1000;
uint32_t lastPeriodicStatusMs = 0;
uint32_t activeI2cClockHz = board::I2C_CLOCK_HZ;

char commandBuffer[96] = {};
uint8_t commandLength = 0;

Signal readSignal(const Signal *table, uint8_t index) {
  Signal signal = {};
  memcpy_P(&signal, &table[index], sizeof(signal));
  return signal;
}

const __FlashStringHelper *asFlashString(PGM_P pointer) {
  return reinterpret_cast<const __FlashStringHelper *>(pointer);
}

void printPin(uint8_t pin) {
  if (pin >= A0 && pin <= A7) {
    Serial.print('A');
    Serial.print(pin - A0);
  } else {
    Serial.print('D');
    Serial.print(pin);
  }
}

bool equalsIgnoreCase(const char *left, const char *right) {
  while (*left != '\0' && *right != '\0') {
    if (tolower(static_cast<unsigned char>(*left)) !=
        tolower(static_cast<unsigned char>(*right))) {
      return false;
    }
    left++;
    right++;
  }
  return *left == '\0' && *right == '\0';
}

bool parseLevel(const char *text, bool &level) {
  if (equalsIgnoreCase(text, "1") || equalsIgnoreCase(text, "on") ||
      equalsIgnoreCase(text, "high")) {
    level = true;
    return true;
  }
  if (equalsIgnoreCase(text, "0") || equalsIgnoreCase(text, "off") ||
      equalsIgnoreCase(text, "low")) {
    level = false;
    return true;
  }
  return false;
}

bool parseUInt(const char *text, uint32_t &value) {
  char *end = nullptr;
  const unsigned long parsed = strtoul(text, &end, 10);
  if (end == text || *end != '\0') {
    return false;
  }
  value = parsed;
  return true;
}

bool parseUIntAutoBase(const char *text, uint32_t &value) {
  char *end = nullptr;
  const unsigned long parsed = strtoul(text, &end, 0);
  if (end == text || *end != '\0') {
    return false;
  }
  value = parsed;
  return true;
}

bool parseI2cAddress(const char *text, uint8_t &address) {
  uint32_t parsed = 0;
  if (!parseUIntAutoBase(text, parsed) || parsed < 0x03 || parsed > 0x77) {
    return false;
  }
  address = static_cast<uint8_t>(parsed);
  return true;
}

bool parseByteValue(const char *text, uint8_t &value) {
  uint32_t parsed = 0;
  if (!parseUIntAutoBase(text, parsed) || parsed > 0xFF) {
    return false;
  }
  value = static_cast<uint8_t>(parsed);
  return true;
}

int8_t findSignal(const Signal *table, uint8_t count, const char *name) {
  for (uint8_t index = 0; index < count; index++) {
    const Signal signal = readSignal(table, index);
    if (strcasecmp_P(name, signal.name) == 0) {
      return static_cast<int8_t>(index);
    }
  }
  return -1;
}

int8_t findOutput(const char *name) {
  return findSignal(OUTPUTS, OUTPUT_COUNT, name);
}

void printHexByte(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

enum class DcMotorPhase : uint8_t {
  Idle,
  Ramp1Up,
  Hold1,
  Ramp1Down,
  Pause,
  Ramp2Up,
  Hold2,
  Ramp2Down,
};

bool dcMotorActive = false;
uint8_t dcMotorMaxDuty = board::DC_MOTOR_DEFAULT_DUTY;
uint8_t dcMotorCurrentDuty = 0;
int8_t dcMotorDirection = 0;
uint32_t dcMotorRampMs = board::DC_MOTOR_DEFAULT_RAMP_MS;
uint32_t dcMotorHoldMs = board::DC_MOTOR_DEFAULT_HOLD_MS;
uint32_t dcMotorPauseMs = board::DC_MOTOR_DEFAULT_PAUSE_MS;
uint32_t dcMotorPhaseStartedMs = 0;
DcMotorPhase dcMotorPhase = DcMotorPhase::Idle;

enum class TcsColor : uint8_t {
  Black,
  White,
  Red,
  Green,
  Blue,
  Gray,
};

struct TcsReading {
  uint32_t frequency[4];
  uint8_t rgb[3];
  TcsColor color;
  bool calibrated;
  bool valid;
};

volatile uint32_t tcsPulseCounter = 0;
bool tcsInitialized = false;
bool tcsMeasurementActive = false;
bool tcsStreamActive = false;
bool tcsDarkCalibrated = false;
bool tcsWhiteCalibrated = false;
uint32_t tcsStreamPeriodMs = board::TCS_DEFAULT_STREAM_MS;
uint32_t tcsLastStreamMs = 0;
uint32_t tcsDarkCalibration[3] = {};
uint32_t tcsWhiteCalibration[3] = {};
TcsReading tcsLastReading = {};

void stopTcsStream(bool verbose);

void trackMotorOutputs(uint8_t in1Duty, uint8_t in2Duty) {
  outputLevels[0] = in1Duty > 0;
  outputLevels[1] = in2Duty > 0;
}

void setDcMotorDuty(int8_t direction, uint8_t duty) {
  if (direction > 0) {
    analogWrite(board::PIN_CC_IN2, 0);
    digitalWrite(board::PIN_CC_IN2, LOW);
    analogWrite(board::PIN_CC_IN1, duty);
    trackMotorOutputs(duty, 0);
  } else if (direction < 0) {
    analogWrite(board::PIN_CC_IN1, 0);
    digitalWrite(board::PIN_CC_IN1, LOW);
    analogWrite(board::PIN_CC_IN2, duty);
    trackMotorOutputs(0, duty);
  } else {
    analogWrite(board::PIN_CC_IN1, 0);
    analogWrite(board::PIN_CC_IN2, 0);
    digitalWrite(board::PIN_CC_IN1, LOW);
    digitalWrite(board::PIN_CC_IN2, LOW);
    trackMotorOutputs(0, 0);
    duty = 0;
  }
  dcMotorDirection = direction;
  dcMotorCurrentDuty = duty;
}

void stopDcMotor(bool verbose) {
  setDcMotorDuty(0, 0);
  dcMotorActive = false;
  dcMotorPhase = DcMotorPhase::Idle;
  if (verbose) {
    Serial.println(F("Motor CC parado; D10 e D11 em LOW."));
  }
}

void startDcMotor(char *argv[], uint8_t argc) {
  uint32_t duty = board::DC_MOTOR_DEFAULT_DUTY;
  uint32_t ramp = board::DC_MOTOR_DEFAULT_RAMP_MS;
  uint32_t hold = board::DC_MOTOR_DEFAULT_HOLD_MS;
  uint32_t pause = board::DC_MOTOR_DEFAULT_PAUSE_MS;

  if (argc > 5 || (argc >= 2 && !parseUInt(argv[1], duty)) ||
      (argc >= 3 && !parseUInt(argv[2], ramp)) ||
      (argc >= 4 && !parseUInt(argv[3], hold)) ||
      (argc == 5 && !parseUInt(argv[4], pause))) {
    Serial.println(F("Uso: cc-jog [duty] [rampa_ms] [patamar_ms] [pausa_ms]"));
    return;
  }
  if (duty < 1 || duty > 255 || ramp < 100 || ramp > 30000 ||
      hold > 30000 || pause < 100 || pause > 30000) {
    Serial.println(F("Erro: duty 1..255; tempos maximos 30000 ms."));
    return;
  }

  stopTcsStream(false);
  stopDcMotor(false);
  dcMotorMaxDuty = static_cast<uint8_t>(duty);
  dcMotorRampMs = ramp;
  dcMotorHoldMs = hold;
  dcMotorPauseMs = pause;
  dcMotorPhaseStartedMs = millis();
  dcMotorPhase = DcMotorPhase::Ramp1Up;
  dcMotorActive = true;
  Serial.println(F("Motor CC: iniciando sentido 1. Use cc-stop para abortar."));
}

uint8_t rampUpDuty(uint32_t elapsed) {
  if (elapsed >= dcMotorRampMs) {
    return dcMotorMaxDuty;
  }
  return static_cast<uint8_t>(
      (static_cast<uint32_t>(dcMotorMaxDuty) * elapsed) / dcMotorRampMs);
}

uint8_t rampDownDuty(uint32_t elapsed) {
  if (elapsed >= dcMotorRampMs) {
    return 0;
  }
  return static_cast<uint8_t>(
      (static_cast<uint32_t>(dcMotorMaxDuty) * (dcMotorRampMs - elapsed)) /
      dcMotorRampMs);
}

void serviceDcMotor() {
  if (!dcMotorActive) {
    return;
  }
  const uint32_t now = millis();
  const uint32_t elapsed = now - dcMotorPhaseStartedMs;
  switch (dcMotorPhase) {
  case DcMotorPhase::Idle:
    return;
  case DcMotorPhase::Ramp1Up:
    setDcMotorDuty(1, rampUpDuty(elapsed));
    if (elapsed >= dcMotorRampMs) {
      dcMotorPhase = DcMotorPhase::Hold1;
      dcMotorPhaseStartedMs = now;
    }
    break;
  case DcMotorPhase::Hold1:
    setDcMotorDuty(1, dcMotorMaxDuty);
    if (elapsed >= dcMotorHoldMs) {
      dcMotorPhase = DcMotorPhase::Ramp1Down;
      dcMotorPhaseStartedMs = now;
    }
    break;
  case DcMotorPhase::Ramp1Down:
    setDcMotorDuty(1, rampDownDuty(elapsed));
    if (elapsed >= dcMotorRampMs) {
      setDcMotorDuty(0, 0);
      dcMotorPhase = DcMotorPhase::Pause;
      dcMotorPhaseStartedMs = now;
      Serial.println(F("Sentido 1 concluido; pausa antes da inversao."));
    }
    break;
  case DcMotorPhase::Pause:
    setDcMotorDuty(0, 0);
    if (elapsed >= dcMotorPauseMs) {
      dcMotorPhase = DcMotorPhase::Ramp2Up;
      dcMotorPhaseStartedMs = now;
      Serial.println(F("Motor CC: iniciando sentido 2."));
    }
    break;
  case DcMotorPhase::Ramp2Up:
    setDcMotorDuty(-1, rampUpDuty(elapsed));
    if (elapsed >= dcMotorRampMs) {
      dcMotorPhase = DcMotorPhase::Hold2;
      dcMotorPhaseStartedMs = now;
    }
    break;
  case DcMotorPhase::Hold2:
    setDcMotorDuty(-1, dcMotorMaxDuty);
    if (elapsed >= dcMotorHoldMs) {
      dcMotorPhase = DcMotorPhase::Ramp2Down;
      dcMotorPhaseStartedMs = now;
    }
    break;
  case DcMotorPhase::Ramp2Down:
    setDcMotorDuty(-1, rampDownDuty(elapsed));
    if (elapsed >= dcMotorRampMs) {
      stopDcMotor(false);
      Serial.println(F("Teste do motor CC concluido."));
    }
    break;
  }
}

void driveOutput(uint8_t outputIndex, bool level, bool verbose) {
  const Signal signal = readSignal(OUTPUTS, outputIndex);
  if (signal.pin == board::PIN_TCS_S2 || signal.pin == board::PIN_TCS_S3) {
    stopTcsStream(false);
  }
  if (dcMotorActive &&
      (signal.pin == board::PIN_CC_IN1 || signal.pin == board::PIN_CC_IN2)) {
    stopDcMotor(false);
  }
  pinMode(signal.pin, OUTPUT);
  digitalWrite(signal.pin, level ? HIGH : LOW);
  outputLevels[outputIndex] = level;
  if (verbose) {
    Serial.print(F("OUT "));
    Serial.print(asFlashString(signal.name));
    Serial.print(F("/"));
    printPin(signal.pin);
    Serial.print(F(" = "));
    Serial.println(level ? 1 : 0);
  }
}

void allOutputsLow() {
  stopTcsStream(false);
  stopDcMotor(false);
  for (uint8_t index = 0; index < OUTPUT_COUNT; index++) {
    driveOutput(index, false, false);
  }
  Serial.println(F("Todas as saidas foram colocadas em LOW."));
}

void delayMicroseconds32(uint32_t durationUs) {
  while (durationUs > 16000) {
    delayMicroseconds(16000);
    durationUs -= 16000;
  }
  delayMicroseconds(static_cast<unsigned int>(durationUs));
}

void pulseOutput(uint8_t outputIndex, uint32_t highTime, uint32_t lowTime,
                 uint32_t count, bool microseconds) {
  if (count == 0 || count > 100000) {
    Serial.println(F("Erro: quantidade de pulsos fora de 1..100000."));
    return;
  }
  for (uint32_t pulse = 0; pulse < count; pulse++) {
    driveOutput(outputIndex, true, false);
    if (microseconds) {
      delayMicroseconds32(highTime);
    } else {
      delay(highTime);
    }
    driveOutput(outputIndex, false, false);
    if (microseconds) {
      delayMicroseconds32(lowTime);
    } else {
      delay(lowTime);
    }
  }
  Serial.println(F("Pulsos finalizados; saida em LOW."));
}

void beginI2c(uint32_t clockHz) {
  Wire.end();
  Wire.begin();
  Wire.setClock(clockHz);
  Wire.setWireTimeout(25000, true);
  activeI2cClockHz = clockHz;
}

enum class LcdPhase : uint8_t {
  Idle,
  Intro,
  Lines,
  Custom,
  Cursor,
  Blink,
  DisplayOff,
  DisplayOn,
  BacklightOff,
  BacklightOn,
  ScrollSetup,
  ScrollLeft,
  ScrollRight,
  Live,
  Characters,
};

bool lcdInitialized = false;
bool lcdBacklightEnabled = false;
bool lcdTestActive = false;
uint8_t lcdColumns = board::LCD_COLUMNS;
uint8_t lcdRows = board::LCD_ROWS;
uint32_t lcdIntervalMs = board::LCD_DEFAULT_INTERVAL_MS;
uint32_t lcdNextStepMs = 0;
uint32_t lcdFrame = 0;
uint8_t lcdPhaseStep = 0;
uint8_t lcdFirstCharacter = 32;
LcdPhase lcdPhase = LcdPhase::Idle;

bool setLcdBacklight(bool enabled) {
  if (!lcdInitialized) {
    return false;
  }
  const int status = enabled ? lcd.backlight() : lcd.noBacklight();
  if (status != 0) {
    return false;
  }
  lcdBacklightEnabled = enabled;
  return true;
}

bool initializeLcd(uint8_t columns, uint8_t rows) {
  beginI2c(activeI2cClockHz);
  const int status = lcd.begin(columns, rows);
  Wire.setClock(activeI2cClockHz);
  if (status != 0) {
    lcdInitialized = false;
    lcdBacklightEnabled = false;
    Serial.print(F("Falha ao iniciar LCD 0x27; codigo "));
    Serial.println(status);
    return false;
  }
  lcdInitialized = true;
  return true;
}

void resetLcdMode() {
  lcd.display();
  setLcdBacklight(true);
  lcd.noCursor();
  lcd.noBlink();
  lcd.noAutoscroll();
  lcd.leftToRight();
}

void printLcdLineF(uint8_t row, const __FlashStringHelper *text) {
  if (row >= lcdRows) {
    return;
  }
  PGM_P pointer = reinterpret_cast<PGM_P>(text);
  lcd.setCursor(0, row);
  uint8_t column = 0;
  char value = static_cast<char>(pgm_read_byte(pointer + column));
  while (column < lcdColumns && value != '\0') {
    lcd.write(static_cast<uint8_t>(value));
    column++;
    value = static_cast<char>(pgm_read_byte(pointer + column));
  }
  while (column++ < lcdColumns) {
    lcd.write(' ');
  }
}

void scheduleLcd(uint32_t now, uint32_t delayMs, LcdPhase next) {
  lcdNextStepMs = now + delayMs;
  lcdPhase = next;
}

void startLcdTest(char *argv[], uint8_t argc) {
  uint32_t columns = board::LCD_COLUMNS;
  uint32_t rows = board::LCD_ROWS;
  uint32_t interval = board::LCD_DEFAULT_INTERVAL_MS;
  if (argc > 4 || (argc >= 2 && !parseUInt(argv[1], columns)) ||
      (argc >= 3 && !parseUInt(argv[2], rows)) ||
      (argc == 4 && !parseUInt(argv[3], interval))) {
    Serial.println(F("Uso: lcd-test [colunas] [linhas] [intervalo_ms]"));
    return;
  }
  if (columns < 8 || columns > 40 || rows < 1 || rows > 4 ||
      interval < 200 || interval > 10000) {
    Serial.println(F("Erro: colunas 8..40, linhas 1..4, intervalo 200..10000."));
    return;
  }
  stopTcsStream(false);
  lcdColumns = static_cast<uint8_t>(columns);
  lcdRows = static_cast<uint8_t>(rows);
  lcdIntervalMs = interval;
  if (!initializeLcd(lcdColumns, lcdRows) || !setLcdBacklight(true)) {
    lcdTestActive = false;
    return;
  }
  resetLcdMode();
  lcd.clear();
  lcdFrame = 0;
  lcdPhaseStep = 0;
  lcdFirstCharacter = 32;
  lcdPhase = LcdPhase::Intro;
  lcdNextStepMs = millis();
  lcdTestActive = true;
  Serial.println(F("Teste continuo do LCD iniciado. Use lcd-stop."));
}

void stopLcdTest() {
  if (!lcdTestActive) {
    Serial.println(F("O teste do LCD ja esta parado."));
    return;
  }
  lcdTestActive = false;
  lcdPhase = LcdPhase::Idle;
  resetLcdMode();
  lcd.clear();
  printLcdLineF(0, F("Teste encerrado"));
  if (lcdRows > 1) {
    printLcdLineF(1, F("LCD pronto"));
  }
  Serial.println(F("Teste do LCD encerrado."));
}

void controlLcdBacklight(char *argv[], uint8_t argc) {
  bool enabled = false;
  if (argc != 2 || !parseLevel(argv[1], enabled)) {
    Serial.println(F("Uso: lcd-backlight <on|off>"));
    return;
  }
  if (!lcdInitialized && !initializeLcd(lcdColumns, lcdRows)) {
    return;
  }
  if (!setLcdBacklight(enabled)) {
    Serial.println(F("Falha ao alterar o backlight."));
    return;
  }
  Serial.print(F("Backlight: "));
  Serial.println(enabled ? F("ON") : F("OFF"));
}

void serviceLcdTest() {
  if (!lcdTestActive) {
    return;
  }
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - lcdNextStepMs) < 0) {
    return;
  }

  switch (lcdPhase) {
  case LcdPhase::Idle:
    return;
  case LcdPhase::Intro:
    resetLcdMode();
    lcd.clear();
    printLcdLineF(0, F("Teste LCD I2C"));
    printLcdLineF(1, F("Endereco 0x27"));
    printLcdLineF(2, F("Arduino Nano V3"));
    printLcdLineF(3, F("20x4 @ 100 kHz"));
    scheduleLcd(now, 1600, LcdPhase::Lines);
    break;
  case LcdPhase::Lines:
    lcd.clear();
    for (uint8_t row = 0; row < lcdRows; row++) {
      lcd.setCursor(0, row);
      lcd.print(F("Linha "));
      lcd.print(row + 1);
      lcd.print('/');
      lcd.print(lcdRows);
      lcd.print(F(" OK"));
    }
    lcd.home();
    scheduleLcd(now, 1600, LcdPhase::Custom);
    break;
  case LcdPhase::Custom: {
    uint8_t heart[8] = {0x00, 0x0A, 0x1F, 0x1F, 0x0E, 0x04, 0x00, 0x00};
    uint8_t smile[8] = {0x00, 0x0A, 0x00, 0x00, 0x11, 0x0E, 0x00, 0x00};
    lcd.createChar(0, heart);
    lcd.createChar(1, smile);
    lcd.clear();
    printLcdLineF(0, F("Caracteres CGRAM"));
    lcd.setCursor(5, lcdRows > 1 ? 1 : 0);
    lcd.write(static_cast<uint8_t>(0));
    lcd.print(F("   "));
    lcd.write(static_cast<uint8_t>(1));
    scheduleLcd(now, 1600, LcdPhase::Cursor);
    break;
  }
  case LcdPhase::Cursor:
    lcd.clear();
    printLcdLineF(0, F("Cursor sublinhado"));
    lcd.setCursor(lcdColumns - 1, lcdRows - 1);
    lcd.cursor();
    scheduleLcd(now, 1300, LcdPhase::Blink);
    break;
  case LcdPhase::Blink:
    printLcdLineF(0, F("Cursor piscando"));
    lcd.setCursor(lcdColumns - 1, lcdRows - 1);
    lcd.blink();
    scheduleLcd(now, 1300, LcdPhase::DisplayOff);
    break;
  case LcdPhase::DisplayOff:
    lcd.noCursor();
    lcd.noBlink();
    lcd.noDisplay();
    scheduleLcd(now, 600, LcdPhase::DisplayOn);
    break;
  case LcdPhase::DisplayOn:
    lcd.display();
    lcd.clear();
    printLcdLineF(0, F("Display: ON"));
    scheduleLcd(now, 1000, LcdPhase::BacklightOff);
    break;
  case LcdPhase::BacklightOff:
    setLcdBacklight(false);
    scheduleLcd(now, 600, LcdPhase::BacklightOn);
    break;
  case LcdPhase::BacklightOn:
    setLcdBacklight(true);
    lcd.clear();
    printLcdLineF(0, F("Backlight: ON"));
    scheduleLcd(now, 1000, LcdPhase::ScrollSetup);
    break;
  case LcdPhase::ScrollSetup:
    lcd.clear();
    printLcdLineF(0, F("<<< TESTE SCROLL >>>"));
    printLcdLineF(1, F("Esquerda e direita"));
    lcdPhaseStep = 0;
    scheduleLcd(now, 600, LcdPhase::ScrollLeft);
    break;
  case LcdPhase::ScrollLeft:
    lcd.scrollDisplayLeft();
    if (++lcdPhaseStep >= 4) {
      lcdPhaseStep = 0;
      scheduleLcd(now, 250, LcdPhase::ScrollRight);
    } else {
      lcdNextStepMs = now + 250;
    }
    break;
  case LcdPhase::ScrollRight:
    lcd.scrollDisplayRight();
    if (++lcdPhaseStep >= 4) {
      lcdPhaseStep = 0;
      scheduleLcd(now, 500, LcdPhase::Live);
    } else {
      lcdNextStepMs = now + 250;
    }
    break;
  case LcdPhase::Live:
    resetLcdMode();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("LCD OK #"));
    lcd.print(lcdFrame++);
    lcd.setCursor(0, 1);
    lcd.print(F("Uptime: "));
    lcd.print(now / 1000);
    lcd.print(F(" s"));
    printLcdLineF(2, F("ASCII: 32..126"));
    printLcdLineF(3, F("SDA=A4 SCL=A5"));
    scheduleLcd(now, lcdIntervalMs, LcdPhase::Characters);
    break;
  case LcdPhase::Characters: {
    lcd.clear();
    uint8_t character = lcdFirstCharacter;
    for (uint8_t row = 0; row < lcdRows; row++) {
      lcd.setCursor(0, row);
      for (uint8_t column = 0; column < lcdColumns; column++) {
        lcd.write(character++);
        if (character > 126) {
          character = 32;
        }
      }
    }
    const uint16_t offset =
        static_cast<uint16_t>(lcdFirstCharacter - 32) + lcdColumns * lcdRows;
    lcdFirstCharacter = 32 + (offset % 95);
    scheduleLcd(now, lcdIntervalMs, LcdPhase::Live);
    break;
  }
  }
}

void tcsPulseInterrupt() {
  tcsPulseCounter++;
}

void trackTcsOutput(uint8_t pin, bool level) {
  for (uint8_t index = 0; index < OUTPUT_COUNT; index++) {
    const Signal signal = readSignal(OUTPUTS, index);
    if (signal.pin == pin) {
      outputLevels[index] = level;
      return;
    }
  }
}

void setTcsFilter(uint8_t filter) {
  bool s2 = false;
  bool s3 = false;
  switch (filter) {
  case 0: // vermelho
    break;
  case 1: // verde
    s2 = true;
    s3 = true;
    break;
  case 2: // azul
    s3 = true;
    break;
  case 3: // clear
    s2 = true;
    break;
  default:
    return;
  }
  digitalWrite(board::PIN_TCS_S2, s2 ? HIGH : LOW);
  digitalWrite(board::PIN_TCS_S3, s3 ? HIGH : LOW);
  trackTcsOutput(board::PIN_TCS_S2, s2);
  trackTcsOutput(board::PIN_TCS_S3, s3);
}

const __FlashStringHelper *tcsColorName(TcsColor color) {
  switch (color) {
  case TcsColor::Black:
    return F("PRETO");
  case TcsColor::White:
    return F("BRANCO");
  case TcsColor::Red:
    return F("VERMELHO");
  case TcsColor::Green:
    return F("VERDE");
  case TcsColor::Blue:
    return F("AZUL");
  case TcsColor::Gray:
  default:
    return F("CINZA");
  }
}

TcsColor classifyTcsColor(const uint8_t rgb[3]) {
  const uint16_t total =
      static_cast<uint16_t>(rgb[0]) + rgb[1] + rgb[2];
  if (total < 150) {
    return TcsColor::Black;
  }
  if (total > 500) {
    return TcsColor::White;
  }
  if (rgb[0] > rgb[1] && rgb[0] > rgb[2]) {
    return TcsColor::Red;
  }
  if (rgb[1] > rgb[0] && rgb[1] > rgb[2]) {
    return TcsColor::Green;
  }
  if (rgb[2] > rgb[0] && rgb[2] > rgb[1]) {
    return TcsColor::Blue;
  }
  return TcsColor::Gray;
}

void stopTcsStream(bool verbose) {
  const bool wasActive = tcsStreamActive;
  tcsStreamActive = false;
  if (tcsMeasurementActive) {
    detachPCINT(digitalPinToPCINT(board::PIN_TCS_OUT));
    tcsMeasurementActive = false;
  }
  if (verbose) {
    Serial.println(wasActive ? F("Stream TCS interrompido.")
                             : F("O stream TCS ja esta parado."));
  }
}

bool prepareTcsLcd() {
  lcdTestActive = false;
  lcdPhase = LcdPhase::Idle;
  const bool dimensionsChanged =
      lcdColumns != board::LCD_COLUMNS || lcdRows != board::LCD_ROWS;
  lcdColumns = board::LCD_COLUMNS;
  lcdRows = board::LCD_ROWS;
  if ((!lcdInitialized || dimensionsChanged) &&
      !initializeLcd(lcdColumns, lcdRows)) {
    return false;
  }
  if (!setLcdBacklight(true)) {
    Serial.println(F("Falha ao ligar backlight para o TCS."));
    return false;
  }
  resetLcdMode();
  return true;
}

void clearTcsLcdRow(uint8_t row) {
  lcd.setCursor(0, row);
  for (uint8_t column = 0; column < board::LCD_COLUMNS; column++) {
    lcd.write(' ');
  }
  lcd.setCursor(0, row);
}

void displayTcsReading(const TcsReading &reading) {
  if (!lcdInitialized || lcdColumns != board::LCD_COLUMNS ||
      lcdRows != board::LCD_ROWS) {
    return;
  }

  clearTcsLcdRow(0);
  lcd.print(F("R:"));
  lcd.print(reading.frequency[0]);
  lcd.print(F(" G:"));
  lcd.print(reading.frequency[1]);
  clearTcsLcdRow(1);
  lcd.print(F("B:"));
  lcd.print(reading.frequency[2]);
  lcd.print(F(" C:"));
  lcd.print(reading.frequency[3]);
  clearTcsLcdRow(2);
  if (reading.calibrated) {
    lcd.print(F("RGB:"));
    lcd.print(reading.rgb[0]);
    lcd.write(',');
    lcd.print(reading.rgb[1]);
    lcd.write(',');
    lcd.print(reading.rgb[2]);
  } else {
    lcd.print(F("RGB: SEM CALIB."));
  }
  clearTcsLcdRow(3);
  lcd.print(F("COR:"));
  lcd.print(reading.calibrated ? tcsColorName(reading.color)
                               : F("SEM CALIB."));
}

void printTcsWarning() {
  Serial.println(F("TCS Nano: OUT=D9 por PCINT, escala externa em 20%."));
}

void printTcsReading(const TcsReading &reading) {
  Serial.print(F("TCS Hz: R="));
  Serial.print(reading.frequency[0]);
  Serial.print(F(" G="));
  Serial.print(reading.frequency[1]);
  Serial.print(F(" B="));
  Serial.print(reading.frequency[2]);
  Serial.print(F(" C="));
  Serial.println(reading.frequency[3]);
  if (reading.calibrated) {
    Serial.print(F("TCS RGB("));
    Serial.print(reading.rgb[0]);
    Serial.write(',');
    Serial.print(reading.rgb[1]);
    Serial.write(',');
    Serial.print(reading.rgb[2]);
    Serial.print(F(") COR="));
    Serial.println(tcsColorName(reading.color));
  } else {
    Serial.println(F("TCS sem calibracao completa; use tcs-cal dark e white."));
  }
  printTcsWarning();
}

uint32_t measureTcsFrequency(uint8_t filter) {
  setTcsFilter(filter);
  delay(2);

  const uint8_t savedSreg = SREG;
  noInterrupts();
  const uint8_t savedTccr1a = TCCR1A;
  const uint8_t savedTccr1b = TCCR1B;
  const uint8_t savedTimsk1 = TIMSK1;
  const uint16_t savedTcnt1 = TCNT1;
  const uint16_t savedOcr1a = OCR1A;
  const uint16_t savedOcr1b = OCR1B;
  const uint16_t savedIcr1 = ICR1;

  TCCR1A = 0;
  TCCR1B = 0;
  TIMSK1 = 0;
  TCNT1 = 0;
  OCR1A = board::TCS_SAMPLE_TICKS;
  TIFR1 = _BV(OCF1A);
  tcsPulseCounter = 0;
  attachPCINT(digitalPinToPCINT(board::PIN_TCS_OUT), tcsPulseInterrupt,
              RISING);
  tcsMeasurementActive = true;
  TCCR1B = _BV(CS11); // Timer1 a F_CPU/8; independe do Timer0/millis().
  SREG = savedSreg;

  while ((TIFR1 & _BV(OCF1A)) == 0) {
  }

  noInterrupts();
  TCCR1B = 0;
  detachPCINT(digitalPinToPCINT(board::PIN_TCS_OUT));
  tcsMeasurementActive = false;
  const uint32_t pulses = tcsPulseCounter;

  TCNT1 = savedTcnt1;
  OCR1A = savedOcr1a;
  OCR1B = savedOcr1b;
  ICR1 = savedIcr1;
  TCCR1A = savedTccr1a;
  TCCR1B = savedTccr1b;
  TIFR1 = _BV(OCF1A);
  TIMSK1 = savedTimsk1;
  SREG = savedSreg;

  return (pulses * 1000UL) / board::TCS_SAMPLE_MS;
}

bool readTcs(TcsReading &reading) {
  if (!tcsInitialized) {
    Serial.println(F("TCS indisponivel: D9 nao possui PCINT valida."));
    return false;
  }
  reading = {};
  for (uint8_t filter = 0; filter < 4; filter++) {
    reading.frequency[filter] = measureTcsFrequency(filter);
  }
  reading.calibrated = tcsDarkCalibrated && tcsWhiteCalibrated;
  if (reading.calibrated) {
    for (uint8_t channel = 0; channel < 3; channel++) {
      const int32_t numerator =
          (static_cast<int32_t>(reading.frequency[channel]) -
           static_cast<int32_t>(tcsDarkCalibration[channel])) *
          255L;
      const int32_t denominator =
          static_cast<int32_t>(tcsWhiteCalibration[channel] -
                               tcsDarkCalibration[channel]);
      int32_t normalized = numerator / denominator;
      if (normalized < 0) {
        normalized = 0;
      } else if (normalized > 255) {
        normalized = 255;
      }
      reading.rgb[channel] = static_cast<uint8_t>(normalized);
    }
    reading.color = classifyTcsColor(reading.rgb);
  }
  reading.valid = true;
  return true;
}

bool averageTcs(uint32_t average[3], TcsReading &lastReading) {
  uint32_t sum[3] = {};
  for (uint8_t sample = 0; sample < board::TCS_CALIBRATION_SAMPLES; sample++) {
    if (!readTcs(lastReading)) {
      return false;
    }
    for (uint8_t channel = 0; channel < 3; channel++) {
      sum[channel] += lastReading.frequency[channel];
    }
  }
  for (uint8_t channel = 0; channel < 3; channel++) {
    average[channel] = sum[channel] / board::TCS_CALIBRATION_SAMPLES;
  }
  return true;
}

void runTcsRead() {
  stopTcsStream(false);
  stopDcMotor(false);
  prepareTcsLcd();
  if (readTcs(tcsLastReading)) {
    printTcsReading(tcsLastReading);
    displayTcsReading(tcsLastReading);
  }
}

void startTcsStream(char *argv[], uint8_t argc) {
  uint32_t periodMs = board::TCS_DEFAULT_STREAM_MS;
  if (argc > 2 || (argc == 2 && !parseUInt(argv[1], periodMs))) {
    Serial.println(F("Uso: tcs-stream [period_ms]"));
    return;
  }
  if (periodMs < board::TCS_MIN_STREAM_MS ||
      periodMs > board::TCS_MAX_STREAM_MS) {
    Serial.println(F("Erro: periodo TCS entre 250 e 60000 ms."));
    return;
  }
  if (!tcsInitialized) {
    Serial.println(F("TCS indisponivel: D9 nao possui PCINT valida."));
    return;
  }
  stopDcMotor(false);
  prepareTcsLcd();
  tcsStreamPeriodMs = periodMs;
  tcsLastStreamMs = millis() - periodMs;
  tcsStreamActive = true;
  Serial.print(F("Stream TCS iniciado, periodo "));
  Serial.print(periodMs);
  Serial.println(F(" ms. Use tcs-stop."));
  printTcsWarning();
}

void calibrateTcs(char *argv[], uint8_t argc) {
  if (argc != 2) {
    Serial.println(F("Uso: tcs-cal <dark|white|reset>"));
    return;
  }
  stopTcsStream(false);
  stopDcMotor(false);
  prepareTcsLcd();

  if (equalsIgnoreCase(argv[1], "reset")) {
    tcsDarkCalibrated = false;
    tcsWhiteCalibrated = false;
    memset(tcsDarkCalibration, 0, sizeof(tcsDarkCalibration));
    memset(tcsWhiteCalibration, 0, sizeof(tcsWhiteCalibration));
    lcd.clear();
    printLcdLineF(0, F("TCS CALIBRACAO"));
    printLcdLineF(1, F("RESET EM RAM"));
    Serial.println(F("Calibracao TCS removida da RAM."));
    return;
  }

  uint32_t average[3] = {};
  TcsReading last = {};
  if (equalsIgnoreCase(argv[1], "dark")) {
    if (!averageTcs(average, last)) {
      return;
    }
    memcpy(tcsDarkCalibration, average, sizeof(average));
    tcsDarkCalibrated = true;
    tcsWhiteCalibrated = false;
    tcsLastReading = last;
    tcsLastReading.calibrated = false;
    Serial.print(F("TCS dark Hz: R="));
    Serial.print(average[0]);
    Serial.print(F(" G="));
    Serial.print(average[1]);
    Serial.print(F(" B="));
    Serial.println(average[2]);
    displayTcsReading(tcsLastReading);
    printTcsWarning();
    return;
  }

  if (!equalsIgnoreCase(argv[1], "white")) {
    Serial.println(F("Uso: tcs-cal <dark|white|reset>"));
    return;
  }
  if (!tcsDarkCalibrated) {
    Serial.println(F("Calibre primeiro com: tcs-cal dark"));
    return;
  }
  if (!averageTcs(average, last)) {
    return;
  }
  for (uint8_t channel = 0; channel < 3; channel++) {
    if (average[channel] <= tcsDarkCalibration[channel]) {
      tcsWhiteCalibrated = false;
      Serial.println(F("White rejeitado: todos os canais devem superar dark."));
      displayTcsReading(last);
      return;
    }
  }
  memcpy(tcsWhiteCalibration, average, sizeof(average));
  tcsWhiteCalibrated = true;
  Serial.print(F("TCS white Hz: R="));
  Serial.print(average[0]);
  Serial.print(F(" G="));
  Serial.print(average[1]);
  Serial.print(F(" B="));
  Serial.println(average[2]);
  if (readTcs(tcsLastReading)) {
    printTcsReading(tcsLastReading);
    displayTcsReading(tcsLastReading);
  }
}

void serviceTcsStream() {
  if (!tcsStreamActive) {
    return;
  }
  const uint32_t now = millis();
  if (now - tcsLastStreamMs < tcsStreamPeriodMs) {
    return;
  }
  tcsLastStreamMs = now;
  if (readTcs(tcsLastReading)) {
    printTcsReading(tcsLastReading);
    displayTcsReading(tcsLastReading);
  } else {
    stopTcsStream(false);
  }
}

void setupTcs() {
  pinMode(board::PIN_TCS_S2, OUTPUT);
  pinMode(board::PIN_TCS_S3, OUTPUT);
  digitalWrite(board::PIN_TCS_S2, LOW);
  digitalWrite(board::PIN_TCS_S3, LOW);
  pinMode(board::PIN_TCS_OUT, INPUT);
  trackTcsOutput(board::PIN_TCS_S2, false);
  trackTcsOutput(board::PIN_TCS_S3, false);
  tcsInitialized =
      digitalPinToPCINT(board::PIN_TCS_OUT) != NOT_AN_INTERRUPT;
}

void printI2cError(uint8_t error) {
  switch (error) {
  case 0:
    Serial.print(F("ACK"));
    break;
  case 1:
    Serial.print(F("buffer excedido"));
    break;
  case 2:
    Serial.print(F("NACK endereco"));
    break;
  case 3:
    Serial.print(F("NACK dado"));
    break;
  case 4:
    Serial.print(F("erro de barramento"));
    break;
  case 5:
    Serial.print(F("timeout"));
    break;
  default:
    Serial.print(F("erro "));
    Serial.print(error);
    break;
  }
}

void printI2cLevels() {
  Wire.end();
  pinMode(board::PIN_I2C_SCL, INPUT);
  pinMode(board::PIN_I2C_SDA, INPUT);
  delay(2);
  Serial.print(F("I2C idle: SCL/A5="));
  Serial.print(digitalRead(board::PIN_I2C_SCL));
  Serial.print(F(" SDA/A4="));
  Serial.println(digitalRead(board::PIN_I2C_SDA));
  beginI2c(activeI2cClockHz);
}

void scanI2c(char *argv[], uint8_t argc) {
  uint32_t clockHz = board::I2C_CLOCK_HZ;
  if (argc > 2 || (argc == 2 && !parseUInt(argv[1], clockHz))) {
    Serial.println(F("Uso: i2c-scan [clock_hz]"));
    return;
  }
  if (clockHz < 10000 || clockHz > 400000) {
    Serial.println(F("Erro: clock permitido entre 10000 e 400000 Hz."));
    return;
  }
  beginI2c(clockHz);
  Serial.print(F("Varredura I2C @ "));
  Serial.print(clockHz);
  Serial.println(F(" Hz..."));
  uint8_t found = 0;
  for (uint8_t address = 0x03; address <= 0x77; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("  encontrado: 0x"));
      printHexByte(address);
      Serial.println();
      found++;
    }
  }
  if (found == 0) {
    Serial.println(F("Nenhum dispositivo I2C respondeu."));
  }
}

void i2cWriteMessage(char *argv[], uint8_t argc) {
  if (argc < 3 || argc > MAX_I2C_MESSAGE_BYTES + 2) {
    Serial.println(F("Uso: i2c-write <addr> <byte> [byte...]"));
    return;
  }
  uint8_t address = 0;
  if (!parseI2cAddress(argv[1], address)) {
    Serial.println(F("Erro: endereco I2C invalido."));
    return;
  }
  uint8_t bytes[MAX_I2C_MESSAGE_BYTES] = {};
  for (uint8_t index = 2; index < argc; index++) {
    if (!parseByteValue(argv[index], bytes[index - 2])) {
      Serial.println(F("Erro: byte I2C invalido."));
      return;
    }
  }
  beginI2c(activeI2cClockHz);
  Wire.beginTransmission(address);
  for (uint8_t index = 0; index < argc - 2; index++) {
    Wire.write(bytes[index]);
  }
  const uint8_t error = Wire.endTransmission();
  Serial.print(F("I2C write 0x"));
  printHexByte(address);
  Serial.print(F(": "));
  printI2cError(error);
  Serial.println();
}

void i2cDriveLow(uint8_t pin) {
  digitalWrite(pin, LOW);
  pinMode(pin, OUTPUT);
}

void i2cReleaseLine(uint8_t pin) {
  pinMode(pin, INPUT);
}

void i2cHalfDelay() {
  delayMicroseconds(5);
}

void i2cStartCondition() {
  i2cReleaseLine(board::PIN_I2C_SDA);
  i2cReleaseLine(board::PIN_I2C_SCL);
  i2cHalfDelay();
  i2cDriveLow(board::PIN_I2C_SDA);
  i2cHalfDelay();
  i2cDriveLow(board::PIN_I2C_SCL);
}

void i2cStopCondition() {
  i2cDriveLow(board::PIN_I2C_SCL);
  i2cDriveLow(board::PIN_I2C_SDA);
  i2cHalfDelay();
  i2cReleaseLine(board::PIN_I2C_SCL);
  i2cHalfDelay();
  i2cReleaseLine(board::PIN_I2C_SDA);
  i2cHalfDelay();
}

void i2cWaveByte(uint8_t value) {
  for (int8_t bit = 7; bit >= 0; bit--) {
    i2cDriveLow(board::PIN_I2C_SCL);
    if (value & (1U << bit)) {
      i2cReleaseLine(board::PIN_I2C_SDA);
    } else {
      i2cDriveLow(board::PIN_I2C_SDA);
    }
    i2cHalfDelay();
    i2cReleaseLine(board::PIN_I2C_SCL);
    i2cHalfDelay();
  }
  i2cDriveLow(board::PIN_I2C_SCL);
  i2cReleaseLine(board::PIN_I2C_SDA);
  i2cHalfDelay();
  i2cReleaseLine(board::PIN_I2C_SCL);
  i2cHalfDelay();
  i2cDriveLow(board::PIN_I2C_SCL);
}

void i2cWaveMessage(char *argv[], uint8_t argc) {
  if (argc < 4 || argc > MAX_I2C_MESSAGE_BYTES + 3) {
    Serial.println(F("Uso: i2c-wave <addr> <repeticoes> <byte> [byte...]"));
    return;
  }
  uint8_t address = 0;
  uint32_t repetitions = 0;
  if (!parseI2cAddress(argv[1], address) ||
      !parseUInt(argv[2], repetitions) || repetitions < 1 ||
      repetitions > 10000) {
    Serial.println(F("Erro: endereco ou repeticoes invalidas."));
    return;
  }
  uint8_t bytes[MAX_I2C_MESSAGE_BYTES] = {};
  for (uint8_t index = 3; index < argc; index++) {
    if (!parseByteValue(argv[index], bytes[index - 3])) {
      Serial.println(F("Erro: byte invalido."));
      return;
    }
  }
  Wire.end();
  i2cReleaseLine(board::PIN_I2C_SDA);
  i2cReleaseLine(board::PIN_I2C_SCL);
  for (uint32_t repetition = 0; repetition < repetitions; repetition++) {
    i2cStartCondition();
    i2cWaveByte(static_cast<uint8_t>(address << 1));
    for (uint8_t index = 0; index < argc - 3; index++) {
      i2cWaveByte(bytes[index]);
    }
    i2cStopCondition();
  }
  beginI2c(activeI2cClockHz);
  Serial.println(F("Forma de onda I2C finalizada; Wire restaurado."));
}

void pcfProbe(char *argv[], uint8_t argc) {
  uint8_t addresses[2] = {board::PCF8574_ADDR_GND,
                          board::PCF8574A_ADDR_GND};
  uint8_t count = 2;
  if (argc > 2) {
    Serial.println(F("Uso: pcf-probe [addr]"));
    return;
  }
  if (argc == 2) {
    if (!parseI2cAddress(argv[1], addresses[0])) {
      Serial.println(F("Erro: endereco invalido."));
      return;
    }
    count = 1;
  }
  beginI2c(activeI2cClockHz);
  for (uint8_t index = 0; index < count; index++) {
    Wire.beginTransmission(addresses[index]);
    const uint8_t error = Wire.endTransmission();
    Serial.print(F("PCF 0x"));
    printHexByte(addresses[index]);
    Serial.print(F(": "));
    printI2cError(error);
    Serial.println();
  }
}

void pcfLoop(char *argv[], uint8_t argc) {
  uint8_t address = board::PCF8574_ADDR_GND;
  uint32_t durationMs = 5000;
  if (argc > 3 || (argc >= 2 && !parseI2cAddress(argv[1], address)) ||
      (argc == 3 && !parseUInt(argv[2], durationMs)) || durationMs > 60000) {
    Serial.println(F("Uso: pcf-loop <addr> [tempo_ms]"));
    return;
  }
  beginI2c(activeI2cClockHz);
  const uint32_t started = millis();
  uint32_t probes = 0;
  uint32_t acknowledgements = 0;
  while (millis() - started < durationMs) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      acknowledgements++;
    }
    probes++;
    delay(2);
  }
  Serial.print(F("PCF probes="));
  Serial.print(probes);
  Serial.print(F(" ACK="));
  Serial.println(acknowledgements);
}

void pcfRead(char *argv[], uint8_t argc) {
  uint8_t address = 0;
  if (argc != 2 || !parseI2cAddress(argv[1], address)) {
    Serial.println(F("Uso: pcf-read <addr>"));
    return;
  }
  beginI2c(activeI2cClockHz);
  const uint8_t requested = Wire.requestFrom(address, static_cast<uint8_t>(1));
  if (requested != 1 || Wire.available() < 1) {
    Serial.println(F("PCF: sem byte de leitura."));
    return;
  }
  Serial.print(F("PCF read = 0x"));
  printHexByte(Wire.read());
  Serial.println();
}

void pcfWrite(char *argv[], uint8_t argc) {
  uint8_t address = 0;
  uint8_t value = 0;
  if (argc != 3 || !parseI2cAddress(argv[1], address) ||
      !parseByteValue(argv[2], value)) {
    Serial.println(F("Uso: pcf-write <addr> <byte>"));
    return;
  }
  beginI2c(activeI2cClockHz);
  Wire.beginTransmission(address);
  Wire.write(value);
  const uint8_t error = Wire.endTransmission();
  Serial.print(F("PCF write 0x"));
  printHexByte(value);
  Serial.print(F(": "));
  printI2cError(error);
  Serial.println();
}

void restartRs485() {
  Rs485Serial.end();
  delay(5);
  Rs485Serial.begin(board::RS485_BAUD);
  Rs485Serial.listen();
}

uint32_t clearRs485Rx(uint16_t settleMs) {
  uint32_t cleared = 0;
  uint32_t lastByteAt = millis();
  do {
    while (Rs485Serial.available() > 0) {
      Rs485Serial.read();
      cleared++;
      lastByteAt = millis();
    }
    if (settleMs > 0) {
      delay(1);
    }
  } while (settleMs > 0 && millis() - lastByteAt < settleMs);
  return cleared;
}

void driveRs485Direction(bool transmit) {
  const int8_t index = findOutput("DIR_MODBUS");
  if (index >= 0) {
    driveOutput(static_cast<uint8_t>(index), transmit, false);
  }
}

void sendRs485(char *argv[], uint8_t argc) {
  if (argc < 2) {
    Serial.println(F("Uso: rs485-send <texto>"));
    return;
  }
  clearRs485Rx(0);
  driveRs485Direction(true);
  delayMicroseconds(200);
  for (uint8_t index = 1; index < argc; index++) {
    if (index > 1) {
      Rs485Serial.write(' ');
    }
    Rs485Serial.print(argv[index]);
  }
  Rs485Serial.print(F("\r\n"));
  delayMicroseconds(200);
  clearRs485Rx(5);
  driveRs485Direction(false);
  Rs485Serial.listen();
  Serial.println(F("RS485 enviado; transceptor em recepcao."));
}

void sendRs485Burst(char *argv[], uint8_t argc) {
  uint32_t durationMs = 3000;
  if (argc > 2 || (argc == 2 && !parseUInt(argv[1], durationMs)) ||
      durationMs > 60000) {
    Serial.println(F("Uso: rs485-burst [tempo_ms], maximo 60000"));
    return;
  }
  clearRs485Rx(0);
  driveRs485Direction(true);
  delayMicroseconds(200);
  const uint32_t started = millis();
  uint32_t sent = 0;
  while (millis() - started < durationMs) {
    Rs485Serial.write(0x55);
    sent++;
  }
  delayMicroseconds(200);
  driveRs485Direction(false);
  Rs485Serial.listen();
  clearRs485Rx(5);
  Serial.print(F("Rajada finalizada; bytes enviados: "));
  Serial.println(sent);
}

void holdRs485Di(char *argv[], uint8_t argc) {
  bool level = false;
  uint32_t holdMs = 5000;
  if (argc < 2 || argc > 3 || !parseLevel(argv[1], level) ||
      (argc == 3 && !parseUInt(argv[2], holdMs)) || holdMs > 60000) {
    Serial.println(F("Uso: rs485-di <0|1|low|high> [tempo_ms]"));
    return;
  }
  Rs485Serial.end();
  driveRs485Direction(true);
  pinMode(board::PIN_TX_MODBUS, OUTPUT);
  digitalWrite(board::PIN_TX_MODBUS, level ? HIGH : LOW);
  Serial.print(F("TX Modbus mantido em "));
  Serial.print(level ? 1 : 0);
  Serial.print(F(" por "));
  Serial.print(holdMs);
  Serial.println(F(" ms."));
  delay(holdMs);
  digitalWrite(board::PIN_TX_MODBUS, HIGH);
  restartRs485();
  driveRs485Direction(false);
  Serial.println(F("Diagnostico RS485 finalizado."));
}

void readRs485Available(bool forceMessage) {
  uint8_t count = 0;
  while (Rs485Serial.available() > 0) {
    const uint8_t value = static_cast<uint8_t>(Rs485Serial.read());
    if (count == 0) {
      Serial.print(F("RS485 RX:"));
    }
    Serial.print(F(" 0x"));
    printHexByte(value);
    if (isprint(value)) {
      Serial.print(F("('"));
      Serial.write(value);
      Serial.print(F("')"));
    }
    count++;
  }
  if (count > 0) {
    Serial.println();
  } else if (forceMessage) {
    Serial.println(F("Nenhum byte pendente no RS485."));
  }
}

void printRs485RxLevel() {
  Serial.print(F("DIR_MODBUS/D13="));
  Serial.print(digitalRead(board::PIN_DIR_MODBUS));
  Serial.print(F(" RX_MODBUS/D5="));
  Serial.println(digitalRead(board::PIN_RX_MODBUS));
  Serial.println(F("Idle UART esperado em RX: HIGH."));
}

int freeRam() {
  extern int __heap_start;
  extern int *__brkval;
  int local;
  return reinterpret_cast<int>(&local) -
         (__brkval == nullptr ? reinterpret_cast<int>(&__heap_start)
                              : reinterpret_cast<int>(__brkval));
}

void printSignals() {
  Serial.println(F("\nSaidas:"));
  for (uint8_t index = 0; index < OUTPUT_COUNT; index++) {
    const Signal signal = readSignal(OUTPUTS, index);
    Serial.print(F("  "));
    Serial.print(asFlashString(signal.name));
    Serial.print(F(" = "));
    printPin(signal.pin);
    Serial.println();
  }
  Serial.println(F("Entradas:"));
  for (uint8_t index = 0; index < INPUT_COUNT; index++) {
    const Signal signal = readSignal(INPUTS, index);
    Serial.print(F("  "));
    Serial.print(asFlashString(signal.name));
    Serial.print(F(" = "));
    printPin(signal.pin);
    Serial.println();
  }
  Serial.println(F("Barramentos:"));
  Serial.println(F("  RS485 RX=D5 TX=D12 DIR=D13 @ 9600 bps"));
  Serial.println(F("  I2C SDA=A4 SCL=A5 @ 100 kHz"));
  Serial.println(F("Indisponiveis no Nano: TRIGGER, ECHO e SENSOR_IND."));
}

void printStatus() {
  Serial.println(F("\nEstado das saidas:"));
  for (uint8_t index = 0; index < OUTPUT_COUNT; index++) {
    const Signal signal = readSignal(OUTPUTS, index);
    Serial.print(F("  "));
    Serial.print(asFlashString(signal.name));
    Serial.print(F("/"));
    printPin(signal.pin);
    Serial.print(F(" = "));
    if (dcMotorActive && signal.pin == board::PIN_CC_IN1) {
      Serial.print(F("PWM "));
      Serial.print(dcMotorDirection > 0 ? dcMotorCurrentDuty : 0);
      Serial.println(F("/255"));
    } else if (dcMotorActive && signal.pin == board::PIN_CC_IN2) {
      Serial.print(F("PWM "));
      Serial.print(dcMotorDirection < 0 ? dcMotorCurrentDuty : 0);
      Serial.println(F("/255"));
    } else {
      Serial.println(outputLevels[index] ? 1 : 0);
    }
  }
  Serial.println(F("Estado das entradas:"));
  for (uint8_t index = 0; index < INPUT_COUNT; index++) {
    const Signal signal = readSignal(INPUTS, index);
    Serial.print(F("  "));
    Serial.print(asFlashString(signal.name));
    Serial.print(F("/"));
    printPin(signal.pin);
    Serial.print(F(" = "));
    Serial.print(digitalRead(signal.pin));
    Serial.print(F(" edges="));
    Serial.println(inputEdges[index]);
  }
  Serial.print(F("RS485 pendente: "));
  Serial.println(Rs485Serial.available());
  Serial.print(F("LCD: "));
  Serial.print(lcdTestActive ? F("teste ativo") : F("parado"));
  Serial.print(F(", backlight "));
  Serial.println(lcdBacklightEnabled ? F("ON") : F("OFF"));
  Serial.print(F("Motor CC: "));
  Serial.println(dcMotorActive ? F("teste ativo") : F("parado"));
  Serial.print(F("TCS: "));
  Serial.print(tcsInitialized ? F("pronto") : F("indisponivel"));
  Serial.print(F(", stream "));
  Serial.print(tcsStreamActive ? F("ATIVO") : F("parado"));
  Serial.print('/');
  Serial.print(tcsStreamPeriodMs);
  Serial.print(F(" ms, cal "));
  Serial.print(tcsDarkCalibrated ? F("dark") : F("sem dark"));
  Serial.println(tcsWhiteCalibrated ? F("+white") : F(""));
  if (tcsLastReading.valid) {
    Serial.print(F("TCS ultimo Hz R="));
    Serial.print(tcsLastReading.frequency[0]);
    Serial.print(F(" G="));
    Serial.print(tcsLastReading.frequency[1]);
    Serial.print(F(" B="));
    Serial.print(tcsLastReading.frequency[2]);
    Serial.print(F(" C="));
    Serial.println(tcsLastReading.frequency[3]);
  }
  printTcsWarning();
  Serial.print(F("RAM livre aproximada: "));
  Serial.print(freeRam());
  Serial.println(F(" bytes"));
}

void printHelp() {
  Serial.println(F("\nComandos do firmware Nano:"));
  Serial.println(F("  help | list | status"));
  Serial.println(F("  set <saida> <0|1|low|high>"));
  Serial.println(F("  pulse <saida> <high_ms> <low_ms> <vezes>"));
  Serial.println(F("  pulseus <saida> <high_us> <low_us> <vezes>"));
  Serial.println(F("  all-low"));
  Serial.println(F("  cc-jog [duty] [rampa_ms] [patamar_ms] [pausa_ms]"));
  Serial.println(F("  cc-stop"));
  Serial.println(F("  i2c-level | i2c-scan [clock_hz]"));
  Serial.println(F("  i2c-write <addr> <byte> [byte...]"));
  Serial.println(F("  i2c-wave <addr> <repeticoes> <byte> [byte...]"));
  Serial.println(F("  lcd-test [colunas] [linhas] [intervalo_ms]"));
  Serial.println(F("  lcd-stop | lcd-backlight <on|off>"));
  Serial.println(F("  tcs-read | tcs-stream [period_ms] | tcs-stop"));
  Serial.println(F("  tcs-cal <dark|white|reset>"));
  Serial.println(F("  pcf-probe [addr] | pcf-loop <addr> [tempo_ms]"));
  Serial.println(F("  pcf-read <addr> | pcf-write <addr> <byte>"));
  Serial.println(F("  rs485-send <texto> | rs485-burst [tempo_ms]"));
  Serial.println(F("  rs485-di <0|1> [tempo_ms]"));
  Serial.println(F("  rs485-read | rs485-clear | rs485-rx-level"));
  Serial.println(F("  edges | clear-edges | watch on|off [period_ms]"));
  Serial.println(F("TCS Nano D9/20%: leitura por PCINT."));
  Serial.println(F("Sem trigger/ECHO e sem SENSOR_IND neste microcontrolador."));
}

void clearEdges() {
  for (uint8_t index = 0; index < INPUT_COUNT; index++) {
    inputEdges[index] = 0;
  }
}

void printEdges() {
  Serial.println(F("Contadores de borda:"));
  for (uint8_t index = 0; index < INPUT_COUNT; index++) {
    const Signal signal = readSignal(INPUTS, index);
    Serial.print(F("  "));
    Serial.print(asFlashString(signal.name));
    Serial.print(F("="));
    Serial.println(inputEdges[index]);
  }
}

void serviceInputWatch() {
  for (uint8_t index = 0; index < INPUT_COUNT; index++) {
    const Signal signal = readSignal(INPUTS, index);
    const uint8_t level = digitalRead(signal.pin);
    if (level != lastInputLevels[index]) {
      lastInputLevels[index] = level;
      inputEdges[index]++;
      if (watchChanges) {
        Serial.print(F("IN "));
        Serial.print(asFlashString(signal.name));
        Serial.print(F("="));
        Serial.print(level);
        Serial.print(F(" edges="));
        Serial.println(inputEdges[index]);
      }
    }
  }
}

void servicePeriodicStatus() {
  if (periodicStatus && millis() - lastPeriodicStatusMs >= periodicStatusMs) {
    lastPeriodicStatusMs = millis();
    printStatus();
  }
}

uint8_t tokenize(char *line, char *argv[], uint8_t maxArgs) {
  uint8_t argc = 0;
  char *token = strtok(line, " \t\r\n");
  while (token != nullptr && argc < maxArgs) {
    argv[argc++] = token;
    token = strtok(nullptr, " \t\r\n");
  }
  return argc;
}

void processCommand(char *line) {
  char *argv[18] = {};
  const uint8_t argc = tokenize(line, argv, 18);
  if (argc == 0) {
    return;
  }

  if (equalsIgnoreCase(argv[0], "help")) {
    printHelp();
  } else if (equalsIgnoreCase(argv[0], "list")) {
    printSignals();
  } else if (equalsIgnoreCase(argv[0], "status")) {
    printStatus();
  } else if (equalsIgnoreCase(argv[0], "all-low")) {
    allOutputsLow();
  } else if (equalsIgnoreCase(argv[0], "cc-jog")) {
    startDcMotor(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "cc-stop")) {
    stopDcMotor(true);
  } else if (equalsIgnoreCase(argv[0], "i2c-level")) {
    printI2cLevels();
  } else if (equalsIgnoreCase(argv[0], "i2c-scan")) {
    scanI2c(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "i2c-write")) {
    i2cWriteMessage(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "i2c-wave")) {
    i2cWaveMessage(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "lcd-test")) {
    startLcdTest(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "lcd-stop")) {
    stopLcdTest();
  } else if (equalsIgnoreCase(argv[0], "lcd-backlight")) {
    controlLcdBacklight(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "tcs-read")) {
    if (argc != 1) {
      Serial.println(F("Uso: tcs-read"));
      return;
    }
    runTcsRead();
  } else if (equalsIgnoreCase(argv[0], "tcs-stream")) {
    startTcsStream(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "tcs-stop")) {
    if (argc != 1) {
      Serial.println(F("Uso: tcs-stop"));
      return;
    }
    stopTcsStream(true);
  } else if (equalsIgnoreCase(argv[0], "tcs-cal")) {
    calibrateTcs(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "pcf-probe")) {
    pcfProbe(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "pcf-loop")) {
    pcfLoop(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "pcf-read")) {
    pcfRead(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "pcf-write")) {
    pcfWrite(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "rs485-send")) {
    sendRs485(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "rs485-burst")) {
    sendRs485Burst(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "rs485-di")) {
    holdRs485Di(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "rs485-read")) {
    readRs485Available(true);
  } else if (equalsIgnoreCase(argv[0], "rs485-clear")) {
    Serial.print(F("Bytes RS485 descartados: "));
    Serial.println(clearRs485Rx(0));
  } else if (equalsIgnoreCase(argv[0], "rs485-rx-level")) {
    printRs485RxLevel();
  } else if (equalsIgnoreCase(argv[0], "edges")) {
    printEdges();
  } else if (equalsIgnoreCase(argv[0], "clear-edges")) {
    clearEdges();
    Serial.println(F("Contadores zerados."));
  } else if (equalsIgnoreCase(argv[0], "set")) {
    if (argc != 3) {
      Serial.println(F("Uso: set <saida> <0|1|low|high>"));
      return;
    }
    const int8_t outputIndex = findOutput(argv[1]);
    bool level = false;
    if (outputIndex < 0 || !parseLevel(argv[2], level)) {
      Serial.println(F("Erro: saida ou nivel invalido."));
      return;
    }
    driveOutput(static_cast<uint8_t>(outputIndex), level, true);
  } else if (equalsIgnoreCase(argv[0], "pulse") ||
             equalsIgnoreCase(argv[0], "pulseus")) {
    if (argc != 5) {
      Serial.println(F("Uso: pulse <saida> <high> <low> <vezes>"));
      return;
    }
    const int8_t outputIndex = findOutput(argv[1]);
    uint32_t highTime = 0;
    uint32_t lowTime = 0;
    uint32_t count = 0;
    if (outputIndex < 0 || !parseUInt(argv[2], highTime) ||
        !parseUInt(argv[3], lowTime) || !parseUInt(argv[4], count)) {
      Serial.println(F("Erro: parametros invalidos para pulso."));
      return;
    }
    pulseOutput(static_cast<uint8_t>(outputIndex), highTime, lowTime, count,
                equalsIgnoreCase(argv[0], "pulseus"));
  } else if (equalsIgnoreCase(argv[0], "watch")) {
    if (argc < 2 || argc > 3) {
      Serial.println(F("Uso: watch on|off [period_ms]"));
      return;
    }
    if (equalsIgnoreCase(argv[1], "on")) {
      uint32_t period = periodicStatusMs;
      if (argc == 3 && (!parseUInt(argv[2], period) || period < 50)) {
        Serial.println(F("Erro: periodo minimo 50 ms."));
        return;
      }
      watchChanges = true;
      periodicStatus = argc == 3;
      periodicStatusMs = period;
      lastPeriodicStatusMs = millis();
      Serial.println(F("Monitoramento de entradas ligado."));
    } else if (equalsIgnoreCase(argv[1], "off")) {
      watchChanges = false;
      periodicStatus = false;
      Serial.println(F("Monitoramento desligado."));
    } else {
      Serial.println(F("Uso: watch on|off [period_ms]"));
    }
  } else {
    Serial.println(F("Comando desconhecido. Use help."));
  }
}

void readConsole() {
  while (Serial.available() > 0) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\r') {
      continue;
    }
    if (value == '\n') {
      Serial.println();
      commandBuffer[commandLength] = '\0';
      processCommand(commandBuffer);
      commandLength = 0;
      commandBuffer[0] = '\0';
      continue;
    }
    if (value == '\b' || value == 0x7F) {
      if (commandLength > 0) {
        commandLength--;
        Serial.print(F("\b \b"));
      }
      continue;
    }
    if (static_cast<size_t>(commandLength + 1) < sizeof(commandBuffer)) {
      commandBuffer[commandLength++] = value;
      Serial.write(value);
    } else {
      commandLength = 0;
      Serial.println(F("Linha muito longa; buffer limpo."));
    }
  }
}

void setupOutputs() {
  stopDcMotor(false);
  for (uint8_t index = 0; index < OUTPUT_COUNT; index++) {
    const Signal signal = readSignal(OUTPUTS, index);
    pinMode(signal.pin, OUTPUT);
    digitalWrite(signal.pin, LOW);
    outputLevels[index] = false;
  }
}

void setupInputs() {
  for (uint8_t index = 0; index < INPUT_COUNT; index++) {
    const Signal signal = readSignal(INPUTS, index);
    pinMode(signal.pin, INPUT);
    lastInputLevels[index] = digitalRead(signal.pin);
  }
  pinMode(board::PIN_RX_MODBUS, INPUT);
}

void setupBuses() {
  beginI2c(board::I2C_CLOCK_HZ);
  restartRs485();
  driveRs485Direction(false);
}

void setup() {
  Serial.begin(board::CONSOLE_BAUD);
  delay(500);
  setupOutputs();
  setupInputs();
  setupBuses();
  setupTcs();

  Serial.println();
  Serial.println(F("MYT Arduino Nano V3 - firmware de ensaio"));
  Serial.println(F("SW1 deve estar em ARDUINO antes de energizar a placa."));
  Serial.println(F("Saidas iniciadas em LOW; RS485 em recepcao."));
  Serial.println(F("TCS: S2=A2 S3=A3 OUT=D9, escala externa em 20%."));
  printTcsWarning();
  Serial.println(F("Use help para listar os comandos."));
  printSignals();
}

void loop() {
  readConsole();
  readRs485Available(false);
  serviceInputWatch();
  servicePeriodicStatus();
  serviceLcdTest();
  serviceDcMotor();
  serviceTcsStream();
}
