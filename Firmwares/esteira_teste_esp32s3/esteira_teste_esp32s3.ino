#include <Arduino.h>
#include <TCS230_ESP32.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace board {
constexpr uint32_t CONSOLE_BAUD = 115200;
constexpr uint32_t RS485_BAUD = 9600;
constexpr uint32_t I2C_CLOCK_HZ = 100000;
constexpr uint8_t LCD_I2C_ADDRESS = 0x27;
constexpr uint8_t LCD_DEFAULT_COLUMNS = 20;
constexpr uint8_t LCD_DEFAULT_ROWS = 4;
constexpr uint32_t LCD_DEFAULT_INTERVAL_MS = 1000;
constexpr uint32_t DC_MOTOR_PWM_HZ = 20000;
constexpr uint8_t DC_MOTOR_PWM_BITS = 8;
constexpr uint8_t DC_MOTOR_PWM_CHANNEL_IN1 = 6;
constexpr uint8_t DC_MOTOR_PWM_CHANNEL_IN2 = 7;
constexpr uint8_t DC_MOTOR_DEFAULT_DUTY = 180;
constexpr uint32_t DC_MOTOR_DEFAULT_RAMP_MS = 2000;
constexpr uint32_t DC_MOTOR_DEFAULT_HOLD_MS = 1000;
constexpr uint32_t DC_MOTOR_DEFAULT_PAUSE_MS = 1000;
constexpr uint8_t PCF8574_ADDR_GND = 0x20;
constexpr uint8_t PCF8574A_ADDR_GND = 0x38;
constexpr uint32_t TCS_SAMPLE_MS = 20;
constexpr uint32_t TCS_DEFAULT_STREAM_MS = 1000;
constexpr uint32_t TCS_MIN_STREAM_MS = 250;
constexpr uint32_t TCS_MAX_STREAM_MS = 60000;
constexpr uint8_t TCS_CALIBRATION_SAMPLES = 4;

constexpr uint8_t PIN_TRIGGER = 1;
constexpr uint8_t PIN_ECHO = 2;
constexpr uint8_t PIN_SENSOR_IND = 5;
constexpr uint8_t PIN_TCS_S2 = 6;
constexpr uint8_t PIN_TCS_S3 = 7;
constexpr uint8_t PIN_DIR_MODBUS = 8;
constexpr uint8_t PIN_ENCODER_A = 9;
constexpr uint8_t PIN_ENCODER_B = 10;
constexpr uint8_t PIN_ENCODER_SW = 11;
constexpr uint8_t PIN_INT_GPIO = 12;
constexpr uint8_t PIN_INT_KEYPAD = 13;
constexpr uint8_t PIN_TCS_OUT = 14;
constexpr uint8_t PIN_TX_MODBUS = 17;
constexpr uint8_t PIN_RX_MODBUS = 18;
constexpr uint8_t PIN_ZERO_MAG = 21;
constexpr uint8_t PIN_CC_IN1 = 35;
constexpr uint8_t PIN_CC_IN2 = 36;
constexpr uint8_t PIN_STEP = 37;
constexpr uint8_t PIN_DIR = 38;
constexpr uint8_t PIN_I2C_SCL = 47;
constexpr uint8_t PIN_I2C_SDA = 48;
} // namespace board

struct Signal {
  const char *name;
  uint8_t pin;
  const char *description;
};

const Signal OUTPUTS[] = {
    {"TRIGGER", board::PIN_TRIGGER, "Pulso de disparo do sensor ultrassonico"},
    {"TCS_S2", board::PIN_TCS_S2, "Selecao S2 do sensor TCS"},
    {"TCS_S3", board::PIN_TCS_S3, "Selecao S3 do sensor TCS"},
    {"DIR_MODBUS", board::PIN_DIR_MODBUS, "Direcao do transceptor RS-485"},
    {"CC_IN1", board::PIN_CC_IN1, "Entrada IN1 do driver do motor CC"},
    {"CC_IN2", board::PIN_CC_IN2, "Entrada IN2 do driver do motor CC"},
    {"STEP", board::PIN_STEP, "Pulso STEP do driver de passo"},
    {"DIR", board::PIN_DIR, "Direcao do driver de passo"},
};

const Signal INPUTS[] = {
    {"ECHO", board::PIN_ECHO, "Retorno do sensor ultrassonico"},
    {"SENSOR_IND", board::PIN_SENSOR_IND, "Entrada do sensor indutivo/optoacoplada"},
    {"ENCODER_A", board::PIN_ENCODER_A, "Canal A do encoder da IHM"},
    {"ENCODER_B", board::PIN_ENCODER_B, "Canal B do encoder da IHM"},
    {"ENCODER_SW", board::PIN_ENCODER_SW, "Chave do encoder da IHM"},
    {"INT_GPIO", board::PIN_INT_GPIO, "Interrupcao do expansor de GPIO"},
    {"INT_KEYPAD", board::PIN_INT_KEYPAD, "Interrupcao do teclado/display HT16K33"},
    {"TCS_OUT", board::PIN_TCS_OUT, "Saida de frequencia do sensor TCS"},
    {"ZERO_MAG", board::PIN_ZERO_MAG, "Sensor de zero do magazine"},
};

constexpr size_t OUTPUT_COUNT = sizeof(OUTPUTS) / sizeof(OUTPUTS[0]);
constexpr size_t INPUT_COUNT = sizeof(INPUTS) / sizeof(INPUTS[0]);
constexpr uint8_t MAX_I2C_MESSAGE_BYTES = 12;

HardwareSerial Rs485Serial(1);
hd44780_I2Cexp lcd(board::LCD_I2C_ADDRESS, I2Cexp_BOARD_YWROBOT);
TCS230 tcs(static_cast<gpio_num_t>(board::PIN_TCS_OUT), board::PIN_TCS_S2,
           board::PIN_TCS_S3);

struct TcsReading {
  uint32_t frequency[4];
  uint8_t rgb[3];
  uint8_t color;
  bool calibrated;
  bool valid;
};

enum class LcdTestPhase : uint8_t {
  Idle,
  Intro,
  Lines,
  CustomCharacters,
  Cursor,
  Blink,
  DisplayOff,
  DisplayOn,
  BacklightOff,
  BacklightOn,
  ScrollSetup,
  ScrollLeft,
  ScrollRight,
  LiveStatus,
  CharacterTable,
};

enum class DcMotorTestPhase : uint8_t {
  Idle,
  RampDirection1Up,
  HoldDirection1,
  RampDirection1Down,
  PauseBeforeDirection2,
  RampDirection2Up,
  HoldDirection2,
  RampDirection2Down,
};

bool outputLevels[OUTPUT_COUNT] = {};
bool outputEnabled[OUTPUT_COUNT] = {};
uint8_t lastInputLevels[INPUT_COUNT] = {};
volatile uint32_t inputEdges[INPUT_COUNT] = {};

bool watchChanges = false;
bool periodicStatus = false;
uint32_t periodicStatusMs = 1000;
uint32_t lastPeriodicStatusMs = 0;
uint32_t activeI2cClockHz = board::I2C_CLOCK_HZ;

bool lcdTestActive = false;
bool lcdInitialized = false;
bool lcdBacklightEnabled = false;
uint8_t lcdColumns = board::LCD_DEFAULT_COLUMNS;
uint8_t lcdRows = board::LCD_DEFAULT_ROWS;
uint32_t lcdTestIntervalMs = board::LCD_DEFAULT_INTERVAL_MS;
uint32_t lcdNextStepMs = 0;
uint32_t lcdFrame = 0;
uint8_t lcdPhaseStep = 0;
uint8_t lcdFirstCharacter = 32;
LcdTestPhase lcdTestPhase = LcdTestPhase::Idle;

bool dcMotorTestActive = false;
bool dcMotorPwmConfigured = false;
uint8_t dcMotorMaxDuty = board::DC_MOTOR_DEFAULT_DUTY;
uint8_t dcMotorCurrentDuty = 0;
int8_t dcMotorDirection = 0;
uint32_t dcMotorRampMs = board::DC_MOTOR_DEFAULT_RAMP_MS;
uint32_t dcMotorHoldMs = board::DC_MOTOR_DEFAULT_HOLD_MS;
uint32_t dcMotorPauseMs = board::DC_MOTOR_DEFAULT_PAUSE_MS;
uint32_t dcMotorPhaseStartedMs = 0;
DcMotorTestPhase dcMotorTestPhase = DcMotorTestPhase::Idle;

bool tcsInitialized = false;
bool tcsStreamActive = false;
bool tcsDarkCalibrated = false;
bool tcsWhiteCalibrated = false;
uint32_t tcsStreamPeriodMs = board::TCS_DEFAULT_STREAM_MS;
uint32_t tcsLastStreamMs = 0;
sensorData tcsDarkCalibration = {};
sensorData tcsWhiteCalibration = {};
TcsReading tcsLastReading = {};

char commandBuffer[200] = {};
size_t commandLength = 0;

void stopTcsStream(bool verbose);

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

  value = static_cast<uint32_t>(parsed);
  return true;
}

