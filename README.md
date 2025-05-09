# 🌱 Sistema de Monitoreo de Cacao con LoRa

[![ESP32](https://img.shields.io/badge/ESP32-Heltec-blue)](https://heltec.org/)
[![LoRa](https://img.shields.io/badge/LoRa-Comunicación-green)](https://lora-alliance.org/)
[![Licencia](https://img.shields.io/badge/Licencia-MIT-yellow)](LICENSE)
[![Estatus](https://img.shields.io/badge/Estatus-En%20Desarrollo-red)](https://github.com/username/repo)

## 📋 Descripción

Sistema de trazabilidad para cultivos de cacao utilizando tecnología LoRa y sensores de humedad y temperatura. Este proyecto permite monitorear en tiempo real las condiciones ambientales y del suelo en las plantaciones de cacao para optimizar la producción y garantizar la calidad del cultivo.

## 🛠️ Hardware Utilizado

- Placas ESP32 Heltec HTIT-WB32LAF con capacidades LoRa
- Sensores de humedad del suelo HW-390
- Sensores de temperatura y humedad ambiental

## ✨ Características

- Transmisión de datos mediante tecnología LoRa para largo alcance
- Monitoreo en tiempo real de humedad del suelo
- Monitoreo de temperatura y humedad ambiental
- Bajo consumo energético para operación prolongada
- Calibración de sensores para lecturas precisas

## 📊 Arquitectura del Sistema

El sistema consta de dos componentes principales:

1. **Nodos Transmisores**: Instalados en el campo de cultivo, estos dispositivos recolectan datos de los sensores y los transmiten a través de LoRa.

2. **Nodo Receptor**: Recibe los datos de los transmisores y los procesa para su visualización o análisis posterior.

## 💾 Estructura del Proyecto

```
├── calibration/              # Calibración de sensores
│   └── humiditySensorCalibration.ino
├── transmisor/               # Código para nodos transmisores
│   └── sender.ino
├── receptor/                 # Código para nodo receptor
│   └── receiber.ino
└── README.md                 # Este archivo
```

## 🚀 Instalación

1. Clona este repositorio:
   ```bash
   git clone https://github.com/username/cacao-trazabilidad.git
   ```

2. Abre los archivos `.ino` en el Arduino IDE o PlatformIO.

3. Instala la biblioteca `heltec_unofficial.h`.

4. Ajusta los parámetros de calibración según tus sensores y condiciones específicas.

5. Carga los programas en sus respectivas placas ESP32.

## ⚙️ Configuración

### Calibración de Sensores de Humedad

Los sensores de humedad del suelo requieren calibración según el tipo de suelo:

```cpp
// Valores de calibración para sensor de humedad del suelo
const int DRY = 3600;  // Valor en suelo completamente seco
const int WET = 1100;  // Valor en suelo saturado de agua
```

### Configuración de LoRa

Ajusta los parámetros de LoRa según tu región y necesidades:

```cpp
RADIOLIB_OR_HALT(radio.setFrequency(905.2));  // Frecuencia en MHz
RADIOLIB_OR_HALT(radio.setOutputPower(10));   // Potencia en dBm
RADIOLIB_OR_HALT(radio.setSpreadingFactor(12));
RADIOLIB_OR_HALT(radio.setBandwidth(250.0));
```

## 📝 Uso

1. Coloca los nodos transmisores en los puntos estratégicos del cultivo de cacao.
2. Ubica el nodo receptor en un lugar con buena recepción.
3. Los datos de humedad del suelo y condiciones ambientales se transmitirán en los intervalos configurados.
4. Visualiza los datos recibidos para tomar decisiones sobre el manejo del cultivo.

## 🤝 Contribuir

Las contribuciones son bienvenidas. Por favor, abre un issue para discutir los cambios propuestos.

## 📄 Licencia

Este proyecto está bajo la Licencia MIT - ver el archivo [LICENSE](LICENSE) para más detalles.

## 📞 Contacto

Para más información, contacta a [tu@email.com](mailto:tu@email.com).

## 🙏 Agradecimientos

- Universidad Juárez Autónoma de Tabasco
- Colaboradores y asesores del proyecto 