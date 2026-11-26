// Bitácora compartida: el envoltorio (correr.mjs) y el servidor escriben aquí, una línea JSON
// por evento; el servidor la sigue y la manda en vivo a la página.
import fs from "node:fs"
import os from "node:os"
import path from "node:path"

export const rutaEventos = process.env.MSA_CONSOLA_LOG || path.join(os.tmpdir(), "msa-consola", "eventos.jsonl")

export function registrar(evento) {
  fs.mkdirSync(path.dirname(rutaEventos), { recursive: true })
  fs.appendFileSync(rutaEventos, JSON.stringify({ t: Date.now(), ...evento }) + "\n")
}
