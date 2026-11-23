// Código emisor para Heltec WiFi_LoRa_32_V3 con múltiples sensores
// Versión optimizada para máxima autonomía + configuración largo alcance
#include <heltec_unofficial.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <BH1750.h>

// Configuración de pines
const int sensorPin = 7;      // GPIO48 - Entrada analógica del sensor HW-390
const int ds18b20Pin = 1;     // GPIO1 - Para el sensor DS18B20
const int sdaPin = 41;        // GPIO41 - SDA para BME280
const int sclPin = 42;        // GPIO42 - SCL para BME280
const int rainAnalogPin = 2;  // GPIO2 - Entrada analógica YL-83
const int rainDigitalPin = 3; // GPIO3 - Entrada digital YL-83
const int dummyLoadPin = 48;  // GPIO48 - Pin para dummy load resistencia  // ← NUEVO

// Variables para el sensor de humedad de suelo
const int DRY = 3600;
const int WET = 1100; 

// Deep Sleep Configuration 
#define uS_TO_S_FACTOR 1000000ULL
#define TIME_TO_SLEEP 1800
#define MAX_RANDOM_DELAY 60

// Variables RTC
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR unsigned long lastSendTime = 0;

// Instanciación de buses I2C
TwoWire I2CBME = TwoWire(1);
Adafruit_BME280 bme;
BH1750 lightMeter;
OneWire oneWire(ds18b20Pin);
DallasTemperature ds18b20(&oneWire);

void setup() {
  ++bootCount;
  heltec_setup();

  // Verificar razón del despertar
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    both.println("Despertar por timer - Boot #" + String(bootCount));
  }

  // 1. Inicializar I2C con pull-ups
  pinMode(sdaPin, INPUT_PULLUP);
  pinMode(sclPin, INPUT_PULLUP);
  I2CBME.begin(sdaPin, sclPin, 100000);
  
  // 2. Inicializar BME280
  if (!bme.begin(0x76, &I2CBME)) {
    both.println("Error BME280! Verificar:");
    both.println("1. Dirección 0x76/0x77");
    both.println("2. Cableado SDA/SCL");
    goToDeepSleep();
  }
  
  // 3. Inicializar BH1750
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &I2CBME);
  
  pinMode(ds18b20Pin, INPUT_PULLUP);
  ds18b20.begin();
  if(ds18b20.getDS18Count() == 0) {
    both.println("Error DS18B20! Verificar:");
    both.println("1. Resistencia pull-up 4.7kΩ");
    both.println("2. Cableado GPIO1");
  }
  
  // 5. Configurar pines del sensor de lluvia
  pinMode(sensorPin, INPUT);
  pinMode(rainDigitalPin, INPUT_PULLUP);
  
  both.print("ID Sensor: 0x");
  both.println(bme.sensorID(), HEX);
  
  // Configuración LoRa para LARGO ALCANCE
  RADIOLIB_OR_HALT(radio.begin());
  RADIOLIB_OR_HALT(radio.setFrequency(905.2)); 
  RADIOLIB_OR_HALT(radio.setOutputPower(22));
  RADIOLIB_OR_HALT(radio.setSpreadingFactor(12));
  RADIOLIB_OR_HALT(radio.setBandwidth(250.0));
  
  // *** CONFIGURACIONES CRÍTICAS PARA 1.5 KM ***
  RADIOLIB_OR_HALT(radio.setCodingRate(8));
  RADIOLIB_OR_HALT(radio.setCurrentLimit(140));
  RADIOLIB_OR_HALT(radio.setPreambleLength(12));
  RADIOLIB_OR_HALT(radio.setCRC(true));
  
  both.println("Sistema inicializado, comenzando lecturas...");
  
  // Retraso anti-colisión
  uint32_t randomDelay = esp_random() % (MAX_RANDOM_DELAY + 1);
  both.print("Retraso anti-colisión: ");
  both.print(randomDelay);
  both.println(" segundos");
  delay(randomDelay * 1000);
  
  // Transmitir datos
  transmitSensorData();
  
  // Activar dummy load antes del deep sleep                    // ← NUEVO
  enableDummyLoad();                                            // ← NUEVO
  
  // Ir a deep sleep
  goToDeepSleep();
}

void loop() {
  // Nunca se ejecuta debido al deep sleep
}

void transmitSensorData() {
  // ... TU CÓDIGO ACTUAL SIN CAMBIOS ...
  
  int rawValue = analogRead(sensorPin);
  int soilMoisture = map(rawValue, WET, DRY, 100, 0);
  soilMoisture = constrain(soilMoisture, 0, 100);

  ds18b20.requestTemperatures();
  float tempDS = ds18b20.getTempCByIndex(0);
  
  float tempBME = NAN, pressure = NAN, humidity = NAN;
  if(!isnan(bme.readTemperature())) {
    tempBME = bme.readTemperature();
    pressure = bme.readPressure() / 100.0F;
    humidity = bme.readHumidity();
  }
  
  int rainAnalog = analogRead(rainAnalogPin);
  bool rainDigital = digitalRead(rainDigitalPin);
  float lux = lightMeter.readLightLevel();
  int rainPercent = map(rainAnalog, 4095, 0, 0, 100);
  rainPercent = constrain(rainPercent, 0, 100);

  String message = "HS:" + String(soilMoisture) + 
                  "% | T1:" + String(tempDS,1) + 
                  "C | T2:" + String(tempBME,1) + 
                  "C | P:" + String(pressure,0) + 
                  "hPa | HA:" + String(humidity,1) + 
                  "% | Lux:" + String(lux,0) + 
                  "lx | Rain:" + String(rainPercent) + 
                  "% | RainBool: " + (rainDigital ? "SI" : "NO") +
                  "\nNodo: 0";

  String messageOLED = "HS:" + String(soilMoisture) + 
                  "% | T1:" + String(tempDS,1) + 
                  "C | \nT2:" + String(tempBME,1) + 
                  "C | P:" + String(pressure,0) + 
                  "hPa | \nHA:" + String(humidity,1) + 
                  "% | Lux:" + String(lux,0) + 
                  "lx | \nRain:" + String(rainPercent) + 
                  "% | RainD: " + (rainDigital ? "SI" : "NO") +
                  "\nNodo: 0";

  both.print("\nDATA  Boot #" + String(bootCount) + "\n" + String(messageOLED));
  
  RADIOLIB(radio.transmit(message.c_str()));
  
  if (_radiolib_status == RADIOLIB_ERR_NONE) {
    both.println("Transmisión exitosa");
  } else {
    both.println("Error TX: " + String(_radiolib_status));
    reiniciarI2C();
  }
  
  lastSendTime = millis();
}

void enableDummyLoad() {                                        // ← NUEVA FUNCIÓN
  // Activar carga dummy para mantener Shield V9 activo
  pinMode(dummyLoadPin, OUTPUT);
  digitalWrite(dummyLoadPin, HIGH);
  both.println("Dummy load activado en GPIO" + String(dummyLoadPin));
}

void goToDeepSleep() {
  both.println("Preparando para deep sleep por 30 minutos...");
  both.flush();
  
  I2CBME.end();
  radio.sleep();
  
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  
  both.println("Entrando en deep sleep...");
  both.flush();
  
  esp_deep_sleep_start();
}

void reiniciarI2C() {
  I2CBME.end();
  delay(100);
  I2CBME.begin(sdaPin, sclPin, 100000);
  bme.begin(0x76, &I2CBME);
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &I2CBME);
}
