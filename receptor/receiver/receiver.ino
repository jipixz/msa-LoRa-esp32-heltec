// Código receptor (gateway) para Heltec WiFi_LoRa_32_V3
// Recibe los paquetes LoRa de los nodos y reenvía cada uno, en una sola línea,
// por Serial al backend (msa-back, parseSensorData). Muestra un resumen en el OLED.
#include <heltec_unofficial.h>

// Datos parseados de un paquete. Mismo formato y claves que parseSensorData() de msa-back:
//   "HS:45% | T1:25.3C | T2:26.1C | P:1008hPa | HA:80.2% | Lux:1234lx | Rain:0% | RainBool:NO | Nodo:0"
// Un valor que no se pudo leer (o que viene como "nan") queda en NAN / -1.
struct SensorData {
  float humedadSuelo = NAN;  // HS
  float tempDS = NAN;        // T1
  float tempBME = NAN;       // T2
  float presion = NAN;       // P
  float humAire = NAN;       // HA
  float lux = NAN;           // Lux
  float lluvia = NAN;        // Rain
  int rainDigital = -1;      // RainBool: 1 = SI, 0 = NO, -1 = no vino
  int nodo = -1;             // Nodo
};
// (El struct va antes de cualquier función: el IDE de Arduino inserta los prototipos
//  automáticos justo antes de la primera función, y deben ver este tipo.)

volatile bool rxFlag = false;

void rxCallback() {
  rxFlag = true;
}

// Pines de LEDs
const int LED_BLANCO = 35;   // LED blanco de la placa: brillo según humedad del suelo
const int LED_NARANJA = 1;   // LED externo: alerta de temperatura y parpadeo al recibir

// PWM del LED blanco
const int pwmFreq = 5000;
const int pwmResolution = 8;

// Alerta de temperatura del suelo (DS18B20)
const float TEMP_ALERTA_C = 30.0;

// Parpadeo del LED naranja sin bloquear el loop (la radio sigue recibiendo)
const int PARPADEOS = 3;
const unsigned long PARPADEO_MS = 100;
int cambiosPendientes = 0;          // cada parpadeo son 2 cambios (encender y apagar)
unsigned long ultimoCambio = 0;
bool alertaTemperatura = false;

#define FIRMWARE_VERSION "2026.11.25"

// Firma de arranque en el LED blanco: 2 largos, dos veces (— —  — —). El nodo hace · · · —.
void firmaLed() {
  pinMode(LED_BLANCO, OUTPUT);
  for (int r = 0; r < 2; r++) {
    for (int i = 0; i < 2; i++) { digitalWrite(LED_BLANCO, HIGH); delay(600); digitalWrite(LED_BLANCO, LOW); delay(250); }
    delay(500);
  }
}

void setup() {
  // En la V3 el OLED se alimenta de Vext (GPIO36) y heltec_setup() solo lo enciende en el
  // Wireless Stick: sin esto la pantalla vive de la corriente que se cuela por sus pines y se
  // ve tenue. Se enciende primero y se espera a que se estabilice antes de inicializarla.
  heltec_ve(true);
  delay(50);
  heltec_setup();
  firmaLed();

  // LEDs
  pinMode(LED_NARANJA, OUTPUT);
  digitalWrite(LED_NARANJA, LOW);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(LED_BLANCO, pwmFreq, pwmResolution);
#else
  ledcSetup(0, pwmFreq, pwmResolution);
  ledcAttachPin(LED_BLANCO, 0);
#endif
  escribirLedBlanco(0);

  // Configuración de la radio LoRa (debe coincidir con el emisor)
  RADIOLIB_OR_HALT(radio.begin());
  RADIOLIB_OR_HALT(radio.setFrequency(905.2));
  RADIOLIB_OR_HALT(radio.setSpreadingFactor(12));
  RADIOLIB_OR_HALT(radio.setBandwidth(250.0));
  RADIOLIB_OR_HALT(radio.setCodingRate(8));        // Máxima redundancia - IGUAL AL TX
  RADIOLIB_OR_HALT(radio.setCurrentLimit(140));    // Para recepción óptima
  RADIOLIB_OR_HALT(radio.setPreambleLength(12));   // DEBE COINCIDIR con TX
  RADIOLIB_OR_HALT(radio.setCRC(true));            // DEBE COINCIDIR con TX

  radio.setDio1Action(rxCallback);
  RADIOLIB_OR_HALT(radio.startReceive());

  // Líneas cortas: el OLED muestra ~21 caracteres por renglón
  both.printf("AgroMonitor gateway\nfw %s\n", FIRMWARE_VERSION);
  both.println("Receptor listo");
  both.println("Esperando datos...");
}

