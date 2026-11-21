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

// tiempo para detectar el short press y el long
const unsigned long DEBOUNCE_MS     = 50;
const unsigned long SHORT_PRESS_MAX = 1000;
const unsigned long LONG_PRESS_MIN  = 2000;
//tiempos de cambio de estado de la misionn
const unsigned long IGNITION_PULSE_MS = 1000;
const unsigned long SLOW_BLINK_MS = 1000; //parpadeo lento desarmado
const unsigned long FAST_BLINK_MS = 200;
const unsigned long BEACON_BLINK_MS = 500; //ATERRIZADO
//8 SEGUNDOS DESPUES DEL DESPEGUE PEROOO SOLO SIN DETECCION DEL CLIC APOGEO
const unsigned long BACKUP_DEPLOY_MS = 8000;

//estados deee la mision
enum EstadoMision {
  DESARMADO,
  ARMADO,
  EN_VUELO,
  APOGEO_IGNICION,
  ATERRIZADO
};

EstadoMision estadoActual = DESARMADO;

//temporizadores para el estado
unsigned long ignitionStartTime = 0;
unsigned long lastBlinkTime = 0;
// CONTROL PARA UNA UNICA IGNICION
unsigned long ascentStartTime = 0;
bool ignitionDone = false;

bool ledBlinkState = LOW;

int lastReading = HIGH;
int stableState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long pressStartTime = 0;
bool isPressed = false;

void cambiarEstado(EstadoMision nuevoEstado) {
  estadoActual = nuevoEstado;

  //esto va a apagar todos los leds antes de configurar el nuevo estado:)
  digitalWrite(PIN_LED_1, LOW);
  digitalWrite(PIN_LED_2, LOW);
  digitalWrite(PIN_LED_3, LOW);
  digitalWrite(PIN_LED_IGN, LOW);
  ledBlinkState = HIGH;

  Serial.print("[");
  Serial.print(millis());
  Serial.print("ms] TRANSICION ->");

//este me va indicar las fases y manda a prender leds
  switch (estadoActual) {
    case DESARMADO:
    Serial.println("DESARMADO (Verde parpadeo lento 1000 ms)");
    break;
    case ARMADO:
    digitalWrite(PIN_LED_1, HIGH);
    Serial.println("ARMADO (Verde fijo - Listo para lanzamiento)");
    break;
    //contador de 8 segundos 
    case EN_VUELO:
    digitalWrite(PIN_LED_2, HIGH);
    ascentStartTime = millis();
    Serial.println("EN_VUELO (Ascenso activo. Amarillo parpadeo rapido - Respaldo 8s iniciado)");
    break;
    case APOGEO_IGNICION:
    digitalWrite(PIN_LED_3, HIGH);
    digitalWrite(PIN_LED_IGN, HIGH);
    ignitionStartTime = millis();
    ignitionDone = false;
    Serial.println("APOGEO_IGNICION (Disparo unico de paracaidas 1000ms - Esperando aterrizaje)");
    break;
    case ATERRIZADO:
    ledBlinkState = HIGH;
    digitalWrite(PIN_LED_3, ledBlinkState);
    lastBlinkTime = millis();
    Serial.println("ATERRIZADO (Baliza de recuperacion activa - LED Rojo parpadeando)");
    break;
  }
}

//como va a cambiar entre los estados dependiendo si short o long push
void procesarPulsacionCorta() {
  switch (estadoActual) {
    case ARMADO:
      cambiarEstado(EN_VUELO);
     break;
    case EN_VUELO:
     cambiarEstado(APOGEO_IGNICION);
     break;
    case APOGEO_IGNICION:
     cambiarEstado(ATERRIZADO);
     break;
    default:
     Serial.println("  -> [IGNORADO] Pulsacion corta no valida en este estado.");
     break;
  }
}

void procesarPulsacionLarga() {
  switch (estadoActual) {
    case DESARMADO:
     cambiarEstado (ARMADO);
     break;
    case ARMADO:
     cambiarEstado (DESARMADO);
     break;
    default:
     Serial.println("  -> [IGNORADO] Pulsacion larga bloqueada en este estado.");
     break;
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_LED_1, OUTPUT);
  pinMode(PIN_LED_2, OUTPUT);
  pinMode(PIN_LED_3, OUTPUT);
  pinMode(PIN_LED_IGN, OUTPUT);

  // Asegurar todos los LEDs apagados al arrancar
  digitalWrite(PIN_LED_1, LOW);
  digitalWrite(PIN_LED_2, LOW);
  digitalWrite(PIN_LED_3, LOW);
  digitalWrite(PIN_LED_IGN, LOW);

  Serial.println("=== SISTEMA DE RECUPERACION INICIADO ===");
  cambiarEstado(DESARMADO);
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
        
      } else if (stableState == HIGH && isPressed) {
        unsigned long duration = millis() - pressStartTime;
        isPressed = false;
        

        Serial.print("[");
        Serial.print(millis());
        Serial.print(" ms] Pulsacion de ");
        Serial.print(duration);
        Serial.print(" ms -> ");

        if (duration < SHORT_PRESS_MAX) {
          procesarPulsacionCorta();
        } else if (duration >= LONG_PRESS_MIN) {
          procesarPulsacionLarga();
        } else {
          Serial.println("   -> Pulsacion ignorada (zona muerta 1s - 2s)");
        }
      }
    }
  }

  lastReading = reading;
  //logica de la maquina de estadosss con temporizadores no bloqueantes con milis
  switch (estadoActual) {
    case DESARMADO:
      if (millis() - lastBlinkTime >= SLOW_BLINK_MS) {
        lastBlinkTime = millis();
        ledBlinkState = !ledBlinkState;
        digitalWrite(PIN_LED_1, ledBlinkState);
      }
      break;

    case EN_VUELO:
    //este hsce el parpadeo rapido del led amarillo (En el ascenso)
      if (millis() - lastBlinkTime >= FAST_BLINK_MS) {
        lastBlinkTime = millis();
        ledBlinkState = !ledBlinkState;
        digitalWrite(PIN_LED_2, ledBlinkState);
      }
      //para el temporizador 8000ms 
      if (millis() - ascentStartTime >= BACKUP_DEPLOY_MS) {
        Serial.println("  -> [ALERTA] Temporizador de respaldo 8000 ms activado!");
        cambiarEstado(APOGEO_IGNICION);
      }
      break;

    case APOGEO_IGNICION:
      if (!ignitionDone && (millis() - ignitionStartTime >= IGNITION_PULSE_MS)) {
        digitalWrite(PIN_LED_IGN, LOW); //CORTA SU CORRIENTE
        ignitionDone = true;
        Serial.println("  -> Pulso de ignicion finalizado (1000 ms). Espereando pulsacion de aterrizaje...");
      }
      break;

    case ATERRIZADO:
      if(millis() - lastBlinkTime >= BEACON_BLINK_MS) {
        lastBlinkTime = millis();
        ledBlinkState = !ledBlinkState;
        digitalWrite(PIN_LED_3, ledBlinkState);
      }
    break;

    default:
    break;

  }
}




// Simulacion de hardware en Wokwi: https://wokwi.com/projects/481081023571677185