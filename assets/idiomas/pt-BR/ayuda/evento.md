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