// src/app/emision/FuenteQr.cpp — ver FuenteQr.hpp
#include "emision/FuenteQr.hpp"

#include "emision/FuentesApp.hpp"

#include <mutex>

namespace ba::app::fuentesqr {

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
    t.nombre = "Código QR";
    t.deApp = true;
    t.defectos = {{"qr", "aparato:publico"}, {"texto", ""}, {"fondo", true}, {"tamano", 720}};
    t.props = [](const json& a) {
        using P = fuentes::Prop;
        P qr{"qr", "Qué QR", "list"};
        qr.opciones = json::array({{{"value", ""}, {"label", "Escribirlo a mano (abajo)"}}});
        {
            std::lock_guard<std::mutex> l(gMu);
            if (gOpciones)
                for (const auto& o : gOpciones()) qr.opciones.push_back(o);
        }
        qr.ayuda = "Los del aparato (Público, Conferencia, Prompter, Rótulos) se mantienen al día solos: si cambia la "
                   "clave o el PIN, el QR cambia. Los guardados se diseñan en «Códigos QR».";
        std::vector<P> out{qr};
        if (a.value("qr", std::string()).empty()) {
            P texto{"texto", "Qué codifica", "text"};
            texto.ayuda = "Un enlace, un texto…";
            out.push_back(texto);
        }
        P fondo{"fondo", "Fondo blanco alrededor", "bool"};
        fondo.ayuda = "Con un margen blanco se escanea sobre cualquier imagen.";
        out.push_back(fondo);
        out.push_back({"tamano", "Tamaño (píxeles)", "int", 128, 2160, 16});
        return out;
    };
    fuentes::registrar(std::move(t));
}

} // namespace ba::app::fuentesqr
