// Código receptor optimizado para Heltec WiFi_LoRa_32_V3
#include <heltec_unofficial.h>

volatile bool rxFlag = false;
String receivedData;

// Estructura para almacenar datos parseados
struct SensorData {
  int humedad;
  float tempDS;
  float tempBME;
  float presion;
  float humAire;
};

void rxCallback() {
  rxFlag = true;
}

void setup() {
  heltec_setup();
  Serial.begin(115200);  // Inicializar Serial para el backend
  
  // Configuración de la radio LoRa
  RADIOLIB_OR_HALT(radio.begin());
  RADIOLIB_OR_HALT(radio.setFrequency(905.2));
  RADIOLIB_OR_HALT(radio.setSpreadingFactor(12));
  RADIOLIB_OR_HALT(radio.setBandwidth(250.0));
  
  radio.setDio1Action(rxCallback);
  RADIOLIB_OR_HALT(radio.startReceive());
  
  both.println("Receptor listo. Esperando datos...");
}

void loop() {
  heltec_loop();
  
  if (rxFlag) {
    rxFlag = false;
    String payload;
    RADIOLIB(radio.readData(payload));
    
    if (_radiolib_status == RADIOLIB_ERR_NONE) {
      procesarDatos(payload);
      mostrarMetricasRadio();
    } else {
      both.print("Error RX: ");
      both.println(_radiolib_status);
    }
    
    RADIOLIB_OR_HALT(radio.startReceive());
  }
}

void procesarDatos(String datos) {
  SensorData medidas = parsearDatos(datos);
  
  // Mostrar en OLED
  both.println("Datos recibidos:");
  both.println(datos);
  
  // Enviar al backend via Serial
  /* Serial.print("Humedad: ");
  Serial.print(medidas.humedad);
  Serial.print("% | TempDS: ");
  Serial.print(medidas.tempDS, 1);
  Serial.print("°C | TempBME: ");
  Serial.print(medidas.tempBME, 1);
  Serial.print("°C | Presion: ");
  Serial.print(medidas.presion, 1);
  Serial.print("hPa | HumAire: ");
  Serial.print(medidas.humAire, 1);
  Serial.println("%"); */
}

SensorData parsearDatos(String datos) {
  SensorData result = {0, NAN, NAN, NAN, NAN};
  
  // Extraer humedad del suelo
  int humStart = datos.indexOf("Humedad: ") + 9;
  int humEnd = datos.indexOf("%", humStart);
  if(humStart >= 0 && humEnd >= 0) {
    result.humedad = datos.substring(humStart, humEnd).toInt();
  }
  
  // Extraer temperatura DS18B20
  int tempDSStart = datos.indexOf("TempDS: ") + 8;
  int tempDSEnd = datos.indexOf("°C", tempDSStart);
  if(tempDSStart >= 0 && tempDSEnd >= 0) {
    result.tempDS = datos.substring(tempDSStart, tempDSEnd).toFloat();
  }
  
  // Extraer temperatura BME280
  int tempBMEStart = datos.indexOf("TempBME: ") + 9;
  int tempBMEEnd = datos.indexOf("°C", tempBMEStart);
  if(tempBMEStart >= 0 && tempBMEEnd >= 0) {
    result.tempBME = datos.substring(tempBMEStart, tempBMEEnd).toFloat();
  }
  
  // Extraer presión
  int presionStart = datos.indexOf("Presion: ") + 9;
  int presionEnd = datos.indexOf("hPa", presionStart);
  if(presionStart >= 0 && presionEnd >= 0) {
    result.presion = datos.substring(presionStart, presionEnd).toFloat();
  }
  
  // Extraer humedad del aire
  int humAireStart = datos.indexOf("HumAire: ") + 9;
  int humAireEnd = datos.indexOf("%", humAireStart);
  if(humAireStart >= 0 && humAireEnd >= 0) {
    result.humAire = datos.substring(humAireStart, humAireEnd).toFloat();
  }
  
  return result;
}

void mostrarMetricasRadio() {
  both.print("RSSI: ");
  both.print(radio.getRSSI());
  both.println(" dBm");
  both.print("SNR: ");
  both.print(radio.getSNR());
  both.println(" dB");
  
  // Opcional: Enviar métricas al backend
  Serial.print("RSSI:");
  Serial.print(radio.getRSSI());
  Serial.print(",SNR:");
  Serial.println(radio.getSNR());
}
