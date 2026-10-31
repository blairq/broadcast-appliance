// ============================================================================
// src/app/video/ElementosGst.cpp — ver ElementosGst.hpp
// ============================================================================

#include "video/ElementosGst.hpp"
#include "ajustes/Ajustes.hpp"

#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"
#include "video/CatalogoGst.hpp"
#include "platform/Hilo.hpp"

#include <QtGlobal>

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
    {"uridecodebin3", QT_TRANSLATE_NOOP("gstreamer", "cámaras de red y archivos"), true},
    {"audioconvert", QT_TRANSLATE_NOOP("gstreamer", "todas las entradas de audio"), true},
    {"audioresample", QT_TRANSLATE_NOOP("gstreamer", "todas las entradas de audio"), true},
    {"videoconvert", QT_TRANSLATE_NOOP("gstreamer", "todas las entradas de video"), true},
    {"videorate", QT_TRANSLATE_NOOP("gstreamer", "normalizar al lienzo"), true},
    {"deinterlace", QT_TRANSLATE_NOOP("gstreamer", "normalizar al lienzo"), true},
    {"appsink", QT_TRANSLATE_NOOP("gstreamer", "entregar a la mezcla"), true},
    {"baanillo", QT_TRANSLATE_NOOP("gstreamer", "audio de los invitados y del remoto"), true},
    {"bavideoanillo", QT_TRANSLATE_NOOP("gstreamer", "video de los invitados y del remoto"), true},
    {"baautomixer", QT_TRANSLATE_NOOP("gstreamer", "automixer de las bancas"), true},
    {"barack", QT_TRANSLATE_NOOP("gstreamer", "racks de audio (LSP/CLAP)"), true},
    {"bavoz", QT_TRANSLATE_NOOP("gstreamer", "voces del asistente y del teleprompter"), true},
    {"avdec_speedhq", QT_TRANSLATE_NOOP("gstreamer", "NDI (decodificar SpeedHQ)"), true},
    {"bandisrc", QT_TRANSLATE_NOOP("gstreamer", "NDI (recibir)"), false},
    {"level", QT_TRANSLATE_NOOP("gstreamer", "medidores"), true},
    {"ebur128level", QT_TRANSLATE_NOOP("gstreamer", "loudness del programa (T11)"), false},
    {"intersink", QT_TRANSLATE_NOOP("gstreamer", "motor sin libobs: unir pipelines"), false},
    {"intersrc", QT_TRANSLATE_NOOP("gstreamer", "motor sin libobs: unir pipelines"), false},
    {"fallbackswitch", QT_TRANSLATE_NOOP("gstreamer", "robustez: placa «sin señal» sin corte"), false},
    {"livesync", QT_TRANSLATE_NOOP("gstreamer", "robustez: tapar microcortes"), false},
    {"audiomixer", QT_TRANSLATE_NOOP("gstreamer", "motor sin libobs: mezcla de audio"), false},
    {"compositor", QT_TRANSLATE_NOOP("gstreamer", "motor sin libobs: composición (CPU)"), false},
    {"glvideomixer", QT_TRANSLATE_NOOP("gstreamer", "motor sin libobs: composición (placa)"), false},
    {"cccombiner", QT_TRANSLATE_NOOP("gstreamer", "subtítulos 608/708"), false},
    {"tttocea608", QT_TRANSLATE_NOOP("gstreamer", "subtítulos 608"), false},
    {"h264ccinserter", QT_TRANSLATE_NOOP("gstreamer", "subtítulos en H.264"), false},
    {"rtmp2sink", QT_TRANSLATE_NOOP("gstreamer", "transmitir (motor sin libobs)"), false},
    {"srtsink", QT_TRANSLATE_NOOP("gstreamer", "SRT (motor sin libobs)"), false},
    {"splitmuxsink", QT_TRANSLATE_NOOP("gstreamer", "grabación en tramos (motor sin libobs)"), false},
};
} // namespace

nlohmann::json revisarElementosGst() {
    nlohmann::json out = nlohmann::json::array();
#if defined(BA_HAVE_GST)
    for (const auto& e : kElementos) {
        GstElementFactory* f = gst_is_initialized() ? gst_element_factory_find(e.nombre) : nullptr;
        out.push_back({{"elemento", e.nombre}, {"para", ba::i18n::traducir("gstreamer", e.para)}, {"esta", f != nullptr}, {"imprescindible", e.imprescindible}});
        if (f != nullptr) gst_object_unref(f);
    }
#endif
    return out;
}

void revisarElementosGstAlArrancar() {
    std::thread([]() {
        ba::platform::nombrarHilo("gst-elementos");
        if (!iniciarGStreamer()) return;
        // Los de la app se registran en otros hilos al arrancar: un rato de gracia.
        std::this_thread::sleep_for(std::chrono::seconds(ba::ajustes::entero("arranque.revisarElementosS", 8)));
        for (const auto& e : revisarElementosGst())
            if (!e.value("esta", true))
                ba::diag::registro().linea(e.value("imprescindible", false) ? ba::diag::Nivel::Error : ba::diag::Nivel::Aviso,
                                           "gstreamer", 0,
                                           ba::i18n::llenar(ba::i18n::traducir("gstreamer", "falta el elemento %1 (%2)"),
                                                            {e.value("elemento", std::string()),
                                                             e.value("para", std::string())}));
    }).detach();
}

} // namespace ba::app::video
