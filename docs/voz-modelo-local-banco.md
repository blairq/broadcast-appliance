# Banco de voz: el detalle por herramienta

Corrida completa v5 (84 herramientas × 3 frases, con el aparato andando), generada con `tools/voz-banco-informe.py build/voz-banco-v5.json`. Resumen y conclusiones en [voz-modelo-local.md](voz-modelo-local.md).

| | frases | % |
|---|---:|---:|
| OK_EJECUTADA | 75 | 30 % |
| OK_SIN_EJEC | 61 | 24 % |
| NO_EXISTE | 62 | 25 % |
| EJEC_ERROR | 27 | 11 % |
| NO_QUEDO | 0 | 0 % |
| FALTA_DATO | 13 | 5 % |
| FORMATO | 9 | 4 % |
| ELIGE_MAL | 5 | 2 % |

**Elección de herramienta:** 247 de 252 (98 %).
**Argumentos:** mediana 2451 ms, p90 3305 ms (191 frases con argumentos).

## Por herramienta

| herramienta | frases | resultado |
|---|---:|---|
| `audio_set` | 3 | OK_EJECUTADA ×3 |
| `audio_measure` | 3 | EJEC_ERROR ×3 |
| `audio_analyze` | 3 | OK_EJECUTADA ×3 |
| `audio_escuchar` | 3 | NO_EXISTE ×2, OK_EJECUTADA ×1 |
| `audio_sincro` | 3 | OK_SIN_EJEC ×2, NO_EXISTE ×1 |
| `mixer_list` | 3 | OK_EJECUTADA ×3 |
| `scene_switch` | 3 | OK_EJECUTADA ×2, NO_EXISTE ×1 |
| `scene_preview` | 3 | FALTA_DATO ×1, NO_EXISTE ×1, OK_EJECUTADA ×1 |
| `scene_take` | 3 | OK_EJECUTADA ×3 |
| `scene_list` | 3 | OK_EJECUTADA ×3 |
| `scene_create` | 3 | OK_SIN_EJEC ×3 |
| `transition_set` | 3 | OK_EJECUTADA ×3 |
| `source_list` | 3 | OK_EJECUTADA ×3 |
| `source_add` | 3 | OK_SIN_EJEC ×2, FALTA_DATO ×1 |
| `source_update` | 3 | OK_SIN_EJEC ×2, NO_EXISTE ×1 |
| `source_remove` | 3 | OK_SIN_EJEC ×2, NO_EXISTE ×1 |
| `capture_program` | 3 | OK_EJECUTADA ×2, FORMATO ×1 |
| `capture_source` | 3 | OK_EJECUTADA ×3 |
| `capture_feed` | 3 | EJEC_ERROR ×3 |
| `room_status` | 3 | OK_EJECUTADA ×3 |
| `participant_admit` | 3 | ELIGE_MAL ×3 |
| `participant_reject` | 3 | NO_EXISTE ×3 |
| `participant_kick` | 3 | NO_EXISTE ×3 |
| `participant_request` | 3 | NO_EXISTE ×3 |
| `chat_send` | 3 | OK_SIN_EJEC ×2, NO_EXISTE ×1 |
| `chat_history` | 3 | OK_EJECUTADA ×3 |
| `room_link` | 3 | EJEC_ERROR ×3 |
| `speaker_next` | 3 | EJEC_ERROR ×3 |
| `speakers_status` | 3 | OK_EJECUTADA ×3 |
| `speaker_time` | 3 | NO_EXISTE ×3 |
| `speaker_on_air` | 3 | NO_EXISTE ×3 |
| `record_start` | 3 | OK_SIN_EJEC ×3 |
| `record_stop` | 3 | OK_SIN_EJEC ×3 |
| `stream_start` | 3 | OK_SIN_EJEC ×3 |
| `stream_stop` | 3 | OK_SIN_EJEC ×3 |
| `status` | 3 | OK_EJECUTADA ×3 |
| `network_status` | 3 | OK_EJECUTADA ×3 |
| `share_list` | 3 | OK_EJECUTADA ×3 |
| `capture_multiview` | 3 | OK_EJECUTADA ×3 |
| `capture_people` | 3 | OK_EJECUTADA ×3 |
| `capture_person` | 3 | NO_EXISTE ×3 |
| `capture_sequence` | 3 | FORMATO ×2, NO_EXISTE ×1 |
| `capture_ui` | 3 | FORMATO ×3 |
| `chat_wait` | 3 | NO_EXISTE ×2, OK_SIN_EJEC ×1 |
| `gateway_restart` | 3 | OK_SIN_EJEC ×3 |
| `grabacion_individual` | 3 | OK_SIN_EJEC ×3 |
| `guest_source_add` | 3 | OK_SIN_EJEC ×3 |
| `imagen_analizar` | 3 | OK_EJECUTADA ×2, ELIGE_MAL ×1 |
| `participant_admit_all` | 3 | OK_SIN_EJEC ×3 |
| `participant_profile` | 3 | NO_EXISTE ×2, FALTA_DATO ×1 |
| `participant_requeue` | 3 | NO_EXISTE ×3 |
| `participant_talk` | 3 | NO_EXISTE ×3 |
| `room_pin` | 3 | OK_SIN_EJEC ×3 |
| `room_set` | 3 | OK_SIN_EJEC ×3 |
| `scene_copy` | 3 | OK_SIN_EJEC ×2, NO_EXISTE ×1 |
| `scene_define` | 3 | FORMATO ×3 |
| `scene_delete` | 3 | OK_SIN_EJEC ×2, NO_EXISTE ×1 |
| `scene_export` | 3 | NO_EXISTE ×2, OK_EJECUTADA ×1 |
| `scene_folder` | 3 | EJEC_ERROR ×2, NO_EXISTE ×1 |
| `scene_move` | 3 | EJEC_ERROR ×3 |
| `scene_protect` | 3 | EJEC_ERROR ×2, NO_EXISTE ×1 |
| `scene_rename` | 3 | OK_SIN_EJEC ×2, NO_EXISTE ×1 |
| `share_delete` | 3 | FALTA_DATO ×1, NO_EXISTE ×1, OK_SIN_EJEC ×1 |
| `share_take` | 3 | FALTA_DATO ×1, OK_SIN_EJEC ×1, NO_EXISTE ×1 |
| `share_view` | 3 | FALTA_DATO ×2, EJEC_ERROR ×1 |
| `source_properties` | 3 | FALTA_DATO ×3 |
| `source_set_property` | 3 | OK_SIN_EJEC ×2, NO_EXISTE ×1 |
| `source_types` | 3 | OK_EJECUTADA ×3 |
| `speaker_move` | 3 | NO_EXISTE ×2, FALTA_DATO ×1 |
| `speaker_presenter` | 3 | NO_EXISTE ×3 |
| `speaker_queue` | 3 | NO_EXISTE ×3 |
| `speakers_config` | 3 | OK_SIN_EJEC ×3 |
| `grafico_mostrar` | 3 | NO_EXISTE ×2, FALTA_DATO ×1 |
| `grafico_ocultar` | 3 | EJEC_ERROR ×2, ELIGE_MAL ×1 |
| `grafico_listar` | 3 | OK_EJECUTADA ×3 |
| `grafico_en_escena` | 3 | OK_EJECUTADA ×3 |
| `rotulo_vincular` | 3 | NO_EXISTE ×2, OK_SIN_EJEC ×1 |
| `rotulo_vinculos` | 3 | OK_EJECUTADA ×3 |
| `rotulo_desvincular` | 3 | OK_SIN_EJEC ×2, NO_EXISTE ×1 |
| `laminas_ir` | 3 | EJEC_ERROR ×2, FALTA_DATO ×1 |
| `laminas_buscar` | 3 | EJEC_ERROR ×3 |
| `laminas_ver` | 3 | OK_EJECUTADA ×3 |
| `laminas_listar` | 3 | OK_EJECUTADA ×3 |
| `laminas_aplicar` | 3 | NO_EXISTE ×2, OK_SIN_EJEC ×1 |

