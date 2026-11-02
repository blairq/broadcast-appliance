// ============================================================================
// src/app/escenas/FuenteGrilla.cpp — ver FuenteGrilla.hpp
// ============================================================================

#include "escenas/FuenteGrilla.hpp"

#include "emision/FuentesApp.hpp"
#include "escenas/GrillaPersonas.hpp"
#include "i18n/I18n.hpp"

#include <string>

namespace ba::app::grilla {

using json = nlohmann::json;
using ba::i18n::llenar;
using ba::i18n::traducir;

namespace {
std::function<void(const std::string&)>& armarEscena() {
    static std::function<void(const std::string&)> fn;
    return fn;
}
} // namespace

void ponerArmarEscenaOrador(std::function<void(const std::string&)> fn) { armarEscena() = std::move(fn); }

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
    t.nombre = traducir("escenas", "Grilla de personas");
    t.video = true;
    t.audio = false;
    t.defectos = Config::defectos();
    t.motor = [](const json& a, int, int) { return ajustesMotor(a); };
    t.botones["armar_escena_orador"] = [](const std::string& fuente) {
        if (armarEscena()) armarEscena()(fuente);
    };
    t.props = [](const json& a) {
        using P = fuentes::Prop;
        std::vector<P> out;
        const auto lista = [](const char* nombre, const std::string& etiqueta,
                              std::initializer_list<std::pair<const char*, std::string>> ops, const std::string& ayuda = "") {
            P p{nombre, etiqueta, "list"};
            for (const auto& [v, l] : ops) p.opciones.push_back({{"value", v}, {"label", l}});
            p.ayuda = ayuda;
            return p;
        };
        out.push_back({"_quienes", traducir("escenas", "Quiénes"), "header"});
        out.push_back(lista("origen", traducir("escenas", "De dónde"), {{"ambas", traducir("escenas", "Conferencia y Asamblea")}, {"sala", traducir("escenas", "Solo la Conferencia")}, {"asamblea", traducir("escenas", "Solo la Asamblea")}},
                            traducir("escenas", "Conferencia: los admitidos (al aire o en la fila de oradores). Asamblea: los integrantes habilitados "
                            "en la sesión remota.")));
        out.push_back(lista("orden", traducir("escenas", "Orden"),
                            {{"llegada", traducir("escenas", "Por llegada")}, {"habla", traducir("escenas", "Quien habla, primero")}, {"prioridad", traducir("escenas", "Por prioridad")}},
                            traducir("escenas", "Decide quiénes entran cuando no entran todos y dónde va el que llega; a los que ya están no los "
                            "mueve. Prioridad: el campo «prioridad» del integrante (Asamblea); en la Conferencia, el presentador fijo.")));
        {
            P tope{"tope", traducir("escenas", "Celdas como mucho"), "int", 0, kMaxCeldas, 1};
            tope.ayuda = traducir("escenas", "Si no entran todos, la última celda es un cuadro «+N». 0 = sin tope (hasta 64).");
            out.push_back(tope);
        }
        out.push_back({"solo_con_camara", traducir("escenas", "Solo los que tienen la cámara prendida"), "bool"});
        {
            P aire{"sin_al_aire", traducir("escenas", "Sin los que ya están al aire en otra fuente"), "bool"};
            aire.ayuda = traducir("escenas", "El invitado que ocupa un lugar del programa, el integrante con la palabra cuando su canal o la "
                         "banca están al aire, y quien ya se ve en otra grilla al aire (también en una escena puesta en "
                         "una capa DSK).");
            out.push_back(aire);
        }
        out.push_back({"_reparto", traducir("escenas", "Reparto entre grillas"), "header"});
        {
            P grupo{"grupo", traducir("escenas", "Grupo"), "text"};
            grupo.ayuda = traducir("escenas", "Las grillas con el mismo grupo se reparten la gente sin repetir (por ejemplo, una por escena). "
                          "Vacío = esta grilla muestra a todos. La misma fuente en dos escenas es la misma imagen.");
            out.push_back(grupo);
        }
        if (!a.value("grupo", std::string()).empty()) {
            P ord{"orden_en_grupo", traducir("escenas", "Lugar en el grupo"), "int", -1000, 1000, 1};
            ord.ayuda = traducir("escenas", "La primera del grupo (el número más chico) manda el orden y el modo.");
            out.push_back(ord);
            out.push_back(lista("reparto", traducir("escenas", "Cómo se reparten"),
                                {{"en_orden", traducir("escenas", "Llenar en orden (la primera hasta su tope, después la siguiente)")},
                                 {"parejo", traducir("escenas", "Parejo (la misma cantidad en cada una)")}}));
        }
        out.push_back({"_aspecto", traducir("escenas", "Aspecto"), "header"});
        out.push_back({"ancho", traducir("escenas", "Ancho"), "int", 64, 3840, 16, "px"});
        out.push_back({"alto", traducir("escenas", "Alto"), "int", 64, 2160, 16, "px"});
        out.push_back(lista("proporcion", traducir("escenas", "Forma de cada celda"),
                            {{"16:9", "16:9"}, {"4:3", "4:3"}, {"1:1", traducir("escenas", "Cuadrada")}, {"3:4", traducir("escenas", "3:4 (retrato)")},
                             {"9:16", traducir("escenas", "9:16 (celular)")},
                             {"llenar", traducir("escenas", "Llenar el lugar")}}));
        out.push_back({"separacion", traducir("escenas", "Separación"), "int", 0, 200, 1, "px"});
        out.push_back({"margen", traducir("escenas", "Margen"), "int", 0, 500, 1, "px"});

        // --- Cuando alguien habla ---------------------------------------------------------
        const bool soloOrador = a.value("muestra", std::string("todos")) == "orador";
        out.push_back({"_habla", traducir("escenas", "Cuando alguien habla"), "header"});
        out.push_back(lista("muestra", traducir("escenas", "Qué muestra"),
                            {{"todos", traducir("escenas", "A todos")},
                             {"orador", traducir("escenas", "Solo al orador de otra grilla (la escena del orador)")}},
                            traducir("escenas", "«Solo al orador»: para armar la escena del orador (grande y con su nombre) que el operador "
                            "o la línea de tiempo ponen al aire, mientras la grilla completa sigue en otra salida.")));
        if (soloOrador) {
            P sigue{"sigue", traducir("escenas", "De qué grilla"), "text"};
            sigue.ayuda = traducir("escenas", "El nombre de la grilla cuyo orador muestra. Vacío = la primera que tenga orador.");
            out.push_back(sigue);
        } else {
            const std::string modo = a.value("al_hablar", std::string("resaltar"));
            out.push_back(lista("al_hablar", traducir("escenas", "Cómo se lo muestra"),
                                {{"resaltar", traducir("escenas", "Resaltar en su lugar")},
                                 {"destacar", traducir("escenas", "Destacar (grande, los demás en una franja)")},
                                 {"superponer", traducir("escenas", "Superponer sin tapar")},
                                 {"escena", traducir("escenas", "Cortar a la escena del orador")},
                                 {"nada", traducir("escenas", "Nada (solo el borde)")}},
                                traducir("escenas", "Resaltar: nadie se mueve; borde y un leve agrandado. Destacar: el orador a un lugar "
                                "grande y los demás a una franja, en el mismo orden. Superponer: el orador encima, en un hueco "
                                "que le deja la grilla (nadie queda tapado). Escena: esta grilla no cambia; otra grilla con «Solo "
                                "al orador» lo muestra en su escena. Al orador se le pide su cámara en alta.")));
            if (modo != "nada") {
                out.push_back(lista("disparo", traducir("escenas", "Quién es el orador"),
                                    {{"voz", traducir("escenas", "El que habla (voz sostenida)")},
                                     {"palabra", traducir("escenas", "El que tiene la palabra (Asamblea)")},
                                     {"orador", traducir("escenas", "El orador al aire (Conferencia)")},
                                     {"manual", traducir("escenas", "A mano")}},
                                    traducir("escenas", "A mano: desde la vista de la grilla o grilla_personas_destacar (también sirve "
                                    "con los otros: lo elegido a mano gana).")));
                if (a.value("disparo", std::string("voz")) == "voz") {
                    P voz{"voz_ms", traducir("escenas", "Voz sostenida"), "int", 0, 10000, 100, "ms"};
                    voz.ayuda = traducir("escenas", "Cuánto tiene que hablar sin parar para pasar a orador.");
                    out.push_back(voz);
                }
                P minimo{"minimo_ms", traducir("escenas", "Tiempo mínimo en pantalla"), "int", 0, 60000, 500, "ms"};
                minimo.ayuda = traducir("escenas", "El orador no cambia antes de esto: que no salte con cada tos.");
                out.push_back(minimo);
                P pre{"precarga", traducir("escenas", "Precargar al siguiente"), "bool"};
                pre.ayuda = traducir("escenas", "El siguiente de la fila (o el que empezó a hablar) ya pide su cámara en alta.");
                out.push_back(pre);
            }
            if (modo == "escena") {
                P boton{"armar_escena_orador", traducir("escenas", "Armar la escena del orador"), "button"};
                boton.ayuda = traducir("escenas", "Crea (si no está) la escena «Orador · <esta grilla>» con una grilla que muestra "
                              "solo al orador de esta, grande y con su nombre y cargo. Ponerla al aire queda en manos del operador "
                              "o de la línea de tiempo.");
                out.push_back(boton);
            }
            if (modo == "destacar") {
                out.push_back(lista("destacar_lugar", traducir("escenas", "Lugar del orador"),
                                    {{"centro", traducir("escenas", "Al centro")}, {"izquierda", traducir("escenas", "A la izquierda")}}));
                out.push_back(lista("franja", traducir("escenas", "Los demás"),
                                    {{"abajo", traducir("escenas", "En una franja abajo")}, {"costado", traducir("escenas", "Al costado")}}));
                out.push_back({"destacar_tamano", traducir("escenas", "Tamaño del orador"), "int", 40, 90, 5, "%"});
            }
            if (modo == "superponer") {
                out.push_back({"superponer_completa", traducir("escenas", "A pantalla completa"), "bool"});
                if (!a.value("superponer_completa", false))
                    out.push_back({"superponer_tamano", traducir("escenas", "Tamaño del hueco"), "int", 30, 90, 5, "%"});
            }
        }
        {
            P fu{"fundido_ms", traducir("escenas", "Fundido a la alta definición"), "int", 0, 2000, 50, "ms"};
            fu.ayuda = traducir("escenas", "Al pasar a grande se pide la cámara en alta: mientras no llega se sigue viendo la baja y, "
                         "cuando llega, se funde de una a la otra (nunca negro).");
            out.push_back(fu);
        }
        out.push_back({"resaltar", traducir("escenas", "Borde a quien habla"), "bool"});
        out.push_back({"color_resaltar", traducir("escenas", "Color del resaltado"), "color"});

        // --- Nombres ------------------------------------------------------------------------
        out.push_back({"_nombres", traducir("escenas", "Nombres"), "header"});
        out.push_back(lista("nombres", traducir("escenas", "Nombre en cada celda"),
                            {{"no", traducir("escenas", "Sin nombre")},
                             {"nombre", traducir("escenas", "El nombre")},
                             {"nombre_cargo", traducir("escenas", "El nombre y el cargo o la banca")}}));
        if (a.value("nombres", std::string("no")) != "no") {
            out.push_back({"nombre_tamano", traducir("escenas", "Alto del rótulo"), "int", 5, 40, 1, "%"});
            out.push_back(lista("nombre_lugar", traducir("escenas", "Dónde"),
                                {{"abajo", traducir("escenas", "Abajo")}, {"arriba", traducir("escenas", "Arriba")}}));
            out.push_back({"nombre_color", traducir("escenas", "Color del texto"), "color"});
            out.push_back({"nombre_fondo", traducir("escenas", "Fondo del rótulo"), "color"});
        }
        {
            P fondo{"fondo", traducir("escenas", "Fondo"), "color"};
            fondo.ayuda = traducir("escenas", "De fábrica, transparente: se ve la escena entre las celdas.");
            out.push_back(fondo);
        }
        {
            P tr{"transicion_ms", traducir("escenas", "Transición al reacomodar"), "int", 0, 3000, 50, "ms"};
            tr.ayuda = traducir("escenas", "Cuánto tarda cada cara en ir a su lugar nuevo cuando entra o sale alguien (0 = de golpe).");
            out.push_back(tr);
        }
        {
            const int canal = a.value("canal", 0);
            P info{"_canal",
                   canal > 0 ? llenar(traducir("escenas", "La arma la conferencia en el canal %1."),
                                      {"BA.Grilla." + std::to_string(canal)})
                             : traducir("escenas", "Todavía sin canal: hay más de 8 grillas, o la conferencia no arrancó."),
                   "info"};
            out.push_back(info);
        }
        return out;
    };
    fuentes::registrar(std::move(t));
}

} // namespace ba::app::grilla
