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

const TickType_t ESPERA_SIN_FIN = 0xFFFFFFFFUL; // Equivale al valor maximo de TickType_t en FreeRTOS (32 bits)

//estados deee la mision
enum EstadoMision {
  DESARMADO,
  ARMADO,
  EN_VUELO,
  APOGEO_IGNICION,
  ATERRIZADO
};

// tipos de eventos que viajan hacia la maquina de estados tareaControlLeds
enum TipoEvento {
  EV_PULSACION_CORTA,
  EV_PULSACION_LARGA,
  EV_ZONA_MUERTA,
  EV_TICK_PARPADEO,
  EV_RESPALDO_8S,
  EV_FIN_IGNICION
};
//ESTRUCTURA PARA ENVIAR REGISTROS AL MONITOR EN SERIES SIN COLISIONES
struct MensajeLog {
  unsigned long timestamp;
  char texto[100];
};

//tres colas FIFO independientes para comunnicar cada etapa
QueueHandle_t colaCrudaISR; 
QueueHandle_t colaEventos;
QueueHandle_t colaLogs;

//Timers por interrupcion interna de FreeRTOS 
TimerHandle_t timerParpadeo;
TimerHandle_t timerRespaldo;
TimerHandle_t timerIgnicion;

//Variables de estado de la mision
EstadoMision estadoActual = DESARMADO;
bool ledBlinkState = LOW;
bool ignitionDone = false;

//variable compartida con la interrupcion por hardware
volatile unsigned long lastIsrTime = 0;

//esto me enpaqueta un mensaje con marca de tiempo 
void enviarLog(const char* msg) {
  if (colaLogs != NULL) {
    MensajeLog paquete;
    paquete.timestamp = millis();
    snprintf(paquete.texto, sizeof(paquete.texto), "%s", msg);
    xQueueSend(colaLogs, &paquete, 0);
  }
}

// Rutina de interrupcion por hardware en gpio4
void IRAM_ATTR isrBoton() {
  unsigned long ahora = millis();
  //filtro antirrebote 50 ms directamente en la interrupcion
  if (ahora - lastIsrTime >= DEBOUNCE_MS) {
    lastIsrTime = ahora;
    int estadoPin = digitalRead(PIN_BTN);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(colaCrudaISR, &estadoPin, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
    }
  }
}

//callbacks de los Timers de FreeRTOS (envian eventos a colaEventos sin bloquear)
void cbTimerParpadeo(TimerHandle_t xTimer) {
  TipoEvento ev= EV_TICK_PARPADEO;
  xQueueSend(colaEventos, &ev, 0);
}

