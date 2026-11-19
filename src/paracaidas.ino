// Firmware de Avionica - Sistema de Recuperacion (ESP32)
// Mapeo de hardware:
// - Boton: GPIO 4 (INPUT_PULLUP a GND)
// - LED Verde (Estado 1): GPIO 18
// - LED Amarillo (Estado 2): GPIO 19
// - LED Rojo (Estado 3): GPIO 21
// - LED Azul (Ignicion): GPIO 22

const int PIN_BTN     = 4;
const int PIN_LED_1   = 18;
const int PIN_LED_2   = 19;
const int PIN_LED_3   = 21;
const int PIN_LED_IGN = 22;

const unsigned long DEBOUNCE_MS     = 50;
const unsigned long SHORT_PRESS_MAX = 1000;
const unsigned long LONG_PRESS_MIN  = 2000;

int lastReading = HIGH;
int stableState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long pressStartTime = 0;
bool isPressed = false;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_LED_1, OUTPUT);
  pinMode(PIN_LED_2, OUTPUT);
  pinMode(PIN_LED_3, OUTPUT);
  pinMode(PIN_LED_IGN, OUTPUT);

  digitalWrite(PIN_LED_1, LOW);
  digitalWrite(PIN_LED_2, LOW);
  digitalWrite(PIN_LED_3, LOW);
  digitalWrite(PIN_LED_IGN, LOW);

  Serial.println("[0 ms] ESP32 listo - Prueba de pines y boton iniciada");
}

void loop() {
  int reading = digitalRead(PIN_BTN);

  if (reading != lastReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading != stableState) {
      stableState = reading;

      if (stableState == LOW) {
        pressStartTime = millis();
        isPressed = true;
        digitalWrite(PIN_LED_1, HIGH);
      } else if (stableState == HIGH && isPressed) {
        unsigned long duration = millis() - pressStartTime;
        isPressed = false;
        digitalWrite(PIN_LED_1, LOW);

        Serial.print("[");
        Serial.print(millis());
        Serial.print(" ms] Pulsacion de ");
        Serial.print(duration);
        Serial.print(" ms -> ");

        if (duration < SHORT_PRESS_MAX) {
          Serial.println("SHORT PRESS");
        } else if (duration >= LONG_PRESS_MIN) {
          Serial.println("LONG PRESS");
        } else {
          Serial.println("IGNORED (entre 1s y 2s)");
        }
      }
    }
  }

  lastReading = reading;
}