## Los casos que no salieron

### `audio_measure`

- «No sale nada de audio al aire, medí la cadena con señal de prueba» → **EJEC_ERROR**: error: 'Micrófono de MacBook Pro' no tiene rack. Argumentos: `{"source": "Micrófono de MacBook Pro", "tipo": "curva"}`
- «Se oye un pitido raro, revisá la cadena de audio ya» → **EJEC_ERROR**: error: 'Micrófono de MacBook Pro' no tiene rack. Argumentos: `{"source": "Micrófono de MacBook Pro", "tipo": "respuesta"}`
- «hay un pitido, medí los módulos de la cadena con un seno de prueba» → **EJEC_ERROR**: error: 'Micrófono de MacBook Pro' no tiene rack. Argumentos: `{"source": "Micrófono de MacBook Pro", "tipo": "respuesta", "nivel_db": -20}`

### `audio_escuchar`

- «Quiero escuchar en auriculares la consola antes de meterla» → **NO_EXISTE**: bloqueado: no existe: Consola. Argumentos: `{"escuchar": true, "name": "Consola"}`
- «Pasame a escucha el canal del teléfono, que lo oigo por monitor» → **NO_EXISTE**: bloqueado: no existe: Teléfono. Argumentos: `{"escuchar": true, "name": "Teléfono"}`

