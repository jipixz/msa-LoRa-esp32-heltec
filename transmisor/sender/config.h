// Configuración de ESTE nodo. Al instalar un nodo nuevo, normalmente solo cambias NODE_ID
// (y la calibración del sensor de humedad si usas otro sensor o tipo de suelo).
#pragma once

// ---- Identidad ----
// Único por nodo dentro de la finca (0–255). El backend lo guarda en el campo "nodo".
const int NODE_ID = 0;

// ---- Ciclo de trabajo ----
// Cada cuánto despierta a medir y transmitir (segundos).
const int INTERVALO_S = 1800;
// Aleatorio que se SUMA al sueño (0..JITTER_MAX_S) para que varios nodos no transmitan a la
// vez. Antes se esperaba despierto antes de transmitir; dormido cuesta casi nada.
const int JITTER_MAX_S = 60;

// ---- Calibración del sensor de humedad de suelo (HW-390) ----
// Valores crudos del ADC: usa calibration/humiditySensorCalibration para medirlos.
const int DRY = 3600;  // suelo completamente seco (o sensor al aire)
const int WET = 1100;  // suelo saturado de agua

// ---- Energía ----
// El shield V9 de 4×18650 en modo NORMAL se apaga si el consumo es muy bajo (deep sleep).
// Con el switch en HOLD no hace falta nada; si tu shield no tiene HOLD, pon 1 para mantener
// una carga en DUMMY_LOAD_PIN también durante el sueño (gasta batería: úsalo solo si es necesario).
#define USAR_DUMMY_LOAD 0
const int DUMMY_LOAD_PIN = 48;

// Mostrar las lecturas en el OLED (útil al instalar; en campo puede ir en 0 para ahorrar).
#define MOSTRAR_OLED 1
