// ============================================================================
// src/app/tests/test_evento_web.cpp — la página web del evento en vivo
//
//   1. Generar escribe portada, una página por sección, cierre, estilo y sitio.json.
//   2. Regenerar no pisa lo editado (salvo sobrescribir).
//   3. El subdominio sale del nombre (sin acentos, sin «_», con guiones).
//   4. El estado público: la página de la sección actual, sin cues ni notas.
//   5. resolverPagina: por nombre, id, número; nada fuera de Web/.
// ============================================================================
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "evento/EventoWeb.hpp"

namespace fs = std::filesystem;
using namespace ba::evento;

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);             \
            ++g_fallos;                                                                  \
        }                                                                                \
    } while (0)
std::string leer(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
} // namespace

int main() {
    setCarpetaPlantillas(BA_PLANTILLAS_EVENTO);
    const fs::path dir = fs::temp_directory_path() / ("ba-evento-web-" + std::to_string(::time(nullptr)));
    fs::remove_all(dir);
    Evento e;
    e.nombre = "Entrevista_Pepito Ñandú";
    e.fecha = "2026-10-15";
    e.inicio = "19:00";
    Bloque a; a.id = "b1"; a.nombre = "Presentación"; a.nota = "NOTA SECRETA DEL OPERADOR";
    Cue c; c.texto = "Cortar a la cámara 2 (instrucción interna)"; c.duracionSeg = 300; a.cues.push_back(c);
    Bloque b; b.id = "b2"; b.nombre = "Preguntas <del público>";
    e.bloques = {a, b};

    // 1
    ResultadoWeb r = generar(e, dir, false);
    CHECK(r.error.empty(), "sin error");
    for (const char* f : {"portada.html", "seccion-b1.html", "seccion-b2.html", "cierre.html", "estilo.css", "sitio.json"})
        CHECK(fs::exists(dir / f), f);
    const std::string p1 = leer(dir / "seccion-b1.html");
    CHECK(p1.find("data-seccion=\"b1\"") != std::string::npos, "la página sabe qué sección es");
    CHECK(p1.find("Presentación") != std::string::npos, "y lleva su nombre en el título (sin JavaScript)");
    CHECK(p1.find("/evento/_/vivo.js") != std::string::npos, "carga vivo.js (que arma el resto)");
    CHECK(p1.find("{{") == std::string::npos, "no quedan marcas sin llenar");
    CHECK(p1.find("NOTA SECRETA") == std::string::npos && p1.find("instrucción interna") == std::string::npos,
          "ni notas ni cues en la página pública");
    CHECK(leer(dir / "seccion-b2.html").find("&lt;del público&gt;") != std::string::npos, "nombres escapados");
    CHECK(leer(dir / "portada.html").find("data-vivo=\"agenda\"") != std::string::npos, "la portada arma el programa en vivo");

    // 2. Una página intacta se actualiza con el guion; una editada, no.
    {
        Evento e2 = e;
        Bloque nuevo; nuevo.id = "b3"; nuevo.nombre = "Sección nueva";
        e2.bloques.push_back(nuevo);
        generar(e2, dir, false);
        CHECK(fs::exists(dir / "seccion-b3.html"), "la sección nueva tiene su página");
        SitioWeb sw = SitioWeb::leer(dir, e2);
        sw.titulo = "Otro título";
        sw.guardar(dir);
        generar(e2, dir, false);
        CHECK(leer(dir / "portada.html").find("Otro título") != std::string::npos, "intacta: se actualiza");
    }
    { std::ofstream(dir / "portada.html") << "MIA"; }
    r = generar(e, dir, false);
    CHECK(leer(dir / "portada.html") == "MIA", "no pisa lo editado");
    CHECK(!r.salteadas.empty(), "dice qué salteó");
    generar(e, dir, true);
    CHECK(leer(dir / "portada.html") != "MIA", "sobrescribir sí pisa");

    // 3
    CHECK(subdominioDe("Entrevista_Pepito Ñandú") == "entrevista-pepito-nandu", "subdominio");
    CHECK(subdominioDe("  --Ánimo!! 2026  ") == "animo-2026", "subdominio con basura");
    CHECK(SitioWeb::leer(dir, e).subdominio == "entrevista-pepito-nandu", "sitio.json trae el subdominio del nombre");

    // 4
    const SitioWeb s = SitioWeb::leer(dir, e);
    nlohmann::json est = estadoPublico(e, s, "en_vivo", 1, 0, 1000, 2000);
    CHECK(est["pagina"] == "seccion-b2.html", "página de la sección actual");
    CHECK(est["secciones"][0]["estado"] == "hecha" && est["secciones"][1]["estado"] == "actual", "estados de la agenda");
    CHECK(est.dump().find("SECRETA") == std::string::npos && est.dump().find("interna") == std::string::npos, "nada privado");
    CHECK(estadoPublico(e, s, "proximo", -1, 0, 0, 0)["pagina"] == "portada.html", "antes: portada");
    CHECK(estadoPublico(e, s, "terminado", -1, 0, 0, 0)["pagina"] == "cierre.html", "después: cierre");

    // 5
    CHECK(resolverPagina(e, "Presentación") == "seccion-b1.html", "por nombre");
    CHECK(resolverPagina(e, "2") == "seccion-b2.html", "por número");
    CHECK(resolverPagina(e, "portada") == "portada.html", "portada");
    CHECK(resolverPagina(e, "../../etc/passwd").empty(), "nada fuera de Web/");
    CHECK(resolverPagina(e, "sub/x.html").empty(), "sin carpetas");
    CHECK(resolverPagina(e, "extra.html") == "extra.html", "un .html propio");

    // 5b. Horario estimado: en vivo, las que siguen se corren desde ahora.
    {
        ExtrasVivo x;
        x.inicioReal[0] = 500000;
        x.restanteActualSeg = 120;
        x.aviso = {{"texto", "Arrancamos en 5"}, {"nivel", "info"}};
        x.habla = "Ana";
        Evento e3 = e;
        e3.bloques[1].cues.push_back(c);
        nlohmann::json est3 = estadoPublico(e3, s, "en_vivo", 0, 400000, 400000, 1000000, x);
        CHECK(est3["secciones"][0]["inicioRealMs"] == 500000, "inicio real de la actual");
        CHECK(est3["secciones"][1]["inicioEstimadoMs"] == 1000000 + 120000, "la siguiente: ahora + lo que falta");
        CHECK(est3["aviso"]["texto"] == "Arrancamos en 5" && est3["habla"] == "Ana", "aviso y quién habla");
        CHECK(estadoPublico(e3, s, "proximo", -1, 400000, 0, 0, x)["habla"] == "", "antes de empezar no habla nadie");
    }

    // 6. Dónde verlo en vivo
    CHECK(idDeYouTube("https://www.youtube.com/watch?v=jfKfPfyJRdk&t=3") == "jfKfPfyJRdk", "id de watch?v=");
    CHECK(idDeYouTube("https://youtu.be/jfKfPfyJRdk") == "jfKfPfyJRdk", "id de youtu.be");
    CHECK(idDeYouTube("https://www.youtube.com/live/jfKfPfyJRdk?si=x") == "jfKfPfyJRdk", "id de /live/");
    CHECK(idDeYouTube("hola").empty(), "sin id");
    SitioWeb sv;
    nlohmann::json pl{{"youtube", {{"liveId", "AAAAAAAAAAA"}, {"canalId", "UCxxxxxxxxxxxxxxxxxxxxxx"}}},
                      {"twitch", {{"login", "micanal"}, {"enVivo", true}}},
                      {"facebook", {{"liveId", "123"}, {"pageId", "456"}}}};
    nlohmann::json v = armarVivos(sv, pl);
    CHECK(v.size() == 3, "tres plataformas");
    CHECK(v[0]["embed"].get<std::string>().find("/embed/AAAAAAAAAAA") != std::string::npos && v[0]["enVivo"] == true, "youtube en vivo");
    CHECK(v[1]["url"] == "https://www.twitch.tv/micanal", "twitch");
    CHECK(v[2]["url"] == "https://www.facebook.com/456/videos/123/", "facebook");
    pl["youtube"]["liveId"] = "";
    v = armarVivos(sv, pl);
    CHECK(v[0]["embed"].get<std::string>().find("live_stream?channel=UCxxxxxxxxxxxxxxxxxxxxxx") != std::string::npos, "youtube: el vivo del canal");
    sv.vivos = {{"youtube", "https://youtu.be/jfKfPfyJRdk"}, {"twitch", "https://www.twitch.tv/Otro"}};
    v = armarVivos(sv, pl);
    CHECK(v[0]["url"] == "https://www.youtube.com/watch?v=jfKfPfyJRdk", "lo fijado a mano manda");
    CHECK(v[1]["url"] == "https://www.twitch.tv/otro", "twitch fijado");
    CHECK(armarVivos(SitioWeb{}, nlohmann::json::object()).empty(), "sin cuentas ni nada fijado: vacío");
    CHECK(estadoPublico(e, s, "proximo", -1, 0, 0, 0, v)["vivos"].size() == 3, "van en el estado");

    // La asamblea: solo si el evento lo es, y sin romperse si la sala no llegó (null).
    {
        Evento ea = e;
        ea.asamblea = true;
        ExtrasVivo xa; // xa.sala queda en null
        nlohmann::json ok;
        bool tiro = false;
        try { ok = estadoPublico(ea, s, "en_vivo", 0, 0, 0, 0, xa); } catch (...) { tiro = true; }
        CHECK(!tiro, "sala en null: no tira");
        CHECK(!tiro && ok["asamblea"].is_null() && ok["ordenDelDia"].is_array(), "sala en null: lo de la sala vacío");
        xa.sala = {{"asamblea", {{"cuerpo", "Consejo"}, {"presentes", 3}, {"total", 5}}}};
        CHECK(estadoPublico(ea, s, "en_vivo", 0, 0, 0, 0, xa)["asamblea"]["presentes"] == 3, "asamblea habilitada: va");
        ea.asamblea = false;
        CHECK(estadoPublico(ea, s, "en_vivo", 0, 0, 0, 0, xa)["asamblea"].is_null(), "asamblea no habilitada: null");
    }

    fs::remove_all(dir);
    std::printf(g_fallos ? "test_evento_web: %d fallos\n" : "test_evento_web: OK\n", g_fallos);
    return g_fallos ? 1 : 0;
}
