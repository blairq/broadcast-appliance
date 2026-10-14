// src/app/emision/FuentePdf.cpp — ver FuentePdf.hpp
#include "emision/FuentePdf.hpp"

#include "emision/FuentesApp.hpp"

#include <mutex>

namespace ba::app::fuentespdf {

using json = nlohmann::json;

namespace {
std::mutex gMu;
std::function<json()> gOpciones;
} // namespace

void ponerOpciones(std::function<json()> f) {
    std::lock_guard<std::mutex> l(gMu);
    gOpciones = std::move(f);
}

void registrar() {
    fuentes::Tipo t;
    t.id = kTipo;
    t.nombre = "PDF (una página por vez)";
    t.deApp = true;
    t.defectos = {{"documento", ""}, {"archivo", ""}, {"pagina", 1}, {"zoom", 1.0}, {"x", 0.5}, {"y", 0.5}, {"ancho", 1920}, {"alto", 1080},
                  {"fondo", static_cast<std::int64_t>(0xFF000000)}};
    t.props = [](const json& a) {
        using P = fuentes::Prop;
        P doc{"documento", "Qué PDF", "list"};
        doc.opciones = json::array({{{"value", ""}, {"label", "Otro archivo (abajo)"}}});
        {
            std::lock_guard<std::mutex> l(gMu);
            if (gOpciones)
                for (const auto& o : gOpciones()) doc.opciones.push_back(o);
        }
        doc.ayuda = "Los de la documentación del evento (pestaña Documentos). Las páginas se pasan desde la "
                    "tarjeta «PDF» del tablero.";
        std::vector<P> out{doc};
        if (a.value("documento", std::string()).empty()) {
            P arch{"archivo", "Archivo PDF", "path"};
            arch.ayuda = "Un PDF que no está en la documentación del evento.";
            out.push_back(arch);
        }
        out.push_back({"pagina", "Página", "int", 1, 100000, 1});
        out.push_back({"zoom", "Zoom (1 = la página entera)", "float", 1, 8, 0.1});
        out.push_back({"x", "Centro horizontal (0 a 1)", "float", 0, 1, 0.01});
        out.push_back({"y", "Centro vertical (0 a 1)", "float", 0, 1, 0.01});
        out.push_back({"ancho", "Ancho del lienzo (píxeles)", "int", 320, 7680, 16});
        out.push_back({"alto", "Alto del lienzo (píxeles)", "int", 240, 4320, 16});
        P fondo{"fondo", "Fondo", "color"};
        fondo.ayuda = "Lo que queda alrededor de la página (no se deforma para llenar).";
        out.push_back(fondo);
        return out;
    };
    fuentes::registrar(std::move(t));
}

} // namespace ba::app::fuentespdf