bool parseUIntAutoBase(const char *text, uint32_t &value) {
  char *end = nullptr;
  const unsigned long parsed = strtoul(text, &end, 0);
  if (end == text || *end != '\0') {
    return false;
  }

  value = static_cast<uint32_t>(parsed);
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

int findOutput(const char *name) {
  for (size_t index = 0; index < OUTPUT_COUNT; index++) {
    if (equalsIgnoreCase(name, OUTPUTS[index].name)) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

int findInput(const char *name) {
  for (size_t index = 0; index < INPUT_COUNT; index++) {
    if (equalsIgnoreCase(name, INPUTS[index].name)) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

void updateDcMotorOutputTracking(uint8_t dutyIn1, uint8_t dutyIn2) {
  const int in1Index = findOutput("CC_IN1");
  const int in2Index = findOutput("CC_IN2");

  if (in1Index >= 0) {
    outputEnabled[in1Index] = true;
    outputLevels[in1Index] = dutyIn1 > 0;
  }
  if (in2Index >= 0) {
    outputEnabled[in2Index] = true;
    outputLevels[in2Index] = dutyIn2 > 0;
  }
}

void setDcMotorDuty(int8_t direction, uint8_t duty) {
  if (!dcMotorPwmConfigured) {
    return;
  }

  if (direction > 0) {
    ledcWrite(board::DC_MOTOR_PWM_CHANNEL_IN2, 0);
    ledcWrite(board::DC_MOTOR_PWM_CHANNEL_IN1, duty);
    updateDcMotorOutputTracking(duty, 0);
  } else if (direction < 0) {
    ledcWrite(board::DC_MOTOR_PWM_CHANNEL_IN1, 0);
    ledcWrite(board::DC_MOTOR_PWM_CHANNEL_IN2, duty);
    updateDcMotorOutputTracking(0, duty);
  } else {
    ledcWrite(board::DC_MOTOR_PWM_CHANNEL_IN1, 0);
    ledcWrite(board::DC_MOTOR_PWM_CHANNEL_IN2, 0);
    updateDcMotorOutputTracking(0, 0);
    duty = 0;
  }

  dcMotorDirection = direction;
  dcMotorCurrentDuty = duty;
}

void stopDcMotorTest(bool verbose) {
  if (dcMotorPwmConfigured) {
    ledcWrite(board::DC_MOTOR_PWM_CHANNEL_IN1, 0);
    ledcWrite(board::DC_MOTOR_PWM_CHANNEL_IN2, 0);
    ledcDetachPin(board::PIN_CC_IN1);
    ledcDetachPin(board::PIN_CC_IN2);
  }

  pinMode(board::PIN_CC_IN1, OUTPUT);
  pinMode(board::PIN_CC_IN2, OUTPUT);
  digitalWrite(board::PIN_CC_IN1, LOW);
  digitalWrite(board::PIN_CC_IN2, LOW);
  updateDcMotorOutputTracking(0, 0);

  dcMotorPwmConfigured = false;
  dcMotorTestActive = false;
  dcMotorCurrentDuty = 0;
  dcMotorDirection = 0;
  dcMotorTestPhase = DcMotorTestPhase::Idle;

  if (verbose) {
    Serial.println("Teste do motor CC interrompido; CC_IN1 e CC_IN2 em LOW.");
  }
}

void startDcMotorTest(char *argv[], int argc) {
  uint32_t requestedDuty = board::DC_MOTOR_DEFAULT_DUTY;
  uint32_t requestedRampMs = board::DC_MOTOR_DEFAULT_RAMP_MS;
  uint32_t requestedHoldMs = board::DC_MOTOR_DEFAULT_HOLD_MS;
  uint32_t requestedPauseMs = board::DC_MOTOR_DEFAULT_PAUSE_MS;

  if (argc > 5 || (argc >= 2 && !parseUInt(argv[1], requestedDuty)) ||
      (argc >= 3 && !parseUInt(argv[2], requestedRampMs)) ||
      (argc >= 4 && !parseUInt(argv[3], requestedHoldMs)) ||
      (argc == 5 && !parseUInt(argv[4], requestedPauseMs))) {
    Serial.println("Uso: cc-jog [duty_1a255] [rampa_ms] [patamar_ms] [pausa_ms]");
    return;
  }

  if (requestedDuty < 1 || requestedDuty > 255 || requestedRampMs < 100 ||
      requestedRampMs > 30000 || requestedHoldMs > 30000 ||
      requestedPauseMs < 100 || requestedPauseMs > 30000) {
    Serial.println("Erro: duty 1..255, rampa 100..30000 ms, patamar 0..30000 ms e pausa 100..30000 ms.");
    return;
  }

  stopTcsStream(false);
  stopDcMotorTest(false);

  const uint32_t frequencyIn1 =
      ledcSetup(board::DC_MOTOR_PWM_CHANNEL_IN1, board::DC_MOTOR_PWM_HZ,
                board::DC_MOTOR_PWM_BITS);
  const uint32_t frequencyIn2 =
      ledcSetup(board::DC_MOTOR_PWM_CHANNEL_IN2, board::DC_MOTOR_PWM_HZ,
                board::DC_MOTOR_PWM_BITS);
  if (frequencyIn1 == 0 || frequencyIn2 == 0) {
    stopDcMotorTest(false);
    Serial.println("Falha ao configurar PWM LEDC para o motor CC.");
    return;
  }

  ledcWrite(board::DC_MOTOR_PWM_CHANNEL_IN1, 0);
  ledcWrite(board::DC_MOTOR_PWM_CHANNEL_IN2, 0);
  ledcAttachPin(board::PIN_CC_IN1, board::DC_MOTOR_PWM_CHANNEL_IN1);
  ledcAttachPin(board::PIN_CC_IN2, board::DC_MOTOR_PWM_CHANNEL_IN2);

  dcMotorPwmConfigured = true;
  dcMotorMaxDuty = static_cast<uint8_t>(requestedDuty);
  dcMotorRampMs = requestedRampMs;
  dcMotorHoldMs = requestedHoldMs;
  dcMotorPauseMs = requestedPauseMs;
  dcMotorPhaseStartedMs = millis();
  dcMotorTestPhase = DcMotorTestPhase::RampDirection1Up;
  dcMotorTestActive = true;
  setDcMotorDuty(1, 0);

  Serial.printf("Teste motor CC iniciado: PWM %lu Hz, duty max %lu/255, rampa %lu ms, patamar %lu ms, pausa %lu ms.\r\n",
                static_cast<unsigned long>(frequencyIn1),
                static_cast<unsigned long>(requestedDuty),
                static_cast<unsigned long>(requestedRampMs),
                static_cast<unsigned long>(requestedHoldMs),
                static_cast<unsigned long>(requestedPauseMs));
  Serial.println("Sentido 1: CC_IN1 com PWM e CC_IN2 em LOW.");
  Serial.println("Use 'cc-stop' para parada imediata.");
}

uint8_t dcMotorRampUpDuty(uint32_t elapsedMs) {
  if (elapsedMs >= dcMotorRampMs) {
    return dcMotorMaxDuty;
  }
  return static_cast<uint8_t>(
      (static_cast<uint32_t>(dcMotorMaxDuty) * elapsedMs) / dcMotorRampMs);
}

uint8_t dcMotorRampDownDuty(uint32_t elapsedMs) {
  if (elapsedMs >= dcMotorRampMs) {
    return 0;
  }
  return static_cast<uint8_t>(
      (static_cast<uint32_t>(dcMotorMaxDuty) * (dcMotorRampMs - elapsedMs)) /
      dcMotorRampMs);
}

void serviceDcMotorTest() {
  if (!dcMotorTestActive) {
    return;
  }

  const uint32_t now = millis();
  const uint32_t elapsedMs = now - dcMotorPhaseStartedMs;

  switch (dcMotorTestPhase) {
  case DcMotorTestPhase::Idle:
    return;

  case DcMotorTestPhase::RampDirection1Up:
    setDcMotorDuty(1, dcMotorRampUpDuty(elapsedMs));
    if (elapsedMs >= dcMotorRampMs) {
      dcMotorPhaseStartedMs = now;
      dcMotorTestPhase = DcMotorTestPhase::HoldDirection1;
    }
    break;

  case DcMotorTestPhase::HoldDirection1:
    setDcMotorDuty(1, dcMotorMaxDuty);
    if (elapsedMs >= dcMotorHoldMs) {
      dcMotorPhaseStartedMs = now;
      dcMotorTestPhase = DcMotorTestPhase::RampDirection1Down;
    }
    break;

  case DcMotorTestPhase::RampDirection1Down:
    setDcMotorDuty(1, dcMotorRampDownDuty(elapsedMs));
    if (elapsedMs >= dcMotorRampMs) {
      setDcMotorDuty(0, 0);
      dcMotorPhaseStartedMs = now;
      dcMotorTestPhase = DcMotorTestPhase::PauseBeforeDirection2;
      Serial.println("Sentido 1 finalizado; motor parado antes da inversao.");
    }
    break;

  case DcMotorTestPhase::PauseBeforeDirection2:
    setDcMotorDuty(0, 0);
    if (elapsedMs >= dcMotorPauseMs) {
      dcMotorPhaseStartedMs = now;
      dcMotorTestPhase = DcMotorTestPhase::RampDirection2Up;
      Serial.println("Sentido 2: CC_IN1 em LOW e CC_IN2 com PWM.");
    }
    break;

  case DcMotorTestPhase::RampDirection2Up:
    setDcMotorDuty(-1, dcMotorRampUpDuty(elapsedMs));
    if (elapsedMs >= dcMotorRampMs) {
      dcMotorPhaseStartedMs = now;
      dcMotorTestPhase = DcMotorTestPhase::HoldDirection2;
    }
    break;

  case DcMotorTestPhase::HoldDirection2:
    setDcMotorDuty(-1, dcMotorMaxDuty);
    if (elapsedMs >= dcMotorHoldMs) {
      dcMotorPhaseStartedMs = now;
      dcMotorTestPhase = DcMotorTestPhase::RampDirection2Down;
    }
    break;

  case DcMotorTestPhase::RampDirection2Down:
    setDcMotorDuty(-1, dcMotorRampDownDuty(elapsedMs));
    if (elapsedMs >= dcMotorRampMs) {
      stopDcMotorTest(false);
      Serial.println("Teste motor CC concluido; CC_IN1 e CC_IN2 em LOW.");
    }
    break;
  }
}

uint32_t edgeCount(size_t inputIndex) {
  noInterrupts();
  const uint32_t value = inputEdges[inputIndex];
  interrupts();
  return value;
}

void clearEdges() {
  noInterrupts();
  for (size_t index = 0; index < INPUT_COUNT; index++) {
    inputEdges[index] = 0;
  }
  interrupts();
}

void driveOutput(size_t outputIndex, bool level, bool verbose) {
  const uint8_t pin = OUTPUTS[outputIndex].pin;
  if (pin == board::PIN_TCS_S2 || pin == board::PIN_TCS_S3) {
    stopTcsStream(false);
  }
  if ((dcMotorTestActive || dcMotorPwmConfigured) &&
      (pin == board::PIN_CC_IN1 || pin == board::PIN_CC_IN2)) {
    stopDcMotorTest(false);
  }

  if (!outputEnabled[outputIndex]) {
    pinMode(OUTPUTS[outputIndex].pin, OUTPUT);
    outputEnabled[outputIndex] = true;
  }

  digitalWrite(OUTPUTS[outputIndex].pin, level ? HIGH : LOW);
  outputLevels[outputIndex] = level;

  if (verbose) {
    Serial.printf("OUT %-11s GPIO%02u = %u\r\n", OUTPUTS[outputIndex].name,
                  OUTPUTS[outputIndex].pin, level ? 1 : 0);
  }
}

void allOutputsLow() {
  stopTcsStream(false);
  for (size_t index = 0; index < OUTPUT_COUNT; index++) {
    driveOutput(index, false, false);
  }
  Serial.println("Todas as saidas digitais foram colocadas em LOW.");
}

void printSignals() {
  Serial.println();
  Serial.println("Saidas digitais:");
  for (size_t index = 0; index < OUTPUT_COUNT; index++) {
    Serial.printf("  %-11s GPIO%02u  %s\r\n", OUTPUTS[index].name,
                  OUTPUTS[index].pin, OUTPUTS[index].description);
  }

  Serial.println("Entradas digitais:");
  for (size_t index = 0; index < INPUT_COUNT; index++) {
    Serial.printf("  %-11s GPIO%02u  %s\r\n", INPUTS[index].name,
                  INPUTS[index].pin, INPUTS[index].description);
  }

  Serial.println("Barramentos:");
  const int dirIndex = findOutput("DIR_MODBUS");
  if (dirIndex >= 0) {
    Serial.printf("  RS485 UART  TX=GPIO%02u RX=GPIO%02u DIR=%s/GPIO%02u %lu bps\r\n",
                  board::PIN_TX_MODBUS, board::PIN_RX_MODBUS,
                  OUTPUTS[dirIndex].name, OUTPUTS[dirIndex].pin,
                  static_cast<unsigned long>(board::RS485_BAUD));
  }
  Serial.printf("  I2C         SCL=GPIO%02u SDA=GPIO%02u %lu Hz\r\n",
                board::PIN_I2C_SCL, board::PIN_I2C_SDA,
                static_cast<unsigned long>(board::I2C_CLOCK_HZ));
}

void printStatus() {
  Serial.println();
  Serial.println("Estado das saidas:");
  for (size_t index = 0; index < OUTPUT_COUNT; index++) {
    if (dcMotorTestActive &&
        (OUTPUTS[index].pin == board::PIN_CC_IN1 ||
         OUTPUTS[index].pin == board::PIN_CC_IN2)) {
      const uint8_t duty =
          (OUTPUTS[index].pin == board::PIN_CC_IN1 && dcMotorDirection > 0) ||
                  (OUTPUTS[index].pin == board::PIN_CC_IN2 &&
                   dcMotorDirection < 0)
              ? dcMotorCurrentDuty
              : 0;
      Serial.printf("  %-11s GPIO%02u = PWM %u/255\r\n",
                    OUTPUTS[index].name, OUTPUTS[index].pin, duty);
      continue;
    }

    if (outputEnabled[index]) {
      Serial.printf("  %-11s GPIO%02u = %u\r\n", OUTPUTS[index].name,
                    OUTPUTS[index].pin, outputLevels[index] ? 1 : 0);
    } else {
      Serial.printf("  %-11s GPIO%02u = Hi-Z\r\n", OUTPUTS[index].name,
                    OUTPUTS[index].pin);
    }
  }

  Serial.println("Estado das entradas:");
  for (size_t index = 0; index < INPUT_COUNT; index++) {
    const uint8_t level = digitalRead(INPUTS[index].pin);
    Serial.printf("  %-11s GPIO%02u = %u  edges=%lu\r\n", INPUTS[index].name,
                  INPUTS[index].pin, level,
                  static_cast<unsigned long>(edgeCount(index)));
  }

  Serial.printf("RS485 disponivel para leitura: %d byte(s)\r\n",
                Rs485Serial.available());
  Serial.printf("Teste LCD 0x%02X: %s (%ux%u), backlight %s\r\n",
                board::LCD_I2C_ADDRESS, lcdTestActive ? "ATIVO" : "parado",
                lcdColumns, lcdRows,
                lcdBacklightEnabled ? "ON" : "OFF");
  Serial.printf("Teste motor CC: %s, sentido %d, duty %u/255\r\n",
                dcMotorTestActive ? "ATIVO" : "parado", dcMotorDirection,
                dcMotorCurrentDuty);
  Serial.printf("TCS: %s, stream %s/%lu ms, calibracao %s%s\r\n",
                tcsInitialized ? "pronto" : "indisponivel",
                tcsStreamActive ? "ATIVO" : "parado",
                static_cast<unsigned long>(tcsStreamPeriodMs),
                tcsDarkCalibrated ? "dark" : "sem dark",
                tcsWhiteCalibrated ? "+white" : "");
  if (tcsLastReading.valid) {
    Serial.printf("TCS ultimo Hz: R=%lu G=%lu B=%lu C=%lu\r\n",
                  static_cast<unsigned long>(tcsLastReading.frequency[0]),
                  static_cast<unsigned long>(tcsLastReading.frequency[1]),
                  static_cast<unsigned long>(tcsLastReading.frequency[2]),
                  static_cast<unsigned long>(tcsLastReading.frequency[3]));
  }
}

void printHelp() {
  Serial.println();
  Serial.println("Comandos do firmware de ensaio:");
  Serial.println("  help");
  Serial.println("  list");
  Serial.println("  status");
  Serial.println("  set <saida> <0|1|low|high>");
  Serial.println("  pulse <saida> <high_ms> <low_ms> <vezes>");
  Serial.println("  pulseus <saida> <high_us> <low_us> <vezes>");
  Serial.println("  all-low");
  Serial.println("  trigger");
  Serial.println("  cc-jog [duty_1a255] [rampa_ms] [patamar_ms] [pausa_ms]");
  Serial.println("  cc-stop");
  Serial.println("  i2c-level");
  Serial.println("  i2c-scan [clock_hz]");
  Serial.println("  i2c-write <addr> <byte> [byte...]");
  Serial.println("  i2c-wave <addr> <repeticoes> <byte> [byte...]");
  Serial.println("  lcd-test [colunas] [linhas] [intervalo_ms]");
  Serial.println("  lcd-stop");
  Serial.println("  lcd-backlight <on|off>");
  Serial.println("  tcs-read");
  Serial.println("  tcs-stream [period_ms]");
  Serial.println("  tcs-stop");
  Serial.println("  tcs-cal <dark|white|reset>");
  Serial.println("  pcf-probe [addr]");
  Serial.println("  pcf-loop <addr> [tempo_ms]");
  Serial.println("  pcf-read <addr>");
  Serial.println("  pcf-write <addr> <byte>");
  Serial.println("  rs485-send <texto>");
  Serial.println("  rs485-burst [tempo_ms]");
  Serial.println("  rs485-di <0|1|low|high> [tempo_ms]");
  Serial.println("  rs485-read");
  Serial.println("  rs485-clear");
  Serial.println("  rs485-rx-level");
  Serial.println("  edges");
  Serial.println("  clear-edges");
  Serial.println("  watch on|off [period_ms]");
  Serial.println();
  Serial.println("Exemplos:");
  Serial.println("  set CC_IN1 1");
  Serial.println("  cc-jog");
  Serial.println("  cc-jog 180 2000 1000 1000");
  Serial.println("  pulseus STEP 500 500 100");
  Serial.println("  set DIR 1");
  Serial.println("  i2c-scan");
  Serial.println("  i2c-wave 0x38 50 0x55 0xAA 0xFF");
  Serial.println("  lcd-test");
  Serial.println("  lcd-test 20 4 1200");
  Serial.println("  lcd-backlight on");
  Serial.println("  pcf-probe");
  Serial.println("  pcf-write 0x20 0xFF");
  Serial.println("  rs485-di 1 5000");
  Serial.println("  rs485-burst 3000");
}

void pulseOutput(size_t outputIndex, uint32_t highTime, uint32_t lowTime,
                 uint32_t count, bool microseconds) {
  if (count == 0 || count > 100000) {
    Serial.println("Erro: quantidade de pulsos fora do intervalo 1..100000.");
    return;
  }

  Serial.printf("Gerando %lu pulso(s) em %s/GPIO%02u.\r\n",
                static_cast<unsigned long>(count), OUTPUTS[outputIndex].name,
                OUTPUTS[outputIndex].pin);

  for (uint32_t pulse = 0; pulse < count; pulse++) {
    driveOutput(outputIndex, true, false);
    if (microseconds) {
      delayMicroseconds(highTime);
    } else {
      delay(highTime);
    }

    driveOutput(outputIndex, false, false);
    if (microseconds) {
      delayMicroseconds(lowTime);
    } else {
      delay(lowTime);
    }
  }

  Serial.println("Pulsos finalizados. Saida terminou em LOW.");
}

void runUltrasonicTrigger() {
  const int triggerIndex = findOutput("TRIGGER");
  if (triggerIndex < 0) {
    Serial.println("Erro interno: TRIGGER nao encontrado.");
    return;
  }

  driveOutput(static_cast<size_t>(triggerIndex), false, false);
  delayMicroseconds(2);
  driveOutput(static_cast<size_t>(triggerIndex), true, false);
  delayMicroseconds(10);
  driveOutput(static_cast<size_t>(triggerIndex), false, false);

  const uint32_t durationUs = pulseIn(board::PIN_ECHO, HIGH, 30000);
  if (durationUs == 0) {
    Serial.println("ECHO timeout apos 30 ms.");
    return;
  }

  const float distanceCm = durationUs / 58.0f;
  Serial.printf("ECHO = %lu us (~%.1f cm para HC-SR04).\r\n",
                static_cast<unsigned long>(durationUs), distanceCm);
}

void beginI2c(uint32_t clockHz) {
  Wire.end();
  Wire.setPins(board::PIN_I2C_SDA, board::PIN_I2C_SCL);
  Wire.begin();
  Wire.setClock(clockHz);
  activeI2cClockHz = clockHz;
}

void printLcdLine(uint8_t row, const char *text) {
  if (row >= lcdRows) {
    return;
  }

  lcd.setCursor(0, row);
  uint8_t column = 0;
  while (column < lcdColumns && text[column] != '\0') {
    lcd.write(static_cast<uint8_t>(text[column]));
    column++;
  }
  while (column < lcdColumns) {
    lcd.write(' ');
    column++;
  }
}

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
    Serial.printf("Falha ao iniciar LCD em 0x%02X (codigo %d).\r\n",
                  board::LCD_I2C_ADDRESS, status);
    return false;
  }

  lcdInitialized = true;
  return true;
}

void resetLcdTextMode() {
  lcd.display();
  setLcdBacklight(true);
  lcd.noCursor();
  lcd.noBlink();
  lcd.noAutoscroll();
  lcd.leftToRight();
}

void scheduleLcdStep(uint32_t now, uint32_t delayMs,
                     LcdTestPhase nextPhase) {
  lcdNextStepMs = now + delayMs;
  lcdTestPhase = nextPhase;
}

void startLcdTest(char *argv[], int argc) {
  uint32_t requestedColumns = board::LCD_DEFAULT_COLUMNS;
  uint32_t requestedRows = board::LCD_DEFAULT_ROWS;
  uint32_t requestedIntervalMs = board::LCD_DEFAULT_INTERVAL_MS;

  if (argc > 4 ||
      (argc >= 2 && !parseUInt(argv[1], requestedColumns)) ||
      (argc >= 3 && !parseUInt(argv[2], requestedRows)) ||
      (argc == 4 && !parseUInt(argv[3], requestedIntervalMs))) {
    Serial.println("Uso: lcd-test [colunas] [linhas] [intervalo_ms]");
    return;
  }

  if (requestedColumns < 8 || requestedColumns > 40 || requestedRows < 1 ||
      requestedRows > 4 || requestedIntervalMs < 200 ||
      requestedIntervalMs > 10000) {
    Serial.println("Erro: use 8..40 colunas, 1..4 linhas e intervalo de 200..10000 ms.");
    return;
  }

  stopTcsStream(false);

  lcdColumns = static_cast<uint8_t>(requestedColumns);
  lcdRows = static_cast<uint8_t>(requestedRows);
  lcdTestIntervalMs = requestedIntervalMs;

  if (!initializeLcd(lcdColumns, lcdRows)) {
    lcdTestActive = false;
    lcdTestPhase = LcdTestPhase::Idle;
    return;
  }

  if (!setLcdBacklight(true)) {
    lcdTestActive = false;
    lcdTestPhase = LcdTestPhase::Idle;
    Serial.println("Falha ao ligar o backlight; teste nao iniciado.");
    return;
  }

  lcdTestActive = true;
  lcdTestPhase = LcdTestPhase::Intro;
  lcdNextStepMs = millis();
  lcdFrame = 0;
  lcdPhaseStep = 0;
  lcdFirstCharacter = 32;
  resetLcdTextMode();
  lcd.clear();

  Serial.printf("Teste continuo do LCD iniciado em 0x%02X (%lux%lu, intervalo %lu ms).\r\n",
                board::LCD_I2C_ADDRESS,
                static_cast<unsigned long>(requestedColumns),
                static_cast<unsigned long>(requestedRows),
                static_cast<unsigned long>(requestedIntervalMs));
  Serial.println("Use 'lcd-stop' para encerrar.");
}

void controlLcdBacklight(char *argv[], int argc) {
  bool enabled = false;
  if (argc != 2 || !parseLevel(argv[1], enabled)) {
    Serial.println("Uso: lcd-backlight <on|off>");
    return;
  }

  if (!lcdInitialized && !initializeLcd(lcdColumns, lcdRows)) {
    return;
  }

  if (!setLcdBacklight(enabled)) {
    Serial.println("Falha ao alterar o backlight do LCD.");
    return;
  }

  Serial.printf("Backlight do LCD 0x%02X: %s.\r\n",
                board::LCD_I2C_ADDRESS, enabled ? "ON" : "OFF");
}

void stopLcdTest() {
  if (!lcdTestActive) {
    Serial.println("O teste do LCD ja esta parado.");
    return;
  }

  lcdTestActive = false;
  lcdTestPhase = LcdTestPhase::Idle;
  resetLcdTextMode();
  lcd.clear();
  printLcdLine(0, "Teste encerrado");
  if (lcdRows > 1) {
    printLcdLine(1, "LCD pronto");
  }
  Serial.println("Teste continuo do LCD encerrado.");
}

void serviceLcdTest() {
  if (!lcdTestActive) {
    return;
  }

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - lcdNextStepMs) < 0) {
    return;
  }

  char line[41] = {};
  switch (lcdTestPhase) {
  case LcdTestPhase::Idle:
    return;

  case LcdTestPhase::Intro:
    resetLcdTextMode();
    lcd.clear();
    printLcdLine(0, "Teste LCD I2C");
    if (lcdRows > 1) {
      printLcdLine(1, "Endereco 0x27");
    }
    if (lcdRows > 2) {
      snprintf(line, sizeof(line), "%ux%u @ %lu kHz", lcdColumns, lcdRows,
               static_cast<unsigned long>(activeI2cClockHz / 1000));
      printLcdLine(2, line);
    }
    scheduleLcdStep(now, 1600, LcdTestPhase::Lines);
    break;

  case LcdTestPhase::Lines:
    lcd.clear();
    for (uint8_t row = 0; row < lcdRows; row++) {
      snprintf(line, sizeof(line), "Linha %u/%u OK", row + 1, lcdRows);
      printLcdLine(row, line);
    }
    lcd.home();
    scheduleLcdStep(now, 1600, LcdTestPhase::CustomCharacters);
    break;

  case LcdTestPhase::CustomCharacters: {
    uint8_t heart[8] = {0x00, 0x0A, 0x1F, 0x1F,
                        0x0E, 0x04, 0x00, 0x00};
    uint8_t smile[8] = {0x00, 0x0A, 0x00, 0x00,
                        0x11, 0x0E, 0x00, 0x00};
    lcd.createChar(0, heart);
    lcd.createChar(1, smile);
    lcd.clear();
    printLcdLine(0, "Caracteres CGRAM");
    const uint8_t customRow = lcdRows > 1 ? 1 : 0;
    lcd.setCursor(lcdColumns >= 8 ? 4 : 0, customRow);
    lcd.print("[ ");
    lcd.write(static_cast<uint8_t>(0));
    lcd.write(' ');
    lcd.write(static_cast<uint8_t>(1));
    lcd.print(" ]");
    scheduleLcdStep(now, 1800, LcdTestPhase::Cursor);
    break;
  }

  case LcdTestPhase::Cursor:
    lcd.clear();
    printLcdLine(0, "Teste de cursor");
    if (lcdRows > 1) {
      printLcdLine(1, "Sublinhado: ON");
    }
    lcd.setCursor(lcdColumns - 1, lcdRows - 1);
    lcd.cursor();
    scheduleLcdStep(now, 1500, LcdTestPhase::Blink);
    break;

  case LcdTestPhase::Blink:
    printLcdLine(0, "Teste de blink");
    if (lcdRows > 1) {
      printLcdLine(1, "Piscando: ON");
    }
    lcd.setCursor(lcdColumns - 1, lcdRows - 1);
    lcd.blink();
    scheduleLcdStep(now, 1500, LcdTestPhase::DisplayOff);
    break;

  case LcdTestPhase::DisplayOff:
    lcd.noCursor();
    lcd.noBlink();
    lcd.noDisplay();
    scheduleLcdStep(now, 700, LcdTestPhase::DisplayOn);
    break;

  case LcdTestPhase::DisplayOn:
    lcd.display();
    lcd.clear();
    printLcdLine(0, "Display: ON");
    if (lcdRows > 1) {
      printLcdLine(1, "Pixels visiveis");
    }
    scheduleLcdStep(now, 1200, LcdTestPhase::BacklightOff);
    break;

  case LcdTestPhase::BacklightOff:
    setLcdBacklight(false);
    scheduleLcdStep(now, 700, LcdTestPhase::BacklightOn);
    break;

  case LcdTestPhase::BacklightOn:
    setLcdBacklight(true);
    lcd.clear();
    printLcdLine(0, "Backlight: ON");
    if (lcdRows > 1) {
      printLcdLine(1, "Controle OK");
    }
    scheduleLcdStep(now, 1200, LcdTestPhase::ScrollSetup);
    break;

  case LcdTestPhase::ScrollSetup:
    lcd.clear();
    printLcdLine(0, "<<< SCROLL >>>");
    if (lcdRows > 1) {
      printLcdLine(1, "Esq. e direita");
    }
    lcdPhaseStep = 0;
    scheduleLcdStep(now, 700, LcdTestPhase::ScrollLeft);
    break;

  case LcdTestPhase::ScrollLeft:
    lcd.scrollDisplayLeft();
    lcdPhaseStep++;
    if (lcdPhaseStep >= 4) {
      lcdPhaseStep = 0;
      scheduleLcdStep(now, 300, LcdTestPhase::ScrollRight);
    } else {
      lcdNextStepMs = now + 300;
    }
    break;

  case LcdTestPhase::ScrollRight:
    lcd.scrollDisplayRight();
    lcdPhaseStep++;
    if (lcdPhaseStep >= 4) {
      lcdPhaseStep = 0;
      scheduleLcdStep(now, 600, LcdTestPhase::LiveStatus);
    } else {
      lcdNextStepMs = now + 300;
    }
    break;

  case LcdTestPhase::LiveStatus:
    resetLcdTextMode();
    lcd.clear();
    snprintf(line, sizeof(line), "LCD OK #%lu",
             static_cast<unsigned long>(lcdFrame));
    printLcdLine(0, line);
    if (lcdRows > 1) {
      snprintf(line, sizeof(line), "Uptime: %lu s",
               static_cast<unsigned long>(now / 1000));
      printLcdLine(1, line);
    }
    if (lcdRows > 2) {
      printLcdLine(2, "ASCII: 32..126");
    }
    if (lcdRows > 3) {
      printLcdLine(3, "SCL47 SDA48");
    }
    lcdFrame++;
    scheduleLcdStep(now, lcdTestIntervalMs, LcdTestPhase::CharacterTable);
    break;

  case LcdTestPhase::CharacterTable: {
    lcd.clear();
    uint8_t character = lcdFirstCharacter;
    for (uint8_t row = 0; row < lcdRows; row++) {
      lcd.setCursor(0, row);
      for (uint8_t column = 0; column < lcdColumns; column++) {
        lcd.write(character);
        character++;
        if (character > 126) {
          character = 32;
        }
      }
    }
    const uint16_t nextOffset =
        static_cast<uint16_t>(lcdFirstCharacter - 32) + lcdColumns * lcdRows;
    lcdFirstCharacter = static_cast<uint8_t>(32 + (nextOffset % 95));
    scheduleLcdStep(now, lcdTestIntervalMs, LcdTestPhase::LiveStatus);
    break;
  }
  }
}

void trackTcsFilter(bool s2, bool s3) {
  const int s2Index = findOutput("TCS_S2");
  const int s3Index = findOutput("TCS_S3");
  if (s2Index >= 0) {
    outputEnabled[s2Index] = true;
    outputLevels[s2Index] = s2;
  }
  if (s3Index >= 0) {
    outputEnabled[s3Index] = true;
    outputLevels[s3Index] = s3;
  }
}

const char *tcsColorName(uint8_t color) {
  switch (color) {
  case BLACK:
    return "PRETO";
  case WHITE:
    return "BRANCO";
  case RED:
    return "VERMELHO";
  case GREEN:
    return "VERDE";
  case BLUE:
    return "AZUL";
  case GRAY:
    return "CINZA";
  default:
    return "DESCONHECIDA";
  }
}

void setDefaultTcsCalibration() {
  sensorData dark = {{4000, 4000, 4000}};
  sensorData white = {{50000, 50000, 50000}};
  tcs.setDarkCal(&dark);
  tcs.setWhiteCal(&white);
}

void stopTcsStream(bool verbose) {
  const bool wasActive = tcsStreamActive;
  tcsStreamActive = false;
  if (tcsInitialized) {
    gpio_intr_disable(static_cast<gpio_num_t>(board::PIN_TCS_OUT));
  }
  if (verbose) {
    Serial.println(wasActive ? "Stream TCS interrompido."
                             : "O stream TCS ja esta parado.");
  }
}

bool prepareTcsLcd() {
  lcdTestActive = false;
  lcdTestPhase = LcdTestPhase::Idle;

  const bool dimensionsChanged =
      lcdColumns != board::LCD_DEFAULT_COLUMNS ||
      lcdRows != board::LCD_DEFAULT_ROWS;
  lcdColumns = board::LCD_DEFAULT_COLUMNS;
  lcdRows = board::LCD_DEFAULT_ROWS;
  if ((!lcdInitialized || dimensionsChanged) &&
      !initializeLcd(lcdColumns, lcdRows)) {
    return false;
  }
  if (!setLcdBacklight(true)) {
    Serial.println("Falha ao ligar o backlight para o teste TCS.");
    return false;
  }
  resetLcdTextMode();
  return true;
}

void displayTcsReading(const TcsReading &reading) {
  if (!lcdInitialized || lcdColumns != board::LCD_DEFAULT_COLUMNS ||
      lcdRows != board::LCD_DEFAULT_ROWS) {
    return;
  }

  char line[21] = {};
  snprintf(line, sizeof(line), "R:%lu G:%lu",
           static_cast<unsigned long>(reading.frequency[0]),
           static_cast<unsigned long>(reading.frequency[1]));
  printLcdLine(0, line);
  snprintf(line, sizeof(line), "B:%lu C:%lu",
           static_cast<unsigned long>(reading.frequency[2]),
           static_cast<unsigned long>(reading.frequency[3]));
  printLcdLine(1, line);
  if (reading.calibrated) {
    snprintf(line, sizeof(line), "RGB:%u,%u,%u", reading.rgb[0],
             reading.rgb[1], reading.rgb[2]);
    printLcdLine(2, line);
    snprintf(line, sizeof(line), "COR:%s", tcsColorName(reading.color));
    printLcdLine(3, line);
  } else {
    printLcdLine(2, "RGB: SEM CALIB.");
    printLcdLine(3, "COR:SEM CALIB.");
  }
}

void printTcsReading(const TcsReading &reading) {
  Serial.printf("TCS Hz: R=%lu G=%lu B=%lu C=%lu\r\n",
                static_cast<unsigned long>(reading.frequency[0]),
                static_cast<unsigned long>(reading.frequency[1]),
                static_cast<unsigned long>(reading.frequency[2]),
                static_cast<unsigned long>(reading.frequency[3]));
  if (reading.calibrated) {
    Serial.printf("TCS RGB(%u,%u,%u) COR=%s\r\n", reading.rgb[0],
                  reading.rgb[1], reading.rgb[2],
                  tcsColorName(reading.color));
  } else {
    Serial.println("TCS sem calibracao completa; execute tcs-cal dark e tcs-cal white.");
  }
}

bool readTcs(TcsReading &reading) {
  if (!tcsInitialized) {
    Serial.println("TCS indisponivel: falha na inicializacao do GPIO/ISR.");
    return false;
  }

  reading = {};
  tcs.read();
  sensorData raw = {};
  tcs.getRaw(&raw);
  for (uint8_t index = 0; index < 3; index++) {
    reading.frequency[index] = static_cast<uint32_t>(raw.value[index]);
  }
  tcs.setFilter(TCS230_RGB_X);
  reading.frequency[3] = tcs.readSingle();
  trackTcsFilter(true, false);

  reading.calibrated = tcsDarkCalibrated && tcsWhiteCalibrated;
  if (reading.calibrated) {
    colorData rgb = {};
    tcs.getRGB(&rgb);
    for (uint8_t index = 0; index < 3; index++) {
      reading.rgb[index] = rgb.value[index];
    }
    reading.color = tcs.getColor();
  }
  reading.valid = true;
  return true;
}

bool averageTcs(sensorData &average, TcsReading &lastReading) {
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
    average.value[channel] =
        sum[channel] / board::TCS_CALIBRATION_SAMPLES;
  }
  return true;
}

