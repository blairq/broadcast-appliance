# Enlace: medir la velocidad por el túnel y cuánto necesita el evento

Los invitados entran **siempre por el túnel** (WireGuard en espacio de usuario hasta un VPS
propio, [CONFERENCIA.md](CONFERENCIA.md) «Túnel WireGuard»). El gateway del lugar les manda
el retorno **una vez a cada uno**: lo que manda es la **subida del lugar** (Starlink: 10–40
Mbps) y cómo se porta el enlace **cargado** (demora, jitter, pérdida). Esta función lo mide
**por el camino real** —el mismo túnel, la misma pila IP del gateway— con nuestros
componentes en las dos puntas, sin nada del sistema (ni iperf3 ni networkQuality).

```
gateway del lugar (ba-gateway)                         VPS (IP pública)
  medidor (net/EnlaceMedidor) ── lwIP 10.99.0.2 ══ WireGuard ══ wg0 10.99.0.1
                                                                  └─ ba-enlace-eco (UDP 51821)
```

## Qué se mide

Una corrida (página **Sala → Enlace**, o `enlace_medir`), unos 20–30 s:

1. **Hola**: ¿contesta el eco del servidor? (versión y topes). Si no, el error dice que
   falta instalarlo o que el túnel no está conectado.
2. **Reposo** (~2 s): un ping cada 50 ms → **RTT** (p50, p95), su variación y la pérdida.
3. **Subida** (UDP, paquetes de 1200 bytes como el video de WebRTC): escalones de 2, 5, 10,
   20, 30, 40, 60, 80, 100… Mbps hasta el tope (de fábrica 100 Mbps), 1,5 s cada uno. En
   cada escalón el eco cuenta lo que le llegó (**tasa entregada, pérdida, jitter** de RFC
   3550) y los ping de mientras dan el **RTT con carga**. La rampa para cuando el enlace se
   llena: pierde más del 5 %, entrega menos del 85 % de lo pedido o la demora sube más de
   300 ms.
4. **Bajada**: lo mismo al revés (el eco manda a ritmo y cuenta este lado).

Con una **tasa fija** (`tasa_kbps`) se manda solo esa tasa durante `segundos` en cada
sentido: sirve para «¿aguanta 32 Mbps de subida sin perder?».

Lo que queda: por sentido, la **capacidad** (lo más que entregó), la **tasa sin pérdida**
(el escalón más alto que no saturó: la que cuenta para WebRTC), su pérdida y jitter, y el
peor RTT con carga. Las tasas cuentan la cabecera IP/UDP de adentro del túnel (lo mismo que
mide la prueba de sala en el gateway); los 60 bytes de WireGuard por paquete van aparte.

## Cuánto necesita el evento

`sala/EnlaceNecesita` (puro, `test_enlace_necesita`), con lo medido en la prueba de sala
(10/20/30/40 invitados a la vista el 2026-11-01; 20 con «alta hasta 10» el 2026-11-02):

| | por invitado |
|---|---|
| sale (subida del lugar: retorno de video 640×360 + audio + RTCP) | ~1620 kbps |
| entra en alta | ~1400 kbps |
| entra en baja | ~150 kbps |

Con más de «**alta definición hasta N**» personas en la grilla, todas en baja salvo el
orador y el precargado (2). Con 20 invitados: ~32 Mbps de subida y ~5,5 de bajada (medido:
33,4 y 5,3). Los encabezados de WireGuard suman ~5 % (lo que hay que pedirle al proveedor).

**Veredicto** por sentido, contra la tasa sin pérdida medida por el túnel:

- **entra**: medido ≥ necesario × 1,3;
- **justo**: medido ≥ necesario (sin margen), o la rampa no llegó hasta lo necesario sin
  llenar el enlace («medí más alto»), o con carga pierde más del 2 %, el jitter pasa de
  30 ms o la demora sube más de 200 ms;
- **no entra**: medido < necesario.

Y **hasta cuántos invitados entran** con margen y justo. La cuenta se hace para el cupo de
la sala (se puede cambiar en la tarjeta) y la grilla como está.

## Seguridad: no romper un evento al aire

- Con **invitados conectados** (que no sean de prueba) o **algo al aire** (transmisión,
  grabación, señal limpia), medir pide **confirmación** y entonces mide **suave**: hasta
  5 Mbps y escalones de 1 s. Alcanza para la demora y la pérdida, no para la velocidad
  máxima (el veredicto dice «medí más alto»).
- Con una **prueba de sala** corriendo, no se mide (ella misma es la carga).
- Una medición a la vez; se puede **cancelar** (corta entre paquetes y le dice al eco que
  pare la bajada).
