// ============================================================================
// El envoltorio de los mensajes del canal de control.
//
// Por el WebSocket no viaja JSON pelado: viaja un frame BINARIO con 4 bytes de
// longitud en little-endian y después el JSON. Es el mismo formato que usa el
// canal de control interno del aparato (src/core/ipc/ControlProtocol.hpp), así
// que el servidor tiene UN solo decodificador para todo el mundo.
//
// Esto no es un detalle decorativo: una página que mande texto pelado se ve
// del lado del servidor como «frame corrupto» y el mensaje se tira. Pasó —el
// televisor quedaba en «conectando» para siempre porque su offer nunca se
// leía—, así que el envoltorio vive acá, en un solo lugar, y lo usan todas las
// superficies.
// ============================================================================
(() => {
  "use strict";

  window.BATrama = {
    /// Objeto -> Uint8Array lista para `ws.send`.
    encode(obj) {
      const json = new TextEncoder().encode(JSON.stringify(obj));
      const buf = new Uint8Array(4 + json.length);
      new DataView(buf.buffer).setUint32(0, json.length, true);
      buf.set(json, 4);
      return buf;
    },

    /// Lo que llegó por `onmessage` -> objeto, o null si no se entiende.
    /// Acepta ArrayBuffer (lo normal) y, por las dudas, texto pelado: una
    /// superficie vieja que todavía no se actualizó se sigue entendiendo.
    decode(data) {
      if (typeof data === "string") {
        try { return JSON.parse(data); } catch (e) { return null; }
      }
      if (!(data instanceof ArrayBuffer)) return null;
      const u8 = new Uint8Array(data);
      if (u8.length < 4) return null;
      const len = new DataView(u8.buffer, u8.byteOffset, u8.byteLength).getUint32(0, true);
      if (u8.length < 4 + len) return null;
      try { return JSON.parse(new TextDecoder().decode(u8.subarray(4, 4 + len))); } catch (e) { return null; }
    },
  };
})();