void runTcsRead() {
  stopTcsStream(false);
  stopDcMotorTest(false);
  prepareTcsLcd();
  if (readTcs(tcsLastReading)) {
    printTcsReading(tcsLastReading);
    displayTcsReading(tcsLastReading);
  }
}

void startTcsStream(char *argv[], int argc) {
  uint32_t periodMs = board::TCS_DEFAULT_STREAM_MS;
  if (argc > 2 || (argc == 2 && !parseUInt(argv[1], periodMs))) {
    Serial.println("Uso: tcs-stream [period_ms]");
    return;
  }
  if (periodMs < board::TCS_MIN_STREAM_MS ||
      periodMs > board::TCS_MAX_STREAM_MS) {
    Serial.println("Erro: periodo TCS deve estar entre 250 e 60000 ms.");
    return;
  }
  if (!tcsInitialized) {
    Serial.println("TCS indisponivel: falha na inicializacao do GPIO/ISR.");
    return;
  }

  stopDcMotorTest(false);
  prepareTcsLcd();
  tcsStreamPeriodMs = periodMs;
  tcsLastStreamMs = millis() - periodMs;
  tcsStreamActive = true;
  Serial.printf("Stream TCS iniciado a cada %lu ms. Use tcs-stop.\r\n",
                static_cast<unsigned long>(periodMs));
}

