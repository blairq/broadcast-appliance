// ============================================================================
// src/app/escenas/FuenteGrilla.cpp — ver FuenteGrilla.hpp
// ============================================================================

#include "escenas/FuenteGrilla.hpp"

#include "emision/FuentesApp.hpp"
#include "escenas/GrillaPersonas.hpp"

#include <string>

namespace ba::app::grilla {

using json = nlohmann::json;

json ajustesMotor(const json& ajustes) {
    const int canal = ajustes.is_object() ? ajustes.value("canal", 0) : 0;
    // Como el video de un invitado (InputSpecs::ajustesVideoInvitado): BGRA a los
    // cuadros que lleguen, sin normalizar ni la compensación del estudio (va por la
    // hora que le pone el gateway). Sin canal todavía, uno que nadie publica: «sin
    // señal» hasta que ControlGrillas le asigna el suyo.
    return json{{"pipeline", "bavideoanillo canal=BA.Grilla." + std::to_string(canal)},
                {"pipeline_audio", ""},
                {"audio", false},
                {"normalizar", false},
                {"estudio", false}};
}

void registrarTipo() {
    fuentes::Tipo t;
    t.id = kTipo;
    t.nombre = "Grilla de personas";
    t.video = true;
    t.audio = false;
    t.defectos = Config::defectos();
    t.motor = ajustesMotor;
    t.props = [](const json& a) {
        using P = fuentes::Prop;
        std::vector<P> out;
        const auto lista = [](const char* nombre, const char* etiqueta, std::initializer_list<std::pair<const char*, const char*>> ops,
                              const char* ayuda = "") {
            P p{nombre, etiqueta, "list"};
            for (const auto& [v, l] : ops) p.opciones.push_back({{"value", v}, {"label", l}});
            p.ayuda = ayuda;
            return p;
        };
        out.push_back({"_quienes", "Quiénes", "header"});
        out.push_back(lista("origen", "De dónde", {{"ambas", "Conferencia y Asamblea"}, {"sala", "Solo la Conferencia"}, {"asamblea", "Solo la Asamblea"}},
                            "Conferencia: los admitidos (al aire o en la fila de oradores). Asamblea: los integrantes habilitados "
                            "en la sesión remota."));
        out.push_back(lista("orden", "Orden",
                            {{"llegada", "Por llegada"}, {"habla", "Quien habla, primero"}, {"prioridad", "Por prioridad"}},
                            "Decide quiénes entran cuando no entran todos y dónde va el que llega; a los que ya están no los "
                            "mueve. Prioridad: el campo «prioridad» del integrante (Asamblea); en la Conferencia, el presentador fijo."));
        {
            P tope{"tope", "Celdas como mucho", "int", 0, kMaxCeldas, 1};
            tope.ayuda = "Si no entran todos, la última celda es un cuadro «+N». 0 = sin tope (hasta 64).";
            out.push_back(tope);
        }
        out.push_back({"solo_con_camara", "Solo los que tienen la cámara prendida", "bool"});
        {
            P aire{"sin_al_aire", "Sin los que ya están al aire en otra fuente", "bool"};
            aire.ayuda = "El invitado que ocupa un lugar del programa, el integrante con la palabra cuando su canal o la "
                         "banca están al aire, y quien ya se ve en otra grilla al aire (también en una escena puesta en "
                         "una capa DSK).";
            out.push_back(aire);
        }
        out.push_back({"_reparto", "Reparto entre grillas", "header"});
        {
            P grupo{"grupo", "Grupo", "text"};
            grupo.ayuda = "Las grillas con el mismo grupo se reparten la gente sin repetir (por ejemplo, una por escena). "
                          "Vacío = esta grilla muestra a todos. La misma fuente en dos escenas es la misma imagen.";
            out.push_back(grupo);
        }
        if (!a.value("grupo", std::string()).empty()) {
            P ord{"orden_en_grupo", "Lugar en el grupo", "int", -1000, 1000, 1};
            ord.ayuda = "La primera del grupo (el número más chico) manda el orden y el modo.";
            out.push_back(ord);
            out.push_back(lista("reparto", "Cómo se reparten",
                                {{"en_orden", "Llenar en orden (la primera hasta su tope, después la siguiente)"},
                                 {"parejo", "Parejo (la misma cantidad en cada una)"}}));
        }
        out.push_back({"_aspecto", "Aspecto", "header"});
        out.push_back({"ancho", "Ancho", "int", 64, 3840, 16, "px"});
        out.push_back({"alto", "Alto", "int", 64, 2160, 16, "px"});
        out.push_back(lista("proporcion", "Forma de cada celda",
                            {{"16:9", "16:9"}, {"4:3", "4:3"}, {"1:1", "Cuadrada"}, {"3:4", "3:4 (retrato)"}, {"9:16", "9:16 (celular)"},
                             {"llenar", "Llenar el lugar"}}));
        out.push_back({"separacion", "Separación", "int", 0, 200, 1, "px"});
        out.push_back({"margen", "Margen", "int", 0, 500, 1, "px"});
        out.push_back({"resaltar", "Resaltar a quien habla", "bool"});
        out.push_back({"color_resaltar", "Color del resaltado", "color"});
        {
            P fondo{"fondo", "Fondo", "color"};
            fondo.ayuda = "De fábrica, transparente: se ve la escena entre las celdas.";
            out.push_back(fondo);
        }
        {
            P tr{"transicion_ms", "Transición al reacomodar", "int", 0, 3000, 50, "ms"};
            tr.ayuda = "Cuánto tarda cada cara en ir a su lugar nuevo cuando entra o sale alguien (0 = de golpe).";
            out.push_back(tr);
        }
        {
            const int canal = a.value("canal", 0);
            P info{"_canal",
                   canal > 0 ? "La arma la conferencia en el canal BA.Grilla." + std::to_string(canal) + "."
                             : "Todavía sin canal: hay más de 8 grillas, o la conferencia no arrancó.",
                   "info"};
            out.push_back(info);
        }
        return out;
    };
    fuentes::registrar(std::move(t));
}

} // namespace ba::app::grilla
