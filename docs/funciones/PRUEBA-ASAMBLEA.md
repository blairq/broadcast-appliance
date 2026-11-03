# Prueba de asamblea (integrantes simulados por la sesión remota)

> La [Prueba de sala](PRUEBA-SALA.md) prueba la **conferencia** con invitados genéricos.
> Esta prueba la **asamblea**: integrantes que entran por la [sesión remota](SESION-REMOTA.md)
> como desde su celular, con su link personal y su PIN, esperan que los habiliten, piden la
> palabra, hablan y votan.

Antes de una sesión con integrantes que sesionan desde afuera (o con el celular de respaldo
en la sala), el operador ensaya todo el circuito con **integrantes de prueba**:

- entran por el **camino real** del integrante (el mismo que la página `…/sesion/`): link +
  PIN o usuario + contraseña, la foto de su cámara para habilitarlos, la palabra, la
  cámara y el micrófono cuando la tienen, el voto;
- cada uno muestra **su número**: en la foto de su banca en el Recinto y en el video cuando
  tiene la palabra;
- el operador (o la presidencia) **los habilita en el Recinto**, les da la palabra y abre una
  votación como en la sesión real;
- al terminar **se borran** los integrantes de prueba y su acceso. Del evento queda el informe.

Pestaña **Asamblea → Conexiones remotas**, tarjeta **«Prueba de asamblea»** (al final, debajo
de los accesos y los aparatos registrados: es el mismo camino que se administra ahí), o las
herramientas MCP `prueba_asamblea_*`. Las dos usan el mismo modelo
(`src/app/ui/PruebaAsambleaViewModel`).

## Corre en un evento de prueba

La prueba crea integrantes, les da acceso y los hace votar: todo eso iría al padrón, a
Quórum y al acta. Por eso **solo corre en un evento sin integrantes reales**:

- si el padrón del evento abierto tiene algún integrante que no es de prueba, la tarjeta lo
  dice y ofrece **«Crear un evento de prueba y abrirlo…»** (un evento vacío, desde cero:
  el programa se reinicia con él). Para volver al tuyo, abrilo desde la pestaña **Eventos**;
- nunca agrega gente a un padrón real ni toca a sus integrantes, sus accesos, su quórum
  ni su acta;
- los integrantes de prueba van **marcados** (`prueba`), con ids, bancas y nombres propios
  («prueba-01», «P01», «Prueba 01»): en Conexiones remotas se ven como «… · de prueba».

## Cómo se usa

1. Abrí (o creá) un evento de prueba.
2. Elegí **cuántos integrantes** (1 a 500) y **cuántos minutos**.
3. El **comportamiento** (los valores de fábrica sirven para una primera prueba):

   | Opción | De fábrica | Qué hacen |
   |---|---|---|
   | % afuera | 20 | dicen estar afuera (en el Recinto: «presente (afuera)») |
   | % piden la palabra | 30 | una vez habilitados, la piden en algún momento |
   | % votan | 90 | votan a los pocos segundos de abrirse una votación |
   | Cómo votan | al azar | o mayoría afirmativa, pareja, mayoría negativa, todos afirmativo |
   | % se reconectan | 10 | se les corta la red una vez y vuelven solos con su celular registrado |
   | % PIN errado | 10 | prueban 5 veces con el PIN equivocado: quedan **bloqueados** 15 minutos, como pasaría de verdad |
   | % con usuario | 20 | entran con usuario y contraseña en vez del link |
   | Habilitarlos solos | 0 | 0 = los habilitás vos en el Recinto; con N, la prueba los habilita a los N segundos |

4. **Generar integrantes de prueba** (opcional): crea los integrantes, los ubica en el
   Recinto y les da acceso, sin que entre nadie todavía. Sirve para mirar el Recinto antes.
5. **Arrancar la prueba**:
   - con **hinchada** (el probador que viene en el programa): esta máquina hace entrar a
     los N, de a uno por segundo. Nadie suena por los parlantes; la primera vez el probador
     graba su video de prueba (unos minutos);
   - **sin hinchada** (desde otra máquina): la tarjeta muestra el **archivo de
     credenciales** (copialo a la otra máquina) y la línea de hinchada para correr ahí.
     Son credenciales de prueba: valen mientras dura la prueba.
6. En vivo: cuántos esperan que los habilites, habilitados, afuera, en la fila, con la
   palabra, votaron, bloqueados. **«Ir al Recinto»** lleva a habilitarlos (se ve el número en
   la cara de cada banca). Dales la palabra desde la fila y abrí una votación en
   **Votación**, como en una sesión.
7. **Terminar la prueba** (o termina sola por tiempo, o cuando el probador termina).

## Qué queda al terminar

- Los integrantes de prueba **salen de la sesión**, se **revoca su acceso**, se **borran del
  Cuerpo** y el Recinto vuelve a como estaba.
- Se borra el archivo de credenciales.
- El **informe** queda en el evento, `Pruebas/prueba-asamblea-<fecha>.json`, y en su lista de
  chequeo (ítem «prueba_asamblea»):
  - lo que vio la app (conectados, habilitados, votos por opción);
  - lo del probador: cuántos entraron, esperaron que los habiliten (cuánto, p50/p95),
    pidieron y tuvieron la palabra (y en cuánto se conectaron su cámara y su micrófono),
    votaron (y en cuánto llegó la confirmación), se reconectaron (y si volvieron
    habilitados), quedaron bloqueados;
  - qué le pasó **a cada uno**.
- Al lado, en `Pruebas/prueba-asamblea-<fecha>/hinchada/`, el informe del probador
  (`informe.html` con los gráficos de la red y `informe.json`).
- Si el programa se cerró en el medio, al volver a arrancar se borran los integrantes de
  prueba que quedaron (y su acceso) y se avisa.

## Por MCP

| Herramienta | Qué hace |
|---|---|
| `prueba_asamblea_arrancar` | `{integrantes, minutos?, hinchada?, comportamiento?}`; `comportamiento`: `afuera_pct`, `piden_palabra_pct`, `votan_pct`, `voto` («azar» o «afirmativo:60,negativo:30,abstencion:10»), `reconectan_pct`, `pin_errado_pct`, `pin_errado_intentos`, `con_usuario_pct`, `entrada_cada_ms`, `habilitar_solos_s`, `programa` |
| `prueba_asamblea_estado` | cómo va, el padrón (reales y de prueba), el archivo y la línea del generador externo |
| `prueba_asamblea_terminar` | termina y limpia; sin prueba, `{limpiar: true}` borra los de prueba que hayan quedado |
| `prueba_asamblea_informe` | el informe completo de la última |

Para habilitarlos, darles la palabra y votar se usan las de siempre: `sesion_habilitar`,
`banca_al_aire` / la fila, `sesion_votacion`.

## Desde otra máquina (generador externo)

```sh
hinchada --servidor integrantes --url https://IP_DE_LA_CAJA:8789 --inseguro \
         --integrantes integrantes.json --modo grabado --durante 5m
```

con el `integrantes.json` que muestra la tarjeta. Con una máquina por cada 150 integrantes
(macOS) o 500 (Linux) alcanza. Ver el README de hinchada (sección «Integrantes de la
asamblea»).

## Lo que no prueba

- **Desde afuera de verdad** (por el túnel): los integrantes que «dicen estar afuera» entran
  por la red local y solo se rotulan afuera. Para probar el túnel, corré el generador
  externo desde una red de afuera (con el link remoto).
- La **atención hablada** mientras esperan (el asistente les habla): los simulados no
  contestan con la voz.
- La consola de la presidencia: la sigue manejando una persona.