void calibrateTcs(char *argv[], int argc) {
  if (argc != 2) {
    Serial.println("Uso: tcs-cal <dark|white|reset>");
    return;
  }
  stopTcsStream(false);
  stopDcMotorTest(false);
  prepareTcsLcd();

  if (equalsIgnoreCase(argv[1], "reset")) {
    tcsDarkCalibrated = false;
    tcsWhiteCalibrated = false;
    tcsDarkCalibration = {};
    tcsWhiteCalibration = {};
    setDefaultTcsCalibration();
    lcd.clear();
    printLcdLine(0, "TCS CALIBRACAO");
    printLcdLine(1, "RESET EM RAM");
    Serial.println("Calibracao TCS removida da RAM.");
    return;
  }

  sensorData average = {};
  TcsReading last = {};
  if (equalsIgnoreCase(argv[1], "dark")) {
    setDefaultTcsCalibration();
    if (!averageTcs(average, last)) {
      return;
    }
    tcsDarkCalibration = average;
    tcsDarkCalibrated = true;
    tcsWhiteCalibrated = false;
    tcsLastReading = last;
    tcsLastReading.calibrated = false;
    Serial.printf("TCS dark: R=%ld G=%ld B=%ld Hz (media de %u).\r\n",
                  static_cast<long>(average.value[0]),
                  static_cast<long>(average.value[1]),
                  static_cast<long>(average.value[2]),
                  board::TCS_CALIBRATION_SAMPLES);
    displayTcsReading(tcsLastReading);
    return;
  }

  if (!equalsIgnoreCase(argv[1], "white")) {
    Serial.println("Uso: tcs-cal <dark|white|reset>");
    return;
  }
  if (!tcsDarkCalibrated) {
    Serial.println("Calibre primeiro o escuro com: tcs-cal dark");
    return;
  }
  if (!averageTcs(average, last)) {
    return;
  }
  for (uint8_t channel = 0; channel < 3; channel++) {
    if (average.value[channel] <= tcsDarkCalibration.value[channel]) {
      tcsWhiteCalibrated = false;
      Serial.println("Calibracao branca rejeitada: cada canal deve superar o dark.");
      displayTcsReading(last);
      return;
    }
  }

  tcsWhiteCalibration = average;
  tcs.setDarkCal(&tcsDarkCalibration);
  tcs.setWhiteCal(&tcsWhiteCalibration);
  tcsWhiteCalibrated = true;
  Serial.printf("TCS white: R=%ld G=%ld B=%ld Hz (media de %u).\r\n",
                static_cast<long>(average.value[0]),
                static_cast<long>(average.value[1]),
                static_cast<long>(average.value[2]),
                board::TCS_CALIBRATION_SAMPLES);
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
  const esp_err_t isrStatus = gpio_install_isr_service(0);
  if (isrStatus != ESP_OK && isrStatus != ESP_ERR_INVALID_STATE) {
    Serial.printf("Falha ao instalar servico ISR do TCS: %s.\r\n",
                  esp_err_to_name(isrStatus));
    return;
  }
  tcs.begin();
  tcs.setSampling(board::TCS_SAMPLE_MS);
  setDefaultTcsCalibration();
  trackTcsFilter(false, false);
  tcsInitialized = true;
}

