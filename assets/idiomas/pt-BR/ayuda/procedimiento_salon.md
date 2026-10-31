O procedimento padrão para um evento **em salão**: público na sala, oradores, transmissão e gravação e, se pedirem, convidados remotos e perguntas do público. Siga-o em ordem; cada passo diz com qual ferramenta. **Proponha antes de fazer** o que muda o evento ou entra no ar, e não invente dados: o que não disseram a você, pergunte.

**0. O que é preciso perguntar** (antes de montar qualquer coisa): nome do evento; data, hora de início e **fuso** (se alguém se conecta de outro lugar); local; duração e partes (blocos) com seu tempo; quem fala em cada parte, com **nome e cargo** como vão aparecer na tela; se há **convidados remotos**; se o **público pergunta e vota**; **onde se transmite** (YouTube, Twitch, Facebook, outro servidor) e se é gravado; música de espera e de encerramento; vídeos que serão exibidos; se há **credenciamento** de participantes; se é preciso teleprompter ou legendas.

**1. Configurar** (tudo o que tem que ficar pronto):
| O quê | Com o quê |
|---|---|
| O evento e seu roteiro: blocos com tempos e deixas (cena, orador com seu tempo, vídeo, música, gráfico, teleprompter, nota, espera) | `evento_armar {nombre, fecha, inicio, huso, invitados, preguntas, bloques}`; depois `guion_bloque` / `guion_cue` |
| As cenas: cada câmera, plano geral, tela compartilhada, convidados | `scene_define`, `scene_list` (help escenas) |
| Música e vídeos do evento (as deixas os nomeiam) | `media_list`, `media_find` (help medios) |
| GCs de cada orador (nome e cargo bem escritos) | gráfico `zocalo` em deixas do tipo gráfico; `grafico_listar`, `grafico_probar` |
| Microfones da sala e níveis | `audio_input_add`, `audio_measure`, `audio_loudness` (help audio) |
| Para onde se transmite | contas: `account_setup_guide`, `accounts_status`; servidor próprio: `stream_destino_agregar` |
| A gravação (disco e qualidade) | `record_disco`, `record_ajustes` |
| A página web do evento (uma página por bloco, atualiza-se sozinha) | `evento_web {accion: generar}`; `evento_web {accion: sitio, participacion: true}` se o público participa; onde assistir ao vivo: `sitio {vivos}` |
| Convidados remotos (se houver) | `room_link`, `room_pin`; sala de espera com `room_set`; quem está: `invitado_chequeo` |
| Perguntas do público (se houver) | a página do público (QR); moderação com `preguntas_ver` / `pregunta_moderar` |
| Participantes e credenciamento (se houver) | `padron_anotar`; na porta, `padron_puerta` |
| O início e o fim: sinal de teste, cartela “Antes de começar”, cartela de fim, blocos de pré-evento e de encerramento | `evento_preparar {musica_espera, musica_cierre, bajada, cierre}` |

**2. Conferir** (o automático): `evento_revisar` (cenas, arquivos, disco, destino, entradas sem sinal) → `evento_ensayo_rapido` (percorre o roteiro inteiro) → `evento_verificar` (a lista de controle completa deste tipo de evento). Corrija cada “falta” com o que diz `como` e rode `evento_verificar` de novo até não faltar nada.

**3. Pedir à pessoa que confira**: `evento_verificar` deixa na lista de verificação do evento o que só alguém olhando e ouvindo confirma (estado «a_mano»). **Mostre essa lista inteira** a quem pediu o evento e peça que confirme cada coisa; à medida que disser que sim, marque com `evento_chequeo {item, hecho: true, nota}`. Não marque nada que não tenham confirmado.

**4. Ensaiar** (testar cada peça, sem entrar no ar):
- **Música:** `audio_escuchar {name}` a manda para os fones do operador; que ele ouça e diga se o volume está bom.
- **Vídeos:** `scene_preview` com a cena do vídeo e `foto_fuente` para olhá-lo; com seu áudio.
- **Gráficos:** `grafico_probar` com os dados reais de cada GC e cartela (olhe as imagens; nomes e cargos bem escritos).
- **Página web:** `evento_web {accion: ver}` e o QR com `qr_mostrar` para abri-la num celular.
- **Gravação de teste:** `record_start`, um minuto, `record_stop`, e que a reproduzam.
- **Convidado de teste** (se houver): `invitado_prueba`: aparece, é ouvido e nos ouve.
- **Pergunta de teste** (se houver): que alguém envie uma pelo celular e você a modere.
- **Transmissão de teste:** com o sinal de teste no ar, `stream_start` e `stream_salud`; que a pessoa a veja na plataforma (barras, hora correndo, tom, que seja o evento certo). Se faltar muito para começar, `stream_stop`.
- **O percurso inteiro:** `evento_correr` com o ensaio ligado (help do evento) para ver a ordem real das cenas.

**5. Deixar pronto para começar:** `evento_verificar` sem faltas e com tudo confirmado; depois `evento_preparar {al_aire: true}` deixa o sinal de teste no programa. Avise a pessoa que ficou pronto e o que falta fazer no dia.

**6. O dia, do sinal de teste ao fim:**
1. **30 a 15 minutos antes:** `evento_iniciar`; o pré-evento do roteiro começa com o sinal de teste (`evento_correr`); `stream_start` e `record_start`. Confirmar que aparece e se ouve na plataforma (a deixa de espera pede isso).
2. **10 a 5 minutos antes:** `evento_correr` → cartela “Antes de começar” com a música de espera.
3. **Está na hora:** `evento_correr` para o primeiro bloco.
4. **Durante:** `evento_correr` para avançar; GCs no tempo certo; perguntas moderadas; acompanhar `stream_salud` e `audio_loudness`.
5. **Encerramento:** o bloco de encerramento põe a música e a cartela de fim; quando terminar, `stream_stop`, `record_stop`, `evento_terminar` e `evento_informe` (quanto durou cada parte, para o próximo). A página web passa sozinha para o encerramento.