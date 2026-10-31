## Como está montado
O texto é mostrado por uma página web: `/prompter/` é a tela do orador (só leitura) e `/prompter/#control` o controle do operador (com senha). O estado —qual texto, onde está, em que velocidade— vive no gateway, não no tablet: se algo reiniciar, o discurso continua onde estava.

Os discursos são arquivos de texto em `Discursos/`, DENTRO da pasta do evento aberto, com subpastas se for preciso (`Apertura/intendente.txt`). São os MESMOS que a deixa `teleprompter` do roteiro carrega: não há uma biblioteca separada.

## Para que vão chamar você
Quase sempre para a mesma coisa: **"este discurso dura nove minutos e eu tenho seis"**. O caminho é `prompter_leer`, contar, reescrever e `prompter_escribir`. A 140 palavras por minuto (uma leitura normal), seis minutos são umas 840 palavras. `prompter_documentos` diz quanto dura cada um sem abri-lo.

## Como se escreve um texto que vai ser lido em voz alta
- **Uma linha em branco separa parágrafos**, e o controle pode pular de um para outro: convém que cada parágrafo seja uma ideia, assim o operador pode pular um se for preciso encurtar ao vivo.
- **Nada de markdown**: nem asteriscos, nem cerquilhas, nem hifens de marcador. No tablet aparece exatamente como está escrito.
- Frases curtas. O que se lê bem no papel nem sempre se diz bem.
- Números redondos e escritos como se pronunciam ("três mil e quinhentos", não "3500").

## Enquanto alguém lê
`prompter_control` inicia e para o texto, muda a velocidade, pula e escreve avisos ao orador ("Conclua"). **Do outro lado há uma pessoa lendo em voz alta**: iniciar ou parar o texto sem que peçam aparece no ar na hora. Se o orador estiver mais lento que o texto, baixe a velocidade de 10 em 10 ppm em vez de pará-lo.

`prompter_estado` também diz quais telas estão conectadas: antes de alguém subir ao púlpito, o que importa é que haja pelo menos uma vista `orador`. Se não houver nenhuma, o tablet está desligado ou ninguém abriu o link, e isso se avisa AGORA, não quando o orador já estiver lá.