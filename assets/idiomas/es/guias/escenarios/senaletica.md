# Pantallas en sala y señalética

## Para qué
Usar el sistema para lo que se ve en el lugar: televisores con la agenda, el orador actual, la
votación o un QR para preguntar; carteles impresos con QR.

## Preguntas propias
- ¿Cuántos televisores y dónde? ¿Son Smart TV en la red o van por HDMI?
- ¿Qué tiene que mostrar cada uno y cuándo cambia?
- ¿Se imprimen QR (carteles, credenciales)? ¿Con cuánta anticipación?

## Qué armar
- Pantallas (pantalla_buscar, pantalla_agregar, pantalla_mostrar) con su contenido por momento
  (programa, reloj, votación, mensaje, QR).
- Escenas propias para pantallas (salida limpia sin zócalos: salida_limpia).
- Códigos QR (qr_generar / qr_guardar) con estilo, exportados para imprimir (qr_exportar).

## Verificar
ANTES DE IMPRIMIR: dirección fija (dominio propio o subdominio reservado:
acceso_remoto_probar_direccion y acceso_remoto_dns_verificar). Si la dirección cambia, todos los
QR impresos dejan de servir y hay que reimprimir. Probá cada QR impreso con un celular.

## Entregar
Los QR para imprimir (en alta resolución), la lista de qué muestra cada pantalla.
