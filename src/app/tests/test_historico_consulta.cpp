// ============================================================================
// src/app/tests/test_historico_consulta.cpp — la API json del histórico (E-MCP25/26)
//
// La página Histórico y el MCP (evento_config, evento_grabaciones, historico_exportar,
// evento_sellos) leen por HistoricoConsulta. Antes el MCP resolvía el evento solo en la
// configuración abierta y con «@otra-config/…» fallaba.
//
//   1. «@B/Sesion» es el evento «Sesion» de la configuración B; «@../x» no sale.
//   2. La foto de Config/ de ese evento: la lista y un archivo (de los objetos de B).
//   3. Comparar dos fotos de B; «actual» con un evento de otra configuración, no.
//   4. Exportar «@B/Sesion»: la copia está, con sus fotos.
//   5. Las grabaciones, con sus horas legibles.
// ============================================================================
#include <QCoreApplication>
#include <QTemporaryDir>

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "evento/EventoStore.hpp"
#include "evento/FotosConfig.hpp"
#include "ui/HistoricoConsulta.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);             \
            ++g_fallos;                                                                  \
        }                                                                                \
    } while (0)

void escribir(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream o(p, std::ios::binary);
    o << s;
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    const fs::path confs = fs::path(tmp.path().toStdString()) / "Configuraciones";
    // A es la abierta; B, otra configuración con su evento cerrado.
    const fs::path a = confs / "A", b = confs / "B";
    ba::evento::EventoStore(a / "Eventos").crear("Propio");
    ba::evento::EventoStore stB(b / "Eventos");
    const std::string sesion = stB.crear("Sesion");
    CHECK(!sesion.empty(), "se crea el evento de B");
    // La foto de Config/ de B, al empezar.
    escribir(b / "Config" / "escenas.json", R"({"escenas": ["Atril"]})");
    ba::evento::FotosConfig fotosB(b / "Config", b / "Eventos" / ".objetos");
    const json foto = fotosB.foto(1000);
    CHECK(ba::evento::FotosConfig::guardar(stB.carpetaDe(sesion) / "config" / "inicio.json", foto), "se guarda la foto");
    CHECK(ba::evento::FotosConfig::guardar(stB.carpetaDe(sesion) / "config" / "cierre.json", foto), "y la de cierre");
    escribir(stB.carpetaDe(sesion) / "grabaciones.json", R"([{"archivo": "/no/esta.mkv", "desde_ms": 1700000000000, "hasta_ms": 1700000060000}])");

    const ba::app::HistoricoConsulta c(confs, "A", a / "Eventos", a / "Config");
    const std::string otro = "@B/" + sesion;

    // 1.
    CHECK(!c.carpeta(otro).empty(), "«@B/…» es el de la otra configuración");
    CHECK(c.carpeta("@B/no-existe").empty(), "uno que no está, no");
    CHECK(c.carpeta("@../A/Eventos").empty(), "no se sale de Configuraciones/");
    CHECK(!c.lugar(otro).abierto && c.lugar("Propio").abierto, "sabe cuál es de la abierta");

    // 2.
    const json lista = c.config(otro, "inicio");
    CHECK(!lista.contains("error") && lista.value("archivos", json::array()).size() == 1, "la foto de B tiene su archivo");
    const json cont = c.contenidoConfig(otro, "inicio", "escenas.json");
    CHECK(cont.value("contenido", json()).value("escenas", json::array()).size() == 1, "el contenido sale de los objetos de B");
    CHECK(c.config(otro, "ultima").contains("error"), "una foto que no tiene: error, no vacío");

    // 3.
    const json cmp = c.comparar(otro, "inicio", otro, "cierre");
    CHECK(!cmp.contains("error") && cmp.value("archivos_distintos", -1) == 0, "inicio y cierre iguales");
    CHECK(c.comparar(otro, "inicio", {}, "actual").contains("error"), "«actual» es de la abierta: con B no se compara");

    // 4.
    fs::path copia;
    const std::string e = c.exportar(otro, fs::path(tmp.path().toStdString()) / "Exportados", false, &copia);
    CHECK(e.empty(), "exporta «@B/…»");
    CHECK(!copia.empty() && fs::exists(copia / "evento.json"), "la copia tiene el guion");
    CHECK(fs::exists(copia / "config" / "inicio.json"), "y sus fotos");

    // 5.
    const json g = c.grabaciones(otro);
    CHECK(g.value("grabaciones", json::array()).size() == 1, "la grabación de B");
    CHECK(!g["grabaciones"][0].value("desde", std::string()).empty(), "con la hora legible");
    CHECK(g["grabaciones"][0].value("duracion_seg", 0) == 60, "y su duración");

    if (g_fallos == 0) std::printf("test_historico_consulta: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
