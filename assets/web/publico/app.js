// La página del público: preguntar y votar. Nada más, a propósito.
//
// No entra a la sala, no pide cámara ni micrófono y no abre WebRTC. En un salón
// con doscientas personas eso sería un acople garantizado y el ancho de banda
// tirado; acá alcanza con HTTP.
(() => {
  'use strict';

  // --- La clave, del fragmento ---------------------------------------------
  // En el fragmento y no en la consulta: así no viaja en la petición ni queda
  // en los registros de ningún servidor por el que pase el enlace.
  const frag = new URLSearchParams(location.hash.replace(/^#/, ''));
  const clave = frag.get('k') || '';
  if (!clave) {
    document.querySelector('.hoja').classList.add('hidden');
    document.getElementById('sinClave').classList.remove('hidden');
    return;
  }

  // --- Quién soy para este teléfono ----------------------------------------
  // Un número inventado acá que se queda acá. NO es identidad: sirve para ver
  // las propias y para que tocar dos veces no sume dos votos. Si el navegador
  // no deja guardar (modo privado, cookies bloqueadas), se usa uno de esta
  // sesión: se pierden las propias al recargar, que es molesto pero no rompe.
  const nuevoYo = () => 'y' + Math.random().toString(36).slice(2) + Date.now().toString(36);
  let yo;
  try {
    yo = localStorage.getItem('ba-publico-yo');
    if (!yo) { yo = nuevoYo(); localStorage.setItem('ba-publico-yo', yo); }
  } catch (_) {
    yo = nuevoYo();
  }

  const $ = (id) => document.getElementById(id);
  const estado = $('estado');
  const aviso = $('aviso');

  const decir = (texto, clase) => {
    aviso.textContent = texto;
    aviso.className = 'aviso' + (clase ? ' ' + clase : '');
  };

  // --- Pintar ---------------------------------------------------------------
  // Con textContent y nunca con innerHTML: el texto lo escribe cualquiera con
  // el enlace, y un `<img onerror>` en una pregunta correría en el teléfono de
  // todos los demás.
  function fila(q, esMia) {
    const div = document.createElement('div');
    div.className = 'q' + (q.alAire || q.estado === 'al_aire' ? ' aire' : '');

    const cuerpo = document.createElement('div');
    cuerpo.className = 'cuerpo';
    const texto = document.createElement('div');
    texto.className = 'texto';
    texto.textContent = q.texto;
    cuerpo.appendChild(texto);

    const pie = document.createElement('div');
    pie.className = 'quien';
    if (esMia) {
      // Saber que la eligieron es la mitad de la gracia de haber preguntado.
      const m = { nueva: 'Esperando', elegida: 'En la cola', al_aire: 'Al aire ahora',
                  respondida: 'Respondida', descartada: 'No se va a preguntar' };
      const marca = document.createElement('span');
      marca.className = 'marca' + (q.estado === 'al_aire' ? ' aire'
                                 : q.estado === 'elegida' ? ' elegida' : '');
      marca.textContent = m[q.estado] || q.estado;
      pie.appendChild(marca);
    } else {
      pie.textContent = q.nombre || 'Anónimo';
      if (q.alAire) {
        const marca = document.createElement('span');
        marca.className = 'marca aire';
        marca.textContent = ' · Al aire ahora';
        pie.appendChild(marca);
      }
    }
    cuerpo.appendChild(pie);
    div.appendChild(cuerpo);

    if (!esMia) {
      const b = document.createElement('button');
      b.className = 'votar';
      b.innerHTML = '';
      const flecha = document.createElement('span');
      flecha.textContent = '▲';
      const n = document.createElement('span');
      n.className = 'n';
      n.textContent = String(q.votos);
      b.append(flecha, n);
      b.addEventListener('click', () => votar(q.id, b));
      div.appendChild(b);
    }
    return div;
  }

  function pintar(p) {
    const mias = $('mias'), otras = $('otras');
    mias.textContent = '';
    otras.textContent = '';
    (p.mias || []).forEach((q) => mias.appendChild(fila(q, true)));
    (p.otras || []).forEach((q) => otras.appendChild(fila(q, false)));
    $('seccionMias').classList.toggle('hidden', !(p.mias || []).length);
    $('seccionOtras').classList.toggle('hidden', !(p.otras || []).length);
  }

  // --- Hablar con el gateway ------------------------------------------------
  async function traer() {
    try {
      const r = await fetch('/api/preguntas?k=' + encodeURIComponent(clave) +
                            '&yo=' + encodeURIComponent(yo), { cache: 'no-store' });
      if (r.status === 403) { estado.textContent = 'Clave rechazada'; return; }
      const j = await r.json();
      if (j.preguntas) pintar(j.preguntas);
      estado.textContent = '';
    } catch (_) {
      // Sin conexión no se borra lo que ya está en pantalla: el que está
      // leyendo una pregunta no tiene por qué perderla porque se cayó el wifi
      // un segundo.
      estado.textContent = 'Sin conexión';
    }
  }

  async function mandar(cuerpo) {
    const r = await fetch('/api/preguntas', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(Object.assign({ clave, yo }, cuerpo)),
    });
    return r.json();
  }

  async function votar(id, boton) {
    boton.disabled = true;
    try {
      const j = await mandar({ votar: id });
      if (j.preguntas) pintar(j.preguntas);
    } catch (_) {
      boton.disabled = false;
    }
  }

  $('mandar').addEventListener('click', async () => {
    const texto = $('texto').value.trim();
    if (!texto) { decir('Escribí la pregunta primero.', 'mal'); return; }
    const b = $('mandar');
    b.disabled = true;
    decir('Mandando…');
    try {
      const j = await mandar({ texto, nombre: $('nombre').value.trim() });
      if (!j.ok) { decir(j.error || 'No se pudo mandar.', 'mal'); return; }
      $('texto').value = '';
      decir('Mandada. Vas a ver acá si la eligen.', 'bien');
      if (j.preguntas) pintar(j.preguntas);
    } catch (_) {
      decir('No se pudo mandar: revisá la conexión.', 'mal');
    } finally {
      b.disabled = false;
    }
  });

  // El nombre se recuerda: nadie quiere escribirlo en cada pregunta.
  try {
    const n = localStorage.getItem('ba-publico-nombre');
    if (n) $('nombre').value = n;
    $('nombre').addEventListener('change', () => {
      try { localStorage.setItem('ba-publico-nombre', $('nombre').value.trim()); } catch (_) {}
    });
  } catch (_) {}

  // El fondo y el nombre del evento los pone el operador; si no hay, la página
  // funciona igual, que es lo que importa.
  fetch('/api/publico-info?k=' + encodeURIComponent(clave), { cache: 'no-store' })
    .then((r) => (r.ok ? r.json() : null))
    .then((j) => {
      if (!j) return;
      if (j.evento) $('evento').textContent = j.evento;
      if (j.cuando) $('cuando').textContent = j.cuando;
      if (j.fondo) $('fondo').style.backgroundImage = 'url("' + j.fondo + '")';
    })
    .catch(() => {});

  // --- Escanear la invitacion acredita --------------------------------------
  // El QR de la invitacion trae el codigo de la persona. Con eso, llegar y
  // mirarlo con el telefono es toda la acreditacion: no hay que hacer cola en
  // un mostrador ni que nadie le mire la cara a nadie.
  //
  // Se manda UNA VEZ por codigo y se recuerda: recargar la pagina no vuelve a
  // avisar que llego, y al que ya entro no le sirve verlo de nuevo.
  const codigoDelQr = (frag.get('c') || '').trim().toUpperCase();
  if (codigoDelQr) {
    let yaAvise = '';
    try { yaAvise = localStorage.getItem('ba-publico-acreditado') || ''; } catch (_) {}
    if (yaAvise !== codigoDelQr) {
      fetch('/api/acreditar', {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ clave, codigo: codigoDelQr }),
      }).then(() => {
        try { localStorage.setItem('ba-publico-acreditado', codigoDelQr); } catch (_) {}
        estado.textContent = 'Llegada registrada';
      }).catch(() => {});
    }
  }

  // --- La urna --------------------------------------------------------------
  // El codigo de votante se guarda en el telefono: en una asamblea se vota
  // varios puntos seguidos y tipearlo cada vez es perder gente en el camino.
  let codigo = codigoDelQr;
  try { codigo = codigoDelQr || localStorage.getItem('ba-publico-codigo') || ''; } catch (_) {}
  // El del QR manda y se guarda: quien escaneo su invitacion no tiene que
  // tipear su codigo despues para votar. Es el mismo.
  if (codigoDelQr) { try { localStorage.setItem('ba-publico-codigo', codigoDelQr); } catch (_) {} }
  $('codigo').value = codigo;
  $('codigo').addEventListener('input', () => {
    codigo = $('codigo').value.trim().toUpperCase();
    $('codigo').value = codigo;
    try { localStorage.setItem('ba-publico-codigo', codigo); } catch (_) {}
    pintarUrna(ultimaUrna);
  });

  let ultimaUrna = null;

  function pintarUrna(v) {
    ultimaUrna = v;
    const caja = $('urna');
    if (!v || !v.hay) { caja.classList.add('hidden'); return; }
    caja.classList.remove('hidden');

    const formal = v.modo === 'formal';
    // Lo que vota el público es una CONSULTA: no reemplaza la votación del
    // cuerpo, y tiene que decirlo siempre (salvo que el cuerpo la adopte).
    $('urnaModo').textContent = (formal
      ? (v.abierta ? 'Votación · abierta' : 'Votación · cerrada')
      : (v.abierta ? 'Encuesta · abierta' : 'Encuesta · cerrada')) +
      (v.vinculante ? '' : ' · consulta, no vinculante');
    $('urnaModo').className = 'marca' + (v.abierta ? ' aire' : '');
    $('urnaPregunta').textContent = v.pregunta;
    // El codigo solo se pide donde hace falta.
    $('urnaCodigo').classList.toggle('hidden', !formal);

    const cont = $('urnaOpciones');
    cont.textContent = '';
    const puedo = v.abierta && (!formal || (codigo && v.puedeVotar !== false));
    (v.opciones || []).forEach((texto, i) => {
      const b = document.createElement('button');
      b.textContent = texto;
      if (v.miVoto === i) b.classList.add('mio');
      b.disabled = !puedo;
      b.addEventListener('click', () => votarOpcion(i));
      cont.appendChild(b);
    });

    if (formal && v.abierta && !codigo)
      decirUrna('Escribí tu código para poder votar.');
    else if (formal && codigo && v.puedeVotar === false)
      decirUrna('Ese código no figura entre los que pueden votar.', 'mal');
    else if (!v.abierta) decirUrna('La votación está cerrada.');
    else if (v.miVoto >= 0) decirUrna('Tu voto quedó registrado. Podés cambiarlo hasta que cierre.', 'bien');
    else decirUrna('');

    // El resultado solo si el gateway lo mandó: mientras está abierta, verlo
    // cambia lo que vota el que todavía no votó, y eso lo decide el operador.
    const caja2 = $('urnaResultado');
    caja2.textContent = '';
    if (!v.resultado) { caja2.classList.add('hidden'); return; }
    caja2.classList.remove('hidden');
    const total = v.resultado.reduce((a, o) => a + o.votos, 0) || 1;
    v.resultado.forEach((o) => {
      const fila = document.createElement('div');
      fila.className = 'barra-voto';
      const arriba = document.createElement('div');
      arriba.className = 'fila-voto';
      const n = document.createElement('span');
      n.textContent = o.opcion;
      const c = document.createElement('span');
      c.textContent = o.votos + (o.votos === 1 ? ' voto' : ' votos');
      arriba.append(n, c);
      const riel = document.createElement('div');
      riel.className = 'riel';
      const lleno = document.createElement('div');
      lleno.className = 'lleno';
      lleno.style.width = Math.round((o.votos / total) * 100) + '%';
      riel.appendChild(lleno);
      fila.append(arriba, riel);
      caja2.appendChild(fila);
    });
  }

  function decirUrna(texto, clase) {
    $('urnaAviso').textContent = texto;
    $('urnaAviso').className = 'aviso' + (clase ? ' ' + clase : '');
  }

  async function votarOpcion(i) {
    try {
      const r = await fetch('/api/votacion', {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ clave, yo, codigo, opcion: i }),
      });
      const j = await r.json();
      if (j.votacion) pintarUrna(j.votacion);
      if (!j.ok && j.error) decirUrna(j.error, 'mal');
    } catch (_) {
      decirUrna('No se pudo votar: revisá la conexión.', 'mal');
    }
  }

  async function traerUrna() {
    try {
      const r = await fetch('/api/votacion?k=' + encodeURIComponent(clave) +
                            '&yo=' + encodeURIComponent(yo) +
                            '&codigo=' + encodeURIComponent(codigo), { cache: 'no-store' });
      const j = await r.json();
      if (j.votacion) pintarUrna(j.votacion);
    } catch (_) { /* se deja lo que ya está en pantalla */ }
  }

  traerUrna();
  traer();
  // Cada tres segundos: alcanza para ver subir los votos y no es nada de
  // tráfico ni de batería. Cuando la página no se está mirando, no se pide:
  // doscientos teléfonos en el bolsillo no tienen por qué preguntar nada.
  setInterval(() => { if (!document.hidden) { traer(); traerUrna(); } }, 3000);
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden) { traer(); traerUrna(); }
  });
})();
