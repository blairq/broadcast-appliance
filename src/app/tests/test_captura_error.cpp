// test_captura_error.cpp — el error de una acción para quien la llama desde C++ (ui/CapturaError)
//
//   Las acciones avisan su error por la señal del modelo; el MCP y las pruebas lo capturan
//   con errorDe. Mientras se captura, la vista no se entera (el que llamó decide).

#include <QString>

#include <cstdio>

#include "ui/CapturaError.hpp"

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                             \
    do {                                                        \
        if (!(c)) {                                             \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__); \
            ++g_fallas;                                         \
        } else {                                                \
            std::printf("ok: %s\n", m);                         \
        }                                                       \
    } while (0)

/// Un modelo de juguete: su aviso de error, como el de cada modelo.
struct Modelo {
    QString alaVista;
    void avisarError(const QString& e) {
        if (!e.isEmpty() && !ba::app::CapturaError::tomar(e)) alaVista = e;
    }
    void accion(bool falla) {
        if (falla) return avisarError("no se pudo");
    }
    void dos() {
        avisarError("primero");
        avisarError("segundo");
    }
};
} // namespace

int main() {
    using ba::app::errorDe;
    Modelo m;
    CHECK(errorDe([&] { m.accion(false); }).isEmpty(), "anduvo: sin error");
    CHECK(errorDe([&] { m.accion(true); }) == "no se pudo" && m.alaVista.isEmpty(), "el error a quien llamó, no a la vista");
    CHECK(errorDe([&] { m.dos(); }) == "primero", "el primero que avisa");
    m.accion(true);
    CHECK(m.alaVista == "no se pudo", "sin captura: a la vista");
    m.alaVista.clear();
    QString interno;
    const QString externo = errorDe([&] {
        interno = errorDe([&] { m.accion(true); });
        m.avisarError("afuera");
    });
    CHECK(interno == "no se pudo" && externo == "afuera" && m.alaVista.isEmpty(), "anidadas: cada una lo suyo");
    if (g_fallas == 0) std::printf("test_captura_error: todo OK\n");
    return g_fallas == 0 ? 0 : 1;
}
