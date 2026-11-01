// ============================================================================
// Los APARATOS REGISTRADOS: el PIN se pide una sola vez, para registrar el aparato.
//
// La consola de la presidencia y el celular de un integrante entran con su link y el
// PIN. La primera vez, con el PIN bien, la sala le da a la página un token de aparato
// (largo, al azar) que queda guardado en el navegador, por superficie y por el
// identificador de su link. Las siguientes, con la misma URL, la página entra con el
// token: recargar, cerrar el navegador o que se reinicie la app o la sala no vuelve a
// pedir el PIN. Si la sala dice que ya no vale (lo revocaron desde la app, o el link
// se renovó), se olvida y recién ahí se pide el PIN otra vez.
// (Del lado de la sala: gateway/Sesion.hpp, «aparatos registrados».)
//
//   BAAparato.leer("presidencia", id)          el token guardado, o ""
//   BAAparato.guardar("presidencia", id, tok)  después de un {auth, ok, aparato}
//   BAAparato.olvidar("presidencia", id)       la sala dijo error_codigo «aparato»
//   BAAparato.nombre()                         cómo se ve este aparato en la lista de la app
//   BAAparato.ESPERA_MS                        cuánto esperar ante error_codigo «esperar»
//
// Una sala recién arrancada que todavía no recibió las credenciales de la app no dice
// «mal»: dice «esperar» (error_codigo), y la página vuelve a probar sola.
// ============================================================================
(() => {
  "use strict";
  const clave = (superficie, id) => "ba-aparato:" + superficie + ":" + id;
  window.BAAparato = {
    ESPERA_MS: 2000,
    leer(superficie, id) {
      try { return localStorage.getItem(clave(superficie, id)) || ""; } catch (e) { return ""; }
    },
    guardar(superficie, id, token) {
      try { localStorage.setItem(clave(superficie, id), token); } catch (e) { /* modo privado: pide el PIN cada vez */ }
    },
    olvidar(superficie, id) {
      try { localStorage.removeItem(clave(superficie, id)); } catch (e) { /* nada */ }
    },
    /// «iPad · Safari», «Android · Chrome»: para reconocerlo en la lista de la app.
    nombre() {
      const ua = navigator.userAgent || "";
      const so = /iPad/.test(ua) ? "iPad" : /iPhone/.test(ua) ? "iPhone" : /Android/.test(ua) ? "Android"
        : /Windows/.test(ua) ? "Windows" : /Mac OS X/.test(ua) ? "Mac" : /Linux/.test(ua) ? "Linux" : "";
      const nav = /Edg\//.test(ua) ? "Edge" : /Firefox\//.test(ua) ? "Firefox" : /Chrome\//.test(ua) ? "Chrome"
        : /Safari\//.test(ua) ? "Safari" : "";
      return [so, nav].filter(Boolean).join(" · ") || "navegador";
    },
  };
})();
