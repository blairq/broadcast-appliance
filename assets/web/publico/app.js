// La página del público: preguntar y votar. Nada más, a propósito.
//
// No entra a la sala, no pide cámara ni micrófono y no abre WebRTC. En un salón
// con doscientas personas eso sería un acople garantizado y el ancho de banda
// tirado; acá alcanza con HTTP.
//
// LO QUE SE MIRA ES IGUAL PARA TODOS. Cada 3 s se pide /api/publico/estado: la urna y
// las preguntas sin nada de este teléfono, así ba-web lo reparte de su microcaché y a la
// sala le llega un pedido por segundo y no doscientos. Lo de cada uno lo sabe el
// teléfono: qué preguntó y qué votó (en el navegador) y, de la urna, su voto. Para saber
// si una suya está en la cola, la sala manda marcas (Preguntas::marcaDe) que solo este
// teléfono reconoce. La clave va en una cabecera (X-BA-Clave), nunca en la dirección.
(async () => {
  'use strict';
  const llenar = (s, ...v) => BAi18n.llenar(s, ...v);
  // Los textos: el idioma del teléfono (o el que elija arriba); si no hay, el del evento.
  await BAi18n.iniciar({ contexto: 'web/publico', preferir: 'navegador' });

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

  // Lo que preguntó y votó este teléfono: la sala no lo dice en lo que ven todos.
  const leerLista = (k) => { try { return JSON.parse(localStorage.getItem(k) || '[]'); } catch (_) { return []; } };
  const guardarLista = (k, v) => { try { localStorage.setItem(k, JSON.stringify(v.slice(-100))); } catch (_) {} };
  let mias = leerLista('ba-publico-mias');        // [{id, texto}] las que mandé
  let votadas = leerLista('ba-publico-votadas');  // [id] las que voté
  const conClave = (h) => Object.assign({ 'X-BA-Clave': clave }, h || {});
  // La misma marca que Preguntas::marcaDe (FNV-1a de 32 bits de «id:yo»).
  function marca(id) {
    let h = 0x811c9dc5;
    for (const b of new TextEncoder().encode(id + ':' + yo)) { h ^= b; h = Math.imul(h, 0x01000193) >>> 0; }
    return h.toString(16).padStart(8, '0');
  }

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
      const m = { nueva: t('Esperando'), elegida: t('En la cola'), al_aire: t('Al aire ahora'),
                  respondida: t('Respondida'), descartada: t('No se va a preguntar') };
      const marca = document.createElement('span');
      marca.className = 'marca' + (q.estado === 'al_aire' ? ' aire'
                                 : q.estado === 'elegida' ? ' elegida' : '');
      marca.textContent = m[q.estado] || q.estado;
      pie.appendChild(marca);
    } else {
      pie.textContent = q.nombre || t('Anónimo');
      if (q.alAire) {
        const marca = document.createElement('span');
        marca.className = 'marca aire';
        marca.textContent = ' · ' + t('Al aire ahora');
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

  let ultimas = null; // las últimas preguntas (para volver a pintar si cambia el idioma)
  // Lo que ven todos ({todas, enCola}) -> {mias, otras} como las pinta la página.
  function separar(c) {
    const propias = new Set(mias.map((q) => q.id).concat(votadas));
    const enCola = new Set(c.enCola || []);
    const out = { mias: [], otras: [] };
    const vistas = new Set();
    for (const q of c.todas || []) {
      vistas.add(q.id);
      if (!propias.has(q.id)) { out.otras.push(q); continue; }
      const estado = q.alAire ? 'al_aire' : q.respondida ? 'respondida' : enCola.has(marca(q.id)) ? 'elegida' : 'nueva';
      out.mias.push(Object.assign({}, q, { estado }));
    }
    // Una que mandé y ya no está: no se va a preguntar (salvo que la acabe de mandar: lo que
    // ven todos puede tener hasta un segundo).
    for (const q of mias)
      if (!vistas.has(q.id))
        out.mias.push({ id: q.id, texto: q.texto, votos: 1, estado: Date.now() - (q.ms || 0) < 10000 ? 'nueva' : 'descartada' });
    return out;
  }
  function pintar(p) {
    ultimas = p;
    const mias = $('mias'), otras = $('otras');
    mias.textContent = '';
    otras.textContent = '';
    (p.mias || []).forEach((q) => mias.appendChild(fila(q, true)));
    (p.otras || []).forEach((q) => otras.appendChild(fila(q, false)));
    $('seccionMias').classList.toggle('hidden', !(p.mias || []).length);
    $('seccionOtras').classList.toggle('hidden', !(p.otras || []).length);
  }

  // --- Hablar con el gateway ------------------------------------------------
  let lectura = '';     // la que da /api/publico-info (deja leer lo que ven todos)
  let comun = null;     // lo último que se vio: {votacion, preguntas: {todas, enCola}}
  async function traer() {
    if (!lectura) { infoDelEvento(); return; }
    try {
      const r = await fetch('/api/publico/estado?l=' + encodeURIComponent(lectura), { cache: 'no-store' });
      if (r.status === 403) { lectura = ''; estado.textContent = t('Clave rechazada'); return; }
      comun = await r.json();
      pintar(separar(comun.preguntas || {}));
      urnaComun(comun.votacion);
      estado.textContent = '';
    } catch (_) {
      // Sin conexión no se borra lo que ya está en pantalla: el que está
      // leyendo una pregunta no tiene por qué perderla porque se cayó el wifi
      // un segundo.
      estado.textContent = t('Sin conexión');
    }
  }
  const repintarPreguntas = () => { if (comun) pintar(separar(comun.preguntas || {})); };

  async function mandar(cuerpo) {
    const r = await fetch('/api/preguntas', {
      method: 'POST',
      headers: conClave({ 'content-type': 'application/json' }),
      body: JSON.stringify(Object.assign({ yo }, cuerpo)),
    });
    return r.json();
  }

  async function votar(id, boton) {
    boton.disabled = true;
    try {
      const j = await mandar({ votar: id });
      if (!votadas.includes(id)) { votadas.push(id); guardarLista('ba-publico-votadas', votadas); }
      // Hasta la próxima vuelta, el voto sumado acá.
      if (j.ok && comun) for (const q of (comun.preguntas || {}).todas || []) if (q.id === id) q.votos++;
      repintarPreguntas();
    } catch (_) {
      boton.disabled = false;
    }
  }

  $('mandar').addEventListener('click', async () => {
    const texto = $('texto').value.trim();
    if (!texto) { decir(t('Escribí la pregunta primero.'), 'mal'); return; }
    const b = $('mandar');
    b.disabled = true;
    decir(t('Mandando…'));
    try {
      const j = await mandar({ texto, nombre: $('nombre').value.trim() });
      if (!j.ok) { decir(textoDeError(j) || t('No se pudo mandar.'), 'mal'); return; }
      if (j.id) { mias.push({ id: j.id, texto, ms: Date.now() }); guardarLista('ba-publico-mias', mias); }
      $('texto').value = '';
      decir(t('Mandada. Vas a ver acá si la eligen.'), 'bien');
      repintarPreguntas();
    } catch (_) {
      decir(t('No se pudo mandar: revisá la conexión.'), 'mal');
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
  // También da la lectura para lo que ven todos (ver arriba).
  const infoDelEvento = () => fetch('/api/publico-info', { cache: 'no-store', headers: conClave() })
    .then((r) => {
      if (r.status === 403) estado.textContent = t('Clave rechazada');
      return r.ok ? r.json() : null;
    })
    .then(async (j) => {
      if (!j) return;
      if (j.lectura && !lectura) { lectura = j.lectura; traer(); }
      // El idioma del evento, si la app lo manda: vale cuando el del teléfono no está.
      if (j.idioma) { await BAi18n.iniciar({ evento: j.idioma }); repintar(); }
      if (j.evento) $('evento').textContent = j.evento;
      if (j.cuando) $('cuando').textContent = j.cuando;
      if (j.fondo) $('fondo').style.backgroundImage = 'url("' + j.fondo + '")';
    })
    .catch(() => {});
  infoDelEvento();

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
        headers: conClave({ 'content-type': 'application/json' }),
        body: JSON.stringify({ codigo: codigoDelQr }),
      }).then(() => {
        try { localStorage.setItem('ba-publico-acreditado', codigoDelQr); } catch (_) {}
        estado.textContent = t('Llegada registrada');
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
    puedeDe = '';
    urnaComun(urnaVista);
  });

  let ultimaUrna = null;
  // La urna como la ven todos + lo de este teléfono: su voto (lo recuerda acá, por la
  // votación: `desdeMs`) y, en una formal, si su código puede votar (se le pregunta a la
  // sala una vez por votación y código, no en cada vuelta).
  let urnaVista = null;
  let puedeDe = '', puede = undefined;
  const leerVoto = () => { try { return JSON.parse(localStorage.getItem('ba-publico-voto') || 'null'); } catch (_) { return null; } };
  function urnaComun(v) {
    urnaVista = v;
    if (!v || !v.hay) { pintarUrna(v); return; }
    const mio = leerVoto();
    const propia = Object.assign({}, v, { miVoto: mio && mio.desde === v.desdeMs ? mio.opcion : -1 });
    if (v.modo === 'formal' && codigo) {
      const de = v.desdeMs + ':' + codigo;
      if (puedeDe !== de) { puedeDe = de; puede = undefined; preguntarCodigo(); }
      if (puede !== undefined) propia.puedeVotar = puede;
    }
    pintarUrna(propia);
  }
  async function preguntarCodigo() {
    try {
      const r = await fetch('/api/votacion?yo=' + encodeURIComponent(yo),
                            { cache: 'no-store', headers: conClave({ 'X-BA-Codigo': codigo }) });
      const j = await r.json();
      if (!j.votacion) return;
      puede = j.votacion.puedeVotar;
      if (j.votacion.miVoto >= 0) recordarVoto(j.votacion);
      urnaComun(urnaVista);
    } catch (_) { puedeDe = ''; }
  }
  function recordarVoto(v) {
    try { localStorage.setItem('ba-publico-voto', JSON.stringify({ desde: v.desdeMs, opcion: v.miVoto })); } catch (_) {}
  }

  function pintarUrna(v) {
    ultimaUrna = v;
    const caja = $('urna');
    if (!v || !v.hay) { caja.classList.add('hidden'); return; }
    caja.classList.remove('hidden');

    const formal = v.modo === 'formal';
    // Lo que vota el público es una CONSULTA: no reemplaza la votación del
    // cuerpo, y tiene que decirlo siempre (salvo que el cuerpo la adopte).
    $('urnaModo').textContent = (formal
      ? (v.abierta ? t('Votación · abierta') : t('Votación · cerrada'))
      : (v.abierta ? t('Encuesta · abierta') : t('Encuesta · cerrada'))) +
      (v.vinculante ? '' : ' · ' + t('consulta, no vinculante'));
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
      decirUrna(t('Escribí tu código para poder votar.'));
    else if (formal && codigo && v.puedeVotar === false)
      decirUrna(t('Ese código no figura entre los que pueden votar.'), 'mal');
    else if (!v.abierta) decirUrna(t('La votación está cerrada.'));
    else if (v.miVoto >= 0) decirUrna(t('Tu voto quedó registrado. Podés cambiarlo hasta que cierre.'), 'bien');
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
      c.textContent = t('%n voto', { n: o.votos });
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

  // Los errores de la sala por su código estable (la frase puede cambiar; el código no).
  const ERRORES = {
    cerrada: 'la votación no está abierta',
    opcion: 'esa opción no existe',
    sin_codigo: 'falta el código: en una votación formal hay que identificarse',
    codigo_invalido: 'ese código no figura entre los que pueden votar',
    sin_telefono: 'no sé de qué teléfono viene',
    sellado: 'tu voto ya está contado',
    vacia: 'la pregunta está vacía',
  };
  function textoDeError(j) { return t(ERRORES[j.error_codigo] || j.error || ''); }

  function decirUrna(texto, clase) {
    $('urnaAviso').textContent = texto;
    $('urnaAviso').className = 'aviso' + (clase ? ' ' + clase : '');
  }

  async function votarOpcion(i) {
    try {
      const r = await fetch('/api/votacion', {
        method: 'POST',
        headers: conClave({ 'content-type': 'application/json' }),
        body: JSON.stringify({ yo, codigo, opcion: i }),
      });
      const j = await r.json();
      if (j.votacion) {
        if (j.ok) recordarVoto(j.votacion); // «ya voté», en el teléfono
        pintarUrna(j.votacion);
      }
      if (!j.ok && j.error) decirUrna(textoDeError(j), 'mal');
    } catch (_) {
      decirUrna(t('No se pudo votar: revisá la conexión.'), 'mal');
    }
  }

  // --- El idioma ---------------------------------------------------------------
  // Un selector chico arriba: los idiomas que hay; al cambiar, se vuelve a pintar.
  function armarIdiomas() {
    const sel = $('idioma');
    const lista = BAi18n.idiomas;
    sel.classList.toggle('hidden', lista.length < 2);
    sel.textContent = '';
    for (const p of lista) {
      const o = document.createElement('option');
      o.value = p.codigo;
      o.textContent = p.nombre;
      sel.appendChild(o);
    }
    sel.value = BAi18n.idioma;
  }
  function repintar() {
    armarIdiomas();
    if (ultimas) pintar(ultimas);
    pintarUrna(ultimaUrna);
  }
  $('idioma').addEventListener('change', async (e) => {
    await BAi18n.elegir(e.target.value);
    repintar();
  });
  armarIdiomas();

  // Cada tres segundos lo que ven todos (del microcaché: ver arriba). Cuando la
  // página no se está mirando, no se pide: doscientos teléfonos en el bolsillo no
  // tienen por qué preguntar nada.
  setInterval(() => { if (!document.hidden) traer(); }, 3000);
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden) traer();
  });
})();
