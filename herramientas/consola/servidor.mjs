#!/usr/bin/env node
// Consola en vivo del banco de pruebas: http://localhost:4747
//  - puertos serie de esta PC y de la Pi (qué dispositivo USB hay detrás de cada uno)
//  - logs reales en vivo: kernel de la Pi (journalctl -k) y, en Windows, las bitácoras System
//    (drivers y servicios del kernel) y Kernel-PnP, con su nivel (error / aviso / info)
//  - los comandos que se corren con correr.mjs y su salida, línea por línea
// Todo es de solo lectura: no abre los puertos ni cambia nada en Windows ni en la Pi.
//
//   MSA_PI=usuario@192.168.0.10 node herramientas/consola/servidor.mjs
import http from "node:http"
import fs from "node:fs"
import path from "node:path"
import { spawn, execFile } from "node:child_process"
import { fileURLToPath } from "node:url"
import { rutaEventos, registrar } from "./eventos.mjs"
import { normalizarLocal, parsearPi, cambiosPuertos, describirLocal, describirPi } from "./puertos.mjs"

const PUERTO = Number(process.env.MSA_CONSOLA_PUERTO || 4747)
const PI = process.env.MSA_PI || ""
const SSH_OPC = ["-o", "BatchMode=yes", "-o", "ConnectTimeout=4", "-o", "ServerAliveInterval=15"]
const AQUI = path.dirname(fileURLToPath(import.meta.url))
const WIN = process.platform === "win32"
const PS = ["-NoProfile", "-ExecutionPolicy", "Bypass"]

// ---- Bitácora → clientes (SSE) ----
const recientes = []  // últimos eventos, para quien abre la página después
const clientes = new Set()
let desplazamiento = 0

function difundir(ev) {
  recientes.push(ev)
  if (recientes.length > 1500) recientes.shift()
  const msg = `data: ${JSON.stringify(ev)}\n\n`
  for (const c of clientes) c.write(msg)
}

function leerNuevos() {
  let tam
  try { tam = fs.statSync(rutaEventos).size } catch { return }
  if (tam < desplazamiento) desplazamiento = 0  // se borró la bitácora
  if (tam === desplazamiento) return
  const fd = fs.openSync(rutaEventos, "r")
  const buf = Buffer.alloc(tam - desplazamiento)
  fs.readSync(fd, buf, 0, buf.length, desplazamiento)
  fs.closeSync(fd)
  const texto = buf.toString("utf8")
  const corte = texto.lastIndexOf("\n")
  if (corte < 0) return
  desplazamiento += Buffer.byteLength(texto.slice(0, corte + 1))
  for (const l of texto.slice(0, corte).split("\n")) {
    try { difundir(JSON.parse(l)) } catch { /* línea a medias */ }
  }
}

// Cada arranque empieza una sesión nueva: lo anterior queda en la bitácora pero no se repite
try { desplazamiento = fs.statSync(rutaEventos).size } catch {}
setInterval(leerNuevos, 250)

// ---- Puertos de esta PC ----
const PS_PUERTOS = String.raw`[Console]::OutputEncoding = [Text.Encoding]::UTF8
Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match '\(COM\d+\)' } | ForEach-Object {
  $u = $null
  if ($_.PNPDeviceID -like 'USB*' -or $_.PNPDeviceID -like 'FTDIBUS*') {
    $u = (Get-PnpDeviceProperty -InstanceId $_.PNPDeviceID -KeyName DEVPKEY_Device_LocationInfo -ErrorAction SilentlyContinue).Data
  }
  [pscustomobject]@{ Name = $_.Name; Id = $_.PNPDeviceID; Fab = $_.Manufacturer; Servicio = $_.Service; Estado = $_.Status; Ubic = $u }
} | ConvertTo-Json -Compress`