const char *i2cErrorText(uint8_t error) {
  switch (error) {
  case 0:
    return "ACK";
  case 1:
    return "dados longos demais";
  case 2:
    return "NACK no endereco";
  case 3:
    return "NACK nos dados";
  case 4:
    return "erro desconhecido";
  case 5:
    return "timeout";
  default:
    return "erro nao mapeado";
  }
}

void printI2cLevels() {
  Wire.end();
  pinMode(board::PIN_I2C_SCL, INPUT);
  pinMode(board::PIN_I2C_SDA, INPUT);
  delay(2);

  const int sclLevel = digitalRead(board::PIN_I2C_SCL);
  const int sdaLevel = digitalRead(board::PIN_I2C_SDA);
  Serial.printf("I2C idle: SCL/GPIO%02u=%d SDA/GPIO%02u=%d\r\n",
                board::PIN_I2C_SCL, sclLevel, board::PIN_I2C_SDA, sdaLevel);
  Serial.println("Esperado em repouso: SCL=1 e SDA=1 com pull-ups externos.");

  beginI2c(activeI2cClockHz);
}

uint8_t probeI2cAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission();
}

bool parseByteList(char *argv[], int startArg, int argc, uint8_t bytes[],
                   uint8_t &byteCount) {
  if (startArg >= argc) {
    return false;
  }

  const int requestedCount = argc - startArg;
  if (requestedCount > MAX_I2C_MESSAGE_BYTES) {
    return false;
  }

  byteCount = 0;
  for (int index = startArg; index < argc; index++) {
    uint8_t value = 0;
    if (!parseByteValue(argv[index], value)) {
      return false;
    }
    bytes[byteCount++] = value;
  }

  return true;
}

