El procedimiento estándar para una **sesión de asamblea** (concejo, consejo, directorio, consorcio): integrantes en bancas, quórum, orden del día, votaciones y, si hay, integrantes a distancia. Una sesión **no es un guion de cues**: la palabra cambia sin orden previo y la maneja la fila de oradores. Seguilo en orden, **proponé antes de hacer** y no inventes datos. Para el detalle de cada pieza: help asamblea, conferencia y sesion.

**0. Lo que hay que preguntar:** nombre del cuerpo y de la sesión; fecha, hora y **huso**; la **lista de integrantes** con banca, nombre, cargo y bloque (mejor una planilla); quiénes **votan**; la **regla del quórum** y la mayoría; el **orden del día** (puntos y expedientes); si hay **central de conferencia** (micrófonos de las bancas) o se trabaja sin ella; si hay **integrantes a distancia**; dónde se transmite y si se graba; si hay himno, minuto de silencio u otros momentos fijos.

**1. Configurar:**
| Qué | Con qué |
|---|---|
| El evento como asamblea y su guion (solo lo que tiene orden: Previa, apertura, cierre) | `evento_armar {nombre, fecha, inicio, huso, asamblea: true, gente, bloques}` |
| El Cuerpo: cada integrante con su banca, cargo y bloque (validados) | `cuerpo_importar` (planilla), `integrante_editar`, `integrante_validar` |
| El recinto: la grilla de bancas | `recinto_editar {filas, columnas, auto: true}` |
| La toma de cada banca | `ptz_posicion_crear` y `banca_toma {banca, camara, posicion}`; probar con `enfocar {que: "banca N"}` |
| LAS ESCENAS de bancas, plano general, remoto con la palabra y debate | `asamblea_escenas_base` (mostrá la propuesta; `crear: true`) |
| La central de conferencia (si hay) | `dcn_conectar`; cada banca con su unidad (help conferencia) |
| Los integrantes a distancia (si hay) | `sesion_credencial {banca}` (link y PIN), `sesion_acceso_opciones`; la consola de la presidencia: `sesion_consola {accion: crear}` |
| El padrón con quién vota | `evento_armar {gente: [{…, vota: true}]}` o `padron_anotar` |
| El quórum | `quorum_regla` |
| El orden del día | `orden_del_dia_cargar` |
| Los gráficos de asamblea: zócalo de integrante, punto del orden del día, quórum, resultado de votación, cuarto intermedio | se vinculan solos a lo validado (help asamblea, `grafico_listar` categoría Asamblea) |
| Adónde se transmite y la grabación | como en un evento en salón (help procedimiento_salon, paso 1) |
| La página web del evento (muestra la asamblea, el recinto con las bancas por bloque, el quórum y las votaciones) | `evento_web {accion: generar}` |
| El principio y el final: señal de ajuste, placa de espera, placa de fin | `evento_preparar` |

**2. Controlar:** `asamblea_revisar` (cuerpo, recinto, presets, escenas, central, banca virtual, orden del día) → `evento_revisar` → `evento_verificar {tipo: asamblea}`. Arreglá cada «falta» y repetí hasta que no falte nada.

**3. Pedirle a la persona que controle:** la lista «a_mano» de `evento_verificar`: además de lo general (cámaras, sonido, gráficos, página, grabación, transmisión de prueba), la **central probada banca por banca**, la **toma de cada banca**, una **votación de prueba**, los **remotos con su link y PIN** (uno probado), la **consola de la presidencia** y **quién toma el acta**. Marcá con `evento_chequeo` solo lo que te confirmen.

**4. Ensayar:**
- **Cada banca:** darle la palabra (`banca_al_aire`) y terminarla (`recinto_orador_terminar`): ¿se mueve la cámara, se arma la escena, sale el zócalo con el nombre correcto, abre el micrófono?
- **Un remoto:** que entre con su link y PIN, habilitarlo, darle la palabra.
- **Votación de prueba:** con la central o con la banca virtual (`sesion_votacion`); el resultado oficial (`votacion_resultado_oficial`) en pantalla.
- **Quórum:** `quorum_presentes` con los presentes.
- Y lo de todo evento: música, gráficos, página web, grabación y transmisión de prueba (help procedimiento_salon, paso 4).

**5. Dejarlo listo:** `evento_verificar {tipo: asamblea}` sin faltas y con todo confirmado; `evento_preparar {al_aire: true}` deja la señal de ajuste en el programa. Avisale a la persona.

**6. El día, de la señal de ajuste al final:**
1. **30 a 15 minutos antes:** `evento_iniciar`; señal de ajuste; `stream_start` y `record_start`; confirmar en la plataforma.
2. **Antes de empezar:** placa de espera con música; los remotos entran y se habilitan; `quorum_presentes`.
3. **Apertura:** con quórum, el primer bloque (himno, apertura de la presidencia).
4. **La sesión:** la fila de oradores (`banca_cola`, `banca_al_aire`, `recinto_orador_terminar`); el punto en tratamiento (`orden_del_dia_punto`); votaciones y su resultado oficial; cuarto intermedio con su placa si hace falta. El registro va solo al acta (`sesion_registro`).
5. **Cierre:** el bloque Cierre (placa de fin); `stream_stop`, `record_stop`, `evento_terminar` y `evento_informe`.