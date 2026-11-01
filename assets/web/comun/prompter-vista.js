// ============================================================================
// La vista del teleprompter: la tablet del atril y el panel del orador en la
// página del invitado son la MISMA (antes el invitado tenía su copia, que
// calculaba y dibujaba distinto).
//
// COMO SE MUEVE EL TEXTO. El servidor no manda la posición cuadro a cuadro:
// manda DESDE DONDE (`posicion`, en palabras), A QUE VELOCIDAD (`ppm`) y DESDE
// CUANDO (`desdeMs`, con el reloj de la sala: comun/reloj.js). Cada vista
// calcula sola dónde va, en su propio requestAnimationFrame. Así un hipo de la
// red no se ve como un tirón en el renglón que alguien está leyendo en vivo, y
// si la red se corta del todo, el texto sigue andando solo.
//
// LA POSICION VA EN PALABRAS, no en píxeles: la tablet y el celular tienen
// pantallas distintas y tienen que hablar del mismo lugar del texto.
//
//   const v = BAPrompterVista.crear({ lienzo, texto, guia: 0.38 });
//   v.aplicar(d)    lo que mandó la sala (entero o sin `texto`); :returns: true si
//                   cambió el texto
//   v.dibujar()     en cada cuadro: marca lo leído y deja la palabra en la guía
//   v.e             el estado (texto, palabras, posicion, ppm, corriendo, aviso…)
//   v.posicion()    dónde va ahora (en palabras)
//   v.faltaSeg()    lo que falta leer, a la velocidad de ahora
// ============================================================================
(() => {
  "use strict";

  window.BAPrompterVista = {
    crear({ lienzo, texto, guia = 0.38 }) {
      const e = { texto: "", titulo: "", palabras: 0, parrafos: [], posicion: 0, ppm: 140, corriendo: false,
                  desdeMs: 0, tamano: 5, espejo: false, aviso: "", presentador: null, duracionSeg: 0, version: -1 };
      let spans = [], ultimoMarcado = -1;

      // La posicion AHORA: lo ultimo que dijo el servidor mas lo que corrio desde
      // entonces. Es la unica cuenta que importa.
      const posicion = () => {
        if (!e.corriendo) return e.posicion;
        const minutos = (BAReloj.ahora() - e.desdeMs) / 60000;
        return Math.min(e.palabras, e.posicion + minutos * e.ppm);
      };

      // El texto se parte en palabras una sola vez: mover 3000 spans por cuadro es
      // caro, pero crearlos en cada cuadro es imposible. Se conservan los espacios y
      // los saltos de linea tal cual: el orador escribio los renglones donde queria
      // respirar.
      const armar = () => {
        texto.textContent = "";
        spans = [];
        ultimoMarcado = -1;
        for (const p of e.texto.split(/(\s+)/)) {
          if (p === "") continue;
          if (/^\s+$/.test(p)) { texto.appendChild(document.createTextNode(p)); continue; }
          const s = document.createElement("span");
          s.textContent = p;
          texto.appendChild(s);
          spans.push(s);
        }
      };

      return {
        e,
        posicion,
        faltaSeg: () => (e.ppm > 0 ? (Math.max(0, e.palabras - posicion()) / e.ppm) * 60 : 0),
        aplicar(d) {
          if (!d || typeof d !== "object") return false;
          BAReloj.tomar(d);
          // Sin `texto`: es un cambio de lo demas (el texto es el que ya esta).
          const nuevo = typeof d.texto === "string" && d.texto !== e.texto;
          if (nuevo) e.texto = d.texto;
          e.titulo = d.titulo || "";
          e.palabras = d.palabras || 0;
          e.parrafos = d.parrafos || [];
          e.posicion = d.posicion || 0;
          e.ppm = d.ppm || 140;
          e.corriendo = !!d.corriendo;
          e.desdeMs = d.desdeMs || 0;
          e.tamano = d.tamano || 5;
          e.espejo = !!d.espejo;
          e.aviso = d.aviso || "";
          e.presentador = d.presentador || null;
          e.duracionSeg = d.duracionSeg || 0;
          e.version = d.version;
          if (nuevo) armar();
          ultimoMarcado = -1; // que el proximo cuadro repinte todo
          return nuevo;
        },
        dibujar() {
          if (!spans.length) return;
          const pos = posicion();
          const i = Math.min(spans.length - 1, Math.max(0, Math.floor(pos)));
          // Marcar lo leido es caro si se hace palabra por palabra en cada cuadro:
          // solo se tocan las que cambiaron desde el cuadro anterior.
          if (i !== ultimoMarcado) {
            const desde = Math.min(i, ultimoMarcado < 0 ? 0 : ultimoMarcado);
            const hasta = Math.max(i, ultimoMarcado < 0 ? spans.length - 1 : ultimoMarcado);
            for (let k = desde; k <= hasta && k < spans.length; k++)
              spans[k].className = k < i ? "leido" : (k === i ? "aqui" : "");
            ultimoMarcado = i;
          }
          // El desplazamiento: la palabra actual tiene que quedar en la guia.
          const palabra = spans[i];
          const y = palabra.offsetTop + palabra.offsetHeight / 2;
          texto.style.transform = "translateY(" + (lienzo.clientHeight * guia - y) + "px)";
        },
      };
    },
  };
})();