### `audio_sincro`

- «El audio de la cámara dos va adelantado a la imagen, atrasalo» → **NO_EXISTE**: bloqueado: no existe: Cámara dos. Argumentos: `{"name": "Cámara dos", "ms": -6.0}`

### `scene_switch`

- «reemplaza lo que sale por el plano dos» → **NO_EXISTE**: bloqueado: no existe: Plano dos. Argumentos: `{"name": "Plano dos"}`

### `scene_preview`

- «no la saques todavia, solo dejala lista» → **FALTA_DATO**: bloqueado: falta requerido: name. Argumentos: `{}`
- «armala en el bus de preview, no al programa» → **NO_EXISTE**: bloqueado: no existe: scene_take. Argumentos: `{"name": "scene_take"}`

### `source_add`

- «Sumá al invitado 2 como fuente en la escena Entrevista, a la izquierda» → **FALTA_DATO**: bloqueado: falta requerido: type. Argumentos: `{"name": "Invitado 2", "scene": "Entrevista", "layout": "left"}`

### `source_update`

- «Poné la cámara 3 adelante de todo en el orden de la escena Mesa.» → **NO_EXISTE**: bloqueado: no existe: Mesa. Argumentos: `{"name": "Cámara", "order": 3, "scene": "Mesa"}`

### `source_remove`

- «Sacá la fuente Reloj de la escena Noticiero, en las otras que quede.» → **NO_EXISTE**: bloqueado: no existe: Reloj; no existe: Noticiero. Argumentos: `{"name": "Reloj", "scene": "Noticiero"}`

### `capture_program`

- «Pasame la imagen del aire en jpeg, a ver si quedó en negro» → **FORMATO**: bloqueado: no es JSON. Argumentos: `null`

### `capture_feed`

- «traeme el cuadro de guest 2, quiero ver si llega video» → **EJEC_ERROR**: error: Sin imagen en 'guest:N' (nadie publica ese video ahora). feeds_list muestra los validos.. Argumentos: `{"key": "guest:N", "format": "jpeg"}`
- «¿Cómo me ve el lobby a mí? Dame el cuadro de operador» → **EJEC_ERROR**: error: Sin imagen en 'operator' (nadie publica ese video ahora). feeds_list muestra los validos.. Argumentos: `{"key": "operator", "format": "jpeg"}`
- «Dame el cuadro de guest 3, quiero ver qué manda su cámara» → **EJEC_ERROR**: error: Sin imagen en 'guest:3' (nadie publica ese video ahora). feeds_list muestra los validos.. Argumentos: `{"key": "guest:3", "format": "jpeg"}`

### `participant_admit`

