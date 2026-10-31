/* La versión de ba-web. La lee también el gateway (src/gateway/BaWeb.cpp): la configuración
 * que le escribe usa directivas propias de esta versión, así que antes de lanzarlo le pregunta
 * (`ba-web -v`) y, si no coincide, no lo usa (el túnel va directo al gateway). Sube cuando
 * cambia lo que entiende la configuración. */
#ifndef BW_VERSION_H
#define BW_VERSION_H
#define BW_VERSION "1.0"
#endif
