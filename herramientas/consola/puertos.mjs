// Puertos serie de esta PC (Windows) y de la Pi: de la salida cruda a una lista con qué
// dispositivo USB hay detrás de cada puerto, y los cambios entre dos sondeos.

// Chips USB-serie conocidos (VID:PID)
export const CHIPS_USB = {
  "10c4:ea60": "CP2102 · Heltec V3",
  "1a86:7523": "CH340",
  "1a86:55d4": "CH9102",
  "0403:6001": "FT232R",
  "303a:1001": "ESP32-S3 USB nativo",
}

/**
 * Windows: objetos de PowerShell { Name, Id, Fab, Servicio, Estado, Ubic } → puertos.
 * Id es el PNPDeviceID: "USB\VID_10C4&PID_EA60\0001" o "BTHENUM\{...}" para Bluetooth.
 */
export function normalizarLocal(lista) {
  return [].concat(lista ?? []).filter(Boolean).map((d) => {
    const id = String(d.Id ?? "")
    const vid = /VID_([0-9A-F]{4})/i.exec(id)?.[1]
    const pid = /PID_([0-9A-F]{4})/i.exec(id)?.[1]
    const usb = vid && pid ? `${vid}:${pid}`.toLowerCase() : null
    const tipo = /^BTHENUM/i.test(id) || /bluetooth/i.test(d.Name ?? "") ? "bluetooth" : usb ? "usb" : "otro"
    return {
      puerto: /\((COM\d+)\)/.exec(d.Name ?? "")?.[1] ?? "?",
      id: id || null,  // instancia PnP (para el rastreo)
      nombre: String(d.Name ?? "").replace(/\s*\(COM\d+\)/, ""),
      tipo,
      usb,
      chip: usb ? CHIPS_USB[usb] ?? null : null,
      serie: tipo === "usb" ? id.split("\\").pop() : null,
      fabricante: d.Fab ?? null,
      driver: d.Servicio ?? null,
      ubicacion: d.Ubic ?? null,
      estado: d.Estado ?? null,
    }
  }).sort((a, b) => a.puerto.localeCompare(b.puerto, "en", { numeric: true }))
}

/**
 * Pi: salida del script de sondeo (líneas TTY|…, USB …, HUB …, MSA …) → estado.
 * TTY|/dev/ttyUSB0|10c4:ea60|producto|fabricante|serie|1-1.3|cp210x|pid:comm:cmdline;…
 */
export function parsearPi(salida) {
  const l = String(salida ?? "").trim().split("\n").map((x) => x.trimEnd())
  const tty = l.filter((x) => x.startsWith("TTY|")).map((x) => {
    const [, puerto, usb, producto, fabricante, serie, rutaUsb, driver, usa] = x.split("|")
    return {
      puerto, usb: usb || null, chip: CHIPS_USB[usb] ?? null, producto: producto || null, fabricante: fabricante || null,
      serie: serie || null, rutaUsb: rutaUsb || null, driver: driver || null,
      abiertoPor: (usa || "").split(";").filter(Boolean).map((p) => {
        const [pid, comm, ...cmd] = p.split(":")
        return { pid: Number(pid), comm, cmd: cmd.join(":") }
      }),
    }
  })
  return {
    tty,
    otrosUsb: l.filter((x) => x.startsWith("USB ")).map((x) => x.slice(4).replace(/^Bus \d+ Device \d+: ID /, ""))
      .filter((u) => !tty.some((t) => t.usb && u.startsWith(t.usb))),
    hub: l.find((x) => x.startsWith("HUB "))?.slice(4).trim() ?? "?",
    msaBack: l.includes("MSA activo"),
  }
}

/** Líneas legibles con lo que cambió entre dos listas de puertos (+ apareció, − se fue). */
export function cambiosPuertos(antes, despues, describir) {
  const a = new Map((antes ?? []).map((p) => [p.puerto, p]))
  const d = new Map((despues ?? []).map((p) => [p.puerto, p]))
  const lineas = []
  for (const [k, p] of d) if (!a.has(k)) lineas.push(`+ ${k} ← ${describir(p)}`)
  for (const [k, p] of a) if (!d.has(k)) lineas.push(`− ${k} se desconectó (${describir(p)})`)
  return lineas
}

export const describirLocal = (p) =>
  p.tipo === "bluetooth" ? "Bluetooth (virtual)" : [p.chip ?? p.nombre, p.usb, p.serie && `serie ${p.serie}`].filter(Boolean).join(" · ")

export const describirPi = (t) =>
  [t.chip ?? t.producto, t.usb, t.rutaUsb && `USB ${t.rutaUsb}`, t.driver].filter(Boolean).join(" · ")
