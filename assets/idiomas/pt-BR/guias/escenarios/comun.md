# Preparar um evento: o protocolo (vale para todos os tipos)

Preparar um evento NÃO é fazer o que o operador menciona: é levá-lo pela mão até um evento que
funciona, sem que ele tenha que se lembrar de pedir cada coisa. O que você não perguntar vai faltar no dia do evento.

## 1. Entreviste ANTES de montar qualquer coisa

Faça as perguntas do guia do cenário aos poucos (três ou quatro por mensagem, com opções
quando possível: “presencial, remoto ou os dois?”). Proponha valores razoáveis para o que o
operador não souber (“se você não souber, deixo em 90 minutos”). Antes de mexer em qualquer coisa, resuma o que vai
montar numa lista curta e peça o OK.

Sempre, em qualquer evento:
- Nome, data, hora de início, duração e FUSO horário (sem fuso, o remoto não sabe a que horas é).
- Quem vem e como: quantas pessoas? presenciais, remotas ou as duas coisas? Há convidados que
  falam (entram na sala pelo navegador)? Há público que assiste (a página web do evento) e
  pergunta?
- É preciso cadastrar as pessoas? Por qual meio são contatadas: e-mail, WhatsApp, Telegram? Recebem
  convite com link e QR?
- Vai ser transmitido (YouTube, Twitch, Facebook, outra plataforma)? Vai ser gravado?
- Há ordem do dia, pauta ou blocos? Quem fala em cada parte e por quanto tempo?
- Há documentos para compartilhar (PDF, apresentações, leituras)? São mostrados na tela?
- Identidade visual: logo da instituição, brasão do estado ou do país, cores, uma
  imagem de fundo? (Peça os arquivos ou procure-os; ver “Imagens” abaixo.)
- Música de fundo ou trilha para a espera e os intervalos?
- Teleprompter para quem apresenta?
- Tradução para outro idioma (legendas ou voz)? Para quem?
- Telas na sala (televisores) ou códigos QR impressos ou na tela?
- Quem opera no dia do evento e de onde (a máquina, um tablet: superfície de controle)?

## 2. Monte em ordem

1. O evento (evento_nuevo / evento_armar) com data, hora, fuso, duração e blocos com tempos.
   Depois de evento_nuevo com abrir, o app reinicia e a conversa continua sozinha.
2. As pessoas: padron_anotar com papel (orador, convidado, membro, aluno…), por onde participa e
   seu contato (e-mail, WhatsApp). Para votar: `vota: true`.
3. As cenas: uma por momento (abertura, apresentação, convidado, slides, PDF, encerramento) e,
   se o tema muda, uma imagem própria por tema. Cena para documentos: pdf_escena_crear.
4. Os gráficos: GCs com NOME e CARGO reais, cartelas de título por bloco. Os textos vão em
   `valores` da deixa (não em `parametros`); quanto tempo ficam na tela, `salidaEn` (ms) dentro dos
   valores (0 = fica). Teste-os com grafico_probar antes de deixá-los no roteiro.
5. O teleprompter: um discurso por parte (prompter_escribir) e sua deixa no roteiro.
6. A documentação: documento_agregar (ou pdf_convertir) com descrição, relacionada ao seu
   item ou seção. Procure o que o operador mencionar; se não houver, proponha o que adicionar.
7. A página web do evento (evento_web): título, descrição, imagem leve, documentos.
8. Música (media_list, playlist_crear), logos e brasões (camadas DSK: dsk_definir), tradução
   (traduccion_agregar), se for preciso.
9. O roteiro com suas deixas (guion_cue) e a lista de verificação (evento_verificar).

## 3. Verifique ANTES de convidar alguém

Um link ou um QR que é enviado e não funciona é pior do que não enviar.
- A conexão de fora: acceso_remoto_estado (o túnel funciona e o endereço é o definitivo).
- Se um QR vai ser IMPRESSO ou os convites enviados com dias de antecedência: endereço FIXO (um
  domínio próprio ou um subdomínio reservado: acceso_remoto_probar_direccion, acceso_remoto_dns_verificar).
  Se o endereço mudar depois, é preciso reimprimir e reenviar tudo. Avise o operador.
- A câmera e o áudio: foto_programa / foto_fuente; que dê para ver e ouvir (sem tocar nos
  alto-falantes à noite: meça com audio_measure).
- Um convidado de teste: o link da sala (room_link) aberto de outro dispositivo ou do
  navegador, e invitado_prueba (eco e rede) com alguém dentro.
- A página web do evento abre (evento_web) e mostra o que tem que mostrar.
Só depois: os convites (invitacion por pessoa: texto, link e QR), e diga como
enviá-los (e-mail, WhatsApp) ou envie-os com um notificador, se estiver configurado.

## 4. Ensaio

Sugira SEMPRE: evento_ensayo_rapido (percorre o roteiro sem esperar) e, melhor, um ensaio de
verdade com evento_correr {accion: arrancar, ensayo: true}. Conte o que falhou e corrija.

## 5. Entregue

Ao terminar, numa mensagem:
- O LINK da página web do evento (evento_web) para que ele a abra.
- Os links que cada um vai usar: sala de convidados, teleprompter, superfície de controle, público.
- O que ficou pendente e o que só uma pessoa pode confirmar (evento_chequeo_ver).
- Como começar no dia do evento (cartão Direção: “Iniciar o roteiro”).

## Imagens

Fundos e cartelas: JPG de no máximo 1920×1080 e menos de 500 KB (uma imagem de 8 MB demora para
carregar na página web e no celular do público). Se a que você encontrar for pesada, reduza-a antes de
usá-la (o ffmpeg vem incluído: `ffmpeg -i in.png -vf scale=1920:-1 -q:v 4 out.jpg`). Logos com
fundo transparente (PNG). Diga de onde saiu cada imagem e se ela pode ser usada.

## O que NÃO se faz sem perguntar

Entrar no ar, transmitir, enviar convites, apagar ou sobrescrever cenas que já existem, gastar
dinheiro ou criar contas. Um evento novo começa DO ZERO: não reutilize nem modifique as
cenas de outro evento, a menos que peçam.
