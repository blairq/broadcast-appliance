// test_sesion_remota.cpp — la banca virtual (legislatura/SesionRemota): link y
// PIN con el mismo hash que verifica el gateway, conectar → esperar →
// habilitar, la gracia al reconectar, presentes sin contar dos veces, registro.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QTemporaryDir>

#include <cstdio>

#include "legislatura/Cuerpo.hpp"
#include "legislatura/SesionRemota.hpp"

using namespace ba::app;
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
std::string sha256(const std::string& s) {
    return QCryptographicHash::hash(QByteArray::fromStdString(s), QCryptographicHash::Sha256).toHex().toStdString();
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const std::filesystem::path d = dir.path().toStdString();
    legislatura::Cuerpo cuerpo(d / "cuerpo.json");
    std::string err;
    const std::string ana = cuerpo.guardar({{"nombre", "Ana Pérez"}, {"banca", "3"}}, &err);
    const std::string juan = cuerpo.guardar({{"nombre", "Juan Gómez"}, {"banca", "7"}}, &err);
    legislatura::SesionRemota s(cuerpo, d);
    std::vector<json> enviados;
    QObject::connect(&s, &legislatura::SesionRemota::enviar,
                     [&](const QString&, const nlohmann::json& m) { enviados.push_back(m); });
    int cortes = 0;
    QObject::connect(&s, &legislatura::SesionRemota::cortar, [&](const QString&, const QString&) { ++cortes; });

    s.setOrigen("https://sesion.ejemplo.org");
    const json c = s.generar(ana);
    CHECK(c.value("pin", std::string()).size() == 6, "PIN de 6 dígitos");
    const std::string link = c.value("link", std::string());
    CHECK(link.rfind("https://sesion.ejemplo.org/sesion/#i=" + ana + "&t=", 0) == 0, "link con el origen y el fragmento");
    CHECK(c.value("qr", json::array()).size() > 20, "trae el QR del link");
    const std::string token = link.substr(link.find("&t=") + 3);
    CHECK(token.size() == 64, "token de 256 bits");
    // Lo que va al gateway verifica con la misma cuenta que hace Sesion.cpp.
    const json g = s.paraGateway();
    CHECK(g.size() == 1 && g[0].value("banca", std::string()) == "3", "una credencial, con su banca");
    const std::string sal = g[0].value("sal", std::string());
    CHECK(g[0].value("hash_token", std::string()) == sha256(sal + token), "hash del token como el gateway");
    CHECK(g[0].value("hash_pin", std::string()) == sha256(sal + ":" + c.value("pin", std::string()) + ":" + token),
          "hash del PIN como el gateway");
    CHECK(g[0].dump().find(token) == std::string::npos && g[0].dump().find(c.value("pin", std::string())) == std::string::npos,
          "ni el token ni el PIN viajan en claro");
    // Persistencia: otra instancia lee lo mismo.
    {
        legislatura::SesionRemota otra(cuerpo, d);
        CHECK(otra.tieneCredencial(ana) && !otra.tieneCredencial(juan), "las credenciales se guardan");
    }

    // Entra → espera → la presidencia lo habilita.
    s.evento({{"tipo", "conexion"}, {"id", ana}, {"conectado", true}, {"ip", "190.1.2.3"}, {"lugar", "afuera"}});
    CHECK(s.presentes() == 0 && s.conectados() == 1, "conectado pero sin habilitar: no es presente");
    CHECK(!enviados.empty() && enviados.back().value("estado", std::string()) == "conectado", "le avisa que espera");
    CHECK(s.habilitar(ana, true, "presidencia").empty() && s.presentes() == 1, "habilitado: presente");
    CHECK(enviados.back().value("estado", std::string()) == "habilitado", "le avisa que está habilitado");
    CHECK(s.presentesBancas() == std::vector<std::string>{"3"}, "su banca");
    CHECK(!s.habilitar(juan, true, "operador").empty(), "no se habilita a alguien que no está conectado");

    // Se cae y vuelve dentro de la gracia: sigue habilitado.
    s.evento({{"tipo", "conexion"}, {"id", ana}, {"conectado", false}});
    CHECK(s.presentes() == 0, "caído no cuenta");
    s.evento({{"tipo", "conexion"}, {"id", ana}, {"conectado", true}, {"lugar", "sala"}});
    CHECK(s.presentes() == 1, "volvió enseguida: sigue habilitado");

    // Expulsado: corta, y si vuelve hay que habilitarlo de nuevo.
    CHECK(s.expulsar(ana, "presidencia", "").empty() && cortes == 1, "expulsar corta la conexión");
    s.evento({{"tipo", "conexion"}, {"id", ana}, {"conectado", false}});
    s.evento({{"tipo", "conexion"}, {"id", ana}, {"conectado", true}});
    CHECK(s.presentes() == 0, "después de expulsado, vuelve esperando");

    // Bloqueo e intentos van al registro y al acta.
    s.evento({{"tipo", "intento"}, {"id", juan}, {"ip", "1.2.3.4"}, {"motivo", "pin"}});
    s.evento({{"tipo", "bloqueo"}, {"id", juan}, {"minutos", 15}});
    const std::string acta = s.anexoActa();
    CHECK(acta.find("Ana Pérez (banca 3): habilitado por presidencia") != std::string::npos, "el acta dice quién habilitó");
    CHECK(acta.find("bloqueado por PIN errado") != std::string::npos, "el acta registra el bloqueo");

    // Revocar: deja de ir al gateway.
    CHECK(s.revocar(ana) && s.paraGateway().empty(), "revocado ya no va al gateway");

    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
