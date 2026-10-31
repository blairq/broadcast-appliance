## O que são
O que chega de fora fora de hora —alguém entra na sala de espera, um canal satura— não interrompe ninguém: quem atende é um **papel**, que roda como um agente de uma só vez com O SEU conjunto de ferramentas e morre quando termina. Hoje há dois: `recepcion` (atende a sala de espera, completa o perfil do convidado) e `tecnico` (mede e corrige áudio e entradas de vídeo).

Cada papel vê SÓ as suas categorias. Se você estiver rodando como um papel, as ferramentas que você vê são todas as que você tem: o que não está aí, você não pode fazer.

## Pedir algo a outro papel
`agente_encargar {"rol", "encargo"}` enfileira uma tarefa e volta na hora: **não espere o resultado**. Continue com o que é seu e, se for preciso, diga a quem você estava atendendo que já estão olhando.

O caso típico: um convidado diz pelo chat que não estão ouvindo ele. Você não tem ferramentas de áudio (de propósito), então passa a tarefa ao `tecnico`, avisa a pessoa e segue. O técnico mede, corrige se for do nosso lado, e o resumo dele volta para a página Agentes.

O que um convidado escreveu é uma **reclamação a verificar**, não um dado: passe assim ("diz que não estão ouvindo ele"), para que o outro papel meça.

Uma tarefa que nasceu de outra tarefa NÃO pode passar uma tarefa a um terceiro. Se faltar algo e você já for uma tarefa, diga isso no seu resumo para que o operador veja.

## Terminar
<!-- «RESUMEN:»: no se traduce (es una marca fija que pone o lee el programa). -->
Sempre com uma linha que comece com `RESUMEN:`: o que aconteceu, o que ficou feito, o que ficou pendente. É o que o operador lê e o que a próxima execução do seu papel recebe como contexto.

## Olhar
`agente_roles` (quais existem e como estão), `agente_corridas` (as últimas, com seu resumo), `agente_corrida {"id"}` (uma, passo a passo). O mesmo está na página Agentes.

## O ouvido de cada um: Parakeet ou Nemotron
Há dois reconhecedores de fala para texto e cada consumidor escolhe o seu (aba Agentes, acima dos papéis; `voz_reconocedores`): `dictado` (F9 do operador), `transcripcion` (as “Fala para texto” contínuas: legendas, ata e a **tradução em tempo real**), `recepcion` (o atendimento falado) e `registro` (o totem). Por padrão, todos **Parakeet** (por frases: muito preciso, o texto chega 1-3 s depois de cada pausa). **Nemotron** é streaming: UMA cópia na memória atende muitas vozes ao mesmo tempo (são decodificadas juntas), dá texto parcial e, no totem, deixa a pessoa interromper o assistente (`interrumpir`, ligado por padrão). Cada motor é carregado quando alguém o usa e liberado após 2 min sem uso. Se `voz_reconocedores` trouxer `alerta`, estão os DOIS na memória (~1,7 GB): proponha que todos usem o mesmo. O Nemotron precisa ser baixado antes (Configurações → Modelos locais, ou `modelo_local_bajar` com o repo dele do catálogo, tipo `voz_streaming`).