void printByteList(const uint8_t bytes[], uint8_t byteCount) {
  for (uint8_t index = 0; index < byteCount; index++) {
    Serial.printf(" 0x%02X", bytes[index]);
  }
}

void i2cWriteMessage(char *argv[], int argc) {
  if (argc < 3) {
    Serial.println("Uso: i2c-write <addr> <byte> [byte...]");
    return;
  }

  uint8_t address = 0;
  uint8_t bytes[MAX_I2C_MESSAGE_BYTES] = {};
  uint8_t byteCount = 0;
  if (!parseI2cAddress(argv[1], address) ||
      !parseByteList(argv, 2, argc, bytes, byteCount)) {
    Serial.println("Erro: parametros invalidos. Exemplo: i2c-write 0x38 0x55 0xAA");
    return;
  }

  beginI2c(activeI2cClockHz);
  Wire.beginTransmission(address);
  for (uint8_t index = 0; index < byteCount; index++) {
    Wire.write(bytes[index]);
  }
  const uint8_t error = Wire.endTransmission();

  Serial.printf("I2C write 0x%02X:", address);
  printByteList(bytes, byteCount);
  Serial.printf(" -> %s\r\n", i2cErrorText(error));
}

void i2cReleaseLine(uint8_t pin) {
  pinMode(pin, INPUT);
}

void i2cDriveLow(uint8_t pin) {
  digitalWrite(pin, LOW);
  pinMode(pin, OUTPUT);
}

void i2cWaveDelay(uint32_t halfPeriodUs) {
  delayMicroseconds(halfPeriodUs);
}

void i2cWaveStart(uint32_t halfPeriodUs) {
  i2cReleaseLine(board::PIN_I2C_SDA);
  i2cReleaseLine(board::PIN_I2C_SCL);
  i2cWaveDelay(halfPeriodUs);
  i2cDriveLow(board::PIN_I2C_SDA);
  i2cWaveDelay(halfPeriodUs);
  i2cDriveLow(board::PIN_I2C_SCL);
}

void i2cWaveStop(uint32_t halfPeriodUs) {
  i2cDriveLow(board::PIN_I2C_SCL);
  i2cDriveLow(board::PIN_I2C_SDA);
  i2cWaveDelay(halfPeriodUs);
  i2cReleaseLine(board::PIN_I2C_SCL);
  i2cWaveDelay(halfPeriodUs);
  i2cReleaseLine(board::PIN_I2C_SDA);
  i2cWaveDelay(halfPeriodUs);
}

void i2cWaveBit(bool level, uint32_t halfPeriodUs) {
  i2cDriveLow(board::PIN_I2C_SCL);
  if (level) {
    i2cReleaseLine(board::PIN_I2C_SDA);
  } else {
    i2cDriveLow(board::PIN_I2C_SDA);
  }
  i2cWaveDelay(halfPeriodUs);
  i2cReleaseLine(board::PIN_I2C_SCL);
  i2cWaveDelay(halfPeriodUs);
}

void i2cWaveByte(uint8_t value, uint32_t halfPeriodUs) {
  for (int bit = 7; bit >= 0; bit--) {
    i2cWaveBit(((value >> bit) & 0x01) != 0, halfPeriodUs);
  }

  // ACK sintetico para validacao visual no decoder. Nao valida slave I2C.
  i2cWaveBit(false, halfPeriodUs);
}

void i2cWaveMessage(char *argv[], int argc) {
  if (argc < 4) {
    Serial.println("Uso: i2c-wave <addr> <repeticoes> <byte> [byte...]");
    return;
  }

  uint8_t address = 0;
  uint32_t repetitions = 0;
  uint8_t bytes[MAX_I2C_MESSAGE_BYTES] = {};
  uint8_t byteCount = 0;
  if (!parseI2cAddress(argv[1], address) || !parseUInt(argv[2], repetitions) ||
      repetitions == 0 ||
      !parseByteList(argv, 3, argc, bytes, byteCount)) {
    Serial.println("Erro: parametros invalidos. Exemplo: i2c-wave 0x38 50 0x55 0xAA 0xFF");
    return;
  }

  if (repetitions > 1000) {
    Serial.println("Erro: repeticoes maximas permitidas = 1000.");
    return;
  }

  const uint32_t halfPeriodUs = max<uint32_t>(2, 500000UL / activeI2cClockHz);
  Wire.end();
  i2cReleaseLine(board::PIN_I2C_SDA);
  i2cReleaseLine(board::PIN_I2C_SCL);
  delay(2);

  Serial.printf("Gerando forma de onda I2C 0x%02X:", address);
  printByteList(bytes, byteCount);
  Serial.printf(" por %lu repeticao(oes), clock aproximado %lu Hz.\r\n",
                static_cast<unsigned long>(repetitions),
                static_cast<unsigned long>(500000UL / halfPeriodUs));
  Serial.println("Observacao: ACK sintetico; use i2c-write/pcf-probe para validar ACK real.");

  for (uint32_t repetition = 0; repetition < repetitions; repetition++) {
    i2cWaveStart(halfPeriodUs);
    i2cWaveByte(static_cast<uint8_t>((address << 1) | 0x00), halfPeriodUs);
    for (uint8_t index = 0; index < byteCount; index++) {
      i2cWaveByte(bytes[index], halfPeriodUs);
    }
    i2cWaveStop(halfPeriodUs);
    delayMicroseconds(200);
  }

  beginI2c(activeI2cClockHz);
  Serial.println("Forma de onda I2C finalizada; periferico Wire restaurado.");
}

