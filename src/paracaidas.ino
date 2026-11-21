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
enum EventoBoton {
  EVENTO_PULSACION_CORTA,
  EVENTO_PULSACION_LARGA
};
struct MensajeLog {
  unsigned long timestamp;
  char texto [100];
};
//colas de freeRTOS (queues)
QueueHandle_t colaEventos;
QueueHandle_t colaLogs;

void enviarLog(const char* msg) {
  if (colaLogs != NULL) {
    MensajeLog paquete;
    paquete.timestamp = millis();
    snprintf(paquete.texto, sizeof(paquete.texto), "%s", msg);
    xQueueSend(colaLogs, &paquete, 0); // 0 ticks no delay
  }
}

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

//este me va indicar las fases y manda a prender leds
  switch (estadoActual) {
    case DESARMADO:
    digitalWrite(PIN_LED_1, HIGH);
    enviarLog("Transicion -> DESARMADO (Verde parpadeo lento 1000 ms)");
    break;
    case ARMADO:
    digitalWrite(PIN_LED_1, HIGH);
    enviarLog("Transicion -> ARMADO (Verde fijo - Listo para lanzamiento)");
    break;
    //contador de 8 segundos 
    case EN_VUELO:
    digitalWrite(PIN_LED_2, HIGH);
    ascentStartTime = millis();
    enviarLog("Transicion -> EN_VUELO (Ascenso activo. Amarillo parpadeo rapido 200 ms - Respaldo 8s iniciado)");
    break;
    case APOGEO_IGNICION:
    digitalWrite(PIN_LED_3, HIGH);
    digitalWrite(PIN_LED_IGN, HIGH);
    ignitionStartTime = millis();
    ignitionDone = false;
    enviarLog("Transicion -> APOGEO_IGNICION (Disparo unico de paracaidas 1000ms - Esperando aterrizaje)");
    break;
    case ATERRIZADO:
    ledBlinkState = HIGH;
    digitalWrite(PIN_LED_3, ledBlinkState);
    lastBlinkTime = millis();
    enviarLog("TRANSICION -> ATERRIZADO (Baliza de recuperacion activa - LED Rojo parpadeando)");
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
     enviarLog("[IGNORADO] Pulsacion corta no valida en este estado.");
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
     enviarLog("[IGNORADO] Pulsacion larga bloqueada en este estado.");
     break;
  }
}

void TareaBoton(void* pvParameters) {
  for (;;) {
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
        unsigned long pressDuration = millis() - pressStartTime;
        isPressed = false;

        if (pressDuration < SHORT_PRESS_MAX) {
          EventoBoton ev = EVENTO_PULSACION_CORTA;
          xQueueSend(colaEventos, &ev, 0);
        } else if (pressDuration >= LONG_PRESS_MIN) {
          EventoBoton ev = EVENTO_PULSACION_LARGA;
          xQueueSend(colaEventos, &ev, 0);
        } else {
          enviarLog("[IGNORADO] Pulsacion en zona muerta (1000-2000 ms)");
        }
      }
    }
   }

   lastReading = reading;
   taskYIELD(); //cede CPU al planificador FreeRTOS sin bloquear ni usar delay()
  }
}
// Maquina de estados, temp y control de leds 
void TareaControlLEDs(void* pvParameters) {
  EventoBoton eventoRecibido;

  for (;;) {
    //1esto me revisa si hay un evento nuevo en la cola
    if (xQueueReceive(colaEventos, &eventoRecibido, 0) == pdTRUE) {
      if (eventoRecibido == EVENTO_PULSACION_CORTA) {
        procesarPulsacionCorta();
      } else if (eventoRecibido == EVENTO_PULSACION_LARGA) {
        procesarPulsacionLarga();
      }
    }
    //2esta es la logica continua de los parpadeos y temporizadores con milis
    switch (estadoActual) {
      case DESARMADO:
      if (millis() - lastBlinkTime >= SLOW_BLINK_MS) {
        lastBlinkTime = millis();
        ledBlinkState = !ledBlinkState;
        digitalWrite(PIN_LED_1, ledBlinkState);
      }
      break;
      
      case EN_VUELO:
       if (millis() - lastBlinkTime >= FAST_BLINK_MS) {
        lastBlinkTime = millis();
        ledBlinkState = !ledBlinkState;
        digitalWrite(PIN_LED_2, ledBlinkState);
       }
       if (millis() - ascentStartTime >= BACKUP_DEPLOY_MS) {
        enviarLog("[ALERTA] Temporizador de respaldo (8000 ms) activado!");
        cambiarEstado(APOGEO_IGNICION);
       }
       break;
      case APOGEO_IGNICION:
       if (!ignitionDone && (millis() - ignitionStartTime >= IGNITION_PULSE_MS)) {
        digitalWrite(PIN_LED_IGN, LOW);
        ignitionDone = true;
        enviarLog("Pulso de ignicion finalizado (1000 ms). Esperando aterrizaje...");
       }
       break;
      case ATERRIZADO:
       if (millis() - lastBlinkTime >= BEACON_BLINK_MS) {
        lastBlinkTime = millis();
        ledBlinkState = !ledBlinkState;
        digitalWrite(PIN_LED_3, ledBlinkState);
       }
       break;

       default:
       break;
    }

    taskYIELD();
  }
}
// escribe los registros seriales desde colalogs sin colisiones
void TareaRegistroSerial(void* pvParameters) {
  MensajeLog paquete;

  for (;;) {
    if (xQueueReceive(colaLogs, &paquete, 0) == pdTRUE) {
      Serial.print("[");
      Serial.print(paquete.timestamp);
      Serial.print(" ms] ");
      Serial.println(paquete.texto);
    }
    taskYIELD();
  }
}
void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_LED_1, OUTPUT);
  pinMode(PIN_LED_2, OUTPUT);
  pinMode(PIN_LED_3, OUTPUT);
  pinMode(PIN_LED_IGN, OUTPUT);
  // CREA COLAS FIFO ANTES DE INICIAR LAS TAREAS 
  colaEventos = xQueueCreate(10, sizeof(EventoBoton));
  colaLogs = xQueueCreate(20, sizeof(MensajeLog));

  enviarLog("=== SISTEMA DE RECUPERACION INICIADO (FreeRTOS Dual-Core) ===");
  cambiarEstado (DESARMADO);

  //esto me crea 3 tareas repartidas en los os nucleos de la esp32
  xTaskCreatePinnedToCore(TareaBoton, "TareaBoton",  2048, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(TareaRegistroSerial, "TareaSerial", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(TareaControlLEDs, "TareaLEDs", 4096, NULL, 2, NULL, 1);
}
void loop() {
  //FreeRTOS toma el control de las tareas, se elimina el loop() para liberar ram/cpu
  vTaskDelete(NULL);
}


// Simulacion de hardware en Wokwi: https://wokwi.com/projects/481081023571677185