let puertosLocal = null, ocupadoLocal = false
function sondearLocal() {
  if (!WIN || ocupadoLocal) return
  ocupadoLocal = true
  execFile("powershell.exe", [...PS, "-Command", PS_PUERTOS], { windowsHide: true, timeout: 15000 }, (err, out) => {
    ocupadoLocal = false
    if (err) return
    let crudo
    try { crudo = JSON.parse(out || "[]") } catch { return }
    const lista = normalizarLocal(crudo)
    const primera = puertosLocal === null
    if (!primera && JSON.stringify(lista) === JSON.stringify(puertosLocal)) return
    const cambios = cambiosPuertos(puertosLocal ?? [], lista, describirLocal)
    puertosLocal = lista
    registrar({ tipo: "puertos-local", tag: "puertos", lista })
    if (primera) {
      registrar({ tipo: "cmd", tag: "puertos", texto: "Get-CimInstance Win32_PnPEntity | ? Name -match '(COMn)'   # cada 2 s" })
      for (const p of lista) registrar({ tipo: "salida", tag: "puertos", texto: `${p.puerto} ← ${describirLocal(p)}` })
    } else {
      for (const c of cambios) registrar({ tipo: "salida", tag: "puertos", texto: c })
    }
  })
}

// ---- Logs de Windows en vivo: System (drivers, servicios del kernel) y Kernel-PnP ----
// Level: 1 crítico, 2 error, 3 aviso, 4 información (0 = siempre registrado, se trata como info)
const NIVEL_WIN = { 1: "err", 2: "err", 3: "warn", 4: "info", 0: "info", 5: "debug" }
const LOGS_WIN = ["System", "Microsoft-Windows-Kernel-PnP/Configuration"]
const ultimoWin = {}
const PS_LOG = (log, desde) => String.raw`[Console]::OutputEncoding = [Text.Encoding]::UTF8
Get-WinEvent -LogName '${log}' -MaxEvents 60 -ErrorAction SilentlyContinue |
  Where-Object { $_.RecordId -gt ${desde} } | Sort-Object RecordId |
  ForEach-Object { [pscustomobject]@{ R = $_.RecordId; T = $_.TimeCreated.ToString('HH:mm:ss'); N = $_.Level; P = $_.ProviderName; Id = $_.Id; M = (($_.Message -split [char]10)[0]).Trim() } } | ConvertTo-Json -Compress`
let ocupadoWin = false
function sondearWindows() {
  if (!WIN || ocupadoWin) return
  ocupadoWin = true
  let pendientes = LOGS_WIN.length
  for (const log of LOGS_WIN) {
    execFile("powershell.exe", [...PS, "-Command", PS_LOG(log, ultimoWin[log] ?? 0)], { windowsHide: true, timeout: 15000 }, (err, out) => {
      if (--pendientes === 0) ocupadoWin = false
      if (err || !out.trim()) return  // sin línea base todavía: se reintenta en el siguiente sondeo
      let evs
      try { evs = [].concat(JSON.parse(out)) } catch { return }
      if (!evs.length) return
      const primera = ultimoWin[log] === undefined
      ultimoWin[log] = Math.max(ultimoWin[log] ?? 0, ...evs.map((e) => e.R))
      if (primera) {  // lo anterior al arranque no se repite
        registrar({ tipo: "cmd", tag: "windows", texto: `Get-WinEvent -LogName '${log}'   # eventos nuevos cada 3 s` })
        return
      }
      for (const e of evs) {
        registrar({ tipo: "salida", tag: "windows", nivel: NIVEL_WIN[e.N] ?? "info", texto: `${e.T}  ${e.P} (${e.Id})  ${e.M || "(sin mensaje)"}` })
      }
    })
  }
}

