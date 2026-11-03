# Cuentas de las plataformas y el vivo de cada salida

Cada salida (ver `docs/planes/PLAN-SALIDAS.md`) puede mandar su programa a uno o más
destinos. Un destino es:

- **el vivo de una cuenta**: (plataforma, cuenta, vivo). YouTube, Twitch o Facebook, con
  **varias cuentas por plataforma** (otro canal, otra Página);
- **uno genérico**: RTMP/RTMPS, SRT o RIST con URL y clave.

Dos destinos (de la misma salida o de dos distintas) **no pueden mandar al mismo vivo**:
la plataforma recibiría dos señales en la misma clave y se pisarían. El aparato no lo deja
guardar y dice qué destino y qué salida lo usan.

## Cuentas

- Son **del aparato**: las comparten todos los eventos. La lista (sin secretos) y el token
  de cada cuenta van cifrados en `Credenciales/` (`security/CredentialStore`); nada va en
  texto plano ni en la línea de comandos.
- La app de cada plataforma (el cliente OAuth de Google Cloud, la app de Twitch, la app de
  Meta) es **una sola** para todas las cuentas de esa plataforma. Se configura como antes,
  en la página de la plataforma (`account_setup_guide`).
- La **principal** es la de siempre: la que usa la página de la plataforma (videos, chat,
  subidas). Su token es el de antes (`youtube-token`, `twitch-token`, `facebook-token`):
  una instalación de antes de las cuentas queda con su cuenta como principal, sin migrar
  nada. Cada cuenta más tiene su token aparte (`youtube-token-cuenta-2`…). Los ids no se
  reusan: un destino viejo nunca termina apuntando a otra cuenta.
- Estado de cada cuenta:

  | estado | qué quiere decir | qué hacer |
  |---|---|---|
  | `conectada` | lista para usar | — |
  | `sin_conectar` | nunca se autorizó (o se desconectó a propósito) | Conectar |
  | `conectando` | esperando que la persona autorice en el navegador | autorizar o Cancelar |
  | `vencida` | estuvo conectada y el token ya no sirve (revocado, vencido) | Reautorizar |
  | `sin_permiso` | conectada, pero la plataforma dice que el permiso no alcanza (YouTube 403) | Reautorizar |
  | `sin_app` | falta la app de la plataforma | `account_setup_guide` |

- La principal no se quita (se desconecta). Una cuenta que usa algún destino del evento
  abierto no se quita: primero se quita o se reasigna el destino.

En la interfaz: cada página (YouTube, Twitch, Facebook) tiene arriba la sección
**Cuentas**: el nombre (se edita en el lugar), el canal, el estado, Conectar/Reautorizar,
Desconectar y ✕ (quitar). «+ Cuenta» suma otra.

## Destinos de cada salida

- Son **del evento**: `Config/destinos.json` (cada uno con su `salida`). Uno de una cuenta
  lleva además `plataforma`, `cuenta`, `vivo` (en Twitch vacío: el vivo es el canal) y
  `vivo_titulo`.
- Al asignarlo se le pide a la plataforma la **ingesta** (en YouTube, si el vivo no tiene
  stream, se crea uno y se enlaza) y queda guardado como cualquier destino: la URL en
  `destinos.json` y la clave cifrada como `stream-<id>`. Arrancar, cortar y volver tras un
  corte de luz no necesitan la red para la clave.
- YouTube: un vivo sin inicio automático pasa solo a «en vivo» cuando YouTube ya recibe la
  señal (lo mismo que hace la página).
- Facebook: los vivos creados quedan en prueba (sin publicar) salvo privacidad «public».
- Twitch: no hay vivos aparte; «crear» le pone el título al canal y el destino usa la clave
  del canal.

En la interfaz (página **Salidas**), cada tarjeta lista sus destinos con plataforma ·
cuenta · estado · salud, el bitrate que sale y, si choca con otro, el aviso. «+ Vivo de una
cuenta…» abre el selector (plataforma → cuenta → un vivo programado o uno nuevo con título,
privacidad e inicio); «+ RTMP/SRT…» el de siempre.

### Estado de cada destino

- `estado`: `detenido`, `conectando` (arrancó y todavía no salió nada), `en_vivo`,
  `reintentando` (se cortó y reconecta);
