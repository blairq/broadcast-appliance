## O que é
Um **evento** é o roteiro do que vai acontecer: **blocos** (Abertura, Palavras do prefeito, Encerramento) e dentro de cada um as **deixas** concretas. O evento tem TUDO o que é dele (cenas, mixer, mídia, assembleia, página web): a pasta do evento inteira pode ser levada para outra máquina. Os arquivos das deixas de música e vídeo são a **mídia do evento** (a página Mídia, `media_list`); os discursos vão em `Discursos/` do roteiro.

## Os tipos de deixa e o que cada um precisa em `datos`
| Tipo | `datos` | Já é executado? |
|---|---|---|
| `escena` | `{escena}` | sim |
| `orador` | `{participante}` + `duracionSeg > 0` | sim |
| `musica` | `{archivo, accion, repetir}` — um arquivo da mídia do evento (`media_list`) | sim |
| `teleprompter` | `{discurso, arrancar}` — texto em `Discursos/` | sim |
| `audio` | `{accion: silenciar\|activar\|volumen, fuente, db}` | sim |
| `video` | `{archivo, alTerminar}` | ainda não |
| `pantalla` | `{pantalla, contenido}` | ainda não |
| `pausa` | espera o que `duracionSeg` disser | sim |
| `espera` | para até o operador confirmar | sim |
| `nota` | não faz nada: é uma indicação | sim |

As que ainda não são executadas podem ficar escritas mesmo assim: contam para os tempos e dizem ao operador o que fazer. Quando chega a vez delas, o sistema anota em vez de falhar calado.

## Tempos
Da `duracionSeg` de cada deixa sai a do bloco e a do evento; com `inicio` (HH:MM) sai a hora prevista de cada coisa, e com `fecha` (YYYY-MM-DD) mais `huso` (nome IANA) sai `cuando`, o início como instante absoluto para quem está em outro fuso. Enquanto roda, o que importa é o **desvio**: `desvioSeg` positivo = estamos atrasados. O que está pausado não conta como atraso.

## Antes do evento
- `evento_revisar`: deixas incompletas, cenas que não existem, arquivos que faltam ou estão vazios.
- `evento_ensayo_rapido`: percorre o roteiro INTEIRO de uma vez, sem mexer em nada, e diz de cada deixa se poderia ser executada. É o que convém rodar depois de montar um roteiro longo.
- O interruptor de **ensaio** (`evento_correr {"ensayo": true}`) percorre o roteiro em tempo real sem entrar no ar.

## Enquanto roda
`evento_correr` com `arrancar`, `siguiente`, `atras`, `ir`, `pausar`, `reanudar`, `parar`. **Entra no ar**, salvo em ensaio, então quem decide é o operador.

Rodar o roteiro NÃO é estar ao vivo: ao vivo é quando sai algo de verdade (transmissão ou gravação), e quem diz é `en_vivo` em `status`. Ao vivo, o app avisa o operador antes de qualquer ação de risco e `settings_set` não aplica o que reinicia a conferência ou o motor (tiraria todos os convidados ou cortaria a saída no pior momento). Com o roteiro rodando para testar, sem transmitir, nada é bloqueado.

## Teleprompter
A deixa `teleprompter` carrega um texto de `Discursos/` e o deixa PRONTO, mas parado. Todo o resto (escrever os discursos, enviá-los, controlar o texto no ar) é a sua própria categoria: `help {"topic": "prompter"}`.
## A cara do evento na web (tema)
Cada evento tem seu **tema**: destaque, fundo, fonte, logo e nome, em **todas** as páginas web do sistema: a do evento, a dos convidados, a sessão remota (onde os integrantes votam), as perguntas do público, o totem de credenciamento e as telas da sala. Os consoles dos operadores (controle, botoeira, presidência, letreiros, correção) pegam o que disser o `alcance`: por padrão, só o destaque e o logo. As cores com significado nunca mudam: tally, no ar e armado, medidores, sim e não da votação.

- `evento_tema {"accion": "ver"}`: o tema, as cores que vão sair (`resuelto`) e os `avisos` de contraste. Se houver avisos, conte ao operador e proponha outra cor.
- `evento_tema {"accion": "cambiar", "colores": {"acento": "#00a86b"}, "marca": {"nombre": "Fórum 2026"}}`: o que não for definido é calculado (o texto em cima do destaque sai do contraste).
- `evento_tema {"accion": "logo", "archivo": "<caminho>", "cual": "logo"}` (SVG, PNG ou WebP, até 2 MB; `logoClaro` para fundos escuros, `favicon` para a aba) e `{"accion": "fuente", "archivo": "<caminho>", "familia": "…"}` (WOFF2, WOFF, TTF ou OTF, até 4 MB). Sem internet: nada de Google Fonts, a fonte viaja com o evento. Um SVG com scripts ou manipuladores de eventos é recusado.
- `alcance`: `{"publico": "todo", "control": "acento"}` (cada um nada, acento ou todo).
- Um site gerado antes do tema: `evento_tema {"accion": "migrar_sitio"}` (respeita o que foi mudado à mão em `estilo.css`).

Fica salvo na pasta do evento (`tema.json` e `Tema/`). Sem tema próprio, o destaque e o fundo saem dos do site (`evento_web sitio {color, fondo}`). No app: Serviços → Aparência das páginas web → Tema do evento.

Fora da web também é o valor padrão: os **gráficos** com uma cor de destaque (tarjas, placas, tickers) saem com a do evento se não receberem outra, e os **QR codes** novos pegam suas cores (sempre escuro sobre claro, para serem lidos) e seu logo no centro. Com um tema claro, as páginas do público se invertem por inteiro (realces, campos e sombras incluídos).
