# Plan: las direcciones de los servicios (una sola fuente, MVC)

**Problema (relevado el 2026-10-15).** Las URL que se muestran, se mandan a los
aparatos o van en un QR se arman en ~13 lugares con 9 algoritmos para «la
dirección local» y 4 para «la de afuera». No coinciden ni en IP, ni en esquema,
ni en puerto. Resultado: 127.0.0.1 donde hace falta la IP de la red, el dominio
del túnel para gente que está en la sala, links muertos con la sala en una sola
IP, IP viejas después de cambiar de red.

Lista de lo relevado (todo se reemplaza):

| Dónde | Para qué |
|---|---|
| gateway `hostLocal`, `bestUrl`, `joinUrl_`, `/network`, `cast-link`, `correccion-link` | base del gateway, `room_status`, los `*-link` |
| `StudioViewModel::joinUrl` | tarjeta de invitados, Servicios |
| `EnlacesViewModel` (filas armadas a mano) | Servicios, `enlaces_listar` |
| `StudioViewModel::urlApiRemota` | Companion |
| `BotoneraGateway::baseRedLocal` | QR de botonera y control web |
| `Presidencia::refrescarOrigen` | integrantes, presidencia |
| `AccesoRemoto::conexion` / enlaces | links de afuera, `acceso_remoto_estado` |
| `PanelEventoWeb.qml` | web del evento |
| `PantallasViewModel` (`puertoWeb` fijo 8787) | televisores |
| `EventoViewModel::enlaceDeCue` | vista previa del teleprompter |
| `QrViewModel` | QR guardados |

## Reglas

1. **Una fuente de verdad: el gateway.** Es el que sabe dónde escucha, qué
   puertas abrió y si el túnel está listo de verdad. Calcula el catálogo de
   direcciones **de nuevo** cuando algo cambia (IP de la máquina —se relee cada
   10 s—, túnel, HTTPS, ajustes) y avisa con el evento `direcciones`.
2. **MVC en la app.** Modelo `sala::Direcciones` (lo que dijo el gateway, con
   `Q_PROPERTY … NOTIFY`); los view models (Servicios, Pantallas, Presidencia,
   Botonera, QR, Evento…) le piden la URL de un servicio; el QML se enlaza a
   propiedades/roles, nunca arma URLs ni llama a un `Q_INVOKABLE` para leer
   estado. El MCP lee el mismo modelo.
3. **Nunca 127.0.0.1 como enlace para otro.** Si un servicio no llega desde la
   red, la dirección local es `null` con el **motivo** («la sala de invitados
   escucha solo en esta máquina: entran por el túnel»). Lo que es para ESTA
   máquina (la vista previa del teleprompter) se rotula «en esta máquina» y no
   se ofrece para copiar/QR.
4. **Local primero; el túnel solo como «desde afuera».** Cada servicio tiene
   `local` y `remoto` por separado; la interfaz muestra los dos rotulados. Los
   de la sala (integrantes, presidencia, tablets) reciben el local; el remoto
   es otro botón/QR explícito.
5. **El remoto solo si está listo de verdad** (pinggy listo; Cloudflare con
   cloudflared andando, no «hay un nombre guardado»; WireGuard arriba) y solo
   para los servicios que lo atienden por el túnel (la botonera nunca).

## La puerta de la sala

Los servicios de la sala (tablets, presidencia, integrantes, televisores,
teleprompter, rótulos, corrección, tablero, público, Companion) son de la red
local por naturaleza: tienen que llegar aunque la **sala de invitados** esté
cerrada a la red. El gateway abre SIEMPRE su puerta (en la IP elegida en
«Acceso», o en todas si está en «Solo esta máquina» / «Red local»):

- **HTTPS 8792** (páginas con cámara, micrófono, portapapeles…),
- **HTTP 8793** (televisores y Companion: no aceptan un certificado propio).

Por la puerta se atiende todo **menos entrar a la sala como invitado**
(`/invitados/`, `/api/join`, subidas de invitados). «Acceso (dónde escucha)»
pasa a decidir solo si los INVITADOS entran por la red local. 8790 es el MCP de
la app y 8791 el login de Facebook.

## El catálogo (`GET /api/v1/direcciones`, evento `direcciones`)

```json
{ "seq": 12,
  "local":  { "ip": "192.168.1.42", "ips": ["192.168.1.42"], "https": "https://192.168.1.42:8792",
              "http": "http://192.168.1.42:8793" } | null,
  "motivo_local": "…" ,
  "remoto": { "url": "https://xxx.a.pinggy.link", "via": "pinggy" } | null,
  "motivo_remoto": "…",
  "servicios": [ { "id": "invitados", "nombre": "…", "local": "https://…/invitados/" | null,
                   "remoto": "…" | null, "motivo_local": "…", "motivo_remoto": "…" } ] }
```

Servicios: `invitados`, `sesion`, `presidencia`, `botonera`, `control`,
`prompter`, `rotulos`, `correccion`, `tablero`, `publico`, `pantalla` (TV, http),
`companion` (http), `evento`. Las claves de cada uno (`#k=`, `#room=`, tokens)
las siguen agregando sus endpoints, sobre el origen del catálogo.

## Fases

- **D0** ✅ La puerta de los aparatos pasa de 8790 (chocaba con el MCP) a 8792.
- **D1** ✅ Gateway: `Direcciones` (función pura `armar(Entradas) → catálogo`, con
  tests), entradas frescas (IP cada 10 s, túnel listo de verdad), evento al
  cambiar, `GET /api/v1/direcciones`. Los `*-link`, `/sesion/origen`, `bestUrl`
  y `/network` salen del catálogo (se borran `hostLocal` fijo, el `bestUrl`
  calculado una vez, el 127.0.0.1 de `correccion-link`, la reescritura de
  `cast-link`).
- **D2** ✅ Gateway: la puerta de la sala (HTTPS 8792 + HTTP 8793) atiende todo
  menos la entrada de invitados.
- **D3** ✅ App: modelo `sala::Direcciones`; Servicios, tarjeta de invitados,
  Pantallas, Presidencia/integrantes (local primero, remoto aparte), botonera y
  control web, Companion, Evento web, QR guardados y el MCP le piden a él. Se
  borran `joinUrl`, `refrescarOrigen` y el 8787 fijo de las pantallas;
  `urlApiRemota`, `baseRedLocal` y los enlaces de `AccesoRemoto` leen del catálogo.
  Queda a propósito: `EventoViewModel::enlaceDeCue` (abre el teleprompter en ESTA
  máquina, 127.0.0.1 es correcto ahí) y la elección de IP por subred del televisor.
- **D4** Pruebas: tests del catálogo (sala local/red/una IP, con y sin túnel,
  IP que cambia, urlHost raro), e2e por MCP, y mirar Servicios con la app.
