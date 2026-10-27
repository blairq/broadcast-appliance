// ============================================================================
// src/app/emision/TiposBasicos.cpp — las propiedades de los tipos de fuente de siempre
//
// Color, texto, imagen y medio: lo que muestra la ventana Propiedades (con los
// nombres de ajustes que ya tienen los app.json, que vienen de OBS). Los demás
// tipos se editan igual con sus ajustes tal cual (PropertiesModel).
// ============================================================================
#include "emision/BroadcastEngine.hpp"
#include "i18n/I18n.hpp"
#include "emision/FuentesApp.hpp"
#include "emision/InputSpecs.hpp"

namespace ba::app {

void registrarTiposBasicos() {
    using fuentes::Prop;
    using fuentes::Tipo;
    using json = nlohmann::json;
    {
        Tipo t;
        t.id = "color_source_v3";
        t.nombre = ba::i18n::traducir("fuentes", "Color");
        t.defectos = {{"color", 0xFFD1D1D1}, {"width", 1920}, {"height", 1080}};
        t.props = [](const json&) {
            return std::vector<Prop>{{"color", ba::i18n::traducir("fuentes", "Color"), "color"},
                                     {"width", ba::i18n::traducir("fuentes", "Ancho"), "int", 1, 7680, 1},
                                     {"height", ba::i18n::traducir("fuentes", "Alto"), "int", 1, 4320, 1}};
        };
        fuentes::registrar(t);
        t.id = "color_source";
        fuentes::registrar(t);
    }
    {
        Tipo t;
        t.id = "text_ft2_source_v2";
        t.nombre = ba::i18n::traducir("fuentes", "Texto");
        t.defectos = {{"text", ""}, {"font", {{"face", "Helvetica"}, {"size", 48}}}, {"color1", 0xFFFFFFFF}, {"outline", false}};
        t.props = [](const json&) {
            return std::vector<Prop>{{"text", ba::i18n::traducir("fuentes", "Texto"), "textarea"},
                                     {"font", ba::i18n::traducir("fuentes", "Letra"), "font"},
                                     {"color1", ba::i18n::traducir("fuentes", "Color"), "color"},
                                     {"outline", ba::i18n::traducir("fuentes", "Contorno"), "bool"}};
        };
        for (const char* id : {"text_ft2_source_v2", "text_ft2_source", "text_gdiplus", "text_gdiplus_v2", "ba_texto"}) {
            t.id = id;
            fuentes::registrar(t);
        }
    }
    {
        Tipo t;
        t.id = "image_source";
        t.nombre = ba::i18n::traducir("fuentes", "Imagen");
        t.props = [](const json&) {
            Prop p{"file", ba::i18n::traducir("fuentes", "Archivo"), "path"};
            p.ayuda = ba::i18n::traducir("fuentes", "PNG, JPEG, WebP… (con su transparencia)");
            return std::vector<Prop>{p};
        };
        fuentes::registrar(t);
    }
    {
        Tipo t;
        t.id = inputs::kMedioType;
        t.nombre = ba::i18n::traducir("fuentes", "Video o audio de un archivo");
        t.audio = true;
        t.defectos = {{"looping", false}, {"clear_on_media_end", false}};
        t.props = [](const json&) {
            return std::vector<Prop>{{"local_file", ba::i18n::traducir("fuentes", "Archivo"), "path"},
                                     {"looping", ba::i18n::traducir("fuentes", "Repetir"), "bool"},
                                     {"clear_on_media_end", ba::i18n::traducir("fuentes", "Desaparecer al terminar"), "bool"}};
        };
        fuentes::registrar(t);
    }
    {
        // La escena la resuelve al publicarse (la toma de la banca de su ranura).
        Tipo t;
        t.id = BroadcastEngine::kTipoBanca;
        t.nombre = ba::i18n::traducir("fuentes", "Banca con la palabra");
        t.defectos = {{"ranura", 1}};
        t.props = [](const json&) {
            Prop p{"ranura", ba::i18n::traducir("fuentes", "Muestra"), "list"};
            p.opciones = json::array({{{"value", 1}, {"label", ba::i18n::traducir("fuentes", "Quien tiene la palabra (en un debate, el primero)")}}});
            for (int r = 2; r <= BroadcastEngine::kRanurasBanca; ++r)
                p.opciones.push_back({{"value", r}, {"label", r == 2 ? ba::i18n::traducir("fuentes", "El segundo de un debate")
                                                                     : ba::i18n::llenar(ba::i18n::traducir("fuentes", "El %1º de un debate"),
                                                                                        {std::to_string(r)})}});
            p.ayuda = ba::i18n::traducir("fuentes", "La cámara y la posición de cada banca se eligen en Asamblea › Recinto (la toma de la banca).");
            return std::vector<Prop>{p};
        };
        fuentes::registrar(t);
    }
}

} // namespace ba::app