void cbTimerRespaldo(TimerHandle_t xTimer) {
  TipoEvento ev = EV_RESPALDO_8S;
  xQueueSend(colaEventos, &ev, 0);
}
void cbTimerIgnicion(TimerHandle_t xTimer) {
  TipoEvento ev = EV_FIN_IGNICION;
  xQueueSend(colaEventos, &ev, 0);
}
void cambiarEstado(EstadoMision nuevoEstado){
  estadoActual = nuevoEstado;

  //detener temporizadores activos del estado anteriror antes de configurar el nuevo
  xTimerStop(timerParpadeo, 0);
  xTimerStop(timerRespaldo, 0);
  xTimerStop(timerIgnicion, 0);

  //apagar todos lo lesds antes de configurar el nuevo estado
  digitalWrite(PIN_LED_1, LOW);
  digitalWrite(PIN_LED_2, LOW);
  digitalWrite(PIN_LED_3, LOW);
  digitalWrite(PIN_LED_IGN, LOW);
  ledBlinkState = HIGH;

  switch (estadoActual) {
    case DESARMADO:
     digitalWrite(PIN_LED_1, HIGH);
     xTimerChangePeriod(timerParpadeo, pdMS_TO_TICKS(SLOW_BLINK_MS), 0);
     xTimerStart(timerParpadeo, 0);
     enviarLog("Transicion -> DESARMADO (Verde parpadeo lento 1000 ms)");
     break;
    case ARMADO:
      digitalWrite(PIN_LED_1, HIGH);
      enviarLog("Transicion -> ARMADO (Verde fijo - Listo para lanzamiento)");
      break;

    case EN_VUELO:
      digitalWrite(PIN_LED_2, HIGH);
      xTimerChangePeriod(timerParpadeo, pdMS_TO_TICKS(FAST_BLINK_MS), 0);
      xTimerStart(timerParpadeo, 0);
      xTimerStart(timerRespaldo, 0); // Inicia cuenta regresiva unica de 8000 ms
      enviarLog("Transicion -> EN_VUELO (Amarillo parpadeo rapido 200 ms - Respaldo 8s iniciado)");
      break;

    case APOGEO_IGNICION:
      digitalWrite(PIN_LED_3, HIGH);
      digitalWrite(PIN_LED_IGN, HIGH);
      ignitionDone = false;
      xTimerStart(timerIgnicion, 0); // Inicia pulso pirotecnico unico de 1000 ms
      enviarLog("Transicion -> APOGEO_IGNICION (Disparo unico de paracaidas 1000 ms - Esperando aterrizaje)");
      break;

    case ATERRIZADO:
      digitalWrite(PIN_LED_3, HIGH);
      xTimerChangePeriod(timerParpadeo, pdMS_TO_TICKS(BEACON_BLINK_MS), 0);
      xTimerStart(timerParpadeo, 0);
      enviarLog("Transicion -> ATERRIZADO (Baliza de recuperacion activa - Rojo parpadeando 500 ms)");
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

// TAREA 1 (Nucleo 0): Calcula duracion del pulso despertada por interrupcion
void TareaBoton(void* pvParameters) {
  int estadoPinRecibido;
  unsigned long tiempoInicioPulsacion = 0;
  bool botonPresionado = false;

  for (;;) {
    // Espera pasiva (0% CPU) hasta que la ISR en GPIO 4 envia un cambio de estado
    if (xQueueReceive(colaCrudaISR, &estadoPinRecibido, ESPERA_SIN_FIN) == pdTRUE) {
      if (estadoPinRecibido == LOW && !botonPresionado) {
        tiempoInicioPulsacion = millis();
        botonPresionado = true;
      } else if (estadoPinRecibido == HIGH && botonPresionado) {
        unsigned long duracion = millis() - tiempoInicioPulsacion;
        botonPresionado = false;

        if (duracion < SHORT_PRESS_MAX) {
          TipoEvento ev = EV_PULSACION_CORTA;
          xQueueSend(colaEventos, &ev, 0);
        } else if (duracion >= LONG_PRESS_MIN) {
          TipoEvento ev = EV_PULSACION_LARGA;
          xQueueSend(colaEventos, &ev, 0);
        } else {
          enviarLog("[IGNORADO] Pulsacion en zona muerta (1000-2000 ms)");
        }
      }
    }
  }
}

// TAREA 2 (Nucleo 1): Ejecuta la Maquina de Estados y LEDs guiada por eventos
void TareaControlLEDs(void* pvParameters) {
  TipoEvento ev;

  for (;;) {
    // Espera pasiva (0% CPU) hasta recibir evento de boton o alarma de timer
    if (xQueueReceive(colaEventos, &ev, ESPERA_SIN_FIN) == pdTRUE) {
      switch (ev) {
        case EV_PULSACION_CORTA:
          procesarPulsacionCorta();
          break;

        case EV_PULSACION_LARGA:
          procesarPulsacionLarga();
          break;

        case EV_TICK_PARPADEO:
          ledBlinkState = !ledBlinkState;
          if (estadoActual == DESARMADO) {
            digitalWrite(PIN_LED_1, ledBlinkState);
          } else if (estadoActual == EN_VUELO) {
            digitalWrite(PIN_LED_2, ledBlinkState);
          } else if (estadoActual == ATERRIZADO) {
            digitalWrite(PIN_LED_3, ledBlinkState);
          }
          break;

        case EV_RESPALDO_8S:
          if (estadoActual == EN_VUELO) {
            enviarLog("[ALERTA] Temporizador de respaldo (8000 ms) activado!");
            cambiarEstado(APOGEO_IGNICION);
          }
          break;

        case EV_FIN_IGNICION:
          if (estadoActual == APOGEO_IGNICION && !ignitionDone) {
            digitalWrite(PIN_LED_IGN, LOW);
            ignitionDone = true;
            enviarLog("Pulso de ignicion finalizado (1000 ms). Esperando aterrizaje...");
          }
          break;

        default:
          break;
      }
    }
  }
}

// TAREA 3 (Nucleo 0): Imprime telemetria serial sin colisiones
void TareaRegistroSerial(void* pvParameters) {
  MensajeLog paquete;

  for (;;) {
    // Espera pasiva (0% CPU) hasta que llega un mensaje a colaLogs
    if (xQueueReceive(colaLogs, &paquete, ESPERA_SIN_FIN) == pdTRUE) {
      Serial.print("[");
      Serial.print(paquete.timestamp);
      Serial.print(" ms] ");
      Serial.println(paquete.texto);
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_LED_1, OUTPUT);
  pinMode(PIN_LED_2, OUTPUT);
  pinMode(PIN_LED_3, OUTPUT);
  pinMode(PIN_LED_IGN, OUTPUT);

  // 1. Crear colas FIFO antes de tareas, timers e interrupciones
  colaCrudaISR = xQueueCreate(10, sizeof(int));
  colaEventos  = xQueueCreate(15, sizeof(TipoEvento));
  colaLogs     = xQueueCreate(20, sizeof(MensajeLog));

  // 2. Crear timers por software del RTOS (pdTRUE = periodico, pdFALSE = un disparo)
  timerParpadeo = xTimerCreate("TimerBlink",  pdMS_TO_TICKS(SLOW_BLINK_MS),     pdTRUE,  NULL, cbTimerParpadeo);
  timerRespaldo = xTimerCreate("TimerBackup", pdMS_TO_TICKS(BACKUP_DEPLOY_MS),  pdFALSE, NULL, cbTimerRespaldo);
  timerIgnicion = xTimerCreate("TimerIgn",    pdMS_TO_TICKS(IGNITION_PULSE_MS), pdFALSE, NULL, cbTimerIgnicion);

  // 3. Registrar interrupcion por hardware en GPIO 4 ante cualquier flanco (CHANGE)
  attachInterrupt(digitalPinToInterrupt(PIN_BTN), isrBoton, CHANGE);

  // 4. Iniciar en estado seguro DESARMADO y arrancar las 3 tareas en dual-core
  enviarLog("=== SISTEMA DE RECUPERACION INICIADO (FreeRTOS + Interrupciones) ===");
  cambiarEstado(DESARMADO);

  xTaskCreatePinnedToCore(TareaBoton,          "TareaBoton",  2048, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(TareaRegistroSerial, "TareaSerial", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(TareaControlLEDs,    "TareaLEDs",   4096, NULL, 2, NULL, 1);
}

void loop() {
  // Se elimina la tarea loop() por defecto; todo el sistema opera por interrupciones
  vTaskDelete(NULL);
}



// Simulacion de hardware en Wokwi: https://wokwi.com/projects/481081023571677185