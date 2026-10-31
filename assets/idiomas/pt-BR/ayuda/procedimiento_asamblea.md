O procedimento padrão para uma **sessão de assembleia** (câmara, conselho, diretoria, condomínio): membros em cadeiras, quórum, ordem do dia, votações e, se houver, membros a distância. Uma sessão **não é um roteiro de deixas**: a palavra muda sem ordem prévia e quem a conduz é a fila de oradores. Siga-o em ordem, **proponha antes de fazer** e não invente dados. Para o detalhe de cada peça: help asamblea, conferencia e sesion.

**0. O que é preciso perguntar:** nome do colegiado e da sessão; data, hora e **fuso**; a **lista de membros** com cadeira, nome, cargo e bancada (melhor uma planilha); quem **vota**; a **regra do quórum** e a maioria; a **ordem do dia** (itens e processos); se há **unidade central de conferência** (microfones das cadeiras) ou se trabalha sem ela; se há **membros a distância**; onde se transmite e se é gravado; se há hino, minuto de silêncio ou outros momentos fixos.

**1. Configurar:**
| O quê | Com o quê |
|---|---|
| O evento como assembleia e seu roteiro (só o que tem ordem: pré-sessão, abertura, encerramento) | `evento_armar {nombre, fecha, inicio, huso, asamblea: true, gente, bloques}` |
| O Colegiado: cada membro com sua cadeira, cargo e bancada (validados) | `cuerpo_importar` (planilha), `integrante_editar`, `integrante_validar` |
| O plenário: a grade de cadeiras | `recinto_editar {filas, columnas, auto: true}` |
| A tomada de cada cadeira | `ptz_posicion_crear` e `banca_toma {banca, camara, posicion}`; testar com `enfocar {que: "banca N"}` |
| AS CENAS de cadeiras, plano geral, remoto com a palavra e debate | `asamblea_escenas_base` (mostre a proposta; `crear: true`) |
| A unidade central de conferência (se houver) | `dcn_conectar`; cada cadeira com sua unidade (help conferencia) |
| Os membros a distância (se houver) | `sesion_credencial {banca}` (link e PIN), `sesion_acceso_opciones`; o console da presidência: `sesion_consola {accion: crear}` |
| A lista de membros com quem vota | `evento_armar {gente: [{…, vota: true}]}` ou `padron_anotar` |
| O quórum | `quorum_regla` |
| A ordem do dia | `orden_del_dia_cargar` |
| Os gráficos da assembleia: GC do membro, item da ordem do dia, quórum, resultado da votação, intervalo | vinculam-se sozinhos ao que foi validado (help asamblea, `grafico_listar` categoria Asamblea) |
| Para onde se transmite e a gravação | como num evento em salão (help procedimiento_salon, passo 1) |
| A página web do evento (mostra a assembleia, o plenário com as cadeiras por bancada, o quórum e as votações) | `evento_web {accion: generar}` |
| O início e o fim: sinal de teste, cartela de espera, cartela de fim | `evento_preparar` |

**2. Conferir:** `asamblea_revisar` (colegiado, plenário, presets, cenas, unidade central, cadeira virtual, ordem do dia) → `evento_revisar` → `evento_verificar {tipo: asamblea}`. Corrija cada “falta” e repita até não faltar nada.

**3. Pedir à pessoa que confira:** a lista «a_mano» de `evento_verificar`: além do geral (câmeras, som, gráficos, página, gravação, transmissão de teste), a **unidade central testada cadeira por cadeira**, a **tomada de cada cadeira**, uma **votação de teste**, os **remotos com seu link e PIN** (um testado), o **console da presidência** e **quem lavra a ata**. Marque com `evento_chequeo` só o que confirmarem a você.

**4. Ensaiar:**
- **Cada cadeira:** conceder a palavra (`banca_al_aire`) e encerrá-la (`recinto_orador_terminar`): a câmera se move, a cena é carregada, sai o GC com o nome correto, o microfone abre?
- **Um remoto:** que entre com seu link e PIN, habilitá-lo, conceder-lhe a palavra.
- **Votação de teste:** com a unidade central ou com a cadeira virtual (`sesion_votacion`); o resultado oficial (`votacion_resultado_oficial`) na tela.
- **Quórum:** `quorum_presentes` com os presentes.
- E o de todo evento: música, gráficos, página web, gravação e transmissão de teste (help procedimiento_salon, passo 4).

**5. Deixar pronto:** `evento_verificar {tipo: asamblea}` sem faltas e com tudo confirmado; `evento_preparar {al_aire: true}` deixa o sinal de teste no programa. Avise a pessoa.

**6. O dia, do sinal de teste ao fim:**
1. **30 a 15 minutos antes:** `evento_iniciar`; sinal de teste; `stream_start` e `record_start`; confirmar na plataforma.
2. **Antes de começar:** cartela de espera com música; os remotos entram e são habilitados; `quorum_presentes`.
3. **Abertura:** com quórum, o primeiro bloco (hino, abertura da presidência).
4. **A sessão:** a fila de oradores (`banca_cola`, `banca_al_aire`, `recinto_orador_terminar`); o item em discussão (`orden_del_dia_punto`); votações e seu resultado oficial; intervalo com sua cartela, se for preciso. O registro vai sozinho para a ata (`sesion_registro`).
5. **Encerramento:** o bloco de encerramento (cartela de fim); `stream_stop`, `record_stop`, `evento_terminar` e `evento_informe`.