// ---- Puertos de la Pi ----
let estadoPi = null
function sondearPi() {
  if (!PI) return
  const hijo = execFile("ssh", [...SSH_OPC, PI, "bash -s"], { windowsHide: true, timeout: 12000 }, (err, out) => {
    if (err) {
      if (estadoPi?.error) return
      estadoPi = { error: "sin conexión SSH" }
      return registrar({ tipo: "puertos-pi", tag: "ssh", host: PI, ...estadoPi })
    }
    const estado = parsearPi(out)
    const primera = !estadoPi || estadoPi.error
    if (!primera && JSON.stringify(estado) === JSON.stringify(estadoPi)) return
    if (primera) registrar({ tipo: "cmd", tag: "ssh", texto: `ssh ${PI} bash -s < sondeo-pi.sh   # cada 4 s` })
    for (const c of cambiosPuertos(primera ? [] : estadoPi.tty, estado.tty, describirPi)) registrar({ tipo: "salida", tag: "ssh", texto: c })
    if (!primera) {
      if (estado.hub !== estadoPi.hub) registrar({ tipo: "salida", tag: "ssh", texto: `hub 1-1: ${estadoPi.hub} → ${estado.hub}` })
      if (estado.msaBack !== estadoPi.msaBack) registrar({ tipo: "salida", tag: "ssh", texto: `msa-back: ${estadoPi.msaBack ? "activo" : "detenido"} → ${estado.msaBack ? "activo" : "detenido"}` })
    }
    for (const t of estado.tty) {
      const a = primera ? null : estadoPi.tty.find((x) => x.puerto === t.puerto)
      const quien = (x) => x?.abiertoPor.map((p) => p.pid).join(",") ?? ""
      if (!a || quien(a) !== quien(t)) registrar({ tipo: "salida", tag: "ssh", texto: `${t.puerto}: ${t.abiertoPor.length ? "abierto por " + t.abiertoPor.map((p) => `pid ${p.pid} ${p.cmd.trim()}`).join(", ") : "nadie lo tiene abierto"}` })
    }
    estadoPi = estado
    registrar({ tipo: "puertos-pi", tag: "ssh", host: PI, ...estado })
  })
  hijo.stdin?.end(fs.readFileSync(path.join(AQUI, "sondeo-pi.sh"), "utf8"))
}

// ---- Log del kernel de la Pi en vivo: todo, con su prioridad (0–3 error, 4 aviso, 5–6 info, 7 debug) ----
const NIVEL_KERNEL = (p) => (p <= 3 ? "err" : p === 4 ? "warn" : p === 7 ? "debug" : "info")
function seguirKernel() {
  if (!PI) return
  registrar({ tipo: "cmd", tag: "kernel", texto: `ssh ${PI} journalctl -k -f -n 0 -o json` })
  const hijo = spawn("ssh", [...SSH_OPC, PI, "journalctl -k -f -n 0 -o json --output-fields=MESSAGE,PRIORITY"], { windowsHide: true })
  let resto = ""
  hijo.stdout.setEncoding("utf8")
  hijo.stdout.on("data", (t) => {
    const partes = (resto + t).split("\n")
    resto = partes.pop() ?? ""
    for (const linea of partes) {
      try {
        const j = JSON.parse(linea)
        if (typeof j.MESSAGE !== "string") continue
        registrar({ tipo: "salida", tag: "kernel", nivel: NIVEL_KERNEL(Number(j.PRIORITY ?? 6)), texto: j.MESSAGE })
      } catch { /* línea incompleta */ }
    }
  })
  hijo.on("close", () => setTimeout(seguirKernel, 5000))
}

// ---- HTTP ----
http.createServer((req, res) => {
  const url = new URL(req.url, "http://localhost")
  if (url.pathname === "/eventos") {
    res.writeHead(200, { "Content-Type": "text/event-stream", "Cache-Control": "no-cache", Connection: "keep-alive" })
    res.write("retry: 2000\n\n")
    for (const ev of recientes) res.write(`data: ${JSON.stringify(ev)}\n\n`)
    clientes.add(res)
    req.on("close", () => clientes.delete(res))
    return
  }
  if (url.pathname === "/" || url.pathname === "/index.html") {
    res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" })
    return fs.createReadStream(path.join(AQUI, "index.html")).pipe(res)
  }
  res.writeHead(404).end()
}).listen(PUERTO, "127.0.0.1", () => {
  console.log(`consola en http://localhost:${PUERTO}  ·  bitácora ${rutaEventos}  ·  Pi ${PI || "(sin MSA_PI)"}`)
  registrar({ tipo: "nota", texto: `consola iniciada · Pi: ${PI || "sin configurar (MSA_PI)"}` })
  sondearLocal(); sondearWindows(); sondearPi(); seguirKernel()
  setInterval(sondearLocal, 2000)
  setInterval(sondearWindows, 3000)
  setInterval(sondearPi, 4000)
})
