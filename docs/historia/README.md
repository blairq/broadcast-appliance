# Historia

Documentos que fueron ciertos en su momento y ya no lo son. Se guardan porque
cuentan **por qué** el sistema quedó como quedó, que es lo que no está en el
código ni en el historial de git.

**No usarlos como referencia de cómo está hoy el sistema.** Para eso:

- lo que falta hacer → [`../IDEAS.md`](../planes/IDEAS.md)
- el plan de eventos → [`../EVENTOS.md`](../funciones/EVENTOS.md)
- cómo se compila → [`../compilacion.rst`](../desarrollo/compilacion.rst)
- el manual → [`../manual/`](../manual)

| Archivo | Qué era | Por qué quedó viejo |
|---|---|---|
| `2026-09-11-todo.md` | Documento de arranque para sesiones nuevas: qué estaba hecho, qué faltaba y cómo se compilaba. | Habla de 23 tests (hoy son 45), de paths "nunca compilados" que hace rato compilan y de un cross-build con MinGW que se cambió por clang-cl con ABI de MSVC. |
| `2026-09-18-tablero.md` | Tablero de progreso con estados `[HECHO]`, `[COLA]`, `[GATED:Win]`. | Daba por gated (sin poder probarse en Linux) casi todo lo que toca Qt, libobs y libwebrtc; hoy el build completo de Linux anda y esas piezas se prueban acá. |
