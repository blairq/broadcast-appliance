# Telas no local e sinalização

## Para quê
Usar o sistema para o que se vê no local: televisores com a agenda, o orador atual, a
votação ou um QR para perguntar; cartazes impressos com QR.

## Perguntas próprias
- Quantos televisores e onde? São Smart TVs na rede ou vão por HDMI?
- O que cada um tem que mostrar e quando muda?
- Vão ser impressos QR (cartazes, crachás)? Com quanta antecedência?

## O que montar
- Telas (pantalla_buscar, pantalla_agregar, pantalla_mostrar) com seu conteúdo por momento
  (programa, relógio, votação, mensagem, QR).
- Cenas próprias para as telas (saída limpa sem GCs: salida_limpia).
- Códigos QR (qr_generar / qr_guardar) com estilo, exportados para imprimir (qr_exportar).

## Verificar
ANTES DE IMPRIMIR: endereço fixo (domínio próprio ou subdomínio reservado:
acceso_remoto_probar_direccion e acceso_remoto_dns_verificar). Se o endereço mudar, todos os
QR impressos deixam de funcionar e é preciso reimprimir. Teste cada QR impresso com um celular.

## Entregar
Os QR para imprimir (em alta resolução), a lista do que cada tela mostra.
