// ============================================================================
// El reloj de la sala, en un solo lugar.
//
// Las cuentas regresivas (el tiempo del orador, la votación, el teleprompter)
// se hacen con la hora de la SALA, no con la del aparato: una tablet con la
// hora corrida arrancaría el texto en cualquier lado o cerraría la votación
// antes de tiempo. Cada respuesta del servidor trae su hora; con ella se
// calcula el desfase.
//
// LAS CLAVES. Según quién contesta, la hora viene como `ahoraMs`, `ahora_ms`,
// `serverNowMs` o `t_srv_ms`. No se renombran en el servidor (las leen
// páginas abiertas y otros clientes): acá se lee cualquiera.
//
//   BAReloj.tomar(d)    lee la hora del servidor de `d` (si la trae)
//   BAReloj.ahora()     la hora de la sala, en ms
// ============================================================================
(() => {
  "use strict";
  const CLAVES = ["t_srv_ms", "ahoraMs", "ahora_ms", "serverNowMs"];
  let desfase = 0; // reloj de la sala - reloj de acá

  window.BAReloj = {
    /// :returns: true si `d` traía la hora del servidor.
    tomar(d) {
      if (!d || typeof d !== "object") return false;
      for (const k of CLAVES)
        if (typeof d[k] === "number" && d[k] > 0) { desfase = d[k] - Date.now(); return true; }
      return false;
    },
    ahora: () => Date.now() + desfase,
  };
})();
