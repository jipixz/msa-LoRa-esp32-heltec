// receiber
#include <heltec_unofficial.h>

volatile bool rxFlag = false;
String receivedData;

void rxCallback() {
  rxFlag = true;
}

void setup() {
  heltec_setup();
  both.println("Iniciando...");
  RADIOLIB_OR_HALT(radio.begin());
  both.println("Configurando frecuencia...");
  RADIOLIB_OR_HALT(radio.setFrequency(905.2));
  both.println("Frecuencia configurada.");
  RADIOLIB_OR_HALT(radio.setSpreadingFactor(12));
  RADIOLIB_OR_HALT(radio.setBandwidth(250.0));
  radio.setDio1Action(rxCallback);
  RADIOLIB_OR_HALT(radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF));
}

void loop() {
  heltec_loop();
  if (rxFlag) {
    rxFlag = false;
    radio.readData(receivedData);
    if (_radiolib_status == RADIOLIB_ERR_NONE) {
      both.print("Mensaje recibido: ");
      both.println(receivedData);
      both.print("RSSI: ");
      both.print(radio.getRSSI());
      both.println(" dBm");
      both.print("SNR: ");
      both.print(radio.getSNR());
      both.println(" dB");
    } else {
      both.print("Error al recibir: ");
      both.println(_radiolib_status);
    }
    RADIOLIB_OR_HALT(radio.startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF));
  }
}