void loop() {
  heltec_loop();

  if (rxFlag) {
    rxFlag = false;
    String payload;
    RADIOLIB(radio.readData(payload));

    if (_radiolib_status == RADIOLIB_ERR_NONE) {
      procesarDatos(payload);
    } else {
      both.print("Error RX: ");
      both.println(_radiolib_status);
    }

    RADIOLIB_OR_HALT(radio.startReceive());
  }

  actualizarParpadeo();
}

void procesarDatos(String datos) {
  // Paquetes de firmware viejo traían "\nNodo: 0" en otra línea: se unen en una sola
  datos.replace("\r", "");
  datos.replace("\n", " | ");
  datos.trim();

  // 1. Al backend: el paquete completo, una línea por paquete
  Serial.println(datos);

  // 2. Métricas de radio (el backend ignora esta línea)
  float rssi = radio.getRSSI();
  float snr = radio.getSNR();
  Serial.print("RSSI:");
  Serial.print(rssi);
  Serial.print(",SNR:");
  Serial.println(snr);

  // 3. Resumen en el OLED
  SensorData m = parsearDatos(datos);
  display.cls();
  display.printf("Nodo %d  RSSI %.0f\n", m.nodo, rssi);
  display.printf("Suelo %.0f%%  %.1fC\n", m.humedadSuelo, m.tempDS);
  display.printf("Aire %.1fC %.0f%%\n", m.tempBME, m.humAire);
  display.printf("%.0fhPa %.0flx\n", m.presion, m.lux);
  display.printf("Lluvia %.0f%% %s\n", m.lluvia, m.rainDigital == 1 ? "SI" : (m.rainDigital == 0 ? "NO" : "?"));

  controlarLEDs(m);
}

// Quita la unidad final ("%", "C", "hPa", "lx") y convierte a número; "nan" o vacío -> NAN
float aNumero(String valor) {
  valor.trim();
  valor.toLowerCase();
  if (valor.length() == 0 || valor.startsWith("nan")) return NAN;
  char c = valor.charAt(0);
  if (!(isDigit(c) || c == '-' || c == '.')) return NAN;
  return valor.toFloat();  // toFloat() se detiene en la unidad
}

SensorData parsearDatos(const String &datos) {
  SensorData r;
  int inicio = 0;

  while (inicio <= (int)datos.length()) {
    int fin = datos.indexOf('|', inicio);
    if (fin < 0) fin = datos.length();
    String parte = datos.substring(inicio, fin);
    inicio = fin + 1;

    int dosPuntos = parte.indexOf(':');
    if (dosPuntos < 0) continue;
    String clave = parte.substring(0, dosPuntos);
    String valor = parte.substring(dosPuntos + 1);
    clave.trim();
    valor.trim();

    if (clave == "HS")            r.humedadSuelo = aNumero(valor);
    else if (clave == "T1")       r.tempDS = aNumero(valor);
    else if (clave == "T2")       r.tempBME = aNumero(valor);
    else if (clave == "P")        r.presion = aNumero(valor);
    else if (clave == "HA")       r.humAire = aNumero(valor);
    else if (clave == "Lux")      r.lux = aNumero(valor);
    else if (clave == "Rain")     r.lluvia = aNumero(valor);
    else if (clave == "RainBool") {
      valor.toUpperCase();
      r.rainDigital = (valor.indexOf("SI") >= 0 || valor == "1" || valor == "TRUE") ? 1 : 0;
    }
    else if (clave == "Nodo") {
      float n = aNumero(valor);
      r.nodo = isnan(n) ? -1 : (int)n;
    }
  }

  return r;
}

void escribirLedBlanco(int brillo) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(LED_BLANCO, brillo);
#else
  ledcWrite(0, brillo);
#endif
}

void controlarLEDs(const SensorData &m) {
  // LED blanco: brillo proporcional a la humedad del suelo (apagado si no vino)
  int brillo = isnan(m.humedadSuelo) ? 0 : map((int)m.humedadSuelo, 0, 100, 0, 255);
  escribirLedBlanco(constrain(brillo, 0, 255));

  // LED naranja: encendido fijo si la temperatura del suelo pasa el umbral
  alertaTemperatura = !isnan(m.tempDS) && m.tempDS > TEMP_ALERTA_C;

  // Parpadeo para indicar recepción; al terminar queda en el estado de la alerta
  cambiosPendientes = PARPADEOS * 2;
  ultimoCambio = millis();
  digitalWrite(LED_NARANJA, HIGH);
}

void actualizarParpadeo() {
  if (cambiosPendientes == 0) return;
  if (millis() - ultimoCambio < PARPADEO_MS) return;

  ultimoCambio = millis();
  cambiosPendientes--;
  if (cambiosPendientes == 0) {
    digitalWrite(LED_NARANJA, alertaTemperatura ? HIGH : LOW);
  } else {
    digitalWrite(LED_NARANJA, !digitalRead(LED_NARANJA));
  }
}
