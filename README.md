# AgroMonitor · firmware LoRa (ESP32 Heltec V3)

Nodos de campo y gateway LoRa para monitorear una plantación (hoy cacao en Cerro Blanco 5ta
Sección, Tacotalpa, Tabasco). Cada nodo mide suelo y ambiente, transmite por LoRa y duerme;
el gateway reenvía cada paquete por USB a una Raspberry Pi, donde
[msa-back](https://github.com/jipixz/msa-back) lo guarda en MongoDB y
[msa-front](https://github.com/jipixz/msa-front) lo muestra.

```
 Nodo 0..N (campo)              Gateway (casa)            Raspberry Pi
┌──────────────────┐  LoRa    ┌───────────────┐  USB    ┌─────────────────────────────┐
│ HW-390  DS18B20  │ 905.2MHz │ Heltec V3     │ serial  │ msa-back (Node) → MongoDB    │
│ BME280  BH1750   │ ───────▶ │ receiver.ino  │ ──────▶ │ msa-back-LR-py (predicciones)│
│ YL-83   sender   │  SF12    │ OLED + LEDs   │ 115200  │ msa-front (panel web)        │
└──────────────────┘          └───────────────┘         └─────────────────────────────┘
```

## Estructura

| Carpeta | Qué es |
|---|---|
| `transmisor/sender/` | Nodo de campo. **`config.h`** tiene lo que cambia por nodo (ID, intervalo, calibración, energía). |
| `receptor/receiver/` | Gateway: recibe, reenvía una línea por paquete por Serial, resumen en OLED y LEDs. |
| `calibration/humiditySensorCalibration/` | Muestra el valor crudo del HW-390 para obtener `DRY`/`WET`. |
| `tests/` | Pruebas del firmware (`node --test`) y compilación de los 3 sketches. |
| `herramientas/consola/` | Consola web en vivo del banco de pruebas: puertos serie (PC y Pi), logs del kernel y de Windows, y build/flasheo. |

## Hardware y pines (nodo)

| Sensor | Mide | Pin (GPIO) |
|---|---|---|
| HW-390 capacitivo | Humedad del suelo (ADC) | 7 |
| DS18B20 | Temperatura del suelo (1-Wire, pull-up 4.7 kΩ) | 1 ⚠️ |
| BME280 | Temperatura, humedad del aire y presión (I²C 0x76) | SDA 41 · SCL 42 |
| BH1750 | Luz (I²C 0x23) | SDA 41 · SCL 42 |
| YL-83 | Lluvia (analógico y digital) | 2 · 3 |

- Placa: **Heltec WiFi LoRa 32 V3** (ESP32-S3 + SX1262). Solo GPIO1–20 tienen ADC.
- ⚠️ En la V3 el **GPIO1 también es VBAT_ADC** (lectura de batería). Mientras el DS18B20 esté
  ahí no se puede reportar el voltaje de la batería: mover el DS18B20 a otro GPIO libre es el
  siguiente cambio de hardware.
- Alimentación: shield **V9 de 4×18650** con el switch en **HOLD** (en NORMAL se apaga solo
  cuando el nodo duerme). Si tu shield no tiene HOLD, activa `USAR_DUMMY_LOAD` en `config.h`.

### Cableado 2 (recomendado para la baquelita)

`config.h` → `CABLEADO`: **1** = cableado actual (tabla de arriba), **2** = este. Cambia a 2
solo después de recablear; si flasheas el 2 con el cableado viejo, el DS18B20 y la lluvia
digital no responderán (el resto sigue funcionando).

| Señal | Cableado 1 (actual) | Cableado 2 | Motivo |
|---|---|---|---|
| DS18B20 datos (pull-up 4.7 kΩ a VCC) | GPIO1 | **GPIO39** | GPIO1 es VBAT_ADC: libre para leer la batería |
| YL-83 salida digital | GPIO3 | **GPIO40** | GPIO3 es pin de arranque (strapping) |
| VCC de HW-390, YL-83, DS18B20, BME280, BH1750 | 3V3 | **Ve** (Vext) | Vext se apaga en deep sleep: el HW-390 y el LED del módulo YL-83 dejan de consumir |
| GND de todos los sensores | GND | GND | — |
| HW-390 analógico | GPIO7 | GPIO7 | — |
| YL-83 analógico | GPIO2 | GPIO2 | — |
| I²C SDA / SCL | GPIO41 / 42 | GPIO41 / 42 | — |

Con el cableado 2 el paquete agrega `Bat:3.91` (voltaje de la batería, V); el backend lo guarda
como `bateria` y el panel muestra la columna "Batería" en la tabla de nodos.

Pines que **no** se deben usar en la V3: 0 (botón), 1 (VBAT), 3/45/46 (arranque), 8–14 (LoRa),
17/18/21 (OLED), 19/20 (USB), 35 (LED), 36 (Vext), 37 (control de VBAT). La prueba
`tests/decisiones.test.mjs` revisa que el cableado 2 no los use.

## Configurar un nodo nuevo

1. Calibra el sensor de humedad con `calibration/` (seco al aire y en suelo saturado) y copia
   los valores a `DRY` y `WET` de `transmisor/sender/config.h`.
2. Pon un `NODE_ID` único (0–255) en `config.h`.
3. Elige el modo: `MODO_AHORRO 1` (campo: cada 30 min, OLED apagado) o `0` (instalación:
   cada 60 s y cada lectura en el OLED).
4. Compila y sube `transmisor/sender` a la placa del nodo.

Al **encender** la placa (no al despertar), en cualquier modo:

- El LED blanco hace la firma del firmware: nodo `· · · —` ×2, gateway `— —` ×2. El LED rojo
  es el de carga de la batería: lo controla el hardware, no el firmware.
- El OLED muestra nombre, versión, modo y cableado (`SEGUNDOS_INICIO`) y luego la revisión de
  sensores en dos páginas (`SEGUNDOS_PAGINA` cada una): `OK` con su dirección I²C o `--` si
  no respondió, y el valor crudo de los analógicos. También busca el BME280 en 0x76/0x77 y el
  BH1750 en 0x23/0x5C, y lista cualquier otro dispositivo del bus I²C.

## Ciclo del nodo y energía

1. Despierta, mide (BME280 en modo forzado y BH1750 de una sola medición: ambos duermen
   entre lecturas) y transmite de inmediato.
2. Duerme `INTERVALO_S` (30 min) **más** un aleatorio de 0–`JITTER_MAX_S` s para que varios
   nodos no transmitan a la vez. El aleatorio se duerme, no se espera despierto.
3. `heltec_deep_sleep()` apaga correctamente el OLED, Vext y la radio.
   Al despertar se enciende Vext a mano antes de `heltec_setup()`: la librería solo lo hace en
   el Wireless Stick, y sin él el OLED de la V3 se ve tenue (y los sensores en Ve no tendrían corriente).
4. Si un sensor falla, el paquete sale igual con `nan` en ese campo y `Err:` con su nombre.

## Protocolo (una línea por paquete)

```
HS:45|T1:25.3|T2:26.1|P:1008|HA:80.2|Lux:18400|Rain:0|RainBool:0|Nodo:3|Seq:152[|Err:BME280]
```

| Clave | Campo en Mongo | Unidad |
|---|---|---|
| `HS` | humedadSuelo | % (0–100) |
| `T1` | temperaturaDS | °C (DS18B20, suelo) |
| `T2` | temperaturaBME | °C (aire) |
| `P` | presion | hPa |
| `HA` | humedadAire | % |
| `Lux` | luminosidad | lx |
| `Rain` | lluvia | % (sensor YL-83) |
| `RainBool` | rainDigital | 1 = llueve, 0 = no |
| `Nodo` | nodo | ID del nodo |
| `Seq` | — | contador de ciclos (un salto = paquete perdido) |
| `Bat` | bateria | V (solo con cableado 2) |
| `Err` | — | sensores que fallaron en ese ciclo |

- Sin unidades ni espacios: ~76 bytes, **≈2.5 s al aire** con SF12/BW250/CR4-8 (antes ~100
  bytes y ≈3.0 s). El backend también acepta el formato anterior (`HS:45% | T1:25.3C | …`).
- El backend descarta valores fuera del rango físico (p. ej. 700 % de un ADC al aire).
- El contrato con el backend vive en `msa-back/test/fixtures/paquetes-firmware.txt`.

## Radio

905.2 MHz · SF12 · BW 250 kHz · CR 4/8 · preámbulo 12 · CRC · 22 dBm. Emisor y receptor
deben coincidir (lo revisa `tests/decisiones.test.mjs`). Revisa la normativa de 902–928 MHz
de tu país (en México, IFT) antes de un despliegue fijo: con un canal de 250 kHz los tiempos
largos al aire pueden exceder lo permitido; BW 500 kHz es la alternativa.

## Compilar

Arduino IDE 2 o `arduino-cli`, placa **`esp32:esp32:heltec_wifi_lora_32_V3`**.

| Dependencia | Versión probada |
|---|---|
| Core esp32 (Espressif) | 3.2.0 |
| Heltec_ESP32_LoRa_v3 (`heltec_unofficial`) | 0.9.2 |
| RadioLib | **7.x** (8.0.0 no compila con heltec_unofficial 0.9.2) |
| Adafruit BME280 / Unified Sensor | 2.3.0 / 1.1.15 |
| OneWire / DallasTemperature | 2.3.8 / 4.0.6 |
| BH1750 | 1.3.0 |

## Pruebas

```bash
node --test tests/decisiones.test.mjs   # radio igual en emisor y receptor, protocolo, energía, calibración
node tests/compilar.mjs                 # compila los 3 sketches (ARDUINO_CLI=ruta si no está en el PATH)
node --test tests/consola.test.mjs      # analizador y puertos de la consola
```

## Consola en vivo

```bash
MSA_PI=usuario@ip-de-la-pi node herramientas/consola/servidor.mjs   # http://localhost:4747
node herramientas/consola/correr.mjs --tag flash -- arduino-cli upload ...   # el comando y su salida, en vivo
```

Solo lectura: muestra los puertos serie de la PC y de la Pi (qué chip USB hay detrás y qué
proceso los tiene abiertos), el log del kernel de la Pi y las bitácoras System y Kernel-PnP
de Windows con su nivel, y los datos del chip y del enlace LoRa que salen al flashear o al
arrancar una placa. Sin dependencias: solo Node.js.

## Pendientes

- Recablear la baquelita al cableado 2 y poner `CABLEADO 2` (batería y sensores apagados en el sueño).
- Probar en campo el consumo del ciclo nuevo y el alcance con varios nodos.
- Paquete binario y ACK con reintento (requiere cambiar receptor y backend a la vez).
- Configuración por plantación/cultivo (umbrales y nodos por archivo), para usarlo con
  otros cultivos además del cacao.

## Licencia

[MIT](LICENSE) · Proyecto de la Maestría en la Universidad Juárez Autónoma de Tabasco.
