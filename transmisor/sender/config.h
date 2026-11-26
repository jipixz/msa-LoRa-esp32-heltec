// Configuración de ESTE nodo. Al instalar un nodo nuevo, normalmente solo cambias NODE_ID
// (y la calibración del sensor de humedad si usas otro sensor o tipo de suelo).
#pragma once

// ---- Identidad ----
// Único por nodo dentro de la finca (0–255). El backend lo guarda en el campo "nodo".
const int NODE_ID = 0;

// ---- Modo ----
// 1 = AHORRO (campo): mide cada 30 min y el OLED se queda apagado (solo se ve al encender).
// 0 = PRUEBA (instalación): mide cada 60 s y muestra cada lectura en el OLED a brillo máximo.
#ifndef MODO_AHORRO  // se puede forzar al compilar: -DMODO_AHORRO=0
#define MODO_AHORRO 1
#endif

#if MODO_AHORRO
const int INTERVALO_S = 1800;  // cada cuánto despierta a medir y transmitir (s)
const int JITTER_MAX_S = 60;   // aleatorio que se SUMA al sueño para que los nodos no choquen
#define MOSTRAR_OLED 0         // lecturas en el OLED en cada ciclo
#else
const int INTERVALO_S = 60;
const int JITTER_MAX_S = 5;
#define MOSTRAR_OLED 1
#endif

// Versión del firmware: aparece en el OLED y por Serial al encender la placa
#define FIRMWARE_VERSION "2026.11.25"

// ---- Calibración del sensor de humedad de suelo (HW-390) ----
// Valores crudos del ADC: usa calibration/humiditySensorCalibration para medirlos.
const int DRY = 3600;  // suelo completamente seco (o sensor al aire)
const int WET = 1100;  // suelo saturado de agua

// ---- Cableado ----
// 1 = cableado actual: DS18B20 en GPIO1, lluvia digital en GPIO3, sensores alimentados de 3V3.
// 2 = cableado nuevo (ver README, "Cableado 2"): DS18B20 en GPIO39, lluvia digital en GPIO40,
//     VCC de los sensores en Ve (Vext: se apagan durante el sueño) y lectura de la batería
//     (GPIO1 queda libre para VBAT). Cambia a 2 SOLO después de recablear la baquelita.
#ifndef CABLEADO  // se puede forzar al compilar: -DCABLEADO=2
#define CABLEADO 1
#endif

// ---- Energía ----
// El shield V9 de 4×18650 en modo NORMAL se apaga si el consumo es muy bajo (deep sleep).
// Con el switch en HOLD no hace falta nada; si tu shield no tiene HOLD, pon 1 para mantener
// una carga en DUMMY_LOAD_PIN también durante el sueño (gasta batería: úsalo solo si es necesario).
#define USAR_DUMMY_LOAD 0
const int DUMMY_LOAD_PIN = 48;

// Al encender la placa (no al despertar) se muestra, también en modo ahorro, el arranque y
// luego la revisión de sensores en dos páginas: sirve para comprobar la instalación.
const int SEGUNDOS_INICIO = 3;   // nombre, versión y modo
const int SEGUNDOS_PAGINA = 2;   // cada página de sensores (1/2 digitales, 2/2 analógicos)
