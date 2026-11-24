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
#include "config.h"

// Configuración de pines
const int sensorPin = 7;      // GPIO7 - Entrada analógica del sensor HW-390
const int ds18b20Pin = 1;     // GPIO1 - DS18B20 (ojo: en la V3 también es VBAT_ADC; ver README)
const int sdaPin = 41;        // GPIO41 - SDA para BME280 y BH1750
const int sclPin = 42;        // GPIO42 - SCL para BME280 y BH1750
const int rainAnalogPin = 2;  // GPIO2 - Entrada analógica YL-83
const int rainDigitalPin = 3; // GPIO3 - Entrada digital YL-83

// Contador de ciclos: sobrevive al deep sleep (RTC). Viaja como "Seq" para detectar
// paquetes perdidos (si el backend ve un salto en Seq, se perdió al menos un paquete).
RTC_DATA_ATTR uint32_t bootCount = 0;

// Estado de los sensores en este ciclo (si uno falla se transmite igual,
// con el valor como "nan" y su nombre en el campo "Err")
bool bmeOk = false;
bool dsOk = false;
bool luxOk = false;

TwoWire I2CBME = TwoWire(1);
Adafruit_BME280 bme;
BH1750 lightMeter;
OneWire oneWire(ds18b20Pin);
DallasTemperature ds18b20(&oneWire);

void setup() {
  ++bootCount;
  heltec_setup();
#if !MOSTRAR_OLED
  heltec_display_power(false);
#endif

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    both.printf("Nodo %d · ciclo #%lu\n", NODE_ID, (unsigned long)bootCount);
  }

  // 1. I2C
  pinMode(sdaPin, INPUT_PULLUP);
  pinMode(sclPin, INPUT_PULLUP);
  I2CBME.begin(sdaPin, sclPin, 100000);

  // 2. BME280 en modo forzado: mide solo cuando se le pide y vuelve a dormir.
  // Si falla no se detiene el nodo: se transmite el resto de los datos.
  bmeOk = bme.begin(0x76, &I2CBME);
  if (!bmeOk) {
    both.println("Error BME280! Verificar:");
    both.println("1. Dirección 0x76/0x77");
    both.println("2. Cableado SDA/SCL");
  } else {
    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::FILTER_OFF);
  }

  // 3. BH1750 en una sola medición: se apaga solo después de medir.
  luxOk = lightMeter.begin(BH1750::ONE_TIME_HIGH_RES_MODE, 0x23, &I2CBME);

  // 4. DS18B20
  pinMode(ds18b20Pin, INPUT_PULLUP);
  ds18b20.begin();
  dsOk = ds18b20.getDS18Count() > 0;
  if (!dsOk) {
    both.println("Error DS18B20! Verificar:");
    both.println("1. Resistencia pull-up 4.7kΩ");
    both.println("2. Cableado GPIO1");
  }

  // 5. Humedad de suelo y lluvia
  pinMode(sensorPin, INPUT);
  pinMode(rainDigitalPin, INPUT_PULLUP);

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
  bool rainDigital = digitalRead(rainDigitalPin);
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
