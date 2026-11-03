// Varias cuentas por plataforma y los vivos de cada salida (vivos/Vivos.hpp), sin red: la
// migración (una instalación vieja queda con su cuenta como principal), agregar, renombrar
// y quitar (los ids no se reusan), asignar vivos y genéricos a salidas con una plataforma
// de mentira, el aviso de dos salidas al mismo vivo y el estado de cada destino.
#include "vivos/Vivos.hpp"

#include <cstdio>
#include <map>
#include <string>

using namespace ba::app::vivos;
using json = nlohmann::json;

static int fallas = 0;
#define CHECK(c, m)                        \
    do {                                   \
        if (!(c)) {                        \
            std::printf("FALLA: %s\n", m); \
            ++fallas;                      \
        } else {                           \
            std::printf("ok: %s\n", m);    \
        }                                  \
    } while (0)

namespace {

/// Una plataforma de mentira: contesta en el acto.
class DeMentira : public Plataforma {
   public:
    std::string id() const override { return "youtube"; }
    bool vivosAparte() const override { return true; }
    bool configurada() const override { return app; }
    bool conectada(const std::string& c) const override { return conectadas.count(c) != 0; }
    bool conectando(const std::string&) const override { return false; }
    bool sinPermiso(const std::string& c) const override { return c == sinPermisoDe; }
    std::string canal(const std::string& c) const override {
        const auto it = conectadas.find(c);
        return it != conectadas.end() ? it->second : std::string();
    }
    void conectar(const std::string& c, Listo l) override {
        conectadas[c] = "Canal " + c;
        l(Resultado::bien({{"canal", conectadas[c]}}));
    }
    void cancelarConexion(const std::string&) override {}
    void desconectar(const std::string& c) override { conectadas.erase(c); }
    void olvidar(const std::string&) override {}
    void vivos(const std::string&, Listo l) override {
        l(Resultado::bien({{"items", json::array({{{"id", "v1"}, {"titulo", "Uno"}}, {{"id", "v2"}, {"titulo", "Dos"}}})}}));
    }
    void crearVivo(const std::string&, const json& d, Listo l) override {
        l(Resultado::bien({{"vivo", {{"id", "nuevo"}, {"titulo", d.value("titulo", std::string())}}}}));
    }
    void ingesta(const std::string& c, const std::string& v, Listo l) override {
        ++ingestas;
        if (v == "roto") return l(Resultado::mal("no hay ingesta"));
        l(Resultado::bien({{"url", "rtmp://a.rtmp.youtube.com/live2"}, {"clave", c + "-" + v}, {"estado_stream", "ready"}}));
    }
    void salud(const std::string&, const std::string&, Listo l) override {
        l(Resultado::bien({{"estado_stream", "active"}, {"salud", "good"}}));
    }

    bool app = true;
    std::map<std::string, std::string> conectadas;
    std::string sinPermisoDe;
    int ingestas = 0;
};

} // namespace

