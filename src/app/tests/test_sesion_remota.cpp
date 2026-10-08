// test_sesion_remota.cpp — la banca virtual (asamblea/SesionRemota): link y
// PIN con el mismo hash que verifica el gateway, conectar → esperar →
// habilitar, la gracia al reconectar, presentes sin contar dos veces, registro.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QTemporaryDir>

#include <cstdio>

#include "asamblea/Cuerpo.hpp"
#include "asamblea/SesionRemota.hpp"

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
    asamblea::Cuerpo cuerpo(d / "cuerpo.json");
    std::string err;
    const std::string ana = cuerpo.guardar({{"nombre", "Ana Pérez"}, {"banca", "3"}}, &err);
    const std::string juan = cuerpo.guardar({{"nombre", "Juan Gómez"}, {"banca", "7"}}, &err);
    asamblea::SesionRemota s(cuerpo, d);
    std::vector<json> enviados;
    QObject::connect(&s, &asamblea::SesionRemota::enviar,
                     [&](const QString&, const nlohmann::json& m) { enviados.push_back(m); });
    int cortes = 0;
    QObject::connect(&s, &asamblea::SesionRemota::cortar, [&](const QString&, const QString&) { ++cortes; });

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
        asamblea::SesionRemota otra(cuerpo, d);
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

    // Votación por el celular (R4): habilitado vota; el voto combinado con la central.
    s.evento({{"tipo", "conexion"}, {"id", juan}, {"conectado", true}, {"lugar", "sala"}});
    s.habilitar(juan, true, "presidencia");
    CHECK(s.abrirVotacion(4, "Proyecto 12", asamblea::SesionRemota::opcionesDeMenu("si_no_abstencion"), 60, "central").empty(),
          "abre la votación");
    CHECK(enviados.back().value("type", std::string()) == "votacion" && enviados.back().value("estado", std::string()) == "abierta",
          "le llega el cartel de votación");
    s.evento({{"tipo", "entrada"}, {"id", juan}, {"msg", {{"type", "voto"}, {"votacion", 4}, {"opcion", "negativo"}}}});
    CHECK(enviados.back().value("ok", false), "voto aceptado");
    s.evento({{"tipo", "entrada"}, {"id", juan}, {"msg", {{"type", "voto"}, {"votacion", 4}, {"opcion", "inventada"}}}});
    CHECK(!enviados.back().value("ok", true), "opción inválida: rechazado");
    s.evento({{"tipo", "entrada"}, {"id", ana}, {"msg", {{"type", "voto"}, {"votacion", 4}, {"opcion", "afirmativo"}}}});
    CHECK(s.votacion().value("votos", json::array()).size() == 1, "el que no está habilitado no vota");
    // La central: banca 7 votó afirmativo en su unidad, banca 9 negativo; el celular de la 7 pisa.
    const json comb = s.resultadoCombinado(json::array({{{"banca", "7"}, {"nombre", "Juan Gómez"}, {"voto", "afirmativo"}},
                                                        {{"banca", "9"}, {"nombre", "Otro"}, {"voto", "negativo"}}}));
    CHECK(comb["totales"].value("negativo", 0) == 2 && comb["totales"].value("afirmativo", 0) == 0,
          "un voto por integrante: vale el del celular");
    CHECK(comb.value("observaciones", json::array()).size() == 1, "queda la observación del doble voto");
    s.cerrarVotacion();

    // La palabra: si se le corta y vuelve («hola»), la sigue teniendo con lo que le queda.
    std::vector<QString> medios;
    QObject::connect(&s, &asamblea::SesionRemota::mediosA,
                     [&](const QStringList& ids) { medios.push_back(ids.join(",")); });
    s.darPalabra(juan, true, 120);
    CHECK(!medios.empty() && medios.back().toStdString() == juan, "con la palabra, sus medios van a la sala");
    enviados.clear();
    s.evento({{"tipo", "entrada"}, {"id", juan}, {"msg", {{"type", "hola"}}}});
    bool volvio = false;
    for (const json& m : enviados)
        if (m.value("type", std::string()) == "palabra" && m.value("activa", false) && m.value("segundos", 0) > 0 &&
            m.value("segundos", 0) <= 120)
            volvio = true;
    CHECK(volvio, "al volver le llega la palabra de nuevo, con el tiempo que le queda");
    enviados.clear();
    s.evento({{"tipo", "entrada"}, {"id", ana}, {"msg", {{"type", "hola"}}}});
    bool aOtro = false;
    for (const json& m : enviados) aOtro = aOtro || m.value("type", std::string()) == "palabra";
    CHECK(!aOtro, "a otro que vuelve no le llega la palabra");
    // Un debate entre remotos: los dos a la vez, cada uno por su canal.
    s.darPalabra(ana, true, 60);
    CHECK(medios.back().toStdString() == juan + "," + ana, "dos con la palabra: canal 1 y 2");
    CHECK(s.tienePalabra(juan) && s.tienePalabra(ana), "los dos la tienen");
    s.darPalabra(juan, false, 0);
    CHECK(medios.back().toStdString() == ana, "sale uno: queda el otro");
    s.darPalabra(ana, false, 0);
    CHECK(medios.back().isEmpty(), "sin la palabra, nadie manda medios");
    s.evento({{"tipo", "entrada"}, {"id", juan}, {"msg", {{"type", "voto"}, {"votacion", 4}, {"opcion", "afirmativo"}}}});
    CHECK(!enviados.back().value("ok", true), "cerrada: no se vota más");

    // Bloqueo e intentos van al registro y al acta.
    s.evento({{"tipo", "intento"}, {"id", juan}, {"ip", "1.2.3.4"}, {"motivo", "pin"}});
    s.evento({{"tipo", "bloqueo"}, {"id", juan}, {"minutos", 15}});
    const std::string acta = s.anexoActa();
    CHECK(acta.find("Ana Pérez (banca 3): habilitado por presidencia") != std::string::npos, "el acta dice quién habilitó");
    CHECK(acta.find("bloqueado por PIN errado") != std::string::npos, "el acta registra el bloqueo");

    // Usuario y contraseña.
    CHECK(s.generarClave(juan).contains("error"), "sin link no hay contraseña");
    const json k = s.generarClave(ana, "Ana.Perez");
    CHECK(k.value("usuario", std::string()) == "ana.perez" && k.value("clave", std::string()).size() == 12,
          "usuario en minúsculas y contraseña de 12");
    const json g2 = s.paraGateway();
    CHECK(g2[0].value("usuario", std::string()) == "ana.perez" &&
              g2[0].value("hash_clave", std::string()) ==
                  sha256(g2[0].value("sal", std::string()) + ":clave:" + k.value("clave", std::string())),
          "hash de la contraseña como el gateway");
    s.generar(juan);
    CHECK(s.generarClave(juan, "ana.perez").contains("error"), "el usuario no se repite");
    CHECK(s.setDatos(ana, {{"email", "ana@ejemplo.org"}, {"telefono", "11 5555-0000"}}).empty(), "datos de contacto");
    const json c2 = s.generar(ana); // link nuevo: conserva usuario y datos, la contraseña no
    const json g3 = s.paraGateway();
    bool ok3 = false;
    for (const auto& x : g3)
        if (x.value("id", std::string()) == ana)
            ok3 = x.value("usuario", std::string()) == "ana.perez" && x.value("hash_clave", std::string()).empty();
    CHECK(ok3, "con link nuevo conserva el usuario y pide contraseña nueva");

    // Revocar: deja de ir al gateway.
    CHECK(s.revocar(ana) && s.revocar(juan) && s.paraGateway().empty(), "revocado ya no va al gateway");

    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