- «Entrá al invitado 3 del lobby a la sala» → **ELIGE_MAL**: eligió `participant_admit_all`. Argumentos: `{}`
- «El que dice Ramiro en el lobby, dejalo pasar» → **ELIGE_MAL**: eligió `participant_admit_all`. Argumentos: `{}`
- «admitilo a Federico, el resto que espere» → **ELIGE_MAL**: eligió `participant_admit_all`. Argumentos: `{}`

### `participant_reject`

- «No admitas a Tomás, que no pase del lobby» → **NO_EXISTE**: bloqueado: no existe: Tomás. Argumentos: `{"participant": "Tomás"}`
- «Negale el ingreso a Gustavo, hoy no sale» → **NO_EXISTE**: bloqueado: no existe: Gustavo. Argumentos: `{"participant": "Gustavo"}`
- «al que espera en el lobby no lo admitas, rechazalo» → **NO_EXISTE**: bloqueado: no existe: participant. Argumentos: `{"participant": "participant"}`

### `participant_kick`

- «Echá a Juan de la sala, está haciendo lío» → **NO_EXISTE**: bloqueado: no existe: Juan. Argumentos: `{"participant": "Juan"}`
- «Afuera Pedro, que se vaya de la sala» → **NO_EXISTE**: bloqueado: no existe: Pedro. Argumentos: `{"participant": "Pedro"}`
- «Sacá de la sala a Acosta, se terminó su participación» → **NO_EXISTE**: bloqueado: no existe: Acosta. Argumentos: `{"participant": "Acosta"}`

### `participant_request`

- «pedile que desmutee el micro» → **NO_EXISTE**: bloqueado: no existe: invitado 2. Argumentos: `{"participant": "invitado 2", "what": "unmute-mic"}`
- «pedile al invitado que mutee el micrófono, se le acopla» → **NO_EXISTE**: bloqueado: no existe: el invitado. Argumentos: `{"participant": "el invitado", "what": "mute-mic"}`
- «pedile a Nico que apague la cámara» → **NO_EXISTE**: bloqueado: no existe: Nico. Argumentos: `{"participant": "Nico", "what": "mute-cam"}`

### `chat_send`

- «mandale por chat a Carla: tenés la luz de espaldas» → **NO_EXISTE**: bloqueado: no existe: Carla. Argumentos: `{"text": "tenés la luz de espaldas", "to": "Carla"}`

### `room_link`

- «enlace de la sala por red local, con su QR» → **EJEC_ERROR**: error: El gateway solo escucha en esta maquina: elegi 'Red local' en Configuracion > Gateway > Acceso.. Argumentos: `{"board": false, "target": "local", "withPin": false}`
- «pasame el link para que entre el invitado, con QR» → **EJEC_ERROR**: error: El gateway solo escucha en esta maquina: elegi 'Red local' en Configuracion > Gateway > Acceso.. Argumentos: `{"board": false, "target": "local", "withPin": false}`
- «El invitado está fuera del estudio, pasame el link remoto con PIN» → **EJEC_ERROR**: error: El gateway solo escucha en esta maquina: elegi 'Red local' en Configuracion > Gateway > Acceso.. Argumentos: `{"board": false, "withPin": true}`

### `speaker_next`

- «Ya fue, dale al siguiente» → **EJEC_ERROR**: error: La sala respondio: no hay nadie mas en la fila. Argumentos: `{}`
- «Rotá al próximo orador de la lista de espera» → **EJEC_ERROR**: error: La sala respondio: no hay nadie mas en la fila. Argumentos: `{}`
- «Siguiente orador, ya» → **EJEC_ERROR**: error: La sala respondio: no hay nadie mas en la fila. Argumentos: `{}`

### `speaker_time`

- «Achicale a Laura el tiempo a 1:30» → **NO_EXISTE**: bloqueado: no existe: Laura. Argumentos: `{"participant": "Laura", "time": "1:30"}`
- «ponele dos minutos más de tiempo para hablar a Pérez» → **NO_EXISTE**: bloqueado: no existe: Pérez. Argumentos: `{"participant": "Pérez", "time": "+2:00"}`
- «Sumale un minuto al tiempo de Herrera, que está hablando» → **NO_EXISTE**: bloqueado: no existe: Herrera. Argumentos: `{"participant": "Herrera", "time": "+1:00"}`

