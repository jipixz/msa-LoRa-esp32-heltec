// Extrae datos del chip y del enlace de una línea de salida (esptool, arranque del ESP32-S3,
// arduino-cli o el Serial de los sketches). Devuelve un objeto con lo encontrado o null.

const PATRONES = [
  [/esptool(?:\.py)? v(\S+)/, (m) => ({ esptool: m[1] })],
  [/Serial port (\S+)/, (m) => ({ puerto: m[1] })],
  [/Chip is (.+?)\s*$/, (m) => ({ chip: m[1] })],
  [/Chip type:\s*(.+?)\s*$/, (m) => ({ chip: m[1] })],
  [/Features:\s*(.+?)\s*$/, (m) => ({ features: m[1] })],
  [/Crystal (?:is|frequency:)\s*(.+?)\s*$/, (m) => ({ cristal: m[1] })],
  [/MAC:\s*([0-9a-f]{2}(?::[0-9a-f]{2}){5})/i, (m) => ({ mac: m[1].toLowerCase() })],
  [/Detected flash size:\s*(\S+)/, (m) => ({ flash: m[1] })],
  [/ESP-ROM:(\S+)/, (m) => ({ rom: m[1] })],
  [/rst:(0x[0-9a-f]+) \(([^)]+)\)/i, (m) => ({ reset: `${m[1]} ${m[2]}` })],
  [/AgroMonitor (gateway|nodo \d+)/, (m) => ({ rol: m[1] })],
  [/^fw (\S+)/, (m) => ({ firmware: m[1] })],
  [/(?:Sketch usa|Sketch uses) (\d+) bytes \((\d+)%\)/, (m) => ({ sketch: `${m[1]} bytes (${m[2]} %)` })],
  [/Hash of data verified/, () => ({ verificado: true })],
  [/RSSI:(-?[\d.]+),\s*SNR:(-?[\d.]+)/, (m) => ({ rssi: Number(m[1]), snr: Number(m[2]) })],
]

export function analizarLinea(linea) {
  if (typeof linea !== "string" || !linea.trim()) return null
  let datos = null
  for (const [re, f] of PATRONES) {
    const m = linea.match(re)
    if (m) datos = { ...(datos ?? {}), ...f(m) }
  }
  return datos
}

/** Líneas de progreso que se reescriben (esptool "Writing at ... (37 %)"). */
export function esProgreso(linea) {
  return /^Writing at 0x[0-9a-f]+.*\(\s*\d+\s*%\)/i.test(linea) || /^\s*\d+\s*%\s*$/.test(linea)
}
