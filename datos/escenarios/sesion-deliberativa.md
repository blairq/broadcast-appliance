# Sesión de un cuerpo deliberativo

## Para qué
Una sesión formal con integrantes en sus bancas, presidencia, secretaría, orden del día, uso de la
palabra reglamentado, quórum y votaciones nominales con resultado oficial. Pública y transmitida.
Lee además help {"topic": "asamblea"} y el procedimiento de sesión de asamblea.

## Preguntas propias
- ¿Qué cuerpo, cuántas bancas, quién preside, quién es secretario/a?
- ¿Está cargado el Cuerpo (integrantes, bloques, bancas)? Si no: cuerpo_importar o cuerpo_plantilla.
- ¿Hay central de conferencia (micrófonos de banca, p. ej. Bosch DCN)? ¿Cámaras PTZ con posición
  por banca?
- ¿Integrantes remotos (banca virtual)? ¿Votan a distancia?
- El orden del día y los expedientes. ¿Mayorías especiales en algún punto?
- ¿Tiempos de uso de la palabra? ¿Cuestiones de privilegio, réplicas?
- ¿Transmisión, intérprete de lengua de señas (capa DSK), subtítulos?
- ¿Televisores en el recinto para mostrar la votación?

## Qué armar
- Cuerpo y recinto (recinto_editar) con cada banca y su preset de cámara (ptz_posicion_crear).
- Escenas base de asamblea (asamblea_escenas_base): presidencia, banca con la palabra, plano
  general, votación, cuarto intermedio; revisá con asamblea_revisar.
- Orden del día (orden_del_dia_cargar) con documentos por punto.
- Quórum (quorum_regla) y votación (votacion_abrir / votacion_resultado_oficial).
- Si hay remotos: sesion_habilitar, credenciales (sesion_credencial) y prueba de cada uno.
- Pantallas del recinto con la votación (pantalla_mostrar contenido votacion).
- Botonera de presidencia o de micrófonos si se opera desde tablet (plantillas «Asamblea»,
  «Micrófonos de bancas»).

## Verificar
Central conectada (dcn_conectar), cámaras a sus posiciones, quórum con el padrón real, un remoto
votando de prueba, la web pública con el orden del día.

## Entregar
Credenciales para remotos, la web pública, la botonera de presidencia; al final, el acta con las
votaciones (evento_acta) y la grabación con capítulos por punto (evento_capitulos).
