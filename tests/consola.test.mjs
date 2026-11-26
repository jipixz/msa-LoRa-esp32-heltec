// Analizador de la consola en vivo (herramientas/consola/analizar.mjs)
import test from "node:test"
import assert from "node:assert/strict"
import { analizarLinea, esProgreso } from "../herramientas/consola/analizar.mjs"

test("esptool: chip, MAC, cristal y verificación", () => {
  assert.deepEqual(analizarLinea("Chip is ESP32-S3 (QFN56) (revision v0.2)"), { chip: "ESP32-S3 (QFN56) (revision v0.2)" })
  assert.deepEqual(analizarLinea("MAC: 48:CA:43:3A:DF:48"), { mac: "48:ca:43:3a:df:48" })
  assert.deepEqual(analizarLinea("Crystal is 40MHz"), { cristal: "40MHz" })
  assert.deepEqual(analizarLinea("Hash of data verified."), { verificado: true })
})

test("arranque del ESP32-S3 y de los sketches", () => {
  assert.deepEqual(analizarLinea("ESP-ROM:esp32s3-20210327"), { rom: "esp32s3-20210327" })
  assert.deepEqual(analizarLinea("rst:0x1 (POWERON),boot:0x29 (SPI_FAST_FLASH_BOOT)"), { reset: "0x1 POWERON" })
  assert.deepEqual(analizarLinea("AgroMonitor gateway"), { rol: "gateway" })
  assert.deepEqual(analizarLinea("AgroMonitor nodo 1"), { rol: "nodo 1" })
  assert.deepEqual(analizarLinea("fw 2026.11.25"), { firmware: "2026.11.25" })
})

test("enlace LoRa y compilación", () => {
  assert.deepEqual(analizarLinea("RSSI:-9.00,SNR:4.75"), { rssi: -9, snr: 4.75 })
  assert.deepEqual(analizarLinea("El Sketch usa 413710 bytes (12%) del espacio"), { sketch: "413710 bytes (12 %)" })
})

test("líneas sin datos y progreso de esptool", () => {
  assert.equal(analizarLinea("Durmiendo 60 s"), null)
  assert.equal(analizarLinea(""), null)
  assert.equal(esProgreso("Writing at 0x0001a000... (37 %)"), true)
  assert.equal(esProgreso("Wrote 432384 bytes"), false)
})

import { normalizarLocal, parsearPi, cambiosPuertos, describirLocal } from "../herramientas/consola/puertos.mjs"

test("Windows: COM USB con su chip y Bluetooth separado", () => {
  const l = normalizarLocal([
    { Name: "Silicon Labs CP210x USB to UART Bridge (COM5)", Id: String.raw`USB\VID_10C4&PID_EA60\0001`, Fab: "Silicon Labs", Servicio: "silabser", Ubic: "Port_#0003.Hub_#0001" },
    { Name: "Serie estándar sobre el vínculo Bluetooth (COM3)", Id: String.raw`BTHENUM\{00001101}_VID&0001000A_PID&FFFF\8&3DF` },
  ])
  assert.deepEqual(l.map((p) => [p.puerto, p.tipo, p.usb, p.chip, p.serie]), [["COM3", "bluetooth", null, null, null], ["COM5", "usb", "10c4:ea60", "CP2102 · Heltec V3", "0001"]])
  assert.equal(l[1].id, String.raw`USB\VID_10C4&PID_EA60\0001`)
})

test("Pi: puerto serie con quién lo tiene abierto; el CP210x no se repite en otros USB", () => {
  const e = parsearPi([
    "TTY|/dev/ttyUSB0|10c4:ea60|CP2102 USB to UART Bridge Controller|Silicon Labs|0001|1-1.3|cp210x|365615:node /home/jipi:node /x/msa-back/index.js ;",
    "USB Bus 001 Device 025: ID 10c4:ea60 Silicon Labs CP210x UART Bridge",
    "USB Bus 002 Device 002: ID 174c:235c ASMedia Technology Inc. Ugreen Storage Device",
    "HUB active on", "MSA activo",
  ].join("\n"))
  assert.equal(e.tty[0].rutaUsb, "1-1.3")
  assert.deepEqual(e.tty[0].abiertoPor.map((p) => p.pid), [365615])
  assert.match(e.tty[0].abiertoPor[0].cmd, /msa-back/)
  assert.deepEqual(e.otrosUsb, ["174c:235c ASMedia Technology Inc. Ugreen Storage Device"])
  assert.equal(e.msaBack, true)
})

test("cambios de puertos: aparece y se desconecta", () => {
  const a = [{ puerto: "COM3", tipo: "bluetooth" }]
  const b = [{ puerto: "COM3", tipo: "bluetooth" }, { puerto: "COM5", tipo: "usb", usb: "10c4:ea60", chip: "CP2102 · Heltec V3", serie: "0001" }]
  assert.deepEqual(cambiosPuertos(a, b, describirLocal), ["+ COM5 ← CP2102 · Heltec V3 · 10c4:ea60 · serie 0001"])
  assert.deepEqual(cambiosPuertos(b, a, describirLocal), ["− COM5 se desconectó (CP2102 · Heltec V3 · 10c4:ea60 · serie 0001)"])
})