void scanI2c(char *argv[], int argc) {
  uint32_t clockHz = board::I2C_CLOCK_HZ;
  if (argc > 2 || (argc == 2 && !parseUInt(argv[1], clockHz))) {
    Serial.println("Uso: i2c-scan [clock_hz]");
    return;
  }

  if (clockHz < 10000 || clockHz > 400000) {
    Serial.println("Erro: clock I2C permitido entre 10000 e 400000 Hz.");
    return;
  }

  beginI2c(clockHz);

  Serial.printf("Varredura I2C em SCL=GPIO%02u SDA=GPIO%02u @ %lu Hz...\r\n",
                board::PIN_I2C_SCL, board::PIN_I2C_SDA,
                static_cast<unsigned long>(clockHz));

  uint8_t found = 0;
  for (uint8_t address = 0x03; address <= 0x77; address++) {
    const uint8_t error = probeI2cAddress(address);

    if (error == 0) {
      Serial.printf("  encontrado: 0x%02X\r\n", address);
      found++;
    } else if (error == 4 || error == 5) {
      Serial.printf("  0x%02X: %s\r\n", address, i2cErrorText(error));
    }
  }

  if (found == 0) {
    Serial.println("Nenhum dispositivo I2C respondeu.");
  }
}

void pcfProbe(char *argv[], int argc) {
  uint8_t addresses[2] = {board::PCF8574_ADDR_GND, board::PCF8574A_ADDR_GND};
  uint8_t count = 2;

  if (argc == 2) {
    if (!parseI2cAddress(argv[1], addresses[0])) {
      Serial.println("Uso: pcf-probe [addr]");
      return;
    }
    count = 1;
  } else if (argc > 2) {
    Serial.println("Uso: pcf-probe [addr]");
    return;
  }

  beginI2c(activeI2cClockHz);
  for (uint8_t index = 0; index < count; index++) {
    const uint8_t address = addresses[index];
    const uint8_t error = probeI2cAddress(address);
    Serial.printf("PCF probe 0x%02X: %s\r\n", address, i2cErrorText(error));
  }
}

void pcfLoop(char *argv[], int argc) {
  if (argc < 2 || argc > 3) {
    Serial.println("Uso: pcf-loop <addr> [tempo_ms]");
    return;
  }

  uint8_t address = 0;
  uint32_t durationMs = 10000;
  if (!parseI2cAddress(argv[1], address) ||
      (argc == 3 && !parseUInt(argv[2], durationMs))) {
    Serial.println("Erro: parametros invalidos. Exemplo: pcf-loop 0x38 10000");
    return;
  }

  if (durationMs > 60000) {
    Serial.println("Erro: tempo maximo permitido e 60000 ms.");
    return;
  }

  beginI2c(activeI2cClockHz);
  Serial.printf("Repetindo probe I2C em 0x%02X por %lu ms.\r\n", address,
                static_cast<unsigned long>(durationMs));
  const uint32_t startedAt = millis();
  uint32_t probes = 0;
  uint32_t ackCount = 0;
  while (millis() - startedAt < durationMs) {
    const uint8_t error = probeI2cAddress(address);
    probes++;
    if (error == 0) {
      ackCount++;
    }
    delay(20);
  }

  Serial.printf("pcf-loop 0x%02X finalizado: %lu probe(s), %lu ACK.\r\n",
                address, static_cast<unsigned long>(probes),
                static_cast<unsigned long>(ackCount));
}

void pcfRead(char *argv[], int argc) {
  if (argc != 2) {
    Serial.println("Uso: pcf-read <addr>");
    return;
  }

  uint8_t address = 0;
  if (!parseI2cAddress(argv[1], address)) {
    Serial.println("Erro: endereco I2C invalido. Use, por exemplo, 0x20 ou 0x38.");
    return;
  }

  beginI2c(activeI2cClockHz);
  const uint8_t requested = Wire.requestFrom(address, static_cast<uint8_t>(1));
  if (requested != 1 || Wire.available() < 1) {
    Serial.printf("PCF 0x%02X: sem byte de leitura.\r\n", address);
    return;
  }

  const uint8_t value = Wire.read();
  Serial.printf("PCF 0x%02X read = 0x%02X\r\n", address, value);
}

void pcfWrite(char *argv[], int argc) {
  if (argc != 3) {
    Serial.println("Uso: pcf-write <addr> <byte>");
    return;
  }

  uint8_t address = 0;
  uint8_t value = 0;
  if (!parseI2cAddress(argv[1], address) || !parseByteValue(argv[2], value)) {
    Serial.println("Erro: parametros invalidos. Exemplo: pcf-write 0x20 0xFF");
    return;
  }

  beginI2c(activeI2cClockHz);
  Wire.beginTransmission(address);
  Wire.write(value);
  const uint8_t error = Wire.endTransmission();
  Serial.printf("PCF 0x%02X write 0x%02X: %s\r\n", address, value,
                i2cErrorText(error));
}

void restartRs485Serial() {
  Rs485Serial.end();
  delay(10);
  Rs485Serial.begin(board::RS485_BAUD, SERIAL_8N1, board::PIN_RX_MODBUS,
                    board::PIN_TX_MODBUS);
}

uint32_t clearRs485RxBuffer(uint32_t settleMs) {
  uint32_t cleared = 0;
  uint32_t deadline = millis() + settleMs;

  do {
    while (Rs485Serial.available() > 0) {
      Rs485Serial.read();
      cleared++;
      deadline = millis() + settleMs;
    }
    delay(1);
  } while (settleMs > 0 && millis() < deadline);

  return cleared;
}

bool driveRs485Direction(bool transmitEnabled) {
  const int dirIndex = findOutput("DIR_MODBUS");
  if (dirIndex < 0) {
    Serial.println("Erro interno: DIR_MODBUS nao encontrado.");
    return false;
  }

  driveOutput(static_cast<size_t>(dirIndex), transmitEnabled, false);
  return true;
}

void holdRs485Di(char *argv[], int argc) {
  if (argc < 2 || argc > 3) {
    Serial.println("Uso: rs485-di <0|1|low|high> [tempo_ms]");
    return;
  }

  bool level = false;
  uint32_t holdMs = 5000;
  if (!parseLevel(argv[1], level) ||
      (argc == 3 && !parseUInt(argv[2], holdMs))) {
    Serial.println("Erro: parametros invalidos para rs485-di.");
    return;
  }

  if (holdMs > 60000) {
    Serial.println("Erro: tempo maximo permitido e 60000 ms.");
    return;
  }

  Rs485Serial.end();
  delay(10);
  if (!driveRs485Direction(true)) {
    restartRs485Serial();
    return;
  }

  pinMode(board::PIN_TX_MODBUS, OUTPUT);
  digitalWrite(board::PIN_TX_MODBUS, level ? HIGH : LOW);

  Serial.printf("Segurando TX_MODBUS/GPIO%02u em %u por %lu ms; DIR_MODBUS em transmissao.\r\n",
                board::PIN_TX_MODBUS, level ? 1 : 0,
                static_cast<unsigned long>(holdMs));
  Serial.println("Meça agora no pino DI do MAX485 e no par A/B.");
  delay(holdMs);

  digitalWrite(board::PIN_TX_MODBUS, HIGH);
  restartRs485Serial();
  driveRs485Direction(false);
  Serial.println("Diagnostico rs485-di finalizado; UART RS-485 reiniciada.");
}

void sendRs485Burst(char *argv[], int argc) {
  uint32_t durationMs = 3000;
  if (argc > 2 || (argc == 2 && !parseUInt(argv[1], durationMs))) {
    Serial.println("Uso: rs485-burst [tempo_ms]");
    return;
  }

  if (durationMs > 60000) {
    Serial.println("Erro: tempo maximo permitido e 60000 ms.");
    return;
  }

  if (!driveRs485Direction(true)) {
    return;
  }
  clearRs485RxBuffer(0);
  delayMicroseconds(200);

  Serial.printf("Enviando rajada 0x55 por %lu ms em TX_MODBUS/GPIO%02u.\r\n",
                static_cast<unsigned long>(durationMs), board::PIN_TX_MODBUS);
  const uint32_t startedAt = millis();
  uint32_t bytesSent = 0;
  while (millis() - startedAt < durationMs) {
    Rs485Serial.write(0x55);
    bytesSent++;
    if ((bytesSent % 64) == 0) {
      delay(1);
    }
  }
  Rs485Serial.flush();

  delayMicroseconds(200);
  uint32_t discarded = clearRs485RxBuffer(5);
  driveRs485Direction(false);
  delayMicroseconds(200);
  discarded += clearRs485RxBuffer(5);
  Serial.printf("Rajada finalizada: %lu byte(s) enviados.\r\n",
                static_cast<unsigned long>(bytesSent));
  if (discarded > 0) {
    Serial.printf("Descartados %lu byte(s) espurios da RX local durante transmissao.\r\n",
                  static_cast<unsigned long>(discarded));
  }
}

void sendRs485(char *argv[], int argc) {
  if (argc < 2) {
    Serial.println("Uso: rs485-send <texto>");
    return;
  }

  if (!driveRs485Direction(true)) {
    return;
  }

  clearRs485RxBuffer(0);
  delayMicroseconds(200);

  for (int index = 1; index < argc; index++) {
    if (index > 1) {
      Rs485Serial.write(' ');
    }
    Rs485Serial.print(argv[index]);
  }
  Rs485Serial.print("\r\n");
  Rs485Serial.flush();

  delayMicroseconds(200);
  uint32_t discarded = clearRs485RxBuffer(5);
  driveRs485Direction(false);
  delayMicroseconds(200);
  discarded += clearRs485RxBuffer(5);
  Serial.println("RS485 enviado e transceptor retornou para recepcao.");
  if (discarded > 0) {
    Serial.printf("Descartados %lu byte(s) espurios da RX local durante transmissao.\r\n",
                  static_cast<unsigned long>(discarded));
  }
}

