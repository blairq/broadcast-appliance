Um catálogo único de equipamentos, com ações declaradas por tipo. É o caminho geral para "o que está conectado?" e "faça X em tal equipamento".

## O caminho
1. `equipos_escanear {"detector"?}` (red, local, ndi, usb; sem detector, todos): cada equipamento com `id` estável («red:192.168.1.90», «local:video:<id de la camara>», «local:audio:<id del microfono>», «local:salida:<id>», «usb:046d:0881:…»), `tipo`, `nombre`, se está `conectado` e suas `acciones`. `equipos_listar` dá o que já foi encontrado, sem procurar.
2. `equipo_ver {"id"}`: tudo o que se sabe e cada ação com seus parâmetros.
3. `equipo_accion {"id", "accion", "args"}`.

## Ações úteis
- Câmera da rede: `pedir_clave` (formulário seguro: nunca peça a senha pelo chat), `ficha` (ONVIF + análise + plano), `funciones_onvif` (quais eixos se movem e como, limites, foco, home, presets, auxiliares, e sua posição), `onvif` (uma operação crua: `servicio` ptz|media|imaging|device e `cuerpo` com o XML do Body; {perfil}, {config_ptz} e {fuente} são substituídos), `http` (a API própria da marca: Axis VAPIX `/axis-cgi/com/ptz.cgi?query=limits`, Hikvision ISAPI…), `agregar`, `activar_ptz`.
- Câmera desta máquina: `agregar` (como entrada de vídeo, com seu nome e seu formato preferido). Microfone: `agregar` (entrada de áudio). NDI: `agregar`.

## Nomes dos dispositivos (da máquina)
As câmeras, placas de captura, microfones, interfaces de áudio, emissores NDI e câmeras de rede têm o nome que o operador lhes dá em “Dispositivos e equipamentos”: `equipo_renombrar {"id", "nombre"}` (vazio: volta ao do sistema). É da MÁQUINA, não do evento, e fica preso ao id estável do equipamento. As entradas de vídeo e de áudio, o mixer e as saídas o mostram assim, e uma entrada nova se chama assim. `equipo_configurar {"id", "config": {"nota", "modo"}}` (`modo`: o formato preferido de uma câmera). Os que a máquina já viu e hoje não estão continuam na lista como ausentes (`estado` «ausente»): uma entrada que os usa espera por eles e volta sozinha; `equipo_olvidar` apaga um ausente.
- USB com câmera UVC: `explorar_uvc` (PTZ padrão e unidades de extensão da marca: o primeiro passo para um plugin).

## Drivers e quarentena
Cada driver ou provedor de dispositivos da máquina (os do GStreamer: WASAPI, Media Foundation, Core Audio, AVFoundation, Pulse, V4L2, DeckLink, plugins; e cada driver ASIO no Windows) é testado num processo separado com tempo limite: se travar ou cair, o app continua. Após 4 falhas seguidas (ajuste `drivers.fallasCuarentena`) fica EM QUARENTENA: não é carregado de novo sozinho, nem ao iniciar nem quando os dispositivos mudam. `drivers_listar` dá o estado de cada um com o `motivo` (p. ex. JackRouter: «no contesta: ¿está corriendo el servidor de JACK?»). `driver_probar {"id"}` o testa mais uma vez (depois de corrigir a causa); `drivers_revisar` testa tudo de novo. É POR MÁQUINA: numa máquina nova tudo é testado ao iniciar; numa conhecida, só o novo (ou o que mudou de versão).

## Diagnosticar um PTZ que não responde
`funciones_onvif` diz o que a câmera declara; `onvif` com `<tptz:GetStatus><tptz:ProfileToken>{perfil}</tptz:ProfileToken></tptz:GetStatus>` diz onde ela está; testar um movimento (`RelativeMove`, `AbsoluteMove`) e olhar de novo. Se o protocolo aceita e a posição não muda, testar a API da marca com `http`; se também não, é a câmera (mecânica ou um limite na configuração dela).