### `speaker_on_air`

- «poné al aire a Gómez y arrancá su reloj» → **NO_EXISTE**: bloqueado: no existe: Gómez. Argumentos: `{"participant": "Gómez", "on": true}`
- «sacá del aire a Gómez, terminó su turno» → **NO_EXISTE**: bloqueado: no existe: Gómez. Argumentos: `{"participant": "Gómez", "on": false}`
- «poné al aire a ese orador, no a la escena» → **NO_EXISTE**: bloqueado: no existe: orador. Argumentos: `{"on": true, "participant": "orador"}`

### `capture_person`

- «Pasame la imagen de la invitada Carla, nada más ella» → **NO_EXISTE**: bloqueado: no existe: Carla. Argumentos: `{"participant": "Carla", "format": "jpeg", "includeScreen": false, "maxWidth": 64, "quality": 85}`
- «dejame ver a Graciela antes de admitirla, su cámara en vivo» → **NO_EXISTE**: bloqueado: no existe: Graciela. Argumentos: `{"participant": "Graciela", "includeScreen": false, "format": "jpeg", "quality": 85, "maxWidth": 64}`
- «El invitado Ramiro dice que tiene luz, sacame una imagen de él» → **NO_EXISTE**: bloqueado: no existe: Ramiro. Argumentos: `{"participant": "Ramiro", "format": "jpeg", "includeScreen": false, "maxWidth": 64, "quality": 85}`

### `capture_sequence`

- «capturá cuadro a cuadro la transición entre escenas» → **FORMATO**: bloqueado: no es JSON. Argumentos: `null`
- «sacale a Laura doce cuadros cada 250 ms, a ver si está congelada» → **NO_EXISTE**: bloqueado: no existe: Laura. Argumentos: `{"frames": 12, "intervalMs": 250, "target": "Laura"}`
- «grabame una secuencia de lo que sale» → **FORMATO**: bloqueado: no es JSON. Argumentos: `null`

### `capture_ui`

- «capturá la app entera, window all, a 1280 de ancho» → **FORMATO**: bloqueado: no es JSON. Argumentos: `null`
- «pantallazo de todas las ventanas de nuestra aplicación, window all» → **FORMATO**: bloqueado: no es JSON. Argumentos: `null`
- «screenshot de nuestra interfaz, la ventana principal nada más» → **FORMATO**: bloqueado: no es JSON. Argumentos: `null`

### `chat_wait`

- «Esperá la respuesta escrita del oyente, máximo 120 segundos» → **NO_EXISTE**: bloqueado: no existe: oyente. Argumentos: `{"participant": "oyente", "timeoutSec": 120}`
- «esperá que Marta conteste en el chat, dale 60 segundos» → **NO_EXISTE**: bloqueado: no existe: Marta. Argumentos: `{"participant": "Marta", "timeoutSec": 60}`

### `imagen_analizar`

- «Mirá la toma armada y contame cuántas personas se ven» → **ELIGE_MAL**: eligió `derivar`. Argumentos: `null`

### `participant_profile`

- «cambiale los datos de contacto a Juan, puso mal el mail» → **NO_EXISTE**: bloqueado: no existe: Juan. Argumentos: `{"contact": "juan@correo.com", "info": "correo mal puesto", "participant": "Juan"}`
- «cambiá la información del invitado Ruiz: concejal de Quilmes» → **NO_EXISTE**: bloqueado: no existe: invitado Ruiz. Argumentos: `{"info": "concejal de Quilmes", "participant": "invitado Ruiz"}`
- «en el tablero figura sin info, agregale que es intendente» → **FALTA_DATO**: bloqueado: falta requerido: participant. Argumentos: `{"info": "intendente"}`

### `participant_requeue`

