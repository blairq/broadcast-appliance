// test_quorum.cpp — quórum, mayorías y resultado oficial (y su lugar en el acta).
#include <QCoreApplication>
#include <QTemporaryDir>

#include <cstdio>

#include "asamblea/Quorum.hpp"

using ba::app::asamblea::Quorum;
using json = nlohmann::json;

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
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    int presentes = 14;
    Quorum q(dir.path().toStdString(), [&]() { return presentes; });
    CHECK(!q.estado().value("configurado", true), "sin miembros no hay regla");
    CHECK(q.setRegla({{"miembros", 27}}).empty(), "27 miembros");
    CHECK(q.estado()["necesarios"] == 14 && q.estado()["hay_quorum"] == true, "14 de 27: hay quórum (mitad más uno)");
    presentes = 13;
    CHECK(q.estado()["hay_quorum"] == false, "13: no hay");
    CHECK(!q.setRegla({{"mayoria", "rara"}}).empty(), "mayoría inválida");

    CHECK(Quorum::necesarios("simple", 20, 25, 27) == 11, "simple: más de la mitad de los votos");
    CHECK(Quorum::necesarios("absoluta", 20, 25, 27) == 13, "absoluta: mitad más uno de los presentes");
    CHECK(Quorum::necesarios("dos_tercios_presentes", 20, 25, 27) == 17, "2/3 de 25 presentes = 17");
    CHECK(Quorum::necesarios("dos_tercios_miembros", 20, 25, 27) == 18, "2/3 de 27 miembros = 18");

    json r = q.cargarOficial({{"asunto", "Ley A"}, {"afirmativos", 10}, {"negativos", 9}, {"abstenciones", 6}});
    CHECK(r["aprobado"] == true && r["mayoria"] == "simple", "simple 10-9: aprobado");
    r = q.cargarOficial({{"asunto", "Ley B"}, {"afirmativos", 12}, {"negativos", 9}, {"abstenciones", 4}, {"mayoria", "absoluta"}});
    CHECK(r["aprobado"] == false && r["necesarios"] == 13, "absoluta con 25 presentes: 12 no alcanza");
    r = q.cargarOficial({{"asunto", "Reforma"}, {"mayoria", "dos_tercios_miembros"},
                         {"csv", "nombre;voto\nAna;afirmativo\nJuan;Negativo\nLuis;abstención\nMara;sí\nPepe;ausente"}});
    CHECK(r["afirmativos"] == 2 && r["negativos"] == 1 && r["abstenciones"] == 1 && r["nominal"].size() == 5, "CSV nominal");
    CHECK(r["aprobado"] == false, "2 de 18 necesarios: rechazado");
    CHECK(q.cargarOficial({{"asunto", "x"}, {"csv", "basura"}}).contains("error"), "CSV sin votos");
    const std::string a = q.anexoActa();
    CHECK(a.find("## Quórum") != std::string::npos && a.find("Ley A") != std::string::npos &&
              a.find("APROBADO") != std::string::npos && a.find("| Ana | afirmativo |") != std::string::npos,
          "anexo del acta");
    {
        Quorum otra(dir.path().toStdString(), [&]() { return presentes; });
        CHECK(otra.oficiales().size() == 3 && otra.regla()["miembros"] == 27, "queda guardado");
    }
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
