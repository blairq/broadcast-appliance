# Room displays and signage

## What it's for
Using the system for what is seen at the venue: TVs with the agenda, the current speaker, the
vote or a QR code to ask questions; printed signs with QR codes.

## Questions of its own
- How many TVs and where? Are they Smart TVs on the network or connected over HDMI?
- What does each one have to show and when does it change?
- Are QR codes printed (signs, badges)? How far in advance?

## What to build
- Displays (pantalla_buscar, pantalla_agregar, pantalla_mostrar) with their content for each moment
  (program, clock, vote, message, QR).
- Scenes of their own for displays (clean output without lower thirds: salida_limpia).
- QR codes (qr_generar / qr_guardar) with a style, exported for printing (qr_exportar).

## Check
BEFORE PRINTING: a fixed address (your own domain or a reserved subdomain:
acceso_remoto_probar_direccion and acceso_remoto_dns_verificar). If the address changes, every
printed QR code stops working and has to be reprinted. Test each printed QR code with a phone.

## Hand over
The QR codes for printing (high resolution), the list of what each display shows.
