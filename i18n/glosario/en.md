# Glosario: español → inglés

Los términos del programa, para que toda la traducción diga lo mismo. Inglés de Estados Unidos,
claro y corto, como en una consola de transmisión profesional. Se tutea («you»), sin
formalidades. Ver docs/desarrollo/I18N.md.

| Español | English | Nota |
|---|---|---|
| al aire / sale al aire | on air / goes on air | |
| fuera del aire | off air | |
| transmisión / transmitir | stream / streaming | a YouTube, Facebook… |
| grabación / grabar | recording / record | |
| escena | scene | |
| fuente | source | |
| entrada (de video/audio) | input | |
| salida | output | |
| lienzo | canvas | |
| vista previa | preview | |
| programa (lo que sale) | program | |
| transición, corte, fundido | transition, cut, fade | |
| rótulo / zócalo | lower third | |
| placa | slate | pantalla fija («Volvemos enseguida») |
| tarjeta (del dashboard) | card | |
| mezclador | mixer | |
| tira (del mezclador) | strip | |
| silenciar / mudo | mute / muted | |
| vúmetro | meter | |
| calibración | calibration | |
| cámara PTZ, preset | PTZ camera, preset | |
| superresolución | super-resolution | |
| Conferencia (la videoconferencia del programa) | Conference | nombre de la función |
| invitado | guest | |
| lobby / sala de espera | lobby | |
| oradores | speakers | |
| Asamblea / sesión de asamblea | Assembly / assembly session | |
| integrante | member | nunca «legislator» |
| banca | seat | |
| presidencia / quien preside | chair | |
| quórum | quorum | |
| orden del día | agenda | |
| votación, afirmativo, negativo, abstención | vote, yes, no, abstain | |
| acta | minutes | |
| padrón | roll | lista de integrantes o asistentes |
| acreditación | check-in | |
| tótem (de registro) | kiosk (check-in kiosk) | |
| evento | event | |
| guion (del evento) | rundown | |
| bloque, cue | block, cue | |
| configuración (carpeta con todo lo de un evento) | configuration | |
| Configuración (la página) | Settings | |
| botonera | control surface | |
| teleprompter / prompter | teleprompter | |
| subtítulos | captions | |
| voz a texto / texto a voz | speech to text / text to speech | |
| asistente (de IA) | assistant | |
| herramienta (MCP) | tool | los NOMBRES de herramientas no se traducen |
| modelo local | local model | |
| túnel | tunnel | |
| aprobar / rechazar | approve / reject | |
| aviso | notice / alert | según el caso |
| equipo / aparato | device | |
| pantalla | screen / display | |
| pen drive | USB drive | |
| placa de video / «en la placa» | GPU / on the GPU | |
| placa de audio / de sonido | audio interface | |
| central (Bosch DCN y similares) | central unit | |
| armar / armada (la escena en vista previa) | load / loaded | no «cue»: choca con el cue del guion |
| bloque (bancada de la asamblea) | caucus | no «block»: choca con el bloque del guion |
| Cuerpo (el panel de la asamblea) | Body | |
| recinto | chamber | |
| dar / pedir / tener la palabra | give / request / have the floor | |
| banca virtual | virtual seat | |
| remoto (integrante a distancia) | remote member | |
| expediente / a comisión | file / sent to committee | |
| aprobado / rechazado (votación) | passed / rejected | |
| libreta (de rostros y personas) | face registry | |
| ficha (de una persona) | profile | |
| ficha (de una herramienta MCP) | reference card | |
| registro (diagnóstico) | log | |
| registro (del tótem) | check-in | |
| bitácora (del evento) | log | |
| superficie (web de control) | surface | |
| plano general | wide shot | |
| colchón (de latencia) | buffer | |
| de fábrica | default | |
| supervisor | watchdog | |
| público (la gente en la sala o remota; la página de preguntas) | audience | |
| atención remota / atender (a la persona del tótem) | remote support / assist | no «attendance»: es la asistencia del quórum |
| guion (de preguntas del tótem) | script | el del evento es «rundown» |
| red (neuronal, de superresolución) | network → «model» | no confundir con la red de cámaras |
| barata (modo de superresolución) | cheap mode | |
| atril | lectern | |

Lo que nunca se traduce: nombres de herramientas MCP y sus parámetros (`scene_switch`),
claves y valores de JSON, las marcas fijas entre corchetes (`[OPERADOR]`,
`[RESPUESTA DEL OPERADOR]`), `%1`…`%9` y `%n`, marcas, siglas y unidades (NDI, SRT, dB).
Las comillas «…» pasan a “…”. Lo que esté entre `backticks` queda igual.

Nombres que el código crea y después busca por nombre (escenas «Asamblea · Orador», fuentes
«Banca con la palabra», «Chat en vivo», «Previa»; valores de enum): si en el texto aparecen
como valor que se pasa a una herramienta, quedan en español; si son lo que ve el operador
como etiqueta, se traducen.
