// Nodo de campo (emisor) para Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262).
// Ciclo: despierta → mide → transmite un paquete LoRa → duerme INTERVALO_S + jitter.
// La configuración del nodo (ID, intervalo, calibración, energía) está en config.h.
#include <heltec_unofficial.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <BH1750.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "config.h"

// Configuración de pines (Heltec V3, ver README: header J2/J3)
const int LED_BLANCO = 35;    // único LED programable de la placa (el rojo es el de carga)
const int sensorPin = 7;      // GPIO7 - Entrada analógica del sensor HW-390
#if CABLEADO == 2
const int ds18b20Pin = 39;    // GPIO39 - DS18B20 (cableado 2: GPIO1 queda para leer la batería)
const int rainDigitalPin = 40; // GPIO40 - Entrada digital YL-83 (cableado 2: GPIO3 es de arranque)
#define LEER_BATERIA 1
#else
const int ds18b20Pin = 1;     // GPIO1 - DS18B20 (cableado 1; en la V3 también es VBAT_ADC)
const int rainDigitalPin = 3; // GPIO3 - Entrada digital YL-83 (cableado 1)
#define LEER_BATERIA 0
#endif
const int sdaPin = 41;        // GPIO41 - SDA para BME280 y BH1750
const int sclPin = 42;        // GPIO42 - SCL para BME280 y BH1750
const int rainAnalogPin = 2;  // GPIO2 - Entrada analógica YL-83

// Contador de ciclos: sobrevive al deep sleep (RTC). Viaja como "Seq" para detectar
// paquetes perdidos (si el backend ve un salto en Seq, se perdió al menos un paquete).
RTC_DATA_ATTR uint32_t bootCount = 0;

// Estado de los sensores en este ciclo (si uno falla se transmite igual,
// con el valor como "nan" y su nombre en el campo "Err")
bool bmeOk = false;
bool dsOk = false;
bool luxOk = false;
uint8_t bmeDir = 0;   // dirección I²C en la que respondió el BME280 (0x76 o 0x77)
uint8_t luxDir = 0;   // dirección I²C en la que respondió el BH1750 (0x23 o 0x5C)

TwoWire I2CBME = TwoWire(1);
Adafruit_BME280 bme;
BH1750 lightMeter;
OneWire oneWire(ds18b20Pin);
DallasTemperature ds18b20(&oneWire);

void setup() {
  ++bootCount;
  // Vext (Ve, GPIO36) alimenta el OLED y, en el cableado 2, los sensores. heltec_setup() no lo
  // enciende en la V3 (solo en el Wireless Stick): sin esto el OLED se ve tenue y los sensores
  // en Ve quedarían sin corriente. heltec_deep_sleep() lo apaga al dormir.
  heltec_ve(true);
  delay(50);  // que el OLED y los sensores se estabilicen
  heltec_setup();

  // Encendido (no despertar del sueño): firma de LED, versión y revisión de sensores
  const bool arranque = esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER;
  // El driver I²C llena el Serial de errores cuando un sensor no responde: eso ya lo dice la
  // revisión de sensores y el campo Err del paquete.
  esp_log_level_set("i2c.master", ESP_LOG_NONE);
  if (arranque) {
    firmaLed();
    display.cls();
    both.printf("AgroMonitor nodo %d\n", NODE_ID);
    both.printf("fw %s\n", FIRMWARE_VERSION);
    both.printf("modo %s\n", MODO_AHORRO ? "AHORRO 30 min" : "PRUEBA 60 s");
    both.printf("cableado %d\n", CABLEADO);
    delay(SEGUNDOS_INICIO * 1000);
  }
#if !MOSTRAR_OLED
  if (!arranque) heltec_display_power(false);
#endif
  if (!arranque) {
    both.printf("Nodo %d · ciclo #%lu\n", NODE_ID, (unsigned long)bootCount);
  }

  // 1. I2C
  pinMode(sdaPin, INPUT_PULLUP);
  pinMode(sclPin, INPUT_PULLUP);
  I2CBME.begin(sdaPin, sclPin, 100000);

  // 2. BME280 en modo forzado: mide solo cuando se le pide y vuelve a dormir.
  // Si falla no se detiene el nodo: se transmite el resto de los datos.
  // (algunos módulos vienen en 0x77 en vez de 0x76: se prueban las dos)
  if (bme.begin(0x76, &I2CBME)) bmeDir = 0x76;
  else if (bme.begin(0x77, &I2CBME)) bmeDir = 0x77;
  bmeOk = bmeDir != 0;
  if (bmeOk) {
    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::FILTER_OFF);
  }

  // 3. BH1750 en una sola medición: se apaga solo después de medir.
  // (0x23 con ADDR a GND o al aire; 0x5C con ADDR a VCC)
  if (lightMeter.begin(BH1750::ONE_TIME_HIGH_RES_MODE, 0x23, &I2CBME)) luxDir = 0x23;
  else if (lightMeter.begin(BH1750::ONE_TIME_HIGH_RES_MODE, 0x5C, &I2CBME)) luxDir = 0x5C;
  luxOk = luxDir != 0;

  // 4. DS18B20
  pinMode(ds18b20Pin, INPUT_PULLUP);
  ds18b20.begin();
  dsOk = ds18b20.getDS18Count() > 0;

  // 5. Humedad de suelo y lluvia
  pinMode(sensorPin, INPUT);
  pinMode(rainDigitalPin, INPUT_PULLUP);

  if (arranque) revisarSensores();

  // Radio LoRa: estos parámetros deben coincidir con el receptor (lo revisa tests/decisiones.test.mjs)
  RADIOLIB_OR_HALT(radio.begin());
  RADIOLIB_OR_HALT(radio.setFrequency(905.2));
  RADIOLIB_OR_HALT(radio.setOutputPower(22));
  RADIOLIB_OR_HALT(radio.setSpreadingFactor(12));
  RADIOLIB_OR_HALT(radio.setBandwidth(250.0));
  RADIOLIB_OR_HALT(radio.setCodingRate(8));
  RADIOLIB_OR_HALT(radio.setCurrentLimit(140));
  RADIOLIB_OR_HALT(radio.setPreambleLength(12));
  RADIOLIB_OR_HALT(radio.setCRC(true));

  // Se transmite de inmediato; el aleatorio anticolisión va en el tiempo de sueño
  transmitSensorData();
  dormir();
}

