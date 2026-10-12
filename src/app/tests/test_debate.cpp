// Prueba del debate (asamblea/Debate) con ganchos de mentira: quién habla, por dónde,
// qué le pasa a la central y qué escena se arma.
#include <QCoreApplication>
#include <QTemporaryDir>

#include <cstdio>
#include <map>
#include <set>
#include <vector>

#include "asamblea/Cuerpo.hpp"
#include "asamblea/Debate.hpp"

using namespace ba::app;
using json = nlohmann::json;

namespace {
int fallas = 0;
#define CHECK(c, m)                                                   \
    do {                                                              \
        if (!(c)) {                                                   \
            std::printf("FALLA: %s (%s:%d)\n", m, __FILE__, __LINE__); \
            ++fallas;                                                 \
        }                                                             \
    } while (0)
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const std::filesystem::path d = dir.path().toStdString();
    asamblea::Cuerpo cuerpo(d / "cuerpo.json");
    std::string err;
    const std::string ana = cuerpo.guardar({{"nombre", "Ana"}, {"banca", "3"}}, &err);
    const std::string juan = cuerpo.guardar({{"nombre", "Juan"}, {"banca", "7"}}, &err);
    const std::string eva = cuerpo.guardar({{"nombre", "Eva"}, {"banca", "9"}}, &err);

    std::set<std::string> remotos{juan, eva};
    std::vector<std::string> conPalabra;        // en orden: canal 1, 2
    std::map<std::string, bool> mezcla;         // fuente -> abierta
    std::map<std::string, bool> central;        // banca -> micrófono
    std::vector<bool> modoCentral;              // true = debate, false = como estaba
    std::string armada;
    std::map<std::string, json> alCelular;      // último mensaje de debate a cada remoto
    int terminados = 0;

    asamblea::Debate::Ganchos g;
    g.esRemoto = [&](const std::string& id) { return remotos.count(id) > 0; };
    g.palabraRemota = [&](const std::string& id, bool p, int) {
        conPalabra.erase(std::remove(conPalabra.begin(), conPalabra.end(), id), conPalabra.end());
        if (p) conPalabra.push_back(id);
    };
    g.canalRemoto = [&](const std::string& id) {
        auto it = std::find(conPalabra.begin(), conPalabra.end(), id);
        return it == conPalabra.end() ? 0 : static_cast<int>(it - conPalabra.begin()) + 1;
    };
    g.microfono = [&](const std::string& f, bool a) { mezcla[f] = a; };
    g.microfonoBanca = [&](const std::string& b, bool a, bool) { central[b] = a; return true; };
    g.apuntar = [&](const std::string&) { return std::string(); };
    g.armar = [&](const std::string& e) { armada = e; };
    g.cortar = [&](const std::string& e) { armada = "AL AIRE " + e; };
    g.central = [&](bool deb) { modoCentral.push_back(deb); };
    g.terminarOrador = [&]() { ++terminados; };
    g.avisarRemoto = [&](const std::string& id, const json& m) { alCelular[id] = m; };

    asamblea::Debate deb(cuerpo, d, g);
    deb.configurar({{"escenas", {{"sala_sala", "Debate sala"}, {"sala_remoto", "Debate mixto"}, {"remoto_remoto", "Debate remoto"}}},
                    {"turno_s", 90}});

    // Uno de la sala contra un remoto.
    CHECK(deb.iniciar("3", "Juan", "a").empty(), "inicia sala contra remoto");
    CHECK(terminados == 1, "termina al orador que estaba");
    CHECK(!modoCentral.empty() && modoCentral.back(), "la central pasa a modo debate (hay uno de la sala)");
    CHECK(armada == "Debate mixto", "arma la escena de sala-remoto");
    CHECK(deb.estadoJson()["tipo"] == "sala_remoto", "tipo sala_remoto");
    CHECK(conPalabra == std::vector<std::string>{juan}, "el remoto tiene la palabra (canal 1) todo el debate");
    CHECK(central["3"] && !mezcla["Remoto con la palabra (audio)"], "turno de A: habla la banca 3, el remoto callado");
    CHECK(alCelular[juan].value("habla", true) == false, "al remoto le llega que no es su turno");
    CHECK(deb.turno("b").empty(), "turno de B");
    CHECK(!central["3"] && mezcla["Remoto con la palabra (audio)"], "turno de B: la banca 3 callada, el remoto habla");
    CHECK(alCelular[juan].value("habla", false) && alCelular[juan].value("segundos", 0) > 0, "su turno, con reloj");
    CHECK(deb.turno("ambos").empty() && central["3"] && mezcla["Remoto con la palabra (audio)"], "ambos hablan");
    CHECK(!deb.turno("cualquiera").empty(), "un turno raro es error");
    deb.terminar();
    CHECK(!deb.activo() && conPalabra.empty(), "al terminar, el remoto deja la palabra");
    CHECK(!modoCentral.back(), "la central vuelve a como estaba");
    CHECK(mezcla["Remoto con la palabra (audio)"], "el audio del canal queda abierto para la fila");
    CHECK(alCelular[juan].value("activo", true) == false, "al celular le llega el fin del debate");

    // Dos remotos: A por el canal 1, B por el 2; la central no se toca.
    modoCentral.clear();
    CHECK(deb.iniciar("Juan", "Eva", "b").empty(), "inicia remoto contra remoto");
    CHECK(conPalabra == (std::vector<std::string>{juan, eva}), "A canal 1, B canal 2");
    CHECK(armada == "Debate remoto", "arma la escena de remoto-remoto");
    CHECK(modoCentral.empty(), "sin nadie de la sala no se toca la central");
    CHECK(!mezcla["Remoto con la palabra (audio)"] && mezcla["Remoto con la palabra 2 (audio)"], "turno de B: suena el canal 2");
    CHECK(deb.estadoJson()["b"].value("canal", 0) == 2, "el estado dice el canal de cada uno");
    deb.terminar();

    // Errores.
    CHECK(!deb.iniciar("3", "3").empty(), "con uno mismo no hay debate");
    CHECK(!deb.iniciar("3", "nadie").empty(), "alguien que no está en el Cuerpo");
    CHECK(!deb.turno("a").empty(), "sin debate no hay turno");

    if (fallas == 0) std::printf("test_debate: OK\n");
    return fallas == 0 ? 0 : 1;
}
