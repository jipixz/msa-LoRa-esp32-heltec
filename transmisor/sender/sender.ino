// Código emisor para Heltec WiFi_LoRa_32_V3 con múltiples sensores
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

// Variables para el sensor de humedad de suelo
const int DRY = 3600;
const int WET = 1100; 
unsigned long lastSendTime = 0;
const unsigned long interval = 1000; // Intervalo de envío (1 segundo)

// Instanciación de buses I2C
TwoWire I2CBME = TwoWire(1); // Usar bus I2C 1
Adafruit_BME280 bme;
BH1750 lightMeter;

// Configuración 1-Wire
OneWire oneWire(ds18b20Pin);
DallasTemperature ds18b20(&oneWire);

void setup() {
  heltec_setup();

  // 1. Inicializar I2C con pull-ups
  pinMode(sdaPin, INPUT_PULLUP);
  pinMode(sclPin, INPUT_PULLUP);
  I2CBME.begin(sdaPin, sclPin, 100000); // 100kHz

  // 2. Inicializar BME280
  if (!bme.begin(0x76, &I2CBME)) {
    both.println("Error BME280! Verificar:");
    both.println("1. Dirección 0x76/0x77");
    both.println("2. Cableado SDA/SCL");
    while(1);
  }

  // 3. Inicializar BH1750 (Corrección clave)
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &I2CBME);

  // 4. Configurar DS18B20
  pinMode(ds18b20Pin, INPUT_PULLUP);
  ds18b20.begin();
  if(ds18b20.getDS18Count() == 0) {
    both.println("Error DS18B20! Verificar:");
    both.println("1. Resistencia pull-up 4.7kΩ");
    both.println("2. Cableado GPIO1");
    while(1);
  }
  
  // 5. Configurar pines del sensor de lluvia
  pinMode(sensorPin, INPUT);
  pinMode(rainDigitalPin, INPUT_PULLUP);
  
  both.print("ID Sensor: 0x");
  both.println(bme.sensorID(), HEX);
  
  // Configuración de la radio LoRa
  RADIOLIB_OR_HALT(radio.begin());
  RADIOLIB_OR_HALT(radio.setFrequency(905.2)); 
  RADIOLIB_OR_HALT(radio.setOutputPower(22));  
  RADIOLIB_OR_HALT(radio.setSpreadingFactor(12));
  RADIOLIB_OR_HALT(radio.setBandwidth(250.0));
  
  both.println("Sistema inicializado, comenzando lecturas...");
}

void loop() {
  heltec_loop();
  
  if (millis() - lastSendTime > interval) {
    // Leer sensor de humedad del suelo
    int rawValue = analogRead(sensorPin);
    int soilMoisture = map(rawValue, WET, DRY, 100, 0);
    soilMoisture = constrain(soilMoisture, 0, 100);

    // Leer temperatura del DS18B20
    ds18b20.requestTemperatures();
    float tempDS = ds18b20.getTempCByIndex(0);
    
    // Leer BME280
    float tempBME = NAN, pressure = NAN, humidity = NAN;
    if(!isnan(bme.readTemperature())) {
      tempBME = bme.readTemperature();
      pressure = bme.readPressure() / 100.0F;
      humidity = bme.readHumidity();
    }
    
    // Leer sensores nuevos
    int rainAnalog = analogRead(rainAnalogPin);
    bool rainDigital = digitalRead(rainDigitalPin);
    float lux = lightMeter.readLightLevel();
    int rainPercent = map(rainAnalog, 4095, 0, 0, 100);
    rainPercent = constrain(rainPercent, 0, 100);

    // Crear mensaje actualizado
    String message = "HS:" + String(soilMoisture) + 
                    "% | T1:" + String(tempDS,1) + 
                    "C | T2:" + String(tempBME,1) + 
                    "C | P:" + String(pressure,0) + 
                    "hPa | HA:" + String(humidity,1) + 
                    "% | Lux:" + String(lux,0) + 
                    "lx | Rain:" + String(rainPercent) + 
                    "% | Alert: " + (rainDigital ? "SI" : "NO");

    String messageOLED = "HS:" + String(soilMoisture) + 
                    "% | T1:" + String(tempDS,1) + 
                    "C | \nT2:" + String(tempBME,1) + 
                    "C | P:" + String(pressure,0) + 
                    "hPa | \nHA:" + String(humidity,1) + 
                    "% | Lux:" + String(lux,0) + 
                    "lx | \nRain:" + String(rainPercent) + 
                    "% | Alert: " + (rainDigital ? "SI" : "NO");

    // Mostrar datos en pantalla OLED
    both.print("\nDATA 	(͡ ° ͜ʖ ͡ °)\n" + String(messageOLED));
    
    // Transmitir por LoRa
    RADIOLIB(radio.transmit(message.c_str()));
    
    if (_radiolib_status == RADIOLIB_ERR_NONE) {
      //both.println("Error TX: " + String(_radiolib_status));
      reiniciarI2C();
    }
    
    lastSendTime = millis();
  }
}

void reiniciarI2C() {
  I2CBME.end();
  delay(100);
  I2CBME.begin(sdaPin, sclPin, 100000);
  bme.begin(0x76, &I2CBME);
  lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &I2CBME);
}
