// test_lista_prompters.cpp — los teleprompters del evento con su nombre (sala/ListaPrompters).
#include <QTemporaryDir>

#include <cstdio>
#include <filesystem>

#include "sala/ListaPrompters.hpp"

using ba::app::sala::ListaPrompters;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                                   \
    do {                                                              \
        if (!(c)) {                                                   \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__);       \
            ++g_fallas;                                               \
        } else {                                                      \
            std::printf("ok: %s\n", m);                               \
        }                                                             \
    } while (0)
} // namespace

int main() {
    QTemporaryDir dir;
    const std::filesystem::path archivo = std::filesystem::path(dir.path().toStdString()) / "Evento" / "prompters.json";
    ListaPrompters l;
    l.cargar(archivo);
    CHECK(l.lista().size() == 1 && l.lista()[0]["id"] == "principal", "de entrada, solo el principal");
    std::string err;
    const std::string a = l.agregar("Atril del Invitado", &err);
    CHECK(a == "atril-del-invitado" && l.existe(a), "el id sale del nombre, como lo acepta el gateway");
    CHECK(l.agregar("Atril del invitado", &err) == "atril-del-invitado-2", "mismo nombre: otro id");
    CHECK(l.agregar("Presidencia ñ", &err) == "presidencia", "las tildes y eñes se caen del id");
    CHECK(l.agregar("¿¿??", &err).empty() && !err.empty(), "sin letras ni números: error");
    CHECK(l.nombreDe(a) == "Atril del Invitado", "el nombre queda tal cual");
    CHECK(!l.quitar("principal").empty(), "el principal no se saca");
    CHECK(l.renombrar("principal", "Atril de la presidencia").empty() && l.nombreDe("principal") == "Atril de la presidencia",
          "pero se renombra");
    CHECK(l.quitar("atril-del-invitado-2").empty() && !l.existe("atril-del-invitado-2"), "quitar");
    {
        ListaPrompters otra;
        otra.cargar(archivo);
        CHECK(otra.lista().size() == 3 && otra.lista()[0]["nombre"] == "Atril de la presidencia" && otra.existe("presidencia"),
              "queda guardada en el evento");
    }
    ListaPrompters sinEvento;
    CHECK(sinEvento.agregar("x", &err).empty() && !err.empty(), "sin evento no se guarda: error");
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
