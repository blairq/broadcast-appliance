## Que se puede automatizar y que no
Crear la app/credencial en Google Cloud, Twitch o Meta lo tiene que hacer la PERSONA (con su cuenta, aceptando los terminos de cada plataforma): vos la guias paso a paso. Todo lo demas lo haces vos.

## Flujo
1. `accounts_status`: que falta en cada plataforma y el paso siguiente (`next`).
2. `account_setup_guide {"platform"}`: los pasos, los VALORES EXACTOS a pegar (`paste`) y los campos del formulario. Explica de a UN paso por vez, en criollo y corto.
3. `account_open {"platform", "step"}` abre la pagina del paso en el navegador. Espera a que la persona diga que termino (o preguntale con `operator_ask`) antes del siguiente.
4. Credencial:
   - YouTube: si ya bajo el JSON de Google, `youtube_setup {"fromDownloads": true}` lo importa solo desde Descargas.
   - Si no (o en Twitch/Facebook): `account_credentials_request {"platform"}` muestra un formulario en la barra; la persona pega los datos en la app y se guardan cifrados. **Nunca le pidas que pegue un secreto en el chat**: el chat pasa por el proveedor de IA.
5. Login: `youtube_login`, `twitch_login` (muestra un codigo: decile que lo confirme en twitch.tv/activate) o `facebook_login` (despues `facebook_pages` para elegir la pagina).
6. `account_check {"platform"}`: prueba real (canal, vivo habilitado, permisos, pagina). Si hay `problem`, explicalo y ayuda a resolverlo.

## Problemas comunes
- YouTube "no tiene habilitadas las transmisiones en vivo": se habilita en youtube.com/features y la primera vez tarda hasta 24 h.
- YouTube pide reconectar cada 7 dias: la app de Google esta en modo prueba; en "Publico" -> "Publicar app" deja de vencer.
- YouTube "app no verificada" al conectar: es normal (la app es de la propia persona): "Configuracion avanzada" -> "Ir a ...".
- Twitch no deja registrar la app: falta la verificacion en dos pasos.
- Facebook "URL bloqueada"/redirect: la URI de redireccion de la app de Meta tiene que ser EXACTAMENTE la de `paste.uriDeRedireccion`.
- Facebook sin paginas: la API transmite a PAGINAS que la persona administra, no al perfil personal.