// Pruebas estáticas del firmware (node --test tests/): decisiones que no deben romperse
// entre emisor, receptor, calibración y el backend (msa-back/lib/parse-sensor-data.js).
import test from "node:test"
import assert from "node:assert/strict"
import fs from "node:fs"
import path from "node:path"
import { fileURLToPath } from "node:url"

const raiz = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..")
const leer = (p) => fs.readFileSync(path.join(raiz, p), "utf8")
const emisor = [leer("transmisor/sender/sender.ino"), leer("transmisor/sender/config.h")].join(String.fromCharCode(10))
const receptor = leer("receptor/receiver/receiver.ino")
const calibracion = leer("calibration/humiditySensorCalibration/humiditySensorCalibration.ino")

// Extrae el argumento de una llamada RADIOLIB_OR_HALT(radio.<metodo>(X))
const radio = (src, metodo) => {
  const m = src.match(new RegExp(String.raw`radio\.${metodo}\(([^)]*)\)`))
  return m ? m[1].trim() : null
}
const constante = (src, nombre) => {
  const m = src.match(new RegExp(String.raw`const\s+int\s+${nombre}\s*=\s*(-?\d+)`))
  return m ? Number(m[1]) : null
}
// Texto del String message = ... ; del emisor (lo que viaja por LoRa)
const mensajeLoRa = (() => {
  const i = emisor.indexOf("String message =")
  return emisor.slice(i, emisor.indexOf(";", i))
})()

for (const metodo of ["setFrequency", "setSpreadingFactor", "setBandwidth", "setCodingRate", "setPreambleLength", "setCRC"]) {
  test(`radio: ${metodo} igual en emisor y receptor`, () => {
    const a = radio(emisor, metodo), b = radio(receptor, metodo)
    assert.ok(a !== null, `el emisor no llama ${metodo}`)
    assert.equal(b, a, `${metodo}: emisor=${a} receptor=${b}`)
  })
}

test("protocolo: la lluvia digital viaja como RainBool 1/0 (lo que lee msa-back desde c52d13e)", () => {
  assert.match(mensajeLoRa, /"\|RainBool:" \+ \(rainDigital \? "1" : "0"\)/)
  assert.doesNotMatch(mensajeLoRa, /RainD:/)
})

