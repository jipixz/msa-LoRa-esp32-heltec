#!/usr/bin/env node
// Corre un comando y manda el comando y cada línea de su salida a la consola en vivo.
// La salida también se imprime aquí, como si se hubiera corrido directo.
//
//   node correr.mjs --tag build --titulo "Compilar receptor" -- arduino-cli compile ...
//   node correr.mjs --tag ssh -- ssh usuario@pi "bash -s" < script.sh   (stdin pasa al comando)
//   node correr.mjs --nota "Flasheando el receptor en la Pi"
import { spawn } from "node:child_process"
import { registrar } from "./eventos.mjs"
import { analizarLinea, esProgreso } from "./analizar.mjs"

const args = process.argv.slice(2)
const sep = args.indexOf("--")
const propias = sep >= 0 ? args.slice(0, sep) : args  // opciones de correr.mjs (antes de --)
const opcion = (nombre) => {
  const i = propias.indexOf(nombre)
  return i >= 0 ? propias[i + 1] : undefined
}

const nota = opcion("--nota")
if (nota !== undefined) {
  registrar({ tipo: "nota", texto: nota })
  process.exit(0)
}

if (sep < 0 || sep === args.length - 1) {
  console.error('Uso: node correr.mjs [--tag build] [--titulo "..."] -- comando args...  |  --nota "texto"')
  process.exit(2)
}
const tag = opcion("--tag") ?? "cmd"
const titulo = opcion("--titulo")
const [cmd, ...cmdArgs] = args.slice(sep + 1)
const citar = (a) => (/[\s"'$]/.test(a) ? `"${a.replace(/"/g, '\\"')}"` : a)

const inicio = Date.now()
const id = `${process.pid}-${inicio}`  // une el comando con su fin (la página marca puertos "en uso")
registrar({ tipo: "cmd", tag, titulo, id, texto: [cmd, ...cmdArgs].map(citar).join(" ") })
const hijo = spawn(cmd, cmdArgs, { stdio: ["inherit", "pipe", "pipe"], windowsHide: true })

function lineas(stream, nombre, salida) {
  let resto = ""
  stream.setEncoding("utf8")
  stream.on("data", (trozo) => {
    salida.write(trozo)
    const partes = (resto + trozo).split(/\r\n|\n|\r/)
    resto = partes.pop() ?? ""
    for (const linea of partes) emitir(linea, nombre)
  })
  stream.on("end", () => { if (resto) emitir(resto, nombre) })
}

function emitir(linea, stream) {
  if (!linea.trim()) return
  registrar({ tipo: esProgreso(linea) ? "progreso" : "salida", tag, stream, texto: linea })
  const datos = analizarLinea(linea)
  if (datos) registrar({ tipo: "chip", tag, datos })
}

lineas(hijo.stdout, "out", process.stdout)
lineas(hijo.stderr, "err", process.stderr)
hijo.on("error", (e) => {
  registrar({ tipo: "fin", tag, id, codigo: -1, ms: Date.now() - inicio, texto: e.message })
  console.error(e.message)
  process.exit(1)
})
hijo.on("close", (codigo) => {
  registrar({ tipo: "fin", tag, id, codigo, ms: Date.now() - inicio })
  process.exit(codigo ?? 1)
})
