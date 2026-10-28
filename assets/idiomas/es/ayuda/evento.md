## Que es
Un **evento** es el guion de lo que va a pasar: **bloques** (Apertura, Palabras del intendente, Cierre) y dentro de cada uno los **cues** concretos. El evento tiene TODO lo suyo (escenas, mezclador, medios, asamblea, web): la carpeta del evento entera se puede llevar a otra máquina. Los archivos de los cues de música y video son los **medios del evento** (la página Medios, `media_list`); los discursos van en `Discursos/` del guion.

## Los tipos de cue y que necesita cada uno en `datos`
| Tipo | `datos` | ¿Se ejecuta hoy? |
|---|---|---|
| `escena` | `{escena}` | si |
| `orador` | `{participante}` + `duracionSeg > 0` | si |
| `musica` | `{archivo, accion, repetir}` — un archivo de los medios del evento (`media_list`) | si |
| `teleprompter` | `{discurso, arrancar}` — texto en `Discursos/` | si |
| `audio` | `{accion: silenciar\|activar\|volumen, fuente, db}` | si |
| `video` | `{archivo, alTerminar}` | todavia no |
| `pantalla` | `{pantalla, contenido}` | todavia no |
| `pausa` | espera lo que diga `duracionSeg` | si |
| `espera` | frena hasta que el operador confirme | si |
| `nota` | no hace nada: es una indicacion | si |

Los que todavia no se ejecutan igual se pueden dejar escritos: cuentan para los tiempos y le dicen al operador que hacer. Cuando les toca el turno, el sistema lo anota en vez de fallar callado.

## Tiempos
De la `duracionSeg` de cada cue sale la del bloque y la del evento; con `inicio` (HH:MM) sale la hora prevista de cada cosa, y con `fecha` (YYYY-MM-DD) mas `huso` (nombre IANA) sale `cuando`, el arranque como instante absoluto para el que esta en otro huso. Mientras corre, lo que importa es el **desvio**: `desvioSeg` positivo = vamos tarde. Lo pausado no cuenta como atraso.

## Antes del evento
- `evento_revisar`: cues incompletos, escenas que no existen, archivos que faltan o estan vacios.
- `evento_ensayo_rapido`: recorre el guion ENTERO de una, sin tocar nada, y dice de cada cue si se podria ejecutar. Es lo que conviene correr despues de armar un guion largo.
- El interruptor de **ensayo** (`evento_correr {"ensayo": true}`) recorre el guion en tiempo real sin salir al aire.

## Mientras corre
`evento_correr` con `arrancar`, `siguiente`, `atras`, `ir`, `pausar`, `reanudar`, `parar`. **Sale al aire** salvo en ensayo, asi que lo decide el operador.

Correr el guion NO es estar en vivo: en vivo es que salga algo de verdad (transmisión o grabación), y lo dice `en_vivo` en `status`. En vivo, la app le avisa al operador antes de cualquier accion de riesgo y `settings_set` no aplica lo que reinicia la conferencia o el motor (sacaria a todos los invitados o cortaria la salida en el peor momento). Con el guion corriendo para probar, sin transmitir, no se bloquea nada.

## Teleprompter
El cue `teleprompter` carga un texto de `Discursos/` y lo deja LISTO pero parado. Todo lo demas (escribir los discursos, mandarlos, manejar el texto al aire) es su propia categoria: `help {"topic": "prompter"}`.