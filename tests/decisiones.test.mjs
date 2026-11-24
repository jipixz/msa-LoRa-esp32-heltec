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
  const ini = emisor.indexOf("bmeOk = bme.begin"), fin = emisor.indexOf("// 3. BH1750")
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
