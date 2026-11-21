# Sistema de Recuperación e Ignición de Paracaídas (ESP32 + FreeRTOS)

**Autor:** Juan Sanabria Marín  
**Carrera y Semestre:** Ingeniería Aeroespacial — 1.er semestre (ENES Juriquilla, UNAM)  
**Ruta elegida:** Ruta 3 — Firmware y sistemas embebidos (con Bonus FreeRTOS y Bonus de Git)  

---

## Descripción del Proyecto

Firmware en C/C++ para el control de bajo nivel del sistema de recuperación e ignición pirotécnica de un cohete experimental. Implementa una máquina de estados finitos (FSM) determinista de 5 estados (`DESARMADO`, `ARMADO`, `EN_VUELO`, `APOGEO_IGNICION` y `ATERRIZADO`) sobre un microcontrolador **ESP32** utilizando **FreeRTOS en arquitectura dual-core**, **interrupciones por hardware** (`IRAM_ATTR`) y **temporizadores por software**, garantizando:

1. **Cero disparos accidentales en tierra:** Armado deliberado mediante pulsación larga ($\ge 2000\text{ ms}$) antes de habilitar la detección de despegue.
2. **Disparo único de ignición:** Pulso pirotécnico aislado de exactamente $1000\text{ ms}$ mediante timer *one-shot* con bloqueo por bandera (`ignitionDone`).
3. **Despliegue de respaldo ante falla de apogeo:** Temporizador automático de $8000\text{ ms}$ tras el despegue que activa la ignición si no se recibe señal de apogeo.
4. **Cero uso de `delay()`:** Ejecución 100% guiada por eventos mediante colas FIFO (`QueueHandle_t`) y espera pasiva (`ESPERA_SIN_FIN = 0xFFFFFFFFUL`) con 0% de consumo de CPU en reposo.

---

## Herramientas y Versiones Utilizadas

* **Microcontrolador:** Espressif ESP32 (arquitectura dual-core Xtensa LX6 a 240 MHz)
* **Sistema Operativo en Tiempo Real:** FreeRTOS (integrado en ESP-IDF / Arduino Core for ESP32 v2.0.17)
* **Lenguaje:** C / C++ embebido
* **Simulador de Hardware:** Wokwi Simulator (`https://wokwi.com/projects/481081023571677185`)
* **Control de Versiones:** Git 2.47+ (UCRT64) y GitHub
* **Documentación Técnica:** LaTeX (Overleaf / TeX Live 2025) con paquetes `tikz` y `circuitikz`

---

## Mapeo de Hardware (GPIO)

| Componente | Pin ESP32 | Configuración | Función en el Sistema |
| :--- | :---: | :--- | :--- |
| Botón Pulsador | `GPIO 4` | `INPUT_PULLUP` + Interrupción `CHANGE` | Entrada de eventos (pulsación corta $<1000\text{ ms}$, larga $\ge 2000\text{ ms}$) |
| LED Verde | `GPIO 18` | `OUTPUT` (resistencia $220\ \Omega$) | Parpadeo lento ($1000\text{ ms}$) en `DESARMADO`; fijo en `ARMADO` |
| LED Amarillo | `GPIO 19` | `OUTPUT` (resistencia $220\ \Omega$) | Parpadeo rápido ($200\text{ ms}$) en `EN_VUELO` |
| LED Rojo | `GPIO 21` | `OUTPUT` (resistencia $220\ \Omega$) | Fijo en `APOGEO_IGNICION`; baliza ($500\text{ ms}$) en `ATERRIZADO` |
| LED Azul (Ignición) | `GPIO 22` | `OUTPUT` (resistencia $220\ \Omega$) | Pulso único de ignición pirotécnica de $1000\text{ ms}$ |

---

## Cómo Ejecutar y Reproducir Paso a Paso

### Opción 1: Simulación en Línea en Wokwi (Recomendada)
1. Abre el proyecto público en Wokwi: **[Simulación Paracaídas V1 — Wokwi](https://wokwi.com/projects/481081023571677185)**.
2. Haz clic en el botón verde **Start Simulation** (`Play`).
3. Abre el **Serial Monitor** (115200 baudios) en la parte inferior para observar la telemetría con marcas de tiempo `[ms]`.
4. Ejecuta la secuencia de prueba con el botón verde (`GPIO 4`):
   * **Pulsación corta ($<1\text{ s}$)** en `DESARMADO`: El sistema ignora la entrada y lo registra en consola.
   * **Pulsación larga ($\ge 2\text{ s}$):** Cambia de `DESARMADO` a `ARMADO` (LED Verde fijo).
   * **Pulsación corta ($<1\text{ s}$):** Simula el despegue pasando a `EN_VUELO` (LED Amarillo parpadeando cada $200\text{ ms}$).
   * **Espera de $8\text{ s}$ sin pulsar (o pulsación corta):** Activa `APOGEO_IGNICION` (LED Rojo fijo + pulso único de $1000\text{ ms}$ en LED Azul).
   * **Pulsación corta ($<1\text{ s}$):** Simula el aterrizaje pasando al estado terminal `ATERRIZADO` (LED Rojo parpadeando cada $500\text{ ms}$).

### Opción 2: Compilación Local en ESP32 Físico
1. Clona este repositorio:
   ```bash
   git clone https://github.com/Snabriab/avionica-reto-2026.git
   ```
2. Abre `src/paracaidas.ino` en Arduino IDE (con soporte para tarjetas **ESP32 by Espressif Systems**) o PlatformIO.
3. Conecta los 4 LEDs (con resistencias de $220\ \Omega$ a GND) y el botón entre `GPIO 4` y `GND`.
4. Selecciona la placa **ESP32 Dev Module**, compila y carga el firmware a `115200` baudios.

---

## Evidencia en Video

* **Demostración en video ($\le 60\text{ s}$):** [Ver en YouTube (No listado)](https://youtu.be/sCkcJUiVbE8)

---

## Estructura del Repositorio

```text
avionica-reto-2026/
├── src/
│   └── paracaidas.ino          # Firmware completo en C/C++ con FreeRTOS, ISR y timers
├── docs/
│   ├── reporte_avionica.tex    # Código fuente en LaTeX del reporte técnico (<= 3 páginas)
│   └── reporte_avionica.pdf    # Reporte técnico compilado
├── media/
│   ├── circuito_wokwi.jpg      # Captura del esquemático y simulación en Wokwi
│   └── monitor_serial.png      # Captura de telemetría serial y tiempos de transición
├── .gitignore                  # Exclusión de archivos temporales de LaTeX y compilación
└── README.md                   # Documentación principal del proyecto
```