// Calibración del sensor de humedad de suelo HW-390 (capacitivo)
// Muestra en el OLED y por Serial el valor crudo del ADC (promedio de varias lecturas)
// y el porcentaje calculado con DRY/WET. Para calibrar:
//   1. Sensor al aire o en suelo completamente seco -> anota el valor crudo como DRY.
//   2. Sensor en suelo saturado de agua             -> anota el valor crudo como WET.
//   3. Copia esos valores a DRY y WET de transmisor/sender/sender.ino (y de aquí).
#include <heltec_unofficial.h>

const int sensorPin = 7;  // GPIO7 (ADC1) - el mismo pin que usa el emisor

// Valores de calibración (deben ser los mismos que en sender.ino)
const int DRY = 3600;  // Valor en suelo completamente seco
const int WET = 1100;  // Valor en suelo saturado de agua

const unsigned long interval = 2000;  // Lectura cada 2 segundos
const int MUESTRAS = 16;              // Lecturas promediadas para reducir ruido del ADC
unsigned long lastReadTime = 0;

void setup() {
  heltec_setup();
  analogSetAttenuation(ADC_11db);  // Rango de ~0 a 3.1 V
  pinMode(sensorPin, INPUT);
  both.println("Calibracion HW-390 (GPIO7)");
}

void loop() {
  heltec_loop();
  if (millis() - lastReadTime < interval) return;
  lastReadTime = millis();

  long suma = 0;
  for (int i = 0; i < MUESTRAS; i++) {
    suma += analogRead(sensorPin);
    delay(5);
  }
  int rawValue = suma / MUESTRAS;

  int percentage = map(rawValue, WET, DRY, 100, 0);
  percentage = constrain(percentage, 0, 100);

  both.printf("Crudo: %d  Humedad: %d%%\n", rawValue, percentage);
}