// Firma de arranque en el LED blanco: 3 cortos + 1 largo, dos veces (· · · —  · · · —).
// Si la ves al conectar la placa, tiene este firmware de nodo. (El receptor hace 2 largos.)
void firmaLed() {
  pinMode(LED_BLANCO, OUTPUT);
  for (int r = 0; r < 2; r++) {
    for (int i = 0; i < 3; i++) { digitalWrite(LED_BLANCO, HIGH); delay(120); digitalWrite(LED_BLANCO, LOW); delay(150); }
    digitalWrite(LED_BLANCO, HIGH); delay(600); digitalWrite(LED_BLANCO, LOW); delay(500);
  }
}

// Qué sensores responden y en qué pin: en el OLED (dos páginas de máximo 6 renglones de 21
// caracteres) y por Serial, al encender la placa.
// Los analógicos (HW-390, YL-83) siempre "responden": se muestra el valor crudo del ADC;
// ~0 o ~4095 suele ser un pin al aire o sin alimentación (el YL-83 seco también da ~4095).
void revisarSensores() {
  // Otros dispositivos en el bus I²C (por si un sensor tiene otra dirección)
  String otros = "";
  for (uint8_t a = 0x08; a < 0x78; a++) {
    if (a == bmeDir || a == luxDir) continue;
    I2CBME.beginTransmission(a);
    if (I2CBME.endTransmission() == 0) otros += String(a, HEX) + " ";
  }

  display.cls();
  both.println("Sensores 1/2");
  both.printf("I2C SDA%d SCL%d\n", sdaPin, sclPin);
  if (bmeOk) both.printf("BME280  OK 0x%02X\n", bmeDir); else both.println("BME280  --");
  if (luxOk) both.printf("BH1750  OK 0x%02X\n", luxDir); else both.println("BH1750  --");
  if (dsOk) both.printf("DS18B20 OK GPIO%d\n", ds18b20Pin); else both.printf("DS18B20 -- GPIO%d\n", ds18b20Pin);
  if (otros.length()) both.println("otros I2C: " + otros);
  delay(SEGUNDOS_PAGINA * 1000);

  display.cls();
  both.println("Sensores 2/2");
  both.printf("Suelo ADC %d G%d\n", analogRead(sensorPin), sensorPin);
  both.printf("Lluvia ADC %d G%d\n", analogRead(rainAnalogPin), rainAnalogPin);
  both.printf("Lluvia dig %d G%d\n", digitalRead(rainDigitalPin), rainDigitalPin);
#if LEER_BATERIA
  both.printf("Bateria %.2f V\n", heltec_vbat());
#endif
  delay(SEGUNDOS_PAGINA * 1000);
  display.cls();
}

void loop() {
  // Nunca se ejecuta: cada ciclo termina en deep sleep
}

/** Número con decimales fijos o "nan" (el backend descarta "nan" y guarda el resto). */
String num(float v, int decimales) {
  return isnan(v) ? String("nan") : String(v, decimales);
}

