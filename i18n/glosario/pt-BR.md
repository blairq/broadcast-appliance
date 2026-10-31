# Glosario: español → portugués de Brasil

Los términos del programa, para que toda la traducción diga lo mismo. Portugués de Brasil,
claro y corto, como en una consola de transmisión profesional (la jerga de la TV y el streaming
brasileños). Se trata al usuario de «você» o, mejor, sin sujeto: imperativos cortos («Salve»,
«Aprove», «Escolha a cena»), sin formalidades («o senhor», «por gentileza»). Cuidado con los
falsos amigos del español (equipo ≠ «equipe», expediente ≠ «expediente», subtítulos ≠
«subtítulos»). Ver docs/desarrollo/I18N.md.

| Español | Português (Brasil) | Nota |
|---|---|---|
| al aire / sale al aire | no ar / entra no ar | |
| fuera del aire | fora do ar | |
| transmisión / transmitir | transmissão / transmitir | para YouTube, Facebook…; «live» solo coloquial |
| grabación / grabar | gravação / gravar | |
| escena | cena | |
| fuente | fonte | |
| entrada (de video/audio) | entrada | |
| salida | saída | |
| lienzo | canvas | no «tela»: «tela» es la pantalla |
| vista previa | prévia | alternativa «pré-visualização» (OBS) o «preview» |
| programa (lo que sale) | programa | sigla PGM |
| transición, corte, fundido | transição, corte, fade | alternativa «fusão» |
| rótulo / zócalo | GC | gerador de caracteres; alternativa «tarja». No «legenda»: son los subtítulos |
| placa | cartela | tela fixa («Já voltamos») |
| tarjeta (del dashboard) | cartão | |
| mezclador | mixer | «mesa de som» solo si es el hardware de audio |
| tira (del mezclador) | canal | channel strip |
| silenciar / mudo | silenciar / mudo | |
| vúmetro | medidor | VU |
| calibración | calibração | |
| cámara PTZ, preset | câmera PTZ, preset | |
| superresolución | super-resolução | |
| Conferencia (la videoconferencia del programa) | Conferência | nombre de la función |
| invitado | convidado | |
| lobby / sala de espera | sala de espera | «lobby» también se entiende |
| oradores | oradores | en eventos, «palestrantes» |
| Asamblea / sesión de asamblea | Assembleia / sessão da assembleia | |
| integrante | membro | nunca «parlamentar», «deputado», «vereador» |
| banca | cadeira | |
| presidencia / quien preside | presidência / quem preside | |
| quórum | quórum | |
| orden del día | ordem do dia | alternativa «pauta» |
| votación, afirmativo, negativo, abstención | votação, sim, não, abstenção | |
| acta | ata | |
| padrón | lista de membros | o «lista de presença» según el caso |
| acreditación | credenciamento | |
| tótem (de registro) | totem (de credenciamento) | |
| evento | evento | |
| guion (del evento) | roteiro | en telejornal, «espelho» |
| bloque, cue | bloco, deixa | |
| configuración (carpeta con todo lo de un evento) | configuração | |
| Configuración (la página) | Configurações | en plural, como en todo software en pt-BR |
| botonera | superfície de controle | |
| teleprompter / prompter | teleprompter | sigla TP |
| subtítulos | legendas | falso amigo; «closed caption» si es CC |
| voz a texto / texto a voz | fala para texto / texto para fala | alternativa «transcrição» / «síntese de voz» |
| asistente (de IA) | assistente | |
| herramienta (MCP) | ferramenta | los NOMBRES de herramientas no se traducen |
| modelo local | modelo local | |
| túnel | túnel | |
| aprobar / rechazar | aprovar / rejeitar | |
| aviso | aviso / alerta | según el caso |
| equipo / aparato | dispositivo / equipamento | falso amigo: «equipe» es un grupo de personas |
| pantalla | tela / monitor | |
| pen drive | pen drive | |
| placa de video / «en la placa» | GPU / na GPU | «placa de vídeo» también se entiende |
| placa de audio / de sonido | interface de áudio | |
| central (Bosch DCN y similares) | unidade central | |
| armar / armada (la escena en vista previa) | carregar / carregada | no «deixa»: choca con el cue del guion |
| bloque (bancada de la asamblea) | bancada | no «bloco»: choca con el bloque del guion |
| Cuerpo (el panel de la asamblea) | Colegiado | |
| recinto | plenário | |
| dar / pedir / tener la palabra | conceder / pedir / estar com a palavra | |
| banca virtual | cadeira virtual | |
| remoto (integrante a distancia) | membro remoto | |
| expediente / a comisión | processo / enviado à comissão | falso amigo: «expediente» es otra cosa (horario, parte de la sesión) |
| aprobado / rechazado (votación) | aprovado / rejeitado | |
| libreta (de rostros y personas) | cadastro de rostos | |
| ficha (de una persona) | perfil | |
| ficha (de una herramienta MCP) | ficha de referência | |
| registro (diagnóstico) | log | |
| registro (del tótem) | credenciamento | |
| bitácora (del evento) | diário do evento | |
| superficie (web de control) | superfície | |
| plano general | plano geral | |
| colchón (de latencia) | buffer | |
| de fábrica | padrão | |
| supervisor | watchdog | |
| público (la gente en la sala o remota; la página de preguntas) | público | |
| atención remota / atender (a la persona del tótem) | atendimento remoto / atender | no «assistência»: se confunde con la presencia del quórum |
| guion (de preguntas del tótem) | script de perguntas | el del evento es «roteiro» |
| red (neuronal, de superresolución) | modelo | no «rede»: se confunde con la red de cámaras |
| barata (modo de superresolución) | modo econômico | |
| atril | púlpito | en el plenario, «tribuna» |
| en vivo | ao vivo | |
| señal | sinal | |
| latencia | latência | |
| retorno (monitor de lo que sale) | retorno | |
| multivista | multiview | |

Lo que nunca se traduce: nombres de herramientas MCP y sus parámetros (`scene_switch`),
claves y valores de JSON, las marcas fijas entre corchetes (`[OPERADOR]`,
`[RESPUESTA DEL OPERADOR]`), `%1`…`%9` y `%n`, marcas, siglas y unidades (NDI, SRT, dB).
Las comillas «…» pasan a “…”. Lo que esté entre `backticks` queda igual.

Nombres que el código crea y después busca por nombre (escenas «Asamblea · Orador», fuentes
«Banca con la palabra», «Chat en vivo», «Previa»; valores de enum): si en el texto aparecen
como valor que se pasa a una herramienta, quedan en español; si son lo que ve el operador
como etiqueta, se traducen.
