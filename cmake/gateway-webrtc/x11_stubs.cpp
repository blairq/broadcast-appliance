// ============================================================================
// cmake/gateway-webrtc/x11_stubs.cpp — X11 inerte para libwebrtc en Linux
//
// El modulo de audio Pulse/ALSA de libwebrtc referencia tres funciones de Xlib
// (deteccion de teclas en KeyPressed). B usa su propio modulo de audio sin
// dispositivo (PullAdm, Webrtc.cpp): ese codigo nunca corre. Estas versiones
// inertes evitan depender de libX11 del sistema; un gateway de servidor no
// tiene por que tener un escritorio instalado.
// ============================================================================
extern "C" {
void* XOpenDisplay(const char*) { return nullptr; }
int XCloseDisplay(void*) { return 0; }
int XQueryKeymap(void*, char*) { return 0; }
}
