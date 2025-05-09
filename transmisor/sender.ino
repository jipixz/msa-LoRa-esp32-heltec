// sender

#include <heltec_unofficial.h>

const int sensorPin = 7;  // GPIO48 - Entrada analógica del sensor HW-390
unsigned long lastSendTime = 0;
const unsigned long interval = 1000; // Intervalo de 5 segundos
int counter = 0;

const int DRY = 3600;
const int WET = 1100; 

void setup() {
  heltec_setup();
  pinMode(sensorPin,INPUT);
  //analogSetAttenuation(ADC_11db); // Configura la atenuación para medir hasta ~3.3V
  RADIOLIB_OR_HALT(radio.begin());
  RADIOLIB_OR_HALT(radio.setFrequency(905.2)); // Ajusta según tu región
  RADIOLIB_OR_HALT(radio.setOutputPower(10));  // Potencia de transmisión en dBm
  RADIOLIB_OR_HALT(radio.setSpreadingFactor(12));
  RADIOLIB_OR_HALT(radio.setBandwidth(250.0));
}

void loop() {
  heltec_loop();
  if (millis() - lastSendTime > interval) {
    int rawValue = analogRead(sensorPin);
    int percentage = map(rawValue, WET, DRY, 100, 0); // Ajusta según calibración

    String message = "Humedad: " + String(percentage) + "% (Valor crudo: " /* + String(rawValue) */ + ")";
    both.println("Enviando: " + message);
    RADIOLIB(radio.transmit(message.c_str()));
    if (_radiolib_status == RADIOLIB_ERR_NONE) {
      both.println("Transmisión exitosa");
    } else {
      both.print("Error en la transmisión: ");
      both.println(_radiolib_status);
    }
    lastSendTime = millis();
  }
}
