//ccalibraate
#include <heltec_unofficial.h>

const int sensorPin = 34;  // GPIO34 - Entrada analógica del sensor HW-390
unsigned long lastSendTime = 0;
const unsigned long interval = 5000; // Intervalo de 5 segundos
int counter = 0;

void setup() {
  heltec_setup();
  analogSetAttenuation(ADC_11db); // Configura la atenuación para medir hasta ~3.3V
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
    // Calibración: ajusta estos valores según tus mediciones
    int dryValue = 3000;  // Valor en suelo seco
    int wetValue = 1000;  // Valor en suelo húmedo
    int percentage = map(rawValue, dryValue, wetValue, 0, 100);
    percentage = constrain(percentage, 0, 100); // Asegura que el valor esté entre 0 y 100

    String message = "Humedad: " + String(percentage) + "% (Valor crudo: " + String(rawValue) + ")";
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
