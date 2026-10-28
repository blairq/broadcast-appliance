// test_manuales.cpp — el encargo de los manuales sale completo y en el idioma de la interfaz.
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

#include "i18n/I18n.hpp"
#include "ui/ManualesViewModel.hpp"

using ba::app::ManualesViewModel;

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

int main(int argc, char** argv) {
    QTemporaryDir datos;
    setenv("BA_DATA_ROOT", datos.path().toUtf8().constData(), 1);
    QCoreApplication app(argc, argv);
    ba::i18n::carpetas(BA_TEST_IDIOMAS, {});
    ba::i18n::usar("en");
    bool enVivo = false;
    QString encargado;
    ManualesViewModel m({.enVivo = [&]() { return enVivo; },
                         .encargar = [&](const QString& t) { encargado = t; },
                         .paginas = []() { return QStringList{"dashboard", "escenas"}; }});
    CHECK(m.idioma() == "en", "el idioma es el de la interfaz");
    CHECK(m.carpeta().endsWith("/Manuales/en"), "la carpeta, por idioma, en los datos");
    const QString e = m.encargo();
    CHECK(!e.isEmpty() && !e.contains("{{") && !e.contains("<!--"), "el encargo sale completo, sin campos sin llenar");
    CHECK(e.contains("`dashboard` · `escenas`") && e.contains("(2)"), "con las páginas del riel");
    CHECK(e.contains(m.carpeta()) && e.contains(m.modeloMinimo()), "con la carpeta y el modelo mínimo");
    m.setAlcance(19);
    CHECK(m.encargo().contains("| 19 ("), "el alcance elegido");
    enVivo = true;
    CHECK(!m.generar().isEmpty() && encargado.isEmpty(), "en vivo no se genera");
    enVivo = false;
    CHECK(m.generar().isEmpty() && encargado == m.encargo() && QDir(m.carpeta() + "/capturas").exists(),
          "sin aire: se lo encarga al asistente y prepara la carpeta");
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