- «Molina ya usó su turno, devolvelo al fondo» → **NO_EXISTE**: bloqueado: no existe: Molina. Argumentos: `{"participant": "Molina"}`
- «Ese ya habló, ponelo otra vez al final» → **NO_EXISTE**: bloqueado: no existe: Orador que ya hablo. Argumentos: `{"participant": "Orador que ya hablo"}`
- «Ya terminó su turno, que se ponga de nuevo último» → **NO_EXISTE**: bloqueado: no existe: Orador que ya hablo. Argumentos: `{"participant": "Orador que ya hablo"}`

### `participant_talk`

- «hablo yo en privado con Micaela antes de que pase» → **NO_EXISTE**: bloqueado: no existe: Micaela. Argumentos: `{"participant": "Micaela", "on": true}`
- «hablemos por privado con Gustavo mientras espera» → **NO_EXISTE**: bloqueado: no existe: Gustavo. Argumentos: `{"participant": "Gustavo", "on": true}`
- «necesito hablar con Camila mientras espera, en privado» → **NO_EXISTE**: bloqueado: no existe: Camila. Argumentos: `{"participant": "Camila", "on": true}`

### `scene_copy`

- «hacele un clon a la del invitado» → **NO_EXISTE**: bloqueado: no existe: Invitado. Argumentos: `{"from": "Invitado", "to": "Invitado"}`

### `scene_define`

- «armame una escena entera con la camara y el logo» → **FORMATO**: bloqueado: no es JSON. Argumentos: `null`
- «definí la escena Entrevista de una: fondo abajo, dos cámaras arriba» → **FORMATO**: bloqueado: no es JSON. Argumentos: `null`
- «Armame entera la escena Noticias: fondo, cámara y logo, de una» → **FORMATO**: bloqueado: no es JSON. Argumentos: `null`

### `scene_delete`

- «Eliminá la escena Split 3, sobra» → **NO_EXISTE**: bloqueado: no existe: Split 3. Argumentos: `{"name": "Split 3"}`

### `scene_export`

- «export de escenas completo, todas» → **NO_EXISTE**: bloqueado: no existe: . Argumentos: `{"name": ""}`
- «exportá todas las escenas en el formato de scene_define» → **NO_EXISTE**: bloqueado: no existe: . Argumentos: `{"name": ""}`

### `scene_folder`

- «Hay mil escenas en la lista, poné las de invitados en su carpeta» → **NO_EXISTE**: bloqueado: no existe: invitados. Argumentos: `{"folder": "invitados", "scene": "invitados"}`
- «La escena Móvil Rosario, a la carpeta Exteriores» → **EJEC_ERROR**: error: falló la llamada: timed out. Argumentos: `{"scene": "Móvil", "folder": "Exteriores"}`
- «Sacá la escena Apertura de su carpeta en la lista de escenas» → **EJEC_ERROR**: error: falló la llamada: timed out. Argumentos: `{"scene": "Apertura", "folder": ""}`

### `scene_move`

- «subi esa escena en la lista» → **EJEC_ERROR**: error: falló la llamada: timed out. Argumentos: `{"scene": "Entrevista", "before": "Panel"}`
- «la de cierre va última en la lista de escenas» → **EJEC_ERROR**: error: falló la llamada: timed out. Argumentos: `{"scene": "Cierre", "before": ""}`
- «mové la escena del clima debajo del noticiero» → **EJEC_ERROR**: error: falló la llamada: timed out. Argumentos: `{"before": "Noticias", "scene": "Clima"}`

### `scene_protect`

- «protegé esa escena contra borrado y renombre» → **EJEC_ERROR**: error: falló la llamada: timed out. Argumentos: `{"protected": true, "scene": "Principal"}`
- «Desprotegé la escena Móvil, ya no hace falta» → **EJEC_ERROR**: error: falló la llamada: timed out. Argumentos: `{"protected": false, "scene": "Móvil"}`
- «Candado a la escena Sponsors, que no la borren» → **NO_EXISTE**: bloqueado: no existe: Sponsors. Argumentos: `{"protected": true, "scene": "Sponsors"}`