- El eco tiene topes propios (de fábrica 200 Mbps y 30 s por pedido, dos bajadas a la vez) y
  **solo contesta a la red del túnel**.

## El eco en el servidor (ba-enlace-eco)

`tools/vps/ba-enlace-eco.py`: Python 3 con la biblioteca estándar (viene en cualquier
Debian/Ubuntu), escucha en la IP del túnel del servidor (10.99.0.1) y el puerto UDP
**51821**. Viaja **adentro del gateway** (cmake/BaEcoEnlace.cmake): el instalador lleva el
script de ese mismo build.

**Instalar o actualizar**: tarjeta **Enlace → Instalar el eco en el servidor…** y pegar lo que
muestra, como root, en una terminal del VPS (se puede repetir: así se actualiza). Hace:

- `/usr/local/lib/broadcast-appliance/ba-enlace-eco.py`;
- el servicio `ba-enlace-eco` de systemd (después de `wg-quick@wg0`, usuario dinámico, se
  reinicia solo);
- si `ufw` está prendido, el permiso de UDP 51821 **solo por wg0**. No hace falta abrir
  nada en el firewall del proveedor: el eco no se ve desde internet.

A mano: copiar el archivo, y `python3 ba-enlace-eco.py --direccion 10.99.0.1 --puerto 51821
--red 10.99.0.0/24`. Ver si anda: `systemctl status ba-enlace-eco`.

## Dónde se usa

- **Configuración → Sistema → Capacidad** ([CAPACIDAD.md](CAPACIDAD.md)): «con esta conexión
  entran N invitados», con esta medición y la del camino de los invitados por pinggy.

- **Sala → Enlace** (`PanelEnlace.qml`): veredicto para N, lo que hace falta, lo medido
  (subida y bajada sin pérdida/máx., demora en reposo y con carga, jitter, pérdida), el
  avance en vivo y el instalador del eco.
- **Prueba de sala**: antes de arrancar dice si el enlace alcanza para los N (con la última
  medición) y ofrece medir; al arrancar avisa si no entra; el informe final lleva el enlace
  (`enlace`: la cuenta y la medición).
- **Lista de chequeo del evento**: al terminar una medición, ítem `enlace` (hecho si
  «entra» para el cupo).

El modelo es uno (`ui/EnlaceViewModel`); la medición la hace el gateway
(`net/EnlaceServicio`, un hilo propio) y avisa el avance con el evento `enlace`.

## MCP

| Herramienta | |
|---|---|
| `enlace_medir` | `{confirmar?, tope_kbps?, tasa_kbps?}` |
| `enlace_cancelar` | corta la que está corriendo |
| `enlace_estado` | estado, avance, lo parcial, la última, el riesgo de medir ahora y la cuenta |
| `enlace_necesita` | `{invitados?}`: cuánto hace falta y el veredicto con la última medición |

## API del gateway (127.0.0.1, la usa la app)

| Método | Ruta | |
|---|---|---|
| GET | `/api/v1/enlace` | `{estado: sin_tunel|libre|midiendo, hayTunel, eco, fase?, avance?, …, ultimo?}` |
| POST | `/api/v1/enlace/medir` | `{subida?, bajada?, topeSubidaKbps?, topeBajadaKbps?, tasaKbps?, segundos?, escalonMs?}` |
| POST | `/api/v1/enlace/cancelar` | |
| GET | `/api/v1/enlace/instalador` | `{ok, eco, script}` |

Evento a la app: `enlace` (el estado entero, hasta 5 por segundo mientras mide).
`BA_ENLACE_ECO=ip:puerto` en el entorno del gateway apunta el medidor a un eco por la red
común (pruebas sin túnel).

## Protocolo (UDP, «BAE1»)

Lo mismo en `src/gateway/net/Enlace.hpp` y en el eco; si cambia, sube `kVersion`.
Cabecera `BAE1` + tipo; todo en orden de red: hola/hola-ok (versión, topes), ping/pong
(prueba 0, seq, hora), dato (prueba, seq, hora de envío, relleno), pedido de informe →
informe (recibidos, enviados, seq máx., jitter, desordenados, bytes, primera y última
llegada), pedido de bajada (prueba, kbps, ms, tamaño) → bajada-ok (lo concedido), parar.

## Pruebas

`test_enlace` (protocolo, cuentas, el pedido, el instalador y el medidor contra el eco de
verdad por UDP local), `test_ipstack` (el medidor **por el túnel**: WireGuard + lwIP de
punta a punta contra el eco), `test_enlace_necesita` y `tests/test_enlace_eco.py` (el eco
con un cliente de Python).
