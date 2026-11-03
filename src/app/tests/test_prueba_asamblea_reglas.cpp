// test_prueba_asamblea_reglas.cpp — la prueba de asamblea (asamblea/PruebaAsambleaReglas) con
// un Cuerpo y una SesionRemota de verdad:
//
//   - no corre sobre un padrón real (con un solo integrante real, no genera nada);
//   - los de prueba van marcados, con id, banca, nombre y usuario propios que no chocan;
//   - su acceso llega al gateway marcado `prueba` y el archivo del probador trae link + PIN y
//     usuario + contraseña (y la línea del generador externo, ningún secreto);
//   - al limpiar se borran SOLO los marcados (los reales y su acceso quedan) y el recinto
//     vuelve a como estaba;
//   - el comportamiento (de fábrica, validado) y cómo va (vivo) según lo que sabe la app.
#include <QCoreApplication>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>

#include "asamblea/Cuerpo.hpp"
#include "asamblea/PruebaAsambleaReglas.hpp"
#include "asamblea/SesionRemota.hpp"
#include "persist/ColaEscritura.hpp"

using namespace ba::app;
namespace pa = ba::app::asamblea::prueba;
using json = nlohmann::json;

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

bool contiene(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const std::filesystem::path d = dir.path().toStdString();
    qputenv("BA_DATA_ROOT", dir.path().toUtf8());

    // --- Un evento con padrón real: no se toca ------------------------------------------------
    {
        asamblea::Cuerpo real(d / "real" / "cuerpo.json");
        std::string err;
        real.guardar({{"nombre", "Ana Pérez"}, {"banca", "3"}}, &err);
        const json ints = real.integrantes();
        CHECK(pa::integrantesReales(ints) == 1 && pa::aBorrar(ints).empty(), "un integrante real, ninguno de prueba");
        const std::string no = pa::puedeGenerar(ints, 10);
        CHECK(!no.empty() && no.find("evento de prueba") != std::string::npos,
              "con un integrante real no se generan los de prueba (pide un evento de prueba)");
        CHECK(!pa::puedeGenerar(json::array(), 0).empty() && !pa::puedeGenerar(json::array(), pa::kMaxIntegrantes + 1).empty(),
              "la cantidad, de 1 al tope");
    }

    // --- Un evento vacío: se generan, con acceso ------------------------------------------------
    asamblea::Cuerpo cuerpo(d / "prueba" / "cuerpo.json");
    std::string err;
    // Un recinto previo cualquiera (tiene que volver igual al terminar).
    cuerpo.setRecinto({{"filas", 2}, {"columnas", 3}, {"distribucion", "hemiciclo"}});
    const json recintoAntes = cuerpo.recintoGuardado();
    CHECK(pa::puedeGenerar(cuerpo.integrantes(), 12).empty(), "en un evento vacío se puede");
    const json nuevos = pa::integrantesNuevos(cuerpo.integrantes(), 12);
    CHECK(nuevos.size() == 12 && nuevos[0]["id"] == "prueba-01" && nuevos[0]["banca"] == "P01" && nuevos[11]["banca"] == "P12" &&
              nuevos[0]["prueba"] == true,
          "12 nuevos: prueba-01…, P01…, marcados");
    int avisos = 0;
    cuerpo.alCambiar([&]() { ++avisos; });
    std::vector<std::string> ids;
    cuerpo.enLote([&]() {
        for (const json& c : nuevos) ids.push_back(cuerpo.guardar(c, &err));
        CHECK(cuerpo.setRecinto(pa::recintoPara(12)).empty(), "el recinto para 12 se arma solo");
    });
    CHECK(avisos == 1, "en lote: la vista se entera una sola vez");
    CHECK(ids.size() == 12 && ids[3] == "prueba-04" && asamblea::Cuerpo::esDePrueba(cuerpo.buscar("prueba-04")),
          "creados con su id y la marca");
    CHECK(pa::integrantesReales(cuerpo.integrantes()) == 0 && pa::aBorrar(cuerpo.integrantes()).size() == 12,
          "12 de prueba, ningún real");
    CHECK(!pa::puedeGenerar(cuerpo.integrantes(), 3).empty(), "con los de una prueba anterior, no empieza otra");
    {
        // Lo que no choca: con un «prueba-01» o una banca «P02» ya tomados, se corre la numeración.
        const json otros = json::array({{{"id", "prueba-01"}, {"nombre", "x"}}, {{"id", "q"}, {"banca", "P02"}, {"nombre", "y"}}});
        const json n2 = pa::integrantesNuevos(otros, 2);
        CHECK(n2.size() == 2 && n2[0]["id"] == "prueba-03" && n2[1]["id"] == "prueba-04", "no repite ids ni bancas");
        CHECK(pa::integrantesNuevos(json::array(), 120)[0]["banca"] == "P001", "con más de 99, tres dígitos");
    }
    const json rec = pa::recintoPara(12);
    CHECK(rec["filas"].get<int>() * rec["columnas"].get<int>() >= 12 && rec["columnas"] >= rec["filas"], "más ancho que alto");
    CHECK(pa::recintoPara(500)["filas"].get<int>() <= 40 && pa::recintoPara(500)["columnas"].get<int>() <= 60, "500 entran en la grilla");

    asamblea::SesionRemota s(cuerpo, d / "prueba");
    s.setOrigenes("https://10.0.0.2:8789", "");
    int sync = 0;
    QObject::connect(&s, &asamblea::SesionRemota::credencialesCambio, [&]() { ++sync; });
    json accesos = json::array();
    s.enLote([&]() {
        for (const std::string& id : ids) {
            json a = s.generar(id, true);
            const json k = s.generarClave(id, pa::usuarioDe(a.value("banca", std::string())));
            a["usuario"] = k.value("usuario", std::string());
            a["clave"] = k.value("clave", std::string());
            accesos.push_back(a);
        }
    });
    CHECK(sync == 1, "dar acceso a 12 en lote: el gateway se entera una vez");
    const json gw = s.paraGateway();
    CHECK(gw.size() == 12 && std::all_of(gw.begin(), gw.end(), [](const json& c) { return c.value("prueba", false); }),
          "al gateway van marcados «prueba»");
    const json archivo = pa::archivoHinchada(accesos);
    const json& l = archivo["integrantes"];
    CHECK(archivo["version"] == 1 && l.size() == 12, "el archivo del probador: los 12");
    CHECK(l[0]["link"].get<std::string>().find("/sesion/#i=prueba-01&t=") != std::string::npos && l[0]["pin"].get<std::string>().size() == 6,
          "con su link personal y su PIN");
    CHECK(l[0]["usuario"] == "prueba01" && l[0]["clave"].get<std::string>().size() == 12, "y su usuario y contraseña");

    // --- Lo que lanza el probador ------------------------------------------------------------
    pa::Comportamiento c;
    CHECK(pa::comportamientoDeJson(json::object(), c).empty() && c.votanPct == 90 && c.pinErradoIntentos == 5,
          "sin nada: el comportamiento de fábrica");
    CHECK(pa::comportamientoDeJson({{"votan_pct", 50}, {"voto", {{"afirmativo", 3}, {"negativo", 1}}}, {"habilitar_solos_s", 5}}, c).empty() &&
              c.votanPct == 50 && c.voto == "afirmativo:3,negativo:1" && c.habilitarSolosS == 5,
          "con proporciones de voto y habilitar solos");
    pa::Comportamiento malo;
    CHECK(!pa::comportamientoDeJson({{"votan_pct", 120}}, malo).empty(), "un % de más: error");
    CHECK(!pa::comportamientoDeJson({{"voto", "afirmativo"}}, malo).empty(), "un voto sin proporción: error");
    CHECK(!pa::comportamientoDeJson({{"pin_errado_intentos", 0}}, malo).empty(), "los intentos, de 1 a 20");
    const std::vector<std::string> args =
        pa::argumentosHinchada(c, "https://127.0.0.1:8789", "/x/integrantes.json", 3, "/x/hinchada", "/g/grabado.hgr");
    CHECK(contiene(args, "integrantes") && contiene(args, "/x/integrantes.json") && contiene(args, "grabado") &&
              contiene(args, "afirmativo:3,negativo:1") && contiene(args, "3m") && contiene(args, "--inseguro"),
          "los argumentos de hinchada: servidor integrantes, el archivo, modo grabado, el voto y la duración");
    const std::string ext = pa::comandoExterno(c, "https://10.0.0.2:8789", 3);
    CHECK(ext.find("--integrantes integrantes.json") != std::string::npos && ext.find(l[0]["pin"].get<std::string>()) == std::string::npos,
          "la línea del generador externo: el archivo, ningún secreto");

    // --- Cómo va (lo que sabe la app) ------------------------------------------------------------
    const json part = json::array({{{"id", "prueba-01"}, {"conectado", true}, {"estado", "habilitado"}, {"lugar", "afuera"}, {"turno", 1}},
                                   {{"id", "prueba-02"}, {"conectado", true}, {"estado", "esperando"}, {"lugar", "sala"}},
                                   {{"id", "prueba-03"}, {"conectado", false}, {"estado", "sin_conectar"}, {"bloqueado", true}},
                                   {{"id", "real-1"}, {"conectado", true}, {"estado", "habilitado"}, {"palabra", true}}});
    const json vot = {{"estado", "abierta"}, {"numero", 2},
                      {"votos", json::array({{{"id", "prueba-01"}, {"opcion", "afirmativo"}}, {{"id", "real-1"}, {"opcion", "negativo"}}})}};
    const json v = pa::vivo(part, vot, ids, 12);
    CHECK(v["conectados"] == 2 && v["esperando"] == 1 && v["habilitados"] == 1 && v["afuera"] == 1 && v["bloqueados"] == 1 &&
              v["enLaFila"] == 1 && v["conPalabra"] == 0,
          "vivo: cuenta solo a los de prueba");
    CHECK(v["votaron"] == 1 && v["porOpcion"]["afirmativo"] == 1 && !v["porOpcion"].contains("negativo"), "y solo sus votos");
    CHECK(pa::nota(v, json::object()).find("12 integrantes de prueba") != std::string::npos, "la nota del informe");

    // --- Limpiar: solo los de prueba -------------------------------------------------------------
    // Un integrante real que se coló (lo cargaron a mano durante la prueba): no se toca.
    const std::string ana = cuerpo.guardar({{"nombre", "Ana Pérez"}, {"banca", "3"}}, &err);
    const json accesoAna = s.generar(ana, true);
    const std::vector<std::string> borrar = pa::aBorrar(cuerpo.integrantes());
    CHECK(borrar.size() == 12 && !contiene(borrar, ana), "a borrar: los 12 de prueba, no la real");
    s.enLote([&]() {
        for (const std::string& id : borrar) s.revocar(id);
    });
    cuerpo.enLote([&]() {
        for (const std::string& id : borrar) cuerpo.borrar(id);
    });
    cuerpo.restaurarRecinto(recintoAntes);
    CHECK(cuerpo.integrantes().size() == 1 && cuerpo.buscar(ana).value("nombre", std::string()) == "Ana Pérez",
          "queda solo la real");
    CHECK(s.tieneCredencial(ana) && !s.tieneCredencial("prueba-01") && s.paraGateway().size() == 1,
          "y su acceso; el de los de prueba, revocado");
    CHECK(cuerpo.recintoGuardado() == recintoAntes, "el recinto vuelve a como estaba");
    cuerpo.restaurarRecinto(json());
    CHECK(cuerpo.recintoGuardado().is_null(), "sin recinto antes: queda sin recinto");

    ba::persist::cola().vaciar();
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
