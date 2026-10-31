// ============================================================================
// src/motor/Fallas.cpp — ver Fallas.hpp
// ============================================================================
#include "Fallas.hpp"

#include "GpuPerdida.hpp"
#include "Reloj.hpp"
#include "fault/FallasProgramadas.hpp"
#include "i18n/I18n.hpp"

namespace ba::motor {

namespace {

/// Lo que llega a `pad` se tira: la pieza «rota» no saca (o no recibe) nada más.
void cortar(GstPad* pad) {
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, +[](GstPad*, GstPadProbeInfo*, gpointer) { return GST_PAD_PROBE_DROP; }, nullptr,
                      nullptr);
}

void avisarError(GstElement* e, GQuark dominio, gint codigo, const char* texto) {
    GError* err = g_error_new_literal(dominio, codigo, texto);
    gst_element_post_message(e, gst_message_new_error(GST_OBJECT(e), err, "falla inyectada por el banco de caos (diag_falla)"));
    g_error_free(err);
}

} // namespace

void aplicarFallasProgramadas(GstElement* pipeline, const std::string& salida, GstElement* codificacion) {
    auto& f = ba::fault::FallasProgramadas::instancia();
    if (pipeline == nullptr || !f.hay()) return;
    const std::string pieza = f.tomar(salida);
    if (pieza.empty()) return;
    const bool codificador = pieza == "codificador";
    GstElement* donde = codificador && codificacion != nullptr ? codificacion : pipeline;
    GstElement* e = gst_bin_get_by_name(GST_BIN(donde), codificador ? "codificador" : salida == "grabacion" ? "mux" : "red");
    if (e == nullptr) {
        registrar(Nivel::Aviso, ba::i18n::llenar(ba::i18n::traducir("motor", "falla inyectada: %1 de %2 no está en el pipeline"), {pieza, salida}));
        return;
    }
    registrar(Nivel::Aviso, ba::i18n::llenar(ba::i18n::traducir("motor", "falla inyectada (banco de caos): %1 de %2"), {pieza, salida}));
    if (codificador) {
        if (GstPad* sale = gst_element_get_static_pad(e, "src")) {
            cortar(sale);
            gst_object_unref(sale);
        }
        avisarError(e, GST_STREAM_ERROR, GST_STREAM_ERROR_ENCODE, "el codificador dejó de andar (falla inyectada)");
    } else {
        gst_element_foreach_sink_pad(
            e, +[](GstElement*, GstPad* p, gpointer) -> gboolean {
                cortar(p);
                return TRUE;
            },
            nullptr);
        avisarError(e, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_WRITE, "no se pudo escribir (falla inyectada)");
    }
    gst_object_unref(e);
}

void aplicarFallaDeGpu(GstElement* mezclador) {
    auto& f = ba::fault::FallasProgramadas::instancia();
    if (mezclador == nullptr || !f.hay() || f.tomar("video").empty()) return;
    registrar(Nivel::Aviso, ba::i18n::traducir("motor", "falla inyectada (banco de caos): la GPU del video se pierde"));
    if (GstPad* sale = gst_element_get_static_pad(mezclador, "src")) {
        cortar(sale);
        gst_object_unref(sale);
    }
    avisarError(mezclador, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED, textoDeGpuPerdida(gpu()).c_str());
}

} // namespace ba::motor