int main() {
    // --- Migración: sin lista guardada, cada plataforma con su principal ------------------
    json guardado;
    int escrituras = 0;
    RegistroCuentas reg({[&]() { return guardado; },
                         [&](const json& j) {
                             guardado = j;
                             ++escrituras;
                             return true;
                         }});
    reg.cargar();
    CHECK(reg.de("youtube").size() == 1 && reg.de("youtube")[0].id == kPrincipal, "instalación vieja: la principal de YouTube");
    CHECK(reg.de().size() == 3, "una principal por plataforma");
    CHECK(escrituras == 1 && guardado.is_object(), "la migración se guarda una vez");
    CHECK(credencialDeCuenta("youtube", kPrincipal) == "youtube-token" && credencialDeCuenta("youtube", "") == "youtube-token",
          "la principal usa el token de siempre (no se pierde la cuenta que había)");
    CHECK(credencialDeCuenta("twitch", "cuenta-2") == "twitch-token-cuenta-2", "las demás, el suyo");

    // --- Agregar, renombrar, quitar --------------------------------------------------------
    std::string id;
    CHECK(reg.agregar("youtube", "Inglés", &id).empty() && id == "cuenta-2", "agregar una cuenta");
    std::string id3;
    CHECK(reg.agregar("youtube", "", &id3).empty() && id3 == "cuenta-3", "otra, con nombre armado");
    CHECK(!reg.agregar("myspace", "x", nullptr).empty(), "plataforma desconocida: no");
    CHECK(reg.renombrar("youtube", id, "English").empty() && reg.buscar("youtube", id)->nombre == "English", "renombrar");
    CHECK(!reg.quitar("youtube", kPrincipal).empty(), "la principal no se quita");
    CHECK(reg.quitar("youtube", id3).empty() && reg.buscar("youtube", id3) == nullptr, "quitar una");
    std::string id4;
    reg.agregar("youtube", "Otra", &id4);
    CHECK(id4 == "cuenta-4", "los ids no se reusan (un destino viejo no apunta a otra cuenta)");
    {
        RegistroCuentas otra({[&]() { return guardado; }, [](const json&) { return true; }});
        otra.cargar();
        CHECK(otra.de("youtube").size() == 3 && otra.buscar("youtube", "cuenta-2")->nombre == "English", "se lee lo guardado");
        std::string id5;
        otra.agregar("youtube", "", &id5);
        CHECK(id5 == "cuenta-5", "y sigue la numeración");
    }

    // --- Estado de una cuenta --------------------------------------------------------------
    CHECK(estadoCuenta(false, false, false, false, false) == "sin_app", "sin la app de la plataforma");
    CHECK(estadoCuenta(true, false, false, false, false) == "sin_conectar", "sin conectar");
    CHECK(estadoCuenta(true, true, false, false, true) == "conectada", "conectada");
    CHECK(estadoCuenta(true, true, false, true, true) == "sin_permiso", "conectada sin permiso");
    CHECK(estadoCuenta(true, false, false, false, true) == "vencida", "estuvo conectada y ya no: vencida");
    CHECK(estadoCuenta(true, false, true, false, true) == "conectando", "conectando");

    // --- El servicio con destinos en memoria -----------------------------------------------
    DeMentira yt;
    json destinos = json::array();
    std::map<std::string, std::string> claves;
    int n = 0;
    ServicioVivos s({reg,
                     [&](const std::string& p) -> Plataforma* { return p == "youtube" ? &yt : nullptr; },
                     [&]() { return destinos; },
                     [&](const std::string& d) { return huella(claves[d]); },
                     [&](const json& d, std::function<void(const std::string&, const std::string&)> listo) {
                         json x = d;
                         std::string did = x.value("id", std::string());
                         if (did.empty()) did = "destino-" + std::to_string(++n);
                         x["id"] = did;
                         if (x.contains("clave")) {
                             claves[did] = x["clave"].get<std::string>();
                             x.erase("clave");
                         }
                         bool reemplazo = false;
                         for (auto& e : destinos)
                             if (e.value("id", std::string()) == did) {
                                 e = x;
                                 reemplazo = true;
                             }
                         if (!reemplazo) destinos.push_back(x);
                         listo({}, did);
                     },
                     [](const std::string& sal) -> std::string {
                         if (sal == "principal") return "Principal";
                         if (sal == "ingles") return "Inglés";
                         if (sal == "tv") return "TV";
                         return {};
                     }});

    std::string err, nuevo;
    const auto asignar = [&](Asignacion a) {
        err.clear();
        nuevo.clear();
        s.asignar(std::move(a), [&](const std::string& e, const std::string& i) {
            err = e;
            nuevo = i;
        });
    };
    {
        Asignacion a;
        a.salida = "principal";
        a.plataforma = "youtube";
        a.vivo = "v1";
        asignar(a);
        CHECK(!err.empty() && yt.ingestas == 0, "sin conectar no se asigna (ni se pide la ingesta)");
    }
    yt.conectar(kPrincipal, [](const Resultado&) {});
    yt.conectar("cuenta-2", [](const Resultado&) {});
    {
        Asignacion a;
        a.salida = "principal";
        a.plataforma = "youtube";
        a.vivo = "v1";
        a.vivoTitulo = "Uno";
        asignar(a);
        CHECK(err.empty() && nuevo == "destino-1", "asignar un vivo de la principal");
        CHECK(destinos[0].value("url", std::string()) == "rtmp://a.rtmp.youtube.com/live2" && claves["destino-1"] == "principal-v1",
              "con la ingesta: la URL al destino y la clave aparte");
        CHECK(destinos[0].value("cuenta", std::string()) == kPrincipal && destinos[0].value("vivo", std::string()) == "v1" &&
                  !destinos[0].contains("clave"),
              "el destino dice cuenta y vivo, sin la clave");
    }
    {
        Asignacion a;
        a.salida = "ingles";
        a.plataforma = "youtube";
        a.vivo = "v1";
        asignar(a);
        CHECK(!err.empty() && err.find("Principal") != std::string::npos && destinos.size() == 1,
              "el mismo vivo en otra salida: no, y el aviso dice qué salida lo usa");
    }
    {
        Asignacion a;
        a.salida = "principal";
        a.plataforma = "youtube";
        a.vivo = "v1";
        asignar(a);
        CHECK(!err.empty() && destinos.size() == 1, "el mismo vivo dos veces en la misma salida: no");
    }
    {
        Asignacion a;
        a.salida = "ingles";
        a.plataforma = "youtube";
        a.cuenta = "cuenta-2";
        a.vivo = "v1";
        asignar(a);
        CHECK(err.empty() && destinos.size() == 2, "el vivo v1 de OTRA cuenta es otro vivo");
    }
    {
        Asignacion a;
        a.id = "destino-1";
        a.salida = "principal";
        a.plataforma = "youtube";
        a.vivo = "v1";
        asignar(a);
        CHECK(err.empty() && destinos.size() == 2, "reasignar el mismo destino a su vivo: no choca consigo");
    }
    {
        Asignacion a;
        a.salida = "ingles";
        a.plataforma = "youtube";
        asignar(a);
        CHECK(!err.empty(), "YouTube sin vivo: no");
        a.vivo = "roto";
        asignar(a);
        CHECK(err == "no hay ingesta" && destinos.size() == 2, "la plataforma no da la ingesta: el error y nada guardado");
        a.salida = "nadie";
        a.vivo = "v2";
        asignar(a);
        CHECK(!err.empty(), "una salida que no existe: no");
    }
    // Genéricos: misma URL y misma clave = mismo vivo; otra clave, otro.
    {
        Asignacion a;
        a.salida = "tv";
        a.url = "srt://10.0.0.5:9000";
        a.clave = "frase";
        asignar(a);
        CHECK(err.empty() && destinos.back().value("plataforma", std::string()) == "srt", "un SRT genérico");
        Asignacion b = a;
        b.salida = "ingles";
        b.url = "srt://10.0.0.5:9000/";
        asignar(b);
        CHECK(!err.empty(), "la misma URL (con / al final) y la misma clave en otra salida: no");
        b.clave = "otra";
        asignar(b);
        CHECK(err.empty(), "la misma URL con otra clave: otro vivo");
    }
    {
        Asignacion a;
        a.salida = "tv";
        a.url = "http://algo";
        asignar(a);
        CHECK(!err.empty(), "una URL que no es de transmisión: no");
    }

    // --- Choques de lo guardado (un archivo de antes o editado a mano) ---------------------
    {
        json viejo = json::array({{{"id", "a"}, {"url", "rtmp://x/app"}, {"salida", "principal"}},
                                  {{"id", "b"}, {"url", "rtmp://x/app"}, {"salida", "tv"}},
                                  {{"id", "c"}, {"url", "rtmp://x/app"}}});
        std::map<std::string, std::string> k{{"a", "k1"}, {"b", "k1"}, {"c", "k2"}};
        const auto ch = choques(viejo, [&](const std::string& d) { return huella(k[d]); });
        CHECK(ch.size() == 1 && ch[0].destinos.size() == 2 && ch[0].salidas.size() == 2, "dos guardados al mismo vivo: un choque");
        CHECK(plataformaDe(viejo[0]) == "rtmp" && plataformaDe(json{{"url", "srt://h:1"}}) == "srt",
              "un destino de antes: la plataforma por la URL");
    }

    // --- Estado de cada destino ------------------------------------------------------------
    CHECK(estadoDestino(false, false, 0, 0) == "detenido", "detenido");
    CHECK(estadoDestino(true, false, 0, 0) == "conectando", "arrancó y todavía no salió nada: conectando");
    CHECK(estadoDestino(true, false, 1000, 3000) == "en_vivo", "sale: en vivo");
    CHECK(estadoDestino(true, true, 1000, 0) == "reintentando", "se cortó: reintentando");
    {
        json conEstado = destinos;
        conEstado[0]["activo"] = true;
        conEstado[0]["bytes"] = 5000;
        bool aviso = false;
        s.sondearSalud(conEstado, [&]() { aviso = true; });
        CHECK(aviso && s.saludDe("destino-1").value("salud", std::string()) == "good", "la salud que informa la plataforma");
        const json e = s.estado(conEstado, json{{"destino-1", 4500}});
        CHECK(e.size() == destinos.size(), "una fila por destino");
        CHECK(e[0].value("estado", std::string()) == "en_vivo" && e[0].value("kbps", 0) == 4500 &&
                  e[0].value("cuenta_nombre", std::string()) == "Principal",
              "el estado, el bitrate y el nombre de la cuenta");
        CHECK(s.estado(conEstado, json::object(), "tv").size() == 1, "los de una salida");
        // Deja de salir: la salud se olvida.
        conEstado[0]["activo"] = false;
        s.sondearSalud(conEstado, {});
        CHECK(s.saludDe("destino-1").empty(), "lo que ya no sale no guarda salud");
    }
    // --- Las cuentas con su estado -----------------------------------------------------------
    {
        yt.sinPermisoDe = "cuenta-2";
        reg.anotarConexion("youtube", kPrincipal, "Canal principal");
        const json c = s.cuentas("youtube");
        CHECK(c.size() == 3 && c[0].value("estado", std::string()) == "conectada" && c[0].value("principal", false),
              "la principal conectada");
        CHECK(c[1].value("estado", std::string()) == "sin_permiso", "la que perdió el permiso");
        yt.desconectar(kPrincipal);
        CHECK(s.cuentas("youtube")[0].value("estado", std::string()) == "vencida", "estuvo conectada y se cayó el token: vencida");
        reg.anotarDesconexion("youtube", kPrincipal);
        CHECK(s.cuentas("youtube")[0].value("estado", std::string()) == "sin_conectar", "desconectada a propósito: sin conectar");
        yt.app = false;
        CHECK(s.cuentas("youtube")[0].value("estado", std::string()) == "sin_app", "sin la app");
    }

    std::printf(fallas == 0 ? "TODO OK\n" : "%d FALLAS\n", fallas);
    return fallas == 0 ? 0 : 1;
}
