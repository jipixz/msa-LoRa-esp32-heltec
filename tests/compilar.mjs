// Compila los 3 sketches para la Heltec WiFi LoRa 32 V3 (prueba estática del firmware).
// Uso: node tests/compilar.mjs   (ARDUINO_CLI=ruta\arduino-cli.exe si no está en el PATH)
// Requisitos probados: core esp32:esp32 3.2.0 y RadioLib 7.x (8.0.0 no compila con heltec_unofficial 0.9.2).
import { execFileSync } from "node:child_process"
import path from "node:path"
import { fileURLToPath } from "node:url"

const raiz = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..")
const cli = process.env.ARDUINO_CLI || "arduino-cli"
const extra = process.env.ARDUINO_CLI_CONFIG ? ["--config-file", process.env.ARDUINO_CLI_CONFIG] : []
const FQBN = "esp32:esp32:heltec_wifi_lora_32_V3"
const sketches = ["transmisor/sender", "receptor/receiver", "calibration/humiditySensorCalibration"]

const libs = execFileSync(cli, [...extra, "lib", "list"], { encoding: "utf8" })
const radiolib = libs.match(/^RadioLib\s+(\d+)\./m)
if (radiolib && Number(radiolib[1]) >= 8) {
  console.error(`RadioLib ${radiolib[1]}.x instalado: heltec_unofficial 0.9.2 necesita RadioLib 7.x`)
  process.exit(1)
}

let fallas = 0
for (const s of sketches) {
  try {
    execFileSync(cli, [...extra, "compile", "--fqbn", FQBN, path.join(raiz, s)], { stdio: "pipe", encoding: "utf8" })
    console.log(`ok    ${s}`)
  } catch (e) {
    fallas++
    console.log(`FALLA ${s}\n${(e.stdout || "") + (e.stderr || "")}`.split("\n").filter((l) => /error/i.test(l)).slice(0, 10).join("\n"))
  }
}
process.exit(fallas ? 1 : 0)