void readRs485Available(bool forceMessage) {
  int count = 0;
  while (Rs485Serial.available() > 0) {
    const int value = Rs485Serial.read();
    if (count == 0) {
      Serial.print("RS485 RX:");
    }

    Serial.printf(" 0x%02X", value & 0xFF);
    if (isprint(static_cast<unsigned char>(value))) {
      Serial.printf("('%c')", value);
    }
    count++;
  }

  if (count > 0) {
    Serial.println();
  } else if (forceMessage) {
    Serial.println("Nenhum byte pendente no RS485.");
  }
}

void printRs485RxLevel() {
  const int dirIndex = findOutput("DIR_MODBUS");
  if (dirIndex >= 0) {
    const int dirLevel = digitalRead(OUTPUTS[dirIndex].pin);
    Serial.printf("DIR_MODBUS/GPIO%02u = %u (%s)\r\n", OUTPUTS[dirIndex].pin,
                  dirLevel, dirLevel ? "transmissao" : "recepcao");
  }

  const int rxLevel = digitalRead(board::PIN_RX_MODBUS);
  Serial.printf("RX_MODBUS/GPIO%02u = %u\r\n", board::PIN_RX_MODBUS,
                rxLevel);
  Serial.println("Idle UART esperado em RX_MODBUS: HIGH.");
  Serial.println("Se RX ficar LOW em repouso, a UART tende a receber 0x00/break.");
}

void printEdges() {
  Serial.println("Contadores de borda:");
  for (size_t index = 0; index < INPUT_COUNT; index++) {
    Serial.printf("  %-11s GPIO%02u edges=%lu\r\n", INPUTS[index].name,
                  INPUTS[index].pin,
                  static_cast<unsigned long>(edgeCount(index)));
  }
}

void serviceInputWatch() {
  for (size_t index = 0; index < INPUT_COUNT; index++) {
    const uint8_t level = digitalRead(INPUTS[index].pin);
    if (level != lastInputLevels[index]) {
      inputEdges[index]++;
      lastInputLevels[index] = level;
      if (watchChanges) {
        Serial.printf("IN  %-11s GPIO%02u = %u  edges=%lu\r\n",
                      INPUTS[index].name, INPUTS[index].pin, level,
                      static_cast<unsigned long>(edgeCount(index)));
      }
    }
  }
}

void servicePeriodicStatus() {
  if (!periodicStatus) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastPeriodicStatusMs >= periodicStatusMs) {
    lastPeriodicStatusMs = now;
    printStatus();
  }
}

int tokenize(char *line, char *argv[], int maxArgs) {
  int argc = 0;
  char *token = strtok(line, " \t\r\n");
  while (token != nullptr && argc < maxArgs) {
    argv[argc++] = token;
    token = strtok(nullptr, " \t\r\n");
  }
  return argc;
}

void processCommand(char *line) {
  char *argv[18] = {};
  const int argc = tokenize(line, argv, 18);
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
  } else if (equalsIgnoreCase(argv[0], "trigger")) {
    runUltrasonicTrigger();
  } else if (equalsIgnoreCase(argv[0], "cc-jog")) {
    startDcMotorTest(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "cc-stop")) {
    stopDcMotorTest(true);
  } else if (equalsIgnoreCase(argv[0], "i2c-scan")) {
    scanI2c(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "i2c-level")) {
    printI2cLevels();
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
      Serial.println("Uso: tcs-read");
      return;
    }
    runTcsRead();
  } else if (equalsIgnoreCase(argv[0], "tcs-stream")) {
    startTcsStream(argv, argc);
  } else if (equalsIgnoreCase(argv[0], "tcs-stop")) {
    if (argc != 1) {
      Serial.println("Uso: tcs-stop");
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
    const uint32_t cleared = clearRs485RxBuffer(0);
    Serial.printf("Buffer RX RS-485 limpo: %lu byte(s) descartado(s).\r\n",
                  static_cast<unsigned long>(cleared));
  } else if (equalsIgnoreCase(argv[0], "rs485-rx-level")) {
    printRs485RxLevel();
  } else if (equalsIgnoreCase(argv[0], "edges")) {
    printEdges();
  } else if (equalsIgnoreCase(argv[0], "clear-edges")) {
    clearEdges();
    Serial.println("Contadores de borda zerados.");
  } else if (equalsIgnoreCase(argv[0], "set")) {
    if (argc != 3) {
      Serial.println("Uso: set <saida> <0|1|low|high>");
      return;
    }

    const int outputIndex = findOutput(argv[1]);
    bool level = false;
    if (outputIndex < 0 || !parseLevel(argv[2], level)) {
      Serial.println("Erro: saida ou nivel invalido.");
      return;
    }

    driveOutput(static_cast<size_t>(outputIndex), level, true);
  } else if (equalsIgnoreCase(argv[0], "pulse") ||
             equalsIgnoreCase(argv[0], "pulseus")) {
    if (argc != 5) {
      Serial.println("Uso: pulse <saida> <high> <low> <vezes>");
      return;
    }

    const int outputIndex = findOutput(argv[1]);
    uint32_t highTime = 0;
    uint32_t lowTime = 0;
    uint32_t count = 0;

    if (outputIndex < 0 || !parseUInt(argv[2], highTime) ||
        !parseUInt(argv[3], lowTime) || !parseUInt(argv[4], count)) {
      Serial.println("Erro: parametros invalidos para pulso.");
      return;
    }

    pulseOutput(static_cast<size_t>(outputIndex), highTime, lowTime, count,
                equalsIgnoreCase(argv[0], "pulseus"));
  } else if (equalsIgnoreCase(argv[0], "watch")) {
    if (argc < 2 || argc > 3) {
      Serial.println("Uso: watch on|off [period_ms]");
      return;
    }

    if (equalsIgnoreCase(argv[1], "on")) {
      uint32_t requestedPeriodMs = periodicStatusMs;
      const bool requestedPeriodicStatus = argc == 3;
      if (requestedPeriodicStatus && !parseUInt(argv[2], requestedPeriodMs)) {
        Serial.println("Erro: periodo invalido.");
        return;
      }

      watchChanges = true;
      periodicStatus = requestedPeriodicStatus;
      periodicStatusMs = requestedPeriodMs;
      lastPeriodicStatusMs = millis();
      Serial.println("Monitoramento de entradas ligado.");
    } else if (equalsIgnoreCase(argv[1], "off")) {
      watchChanges = false;
      periodicStatus = false;
      Serial.println("Monitoramento automatico desligado.");
    } else {
      Serial.println("Uso: watch on|off [period_ms]");
    }
  } else {
    Serial.println("Comando desconhecido. Use: help");
  }
}

void readConsole() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      Serial.println();
      commandBuffer[commandLength] = '\0';
      processCommand(commandBuffer);
      commandLength = 0;
      commandBuffer[0] = '\0';
      continue;
    }

    if ((c == '\b') || (c == 0x7F)) {
      if (commandLength > 0) {
        commandLength--;
        Serial.print("\b \b");
      }
      continue;
    }

    if (commandLength + 1 < sizeof(commandBuffer)) {
      commandBuffer[commandLength++] = c;
      Serial.print(c);
    } else {
      commandLength = 0;
      Serial.println("Linha de comando muito longa; buffer limpo.");
    }
  }
}

void setupOutputs() {
  for (size_t index = 0; index < OUTPUT_COUNT; index++) {
    pinMode(OUTPUTS[index].pin, INPUT);
    outputLevels[index] = false;
    outputEnabled[index] = false;
  }

  const int rs485DirIndex = findOutput("DIR_MODBUS");
  if (rs485DirIndex >= 0) {
    driveOutput(static_cast<size_t>(rs485DirIndex), false, false);
  }
}

void setupInputs() {
  for (size_t index = 0; index < INPUT_COUNT; index++) {
    pinMode(INPUTS[index].pin, INPUT);
    lastInputLevels[index] = digitalRead(INPUTS[index].pin);
  }
}

void setupBuses() {
  beginI2c(board::I2C_CLOCK_HZ);

  Rs485Serial.begin(board::RS485_BAUD, SERIAL_8N1, board::PIN_RX_MODBUS,
                    board::PIN_TX_MODBUS);
}

void setup() {
  Serial.begin(board::CONSOLE_BAUD);
  delay(1500);

  setupOutputs();
  setupInputs();
  setupBuses();
  setupTcs();

  Serial.println();
  Serial.println("MYT ESP32-S3 - firmware de ensaio de hardware");
  Serial.println("Modulo informado: ESP32-S3-WROOM-1 N16R8; PSRAM nao habilitada no PlatformIO.");
  Serial.println("Saidas em alta impedancia, exceto DIR_MODBUS e TCS_S2/S3 inicializados em LOW.");
  Serial.println("TCS: S2=GPIO6 S3=GPIO7 OUT=GPIO14, escala externa em 20%.");
  Serial.println("set/pulse habilita a saida usada.");
  Serial.println("Use all-low para habilitar todas as saidas em LOW.");
  Serial.println("Use 'help' para ver os comandos.");
  printSignals();
}

void loop() {
  readConsole();
  readRs485Available(false);
  serviceInputWatch();
  servicePeriodicStatus();
  serviceLcdTest();
  serviceDcMotorTest();
  serviceTcsStream();
}
