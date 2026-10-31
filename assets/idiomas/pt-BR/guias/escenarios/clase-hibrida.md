# Aula híbrida

## Para quê
Um professor dá uma aula com alunos presentes e outros conectados. Sem quórum nem votação, mas com
chamada (feita manualmente), materiais, perguntas e uma gravação para quem faltou.

## Perguntas próprias
- Qual matéria, tema e nível? Quanto dura e em que partes se divide?
- Quantos alunos na sala de aula e quantos remotos? Precisam se cadastrar ou o link basta?
- Vai haver chamada? (Manual: padron_anotar / padron_acreditar por aluno.)
- Materiais: fichas, leituras, apresentações? São mostrados na tela? (Cena PDF.)
- Teleprompter para o professor com o roteiro da aula?
- Algum aluno precisa de tradução ou legendas (outro idioma, deficiência auditiva)?
- Logo da escola ou brasão do estado? Cartelas por tema com imagens?
- Os remotos perguntam pelo chat ou falam? Vai ser gravada para publicar depois?

## O que montar
- Blocos por tema com uma cartela de título por tema (texto real em `valores`, salidaEn 4000-5000)
  e uma imagem leve do tema na sua cena.
- Cena “Professor”, “Quadro/Apresentação”, “PDF” (pdf_escena_crear) com as fichas carregadas na
  documentação do evento, relacionadas a cada tema.
- Teleprompter com o roteiro de cada parte.
- Lista de presença dos alunos para a chamada (sem `vota`), com o contato deles se o convite for enviado.
- Legendas ou tradução, se foi pedido (traduccion_agregar).
- Logo da escola como camada DSK.

## Verificar
Link dos remotos testado a partir de um celular; a cena PDF com as fichas; áudio do professor
(microfone de lapela, se houver).

## Entregar
O link para os alunos (ou o convite por aluno), a página web da aula com os materiais,
e no final a gravação com capítulos por tema.