test("protocolo compacto: sin unidades ni espacios entre campos", () => {
  assert.doesNotMatch(mensajeLoRa, /" \| /, "separador con espacios")
  assert.doesNotMatch(mensajeLoRa, /"(%|C|hPa|lx) ?\|/, "unidades pegadas al valor")
})

test("protocolo: el nodo va en la misma línea del paquete ('| Nodo:'), no en una línea aparte", () => {
  assert.match(mensajeLoRa, /"\|Nodo:"/)
  assert.doesNotMatch(mensajeLoRa, /\\nNodo/) // el "\n" escrito en el código C++
})

test("protocolo: claves que entiende el backend, en el orden del firmware", () => {
  const claves = [...mensajeLoRa.matchAll(/"(?:[^"]*\| ?)?([A-Za-z0-9]+):"/g)].map((m) => m[1])
  assert.deepEqual(claves, ["HS", "T1", "T2", "P", "HA", "Lux", "Rain", "RainBool", "Nodo", "Seq"])
})

test("emisor: NODE_ID definido", () => {
  assert.notEqual(constante(emisor, "NODE_ID"), null)
})

test("emisor: si falla el BME280 ya no se duerme sin transmitir", () => {
  const ini = emisor.indexOf("bme.begin(0x76"), fin = emisor.indexOf("// 3. BH1750")
  assert.ok(ini >= 0 && fin > ini, "no se encontró la inicialización del BME280 con bmeOk")
  assert.doesNotMatch(emisor.slice(ini, fin), /goToDeepSleep\(\)/)
})

test("calibración: mismo pin y mismos DRY/WET que el emisor", () => {
  assert.equal(constante(calibracion, "sensorPin"), constante(emisor, "sensorPin"))
  assert.equal(constante(calibracion, "DRY"), constante(emisor, "DRY"))
  assert.equal(constante(calibracion, "WET"), constante(emisor, "WET"))
})

test("calibración: el pin es ADC en el ESP32-S3 (GPIO1–20)", () => {
  const pin = constante(calibracion, "sensorPin")
  assert.ok(pin >= 1 && pin <= 20, `GPIO${pin} no tiene ADC en el S3`)
})

test("receptor: reenvía cada paquete una sola vez por Serial al backend", () => {
  assert.equal((receptor.match(/Serial\.println\(datos\)/g) || []).length, 1)
})

test("receptor: el parpadeo no usa delay() (la radio sigue escuchando)", () => {
  const fn = receptor.slice(receptor.indexOf("void actualizarParpadeo"))
  assert.doesNotMatch(fn, /delay\(/)
})

test("receptor: SensorData se declara antes de la primera función (prototipos automáticos del IDE)", () => {
  assert.ok(receptor.indexOf("struct SensorData") < receptor.indexOf("void rxCallback"))
})

test("energía: el aleatorio anticolisión se duerme, no se espera despierto", () => {
  assert.doesNotMatch(emisor, /delay\(\s*randomDelay/)
  assert.match(emisor, /INTERVALO_S \+ esp_random\(\) % \(JITTER_MAX_S \+ 1\)/)
  assert.match(emisor, /heltec_deep_sleep\(segundos\)/)
})

test("energía: dummy load desactivado por defecto y, si se activa, se mantiene en deep sleep", () => {
  assert.match(emisor, /#define USAR_DUMMY_LOAD 0/)
  assert.match(emisor, /gpio_hold_en\(\(gpio_num_t\)DUMMY_LOAD_PIN\)/)
  assert.match(emisor, /gpio_deep_sleep_hold_en\(\)/)
})

test("energía: BME280 forzado y BH1750 de una sola medición (duermen entre lecturas)", () => {
  assert.match(emisor, /MODE_FORCED/)
  assert.match(emisor, /takeForcedMeasurement\(\)/)
  assert.match(emisor, /ONE_TIME_HIGH_RES_MODE/)
})

test("config.h: NODE_ID en rango 0–255", () => {
  const id = constante(emisor, "NODE_ID")
  assert.ok(id >= 0 && id <= 255, `NODE_ID=${id}`)
})

test("cableado 2: no usa pines reservados de la Heltec V3 ni de arranque", () => {
  const bloque = emisor.slice(emisor.indexOf("#if CABLEADO == 2"), emisor.indexOf("#else", emisor.indexOf("#if CABLEADO == 2")))
  const pines = [...bloque.matchAll(/const int \w+ = (\d+);/g)].map((m) => Number(m[1]))
  // 0 botón · 1 VBAT · 3/45/46 arranque · 8–14 LoRa · 17/18/21 OLED · 19/20 USB · 35 LED · 36 Vext · 37 VBAT_CTRL
  const reservados = [0, 1, 3, 8, 9, 10, 11, 12, 13, 14, 17, 18, 19, 20, 21, 35, 36, 37, 45, 46]
  assert.ok(pines.length >= 2, "no se encontraron los pines del cableado 2")
  for (const p of pines) assert.ok(!reservados.includes(p), `GPIO${p} está reservado`)
})

test("cableado 2: lee la batería y la manda como Bat (en el cableado 1 no, GPIO1 está ocupado)", () => {
  assert.match(emisor, /#if CABLEADO == 2[\s\S]*#define LEER_BATERIA 1[\s\S]*#else[\s\S]*#define LEER_BATERIA 0/)
  assert.match(emisor, /"\|Bat:" \+ num\(heltec_vbat\(\), 2\)/)
})

test("Vext se enciende antes de heltec_setup en ambos sketches (OLED a brillo normal, sensores en Ve)", () => {
  // heltec_unofficial solo enciende Vext en el Wireless Stick; en la V3 hay que hacerlo a mano
  for (const [nombre, codigo] of [["emisor", emisor], ["receptor", receptor]]) {
    assert.match(codigo, /heltec_ve\(true\);\s*delay\(\d+\);[^\n]*\s*heltec_setup\(\);/, nombre)
  }
})

test("modo ahorro por defecto en el repo: 30 min y OLED apagado; prueba: 60 s con OLED", () => {
  const cfg = leer("transmisor/sender/config.h")
  assert.match(cfg, /#define MODO_AHORRO 1/)
  assert.match(cfg, /#if MODO_AHORRO\s+const int INTERVALO_S = 1800;[\s\S]*#define MOSTRAR_OLED 0[\s\S]*#else\s+const int INTERVALO_S = 60;[\s\S]*#define MOSTRAR_OLED 1/)
})

test("firma de arranque: el nodo y el gateway parpadean distinto en el LED blanco (GPIO35)", () => {
  for (const codigo of [emisor, receptor]) {
    assert.match(codigo, /LED_BLANCO = 35/)
    assert.match(codigo, /void firmaLed\(\)/)
  }
  // Solo al encender (no en cada despertar) para no gastar batería
  assert.match(emisor, /const bool arranque = esp_sleep_get_wakeup_cause\(\) != ESP_SLEEP_WAKEUP_TIMER;[\s\S]*?if \(arranque\) \{\s*firmaLed\(\);/)
})

test("los mensajes de arranque del gateway caben en el OLED (≤ 21 caracteres por renglón)", () => {
  for (const m of receptor.matchAll(/both\.println\("([^"]*)"\)/g)) assert.ok(m[1].length <= 21, m[1])
})

test("pantallas de arranque del nodo: cada renglón cabe en el OLED y cada página tiene ≤ 6", () => {
  const ini = emisor.indexOf("void revisarSensores()"), fin = emisor.indexOf("void loop()")
  const paginas = emisor.slice(ini, fin).split("display.cls();").slice(1, 3)
  assert.equal(paginas.length, 2)
  for (const pg of paginas) {
    const renglones = new Set([...pg.matchAll(/both\.print(?:ln|f)\("([^"]*)/g)].map((m) => m[1].replace(/\\n$/, "")))
    // BME/BH1750/DS18B20 tienen dos variantes (OK / --) pero ocupan un renglón cada uno
    const ocupados = [...renglones].filter((r) => !r.endsWith("--") && !r.startsWith("DS18B20 --")).length
    assert.ok(ocupados <= 6, `${ocupados} renglones`)
    for (const r of renglones) {
      // los formatos se reemplazan por su ancho máximo
      const ancho = r.replace(/%02X/g, "XX").replace(/%\.2f/g, "4.20").replace(/%d/g, "4095").length
      assert.ok(ancho <= 21, `"${r}" mide ${ancho}`)
    }
  }
  // "otros I2C: " + direcciones también cuenta: con hasta 3 direcciones cabe
  assert.ok("otros I2C: 5c 77 3c ".length <= 21)
})
