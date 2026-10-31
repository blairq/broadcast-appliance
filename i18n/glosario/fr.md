# Glosario: español → francés

Los términos del programa, para que toda la traducción diga lo mismo. Francés de Francia,
claro y corto, como en una consola de régie profesional. Se trata de usted («vous»), sin
floreos; los botones y comandos van en infinitivo («Enregistrer», «Passer à l'antenne»), los
mensajes al operador en imperativo con «vous» («Vérifiez la connexion»). Vocabulario de la
televisión francesa (régie, antenne, synthé). Ver docs/desarrollo/I18N.md.

| Español | Français | Nota |
|---|---|---|
| al aire / sale al aire | à l'antenne / passe à l'antenne | botón corto: «ANTENNE» |
| fuera del aire | hors antenne | |
| transmisión / transmitir | diffusion en direct / diffuser | «stream / streamer» se entiende, pero en la UI «diffusion»; a YouTube, Facebook… |
| grabación / grabar | enregistrement / enregistrer | |
| escena | scène | |
| fuente | source | |
| entrada (de video/audio) | entrée | |
| salida | sortie | |
| lienzo | canevas | |
| vista previa | preview | en régie se dice «preview» (PVW); «aperçu» solo en textos largos |
| programa (lo que sale) | programme | PGM |
| transición, corte, fundido | transition, cut, fondu | «cut» es lo usual en régie; «coupe franche» en textos largos |
| rótulo / zócalo | synthé | lo más usado en tele francesa; también «bandeau», «incrustation», «lower third» |
| placa | carton | pantalla fija («Nous revenons tout de suite»); también «slate» |
| tarjeta (del dashboard) | carte | |
| mezclador | console de mixage | corto: «mixeur»; para video sería «mélangeur» |
| tira (del mezclador) | tranche | |
| silenciar / mudo | couper le son / muet | botón: «MUTE» o «Muet» |
| vúmetro | vumètre | |
| calibración | calibrage | |
| cámara PTZ, preset | caméra PTZ, preset | «position mémorisée» en textos largos |
| superresolución | super-résolution | |
| Conferencia (la videoconferencia del programa) | Conférence | nombre de la función |
| invitado | invité | |
| lobby / sala de espera | salle d'attente | |
| oradores | intervenants | |
| Asamblea / sesión de asamblea | Assemblée / séance de l'assemblée | |
| integrante | membre | nunca «député» ni «parlementaire» |
| banca | siège | |
| presidencia / quien preside | présidence / président de séance | |
| quórum | quorum | |
| orden del día | ordre du jour | |
| votación, afirmativo, negativo, abstención | vote, pour, contre, abstention | |
| acta | procès-verbal | |
| padrón | liste des membres | lista de integrantes o asistentes; «registre» si es de asistencia |
| acreditación | accréditation | |
| tótem (de registro) | borne (borne d'accueil) | |
| evento | événement | |
| guion (del evento) | conducteur | el término de régie francés |
| bloque, cue | séquence, top | «top» es el disparo en régie; «cue» se entiende |
| configuración (carpeta con todo lo de un evento) | configuration | |
| Configuración (la página) | Paramètres | |
| botonera | surface de contrôle | |
| teleprompter / prompter | prompteur | |
| subtítulos | sous-titres | |
| voz a texto / texto a voz | transcription vocale / synthèse vocale | |
| asistente (de IA) | assistant | |
| herramienta (MCP) | outil | los NOMBRES de herramientas no se traducen |
| modelo local | modèle local | |
| túnel | tunnel | |
| aprobar / rechazar | approuver / refuser | |
| aviso | avertissement / alerte | según el caso |
| equipo / aparato | appareil | |
| pantalla | écran | |
| pen drive | clé USB | |
| placa de video / «en la placa» | GPU / sur le GPU | |
| placa de audio / de sonido | interface audio | |
| central (Bosch DCN y similares) | unité centrale | |
| armar / armada (la escena en vista previa) | préparer / prête | no «top»: choca con el top del conducteur |
| bloque (bancada de la asamblea) | groupe | no «séquence»: choca con la del conducteur |
| Cuerpo (el panel de la asamblea) | Assemblée plénière | corto: «Plénière» |
| recinto | hémicycle | «salle des séances» si no es semicircular |
| dar / pedir / tener la palabra | donner / demander / avoir la parole | |
| banca virtual | siège virtuel | |
| remoto (integrante a distancia) | membre à distance | |
| expediente / a comisión | dossier / renvoyé en commission | |
| aprobado / rechazado (votación) | adopté / rejeté | |
| libreta (de rostros y personas) | registre des visages | |
| ficha (de una persona) | fiche | |
| ficha (de una herramienta MCP) | fiche de référence | |
| registro (diagnóstico) | journal | |
| registro (del tótem) | enregistrement à l'accueil | corto: «accueil» |
| bitácora (del evento) | journal de l'événement | |
| superficie (web de control) | surface | |
| plano general | plan large | |
| colchón (de latencia) | tampon | |
| de fábrica | par défaut | |
| supervisor | chien de garde | en textos técnicos se acepta «watchdog» |
| público (la gente en la sala o remota; la página de preguntas) | public | |
| atención remota / atender (a la persona del tótem) | assistance à distance / prendre en charge | no «présence»: es la asistencia del quórum |
| guion (de preguntas del tótem) | script | el del evento es «conducteur» |
| red (neuronal, de superresolución) | modèle | no «réseau»: se confunde con la red de cámaras |
| barata (modo de superresolución) | mode économique | corto: «éco» |
| atril | pupitre | |
| en vivo / en directo | en direct | |
| sala de control / control | régie | |
| multivista | multiview | también «mosaïque» |
| retorno (monitoreo) | retour | |
| intercomunicador | intercom | |

Lo que nunca se traduce: nombres de herramientas MCP y sus parámetros (`scene_switch`),
claves y valores de JSON, las marcas fijas entre corchetes (`[OPERADOR]`,
`[RESPUESTA DEL OPERADOR]`), `%1`…`%9` y `%n`, marcas, siglas y unidades (NDI, SRT, dB).
Las comillas «…» quedan «…» con espacio fino insecable por dentro (« … »), y antes de
`: ; ! ?` va espacio insecable, como manda la tipografía francesa. Lo que esté entre
`backticks` queda igual.

Nombres que el código crea y después busca por nombre (escenas «Asamblea · Orador», fuentes
«Banca con la palabra», «Chat en vivo», «Previa»; valores de enum): si en el texto aparecen
como valor que se pasa a una herramienta, quedan en español; si son lo que ve el operador
como etiqueta, se traducen.