- `kbps`: lo que sale (el mismo dato de la salud de las salidas);
- `salud`: lo que informa la plataforma, pedido cada 30 s **solo** mientras el destino sale
  (cuota de las APIs): en YouTube `estado_stream` (active, ready, inactive, error), `salud`
  (good, ok, bad, noData) y `estado_vivo`; en Twitch y Facebook `estado_vivo` y
  `espectadores`;
- `choque`: si manda al mismo vivo que otro destino (un `destinos.json` de antes o editado a
  mano).

## MCP

| herramienta | clase | para qué |
|---|---|---|
| `cuentas_listar` | lectura | las cuentas (todas o de `plataforma`) con su estado |
| `cuenta_agregar` | escritura | otra cuenta (`conectar: true` abre la autorización) |
| `cuenta_renombrar` | escritura | el nombre que ve el operador |
| `cuenta_quitar` | escritura (destructiva) | revoca, borra el token y la saca |
| `cuenta_conectar` | escritura | conectar o reautorizar (`wait: true` espera) |
| `vivos_listar` | lectura | los vivos programados o activos de una cuenta |
| `vivo_crear` | escritura | uno nuevo; con `salida`, además lo asigna |
| `salida_destino_asignar` | escritura | (plataforma, cuenta, vivo) o URL + clave a una salida |
| `salida_destinos_estado` | lectura | estado, bitrate, salud y choque de cada destino |

Quitar un destino es `stream_destino_borrar`; arrancarlo, `stream_start` con `destino`.
Las de escritura piden aprobación como las demás.

Ejemplos:

```json
// ¿qué cuentas hay?
{"name": "cuentas_listar", "arguments": {"plataforma": "youtube"}}

// otro canal de YouTube, autorizando ya
{"name": "cuenta_agregar", "arguments": {"plataforma": "youtube", "nombre": "Canal en inglés", "conectar": true}}

// sus vivos programados
{"name": "vivos_listar", "arguments": {"plataforma": "youtube", "cuenta": "cuenta-2"}}

// uno nuevo, no listado, asignado a la salida «ingles»
{"name": "vivo_crear", "arguments": {"plataforma": "youtube", "cuenta": "cuenta-2", "titulo": "Sesión en inglés",
                                     "privacidad": "unlisted", "salida": "ingles"}}

// la principal de Twitch como destino de la salida por omisión
{"name": "salida_destino_asignar", "arguments": {"salida": "principal", "plataforma": "twitch"}}

// un SRT genérico para la salida «tv» (la clave va cifrada)
{"name": "salida_destino_asignar", "arguments": {"salida": "tv", "url": "srt://10.0.0.5:9000?mode=caller",
                                                 "clave": "frase-secreta", "nombre": "Canal de TV"}}

// arrancarlo y mirar cómo sale
{"name": "stream_start", "arguments": {"destino": "destino-3"}}
{"name": "salida_destinos_estado", "arguments": {"salida": "ingles"}}
```

Si se pide el mismo vivo para otra salida, `salida_destino_asignar` contesta con error:
«Ese vivo ya lo usa el destino «YouTube · Sesión en inglés» de la salida «Inglés»…».

## Código

- `src/app/vivos/Vivos.*`: el registro de cuentas, los destinos, los choques, el estado y
  el servicio (sin Qt ni red; `tests/test_vivos.cpp` con una plataforma de mentira).
- `src/app/vivos/PlataformasVivo.*`: YouTube, Twitch y Facebook con un cliente por cuenta
  (la principal es el de la página).
- `src/app/ui/VivosViewModel.*`: la vista (`Studio.cuentas.vivos`).
- `src/app/mcp/McpVivosTools.cpp`: las herramientas.
- `ServicioEmision::guardarDestino` rechaza un destino que choca con otro (también los que
  llegan por `stream_destino_agregar` o el diálogo de RTMP/SRT).

## Falta probar a mano

Con cuentas reales (no se hizo): conectar una segunda cuenta de YouTube, Twitch y Facebook;
listar y crear vivos; asignar a dos salidas y transmitir a la vez; ver la salud de YouTube;
una cuenta revocada desde la plataforma (tiene que quedar «vencida»); y el permiso
insuficiente de YouTube («sin permiso»).
