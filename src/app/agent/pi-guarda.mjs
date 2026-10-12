// La guarda del chat del modelo local (Broadcast Appliance → Asistente → «Modelo local»).
//
// En JavaScript (no TypeScript): el pi empaquetado no trae jiti para transpilar.
// pi la carga con -e (agent/AgentViewModel.cpp la deja en su perfil portable).
// Un modelo chico, solo, se enrosca y a veces hace lo que nadie pidió; esto lo
// frena con código, no con el prompt:
//   1. Todo lo que CAMBIA algo en el programa (una herramienta del MCP sin
//      readOnlyHint) se le pregunta al operador en la terminal antes de hacerlo.
//   2. La misma llamada con los mismos datos, dos veces seguidas: no (y si insiste,
//      se corta su turno: terminate).
//   3. Más de 12 llamadas para un mismo pedido: se corta su turno.

const MAX_LLAMADAS = 12;

export default function (pi) {
  let ultima = "";
  let llamadas = 0;
  let bloqueos = 0;

  // Cada pedido del operador empieza de cero.
  pi.on("agent_start", () => {
    ultima = "";
    llamadas = 0;
    bloqueos = 0;
  });

  pi.on("tool_call", async (event, ctx) => {
    const nombre = event.toolName ?? "";
    const datos = JSON.stringify(event.input ?? {});
    const clave = nombre + " " + datos;
    llamadas += 1;
    if (clave === ultima) {
      bloqueos += 1;
      return {
        block: true,
        reason: "Ya hiciste esta misma llamada y tenés el resultado arriba: no la repitas. Contestá con lo que ya sabés.",
        terminate: bloqueos > 1, // insistió: se corta su turno
      };
    }
    ultima = clave;
    if (llamadas > MAX_LLAMADAS)
      return {
        block: true,
        reason: "Ya hiciste muchas llamadas para este pedido: pará y contestale al operador con lo que averiguaste.",
        terminate: true,
      };

    const herramienta = pi.getAllTools().find((t) => t.name === nombre);
    const cambia = nombre.startsWith("mcp__") && herramienta?.annotations?.readOnlyHint !== true;
    if (!cambia) return;
    const corto = nombre.replace(/^mcp__[^_]+__/, "");
    const ok = await ctx.ui.confirm("¿Hago este cambio en el programa?", corto + " " + datos);
    if (!ok)
      return {
        block: true,
        reason: "El operador NO lo aprobó: no lo hagas. Preguntale qué prefiere.",
      };
  });
}
