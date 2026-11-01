// src/app/tests/test_resumen_nodo.cpp — el resumen de la tarjeta de un nodo de video nunca tira
//
// Regresión de a42d4a31: un nodo con «controlador» como texto (el id del controlador)
// tiraba type_error en resumenDeNodo (value<bool> con un texto) y la app se caía al
// arrancar. El grafo viene de disco y de versiones anteriores: cualquier campo puede
// tener otro tipo, y el resumen tiene que salir igual (sin la app: QCoreApplication::
// translate sin instancia devuelve el original).

#include "ui/ResumenNodo.hpp"

#include <cstdio>
#include <exception>
#include <string>

using ba::app::conResumenes;
using ba::app::resumenDeNodo;
using json = nlohmann::json;

namespace {
int fallos = 0;
#define CHECK(c, m)                                                                                          \
    do {                                                                                                     \
        if (c)                                                                                               \
            std::printf("ok: %s\n", m);                                                                      \
        else {                                                                                               \
            std::printf("FALLO: %s (%s, linea %d)\n", m, #c, __LINE__);                                      \
            ++fallos;                                                                                        \
        }                                                                                                    \
    } while (0)

/// El resumen, o «EXCEPCIÓN: …» si tiró.
std::string resumen(const json& n) {
    try {
        return resumenDeNodo(n, 1).toStdString();
    } catch (const std::exception& e) {
        return std::string("EXCEPCIÓN: ") + e.what();
    }
}
} // namespace

int main() {
    // El caso de a42d4a31: «controlador» es el id (texto).
    const json conId = {{"id", "n1"}, {"tipo", "color"}, {"controlador", "ctl-1"}, {"params", {{"local", json::object()}}}};
    CHECK(resumen(conId) == "↳ del controlador", "controlador como texto (el id): del controlador, sin excepción");
    CHECK(resumen({{"tipo", "color"}, {"controlador", "ctl-1"}, {"params", {{"local", {{"brillo", 0.2}}}}}}) ==
              "↳ del controlador · con ajuste propio",
          "controlador como texto con ajuste propio");
    CHECK(resumen({{"tipo", "color"}, {"controlador", true}}) == "↳ del controlador", "controlador true");
    CHECK(resumen({{"tipo", "color"}, {"controlador", false}}).rfind("brillo", 0) == 0, "controlador false: el resumen del tipo");
    CHECK(resumen({{"tipo", "color"}, {"controlador", ""}}).rfind("brillo", 0) == 0, "controlador texto vacío: el resumen del tipo");
    CHECK(resumen({{"tipo", "color"}, {"controlador", 3}}).rfind("brillo", 0) == 0, "controlador número: no tira");

    // Cualquier campo con otro tipo: ningún tipo de nodo tira.
    const char* tipos[] = {"entrada", "escena", "obs", "control", "gst", "compositor", "mezcla", "color", "croma", "recorte",
                           "vista360", "panoramica", "salida", "retardo", "ptz", "seguir", "rostros", "otro"};
    const json raros[] = {json("texto"), json(7), json(true), json(nullptr), json::array({1, "a"}), json::object({{"x", 1}})};
    int tiraron = 0;
    std::string primero;
    for (const char* tipo : tipos)
        for (const json& r : raros) {
            json params = json::object();
            for (const char* k : {"filtro", "tarjeta", "params", "elemento", "propiedades", "ancho", "alto", "brillo", "clave",
                                  "similitud", "izquierda", "espejoH", "espejoV", "ver", "guia", "yaw", "fov", "nombre", "cuadros",
                                  "activo", "objetivo", "uso", "marcar", "estilo", "texto", "local"})
                params[k] = r;
            for (const json& n : {json{{"tipo", tipo}, {"controlador", r}, {"publicada", r}, {"entradas", r}, {"params", params}},
                                  json{{"tipo", tipo}, {"params", r}}, json{{"tipo", r}}, json{{"tipo", tipo}, {"error", r}}, r}) {
                const std::string s = resumen(n);
                if (s.rfind("EXCEPCIÓN", 0) == 0) {
                    ++tiraron;
                    if (primero.empty()) primero = std::string(tipo) + " con " + r.dump() + ": " + s;
                }
            }
        }
    if (tiraron) std::printf("  primero: %s\n", primero.c_str());
    CHECK(tiraron == 0, "ningún nodo con campos de otro tipo tira");

    // El grafo entero: cada nodo con su resumen (el de a42d4a31 incluido).
    json g = {{"nodos", json::array({conId, {{"id", "m"}, {"tipo", "mezcla"}, {"params", {{"elemento", "compositor"}}}}, 5})},
              {"cables", json::array({{{"desde", "n1"}, {"hacia", "m"}}, "roto"})}};
    try {
        g = conResumenes(g);
        CHECK(g["nodos"][0]["resumen"] == "↳ del controlador", "conResumenes: el nodo con controlador texto");
        CHECK(g["nodos"][1]["resumen"].get<std::string>().find("1 conectada") != std::string::npos, "conResumenes: la mezcla cuenta su cable");
    } catch (const std::exception& e) {
        std::printf("  %s\n", e.what());
        CHECK(false, "conResumenes no tira");
    }

    std::printf("%s\n", fallos ? "HAY FALLOS" : "todo bien");
    return fallos ? 1 : 0;
}