### `scene_rename`

- «renombrá la escena Escena 2, llamala Móvil» → **NO_EXISTE**: bloqueado: no existe: Escena 2. Argumentos: `{"from": "Escena 2", "to": "Móvil"}`

### `share_delete`

- «Volá ese archivo compartido, no lo vamos a usar» → **FALTA_DATO**: bloqueado: falta requerido: id. Argumentos: `{}`
- «eliminá ese archivo compartido, es cualquier cosa» → **NO_EXISTE**: bloqueado: no existe: any. Argumentos: `{"id": "any"}`

### `share_take`

- «metele a la escena el archivo compartido» → **FALTA_DATO**: bloqueado: falta requerido: id. Argumentos: `{}`
- «Ese clip compartido del oyente, incorporalo a la escena al aire» → **NO_EXISTE**: bloqueado: no existe: clip. Argumentos: `{"id": "clip"}`

### `share_view`

- «qué es eso que mandaron, mostrámelo» → **FALTA_DATO**: bloqueado: falta requerido: id. Argumentos: `{}`
- «previsualizame el archivo compartido» → **FALTA_DATO**: bloqueado: falta requerido: id. Argumentos: `{}`
- «El invitado compartió una foto, revisala antes de ponerla» → **EJEC_ERROR**: error: No existe ese archivo (share_list).. Argumentos: `{"id": "Invitado 1"}`

### `source_properties`

- «¿Qué opciones tiene la fuente navegador en el panel de propiedades?» → **FALTA_DATO**: bloqueado: falta requerido: name. Argumentos: `{}`
- «¿Qué propiedades expone la placa de captura? Nombre y tipo» → **FALTA_DATO**: bloqueado: falta requerido: name. Argumentos: `{}`
- «qué propiedades y rangos tiene la fuente Cámara 2» → **FALTA_DATO**: bloqueado: falta requerido: name. Argumentos: `{}`

### `source_set_property`

- «cambiale la propiedad resolución a la fuente Atril, ponela en 1080» → **NO_EXISTE**: bloqueado: no existe: Atril. Argumentos: `{"property": "resolución", "source": "Atril", "value": "1080"}`

### `speaker_move`

- «bajá a Tomás al último puesto de la fila» → **NO_EXISTE**: bloqueado: no existe: Tomás. Argumentos: `{"participant": "Tomás", "position": -1.0}`
- «subi a Marta en la fila» → **FALTA_DATO**: bloqueado: falta requerido: participant; falta requerido: position. Argumentos: `{}`
- «cambiale el orden a los oradores» → **NO_EXISTE**: bloqueado: no existe: oradores. Argumentos: `{"participant": "oradores", "position": 1}`

### `speaker_presenter`

- «Que Carla quede de presentadora, fija al costado» → **NO_EXISTE**: bloqueado: no existe: Carla. Argumentos: `{"participant": "Carla", "on": true}`
- «Presentador: Leandro, con su lugar propio» → **NO_EXISTE**: bloqueado: no existe: Leandro. Argumentos: `{"participant": "Leandro", "on": true}`
- «Ya no es presentador Hugo, desmarcalo» → **NO_EXISTE**: bloqueado: no existe: Hugo. Argumentos: `{"participant": "Hugo", "on": false}`

### `speaker_queue`

- «Agregá al participante 12 a la fila de los que van a hablar» → **NO_EXISTE**: bloqueado: no existe: 12. Argumentos: `{"participant": "12"}`
- «sumá a la concejala a la cola de oradores» → **NO_EXISTE**: bloqueado: no existe: concejala. Argumentos: `{"participant": "concejala"}`
- «encolá al 22 para el próximo turno» → **NO_EXISTE**: bloqueado: no existe: 22. Argumentos: `{"participant": "22"}`

### `grafico_mostrar`