void transmitSensorData() {
  int rawValue = analogRead(sensorPin);
  int soilMoisture = constrain(map(rawValue, WET, DRY, 100, 0), 0, 100);

  // DS18B20: -127 °C (DEVICE_DISCONNECTED_C) significa que no respondió
  float tempDS = NAN;
  if (dsOk) {
    ds18b20.requestTemperatures();
    tempDS = ds18b20.getTempCByIndex(0);
    if (tempDS == DEVICE_DISCONNECTED_C) tempDS = NAN;
  }

  float tempBME = NAN, pressure = NAN, humidity = NAN;
  if (bmeOk && bme.takeForcedMeasurement()) {
    tempBME = bme.readTemperature();
    pressure = bme.readPressure() / 100.0F;
    humidity = bme.readHumidity();
  }

  float lux = NAN;
  if (luxOk) {
    uint32_t t0 = millis();
    while (!lightMeter.measurementReady(true) && millis() - t0 < 400) delay(10);
    lux = lightMeter.readLightLevel();  // negativo = error del BH1750
    if (lux < 0) lux = NAN;
  }

  int rainAnalog = analogRead(rainAnalogPin);
  // D0 del YL-83 es activa en BAJO: su comparador (LM393) la pone en LOW cuando la placa se
  // moja (el LED del módulo se enciende). Desconectada, el pull-up la deja en HIGH = no llueve.
  bool rainDigital = digitalRead(rainDigitalPin) == LOW;
  int rainPercent = constrain(map(rainAnalog, 4095, 0, 0, 100), 0, 100);

  // Sensores que fallaron en este ciclo (el backend ignora claves que no conoce)
  String errores = "";
  if (isnan(tempDS))  errores += "DS18B20,";
  if (isnan(tempBME)) errores += "BME280,";
  if (isnan(lux))     errores += "BH1750,";
  if (errores.length() > 0) errores.remove(errores.length() - 1);

  // Paquete compacto, en una línea, en el formato de parseSensorData() de msa-back:
  //   "HS:45|T1:25.3|T2:26.1|P:1008|HA:80.2|Lux:18400|Rain:0|RainBool:0|Nodo:3|Seq:152"
  // Sin unidades ni espacios (~76 bytes en vez de ~100: ~0.5 s menos al aire con SF12).
  // RainBool: 1 = llueve, 0 = no. Seq: contador de ciclos para detectar paquetes perdidos.
  String message = "HS:" + String(soilMoisture) +
                   "|T1:" + num(tempDS, 1) +
                   "|T2:" + num(tempBME, 1) +
                   "|P:" + num(pressure, 0) +
                   "|HA:" + num(humidity, 1) +
                   "|Lux:" + num(lux, 0) +
                   "|Rain:" + String(rainPercent) +
                   "|RainBool:" + (rainDigital ? "1" : "0") +
                   "|Nodo:" + String(NODE_ID) +
                   "|Seq:" + String(bootCount);
#if LEER_BATERIA
  message += "|Bat:" + num(heltec_vbat(), 2);  // voltaje de la batería (V)
#endif
  if (errores.length() > 0) message += "|Err:" + errores;

#if MOSTRAR_OLED
  both.printf("\nNodo %d · #%lu\n", NODE_ID, (unsigned long)bootCount);
  both.println("Suelo " + String(soilMoisture) + "%  T1 " + num(tempDS, 1) + "C");
  both.println("Aire " + num(tempBME, 1) + "C  " + num(humidity, 0) + "%");
  both.println("Lluvia " + String(rainPercent) + "%  " + (rainDigital ? "SI" : "NO"));
  if (errores.length() > 0) both.println("Err: " + errores);
#endif
  Serial.println(message);

  RADIOLIB(radio.transmit(message.c_str()));
  if (_radiolib_status == RADIOLIB_ERR_NONE) {
    both.println("Transmisión exitosa");
  } else {
    both.println("Error TX: " + String(_radiolib_status));
  }
}

void dormir() {
  // Aleatorio anticolisión: se suma al sueño en vez de esperarse despierto
  uint32_t segundos = INTERVALO_S + esp_random() % (JITTER_MAX_S + 1);
  both.printf("Durmiendo %lu s\n", (unsigned long)segundos);

#if USAR_DUMMY_LOAD
  // Mantiene la carga encendida DURANTE el deep sleep: sin gpio_hold el pin se suelta
  // al dormir y el shield (modo NORMAL) se apaga.
  pinMode(DUMMY_LOAD_PIN, OUTPUT);
  digitalWrite(DUMMY_LOAD_PIN, HIGH);
  gpio_hold_en((gpio_num_t)DUMMY_LOAD_PIN);
  gpio_deep_sleep_hold_en();
#endif

  I2CBME.end();
  // Apaga OLED, Vext y radio correctamente y entra en deep sleep
  heltec_deep_sleep(segundos);
}
