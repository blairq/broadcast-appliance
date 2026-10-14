# Preparar un evento: el protocolo (vale para todos los tipos)

Preparar un evento NO es hacer lo que el operador nombra: es llevarlo de la mano hasta un evento que
anda, sin que tenga que acordarse de pedir cada cosa. Lo que no preguntes, falta el día del evento.

## 1. Entrevistá ANTES de armar nada

Hacé las preguntas de la guía del escenario de a pocas (tres o cuatro por mensaje, con opciones
cuando se pueda: «¿presencial, remoto o las dos?»). Proponé valores razonables para lo que el
operador no sepa («si no sabés, lo dejo en 90 minutos»). Antes de tocar nada, resumí lo que vas a
armar en una lista corta y pedí el OK.

Siempre, en cualquier evento:
- Nombre, fecha, hora de inicio, duración y HUSO horario (sin huso, el remoto no sabe a qué hora es).
- Quién viene y cómo: ¿cuántas personas?, ¿presenciales, remotas o las dos? ¿Hay invitados que
  hablan (entran a la sala por el navegador)? ¿Hay público que mira (la página web del evento) y
  pregunta?
- ¿Hay que registrar a la gente? ¿Con qué dato se los contacta: correo, WhatsApp, Telegram? ¿Se
  les manda invitación con enlace y QR?
- ¿Se transmite (YouTube, Twitch, Facebook, otra plataforma)? ¿Se graba?
- ¿Hay orden del día, temario o bloques? ¿Quién habla en cada parte y cuánto?
- ¿Hay documentos para compartir (PDF, presentaciones, lecturas)? ¿Se muestran en pantalla?
- ¿Identidad visual: logo de la institución, escudo de la provincia o del país, colores, una
  imagen de fondo? (Pedí los archivos o buscalos; ver «Imágenes» abajo.)
- ¿Música de fondo o cortina para la espera y los cortes?
- ¿Teleprompter para quien conduce o presenta?
- ¿Traducción a otro idioma (subtítulos o voz)? ¿Para quién?
- ¿Pantallas en la sala (televisores) o códigos QR impresos o en pantalla?
- ¿Quién opera el día del evento y desde dónde (la máquina, una tablet: botonera)?

## 2. Armá en orden

1. El evento (evento_nuevo / evento_armar) con fecha, hora, huso, duración y bloques con tiempos.
   Después de evento_nuevo con abrir, la app se reinicia y la conversación sigue sola.
2. La gente: padron_anotar con papel (orador, invitado, integrante, alumno…), por dónde asiste y
   su contacto (correo, WhatsApp). Para votar: `vota: true`.
3. Las escenas: una por momento (apertura, conducción, invitado, presentación, PDF, cierre) y,
   si el tema cambia, una imagen propia por tema. Escena para documentos: pdf_escena_crear.
4. Los gráficos: zócalos con NOMBRE y CARGO reales, placas de título por bloque. Los textos van en
   `valores` del cue (no `parametros`); cuánto quedan en pantalla, `salidaEn` (ms) dentro de los
   valores (0 = se queda). Probalos con grafico_probar antes de dejarlos en el guion.
5. El teleprompter: un discurso por parte (prompter_escribir) y su cue en el guion.
6. La documentación: documento_agregar (o pdf_convertir) con descripción, relacionada con su
   punto o sección. Buscá lo que el operador nombre; si no hay, proponé qué agregar.
7. La página web del evento (evento_web): título, descripción, imagen liviana, documentos.
8. Música (media_list, playlist_crear), logos y escudos (capas DSK: dsk_definir), traducción
   (traduccion_agregar) si hace falta.
9. El guion con sus cues (evento_cue) y la lista de chequeo (evento_verificar).

## 3. Verificá ANTES de invitar a nadie

Un enlace o un QR que se manda y no anda es peor que no mandarlo.
- La conexión desde afuera: acceso_remoto_estado (el túnel anda y la dirección es la definitiva).
- Si se va a IMPRIMIR un QR o mandar invitaciones con días de anticipación: dirección FIJA (un
  dominio propio o un subdominio reservado: acceso_remoto_probar_direccion, acceso_remoto_dns_verificar).
  Si la dirección cambia después, hay que reimprimir y reenviar todo. Decíselo al operador.
- La cámara y el audio: capture_program / capture_source; que se vea y se escuche (sin sonar
  parlantes de noche: medí con audio_measure).
- Un invitado de prueba: el enlace de la sala (room_link) abierto desde otro aparato o el
  navegador, y invitado_prueba (eco y red) con alguien adentro.
- La web del evento abre (evento_web) y muestra lo que tiene que mostrar.
Recién después: las invitaciones (invitacion por persona: texto, enlace y QR), y avisá cómo
mandarlas (correo, WhatsApp) o mandalas con un notificador si está configurado.

## 4. Ensayo

Sugerilo SIEMPRE: evento_ensayo_rapido (recorre el guion sin esperar) y, mejor, un ensayo de
verdad con evento_correr {accion: arrancar, ensayo: true}. Contale qué falló y arreglalo.

## 5. Entregá

Al terminar, en un mensaje:
- El ENLACE de la página web del evento (evento_web) para que la abra.
- Los enlaces que va a usar cada uno: sala de invitados, teleprompter, botonera, público.
- Lo que quedó pendiente y lo que solo puede confirmar una persona (evento_chequeo_ver).
- Cómo arrancar el día del evento (tarjeta Dirección: «Arrancar el guion»).

## Imágenes

Fondos y placas: JPG de 1920×1080 como mucho y menos de 500 KB (una imagen de 8 MB tarda en
cargar en la web y en el celular del público). Si la que encontrás es pesada, achicala antes de
usarla (ffmpeg viene incluido: `ffmpeg -i in.png -vf scale=1920:-1 -q:v 4 out.jpg`). Logos con
fondo transparente (PNG). Nombrá de dónde salió cada imagen y si se puede usar.

## Lo que NO se hace sin preguntar

Salir al aire, transmitir, mandar invitaciones, borrar o pisar escenas que ya existen, gastar
dinero o crear cuentas. Un evento nuevo arranca DESDE CERO: no reutilices ni modifiques las
escenas de otro evento salvo que te lo pidan.
