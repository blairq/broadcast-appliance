// src/core/platform/Herramientas.hpp — ffmpeg, ffprobe, yt-dlp, deno y las de
// GStreamer, a mano de todo lo que lanza el programa
//
// Las herramientas de medios viajan adentro del bundle (herramientas/, ver
// tools/deps/herramientas.sh). `prepararHerramientas` las pone PRIMERAS en el
// PATH del propio proceso al arrancar, junto con las de GStreamer
// (gst-launch-1.0, gst-discoverer-1.0…, con su entorno: prepararGst). Todo lo
// que se lanza después las hereda: los plugins, la terminal del asistente, los
// comandos que corre el agente. Así `ffmpeg -i …` o `yt-dlp URL` andan sin
// bajar nada ni depender de lo que tenga instalado la máquina.
//
// También deja BA_HERRAMIENTAS (la carpeta) y BA_FFMPEG, BA_FFPROBE, BA_YTDLP,
// BA_GST_LAUNCH (rutas completas) para quien prefiera no fiarse del PATH.
#pragma once

#include <filesystem>

namespace ba::platform {

/// La carpeta herramientas/ del bundle (vacía si no está). En desarrollo,
/// BA_HERRAMIENTAS_RAIZ la puede indicar.
std::filesystem::path herramientasDir();

/// Ruta completa de una herramienta («ffmpeg», «yt-dlp», «gst-launch-1.0»…;
/// sin .exe), o vacía si no está en el bundle.
std::filesystem::path herramienta(const char* nombre);

/// Arma el PATH y las variables. `extra` (si existe) va antes que todo: una
/// carpeta de los datos donde se puede dejar, p. ej., un yt-dlp más nuevo sin
/// rehacer el programa. Devuelve false si el bundle no trae herramientas.
bool prepararHerramientas(const std::filesystem::path& extra = {});

} // namespace ba::platform