- «Tirá el gráfico del clima, que ya estamos en programa» → **NO_EXISTE**: bloqueado: no existe: grafico_clima. Argumentos: `{"nombre": "grafico_clima", "fuente": 0, "duracionMs": 1500, "fps": 30, "repetir": false}`
- «dibujá al aire el gráfico del zócalo con nombre y cargo de Juan» → **FALTA_DATO**: bloqueado: falta requerido: fuente. Argumentos: `{"nombre": "zocalo", "parametros": {"nombre": "Juan"}}`
- «Dispará ya el gráfico de votos, que lo vea la audiencia» → **NO_EXISTE**: bloqueado: no existe: grafico_en_escena. Argumentos: `{"duracionMs": 1500, "fps": 30, "fuente": 0, "nombre": "grafico_en_escena", "parametros": {"grafico": "graficos"}, "repetir": false}`

### `grafico_ocultar`

- «Sigue clavado el gráfico de encuestas al aire, bajalo.» → **ELIGE_MAL**: eligió `grafico_mostrar`. Argumentos: `{"duracionMs": 1500, "fps": 30, "fuente": 0, "nombre": "grafico_en_escena"}`
- «El gráfico de la encuesta ya cumplió, dejalo transparente» → **EJEC_ERROR**: error: esa fuente de gráfico ya no está. Argumentos: `{"fuente": 1}`
- «fuera ese gráfico, vaciá la fuente del animado» → **EJEC_ERROR**: error: esa fuente de gráfico ya no está. Argumentos: `{"fuente": 1}`

### `rotulo_vincular`

- «atá el nombre al que está hablando» → **NO_EXISTE**: bloqueado: no existe: grafico; falta requerido: fuente. Argumentos: `{"dispara": true, "grafico": "grafico", "valores": {"nombre": "origenes"}}`
- «atá el rótulo de la fuente Nombres al que habla, sin disparar» → **NO_EXISTE**: bloqueado: no existe: Nombres; no existe: grafico; falta requerido: valores. Argumentos: `{"dispara": false, "fuente": "Nombres", "grafico": "grafico"}`

### `rotulo_desvincular`

- «que el rótulo de Gómez deje de escribirse solo, borrale el vínculo» → **NO_EXISTE**: bloqueado: no existe: Gómez. Argumentos: `{"id": "Gómez", "activo": false}`

### `laminas_ir`

- «Poné la diapositiva siete del informe» → **EJEC_ERROR**: error: Ese carrusel no tiene una lamina 7. laminas_ver dice cuantas hay.. Argumentos: `{"carrusel": "informe", "numero": 7}`
- «tirá la lámina que sigue en el carrusel» → **EJEC_ERROR**: error: Deci `numero` o `hacia` (siguiente/anterior).. Argumentos: `{"carrusel": "siguiente"}`
- «El conductor pide la lámina cinco, saltá ya» → **FALTA_DATO**: bloqueado: falta requerido: carrusel. Argumentos: `{"numero": 5}`

### `laminas_buscar`

- «Buscame por el texto «resultados» una lámina del carrusel electoral» → **EJEC_ERROR**: error: No hay ningun carrusel llamado «electoral». laminas_listar los da.. Argumentos: `{"carrusel": "electoral", "texto": "resultados"}`
- «encontrame la del mapa» → **EJEC_ERROR**: error: No hay ningun carrusel llamado «grafico barras». laminas_listar los da.. Argumentos: `{"carrusel": "grafico barras", "texto": "mapa"}`
- «Filtrá por el texto 'desempleo' en el carrusel Informe» → **EJEC_ERROR**: error: No hay ningun carrusel llamado «Informe». laminas_listar los da.. Argumentos: `{"carrusel": "Informe", "texto": "desempleo"}`

### `laminas_aplicar`

- «Arrancamos el segmento de deportes, instalá el carrusel Goles en la escena Central.» → **NO_EXISTE**: bloqueado: no existe: Central. Argumentos: `{"carrusel": "Goles", "escena": "Central"}`
- «Poné el carrusel Deportes en la escena Dúplex, que tenga su fuente» → **NO_EXISTE**: bloqueado: no existe: Dúplex. Argumentos: `{"carrusel": "Deportes", "escena": "Dúplex"}`

