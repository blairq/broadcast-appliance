// ============================================================================
// src/app/video/ElementosGst.cpp — ver ElementosGst.hpp
// ============================================================================

#include "video/ElementosGst.hpp"

#include "diag/Registro.hpp"
#include "video/CatalogoGst.hpp"

#include <thread>

#if defined(BA_HAVE_GST)
#include <gst/gst.h>
#endif

namespace ba::app::video {

namespace {
struct Elemento {
    const char* nombre;
    const char* para;
    bool imprescindible; ///< sin él algo de hoy no anda (los demás son para lo que viene)
};
// Los de la app (barack, bavoz, bandisrc) se registran solos al arrancar: van
// igual en la lista para que se note si no se registraron.
const Elemento kElementos[] = {
    {"uridecodebin3", "cámaras de red y archivos", true},
    {"audioconvert", "todas las entradas de audio", true},
    {"audioresample", "todas las entradas de audio", true},
    {"videoconvert", "todas las entradas de video", true},
    {"videorate", "normalizar al lienzo", true},
    {"deinterlace", "normalizar al lienzo", true},
    {"appsink", "entregar a la mezcla", true},
    {"baanillo", "audio de los invitados y del remoto", true},
    {"bavideoanillo", "video de los invitados y del remoto", true},
    {"baautomixer", "automixer de las bancas", true},
    {"barack", "racks de audio (LSP/CLAP)", true},
    {"bavoz", "voces del asistente y del teleprompter", true},
    {"avdec_speedhq", "NDI (decodificar SpeedHQ)", true},
    {"bandisrc", "NDI (recibir)", false},
    {"level", "medidores", true},
    {"ebur128level", "loudness del programa (T11)", false},
    {"intersink", "motor sin libobs: unir pipelines", false},
    {"intersrc", "motor sin libobs: unir pipelines", false},
    {"fallbackswitch", "robustez: placa «sin señal» sin corte", false},
    {"livesync", "robustez: tapar microcortes", false},
    {"audiomixer", "motor sin libobs: mezcla de audio", false},
    {"compositor", "motor sin libobs: composición (CPU)", false},
    {"glvideomixer", "motor sin libobs: composición (placa)", false},
    {"cccombiner", "subtítulos 608/708", false},
    {"tttocea608", "subtítulos 608", false},
    {"h264ccinserter", "subtítulos en H.264", false},
    {"rtmp2sink", "transmitir (motor sin libobs)", false},
    {"srtsink", "SRT (motor sin libobs)", false},
    {"splitmuxsink", "grabación en tramos (motor sin libobs)", false},
};
} // namespace

nlohmann::json revisarElementosGst() {
    nlohmann::json out = nlohmann::json::array();
#if defined(BA_HAVE_GST)
    for (const auto& e : kElementos) {
        GstElementFactory* f = gst_is_initialized() ? gst_element_factory_find(e.nombre) : nullptr;
        out.push_back({{"elemento", e.nombre}, {"para", e.para}, {"esta", f != nullptr}, {"imprescindible", e.imprescindible}});
        if (f != nullptr) gst_object_unref(f);
    }
#endif
    return out;
}

void revisarElementosGstAlArrancar() {
    std::thread([]() {
        if (!iniciarGStreamer()) return;
        // Los de la app se registran en otros hilos al arrancar: un rato de gracia.
        std::this_thread::sleep_for(std::chrono::seconds(8));
        for (const auto& e : revisarElementosGst())
            if (!e.value("esta", true))
                ba::diag::registro().linea(e.value("imprescindible", false) ? ba::diag::Nivel::Error : ba::diag::Nivel::Aviso,
                                           "gstreamer", 0,
                                           "falta el elemento " + e.value("elemento", std::string()) + " (" +
                                               e.value("para", std::string()) + ")");
    }).detach();
}

} // namespace ba::app::video
