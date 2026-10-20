// src/app/tests/test_resolucion.cpp — el plan de resolución (resolucion::planear) con entradas de mentira
#include "resolucion/Plan.hpp"

#include <cstdio>
#include <string>

using json = nlohmann::json;
using namespace ba::app::resolucion;

static int g_fallas = 0;
#define CHECK(c, m)                        \
    do {                                   \
        if (!(c)) {                        \
            std::printf("FALLA: %s\n", m); \
            ++g_fallas;                    \
        }                                  \
    } while (0)

static const json* de(const json& plan, const std::string& tipo) {
    for (const auto& h : plan["hallazgos"])
        if (h["tipo"] == tipo) return &h;
    return nullptr;
}

static Tarjeta tarjeta(const std::string& id, const std::string& elemento, Tam sale, double zoom = 1, bool soloRam = false,
                       bool gpu = false) {
    Tarjeta t;
    t.id = id;
    t.titulo = id;
    t.elemento = elemento;
    t.sale = sale;
    t.zoom = zoom;
    t.soloRam = soloRam;
    t.gpuPosible = gpu;
    return t;
}

static ba::motor::ModoVideo modo(int w, int h, double fps, const std::string& formato) {
    ba::motor::ModoVideo m;
    m.ancho = w;
    m.alto = h;
    m.fps = fps;
    m.cps = {fps};
    m.formato = formato;
    if (formato != "crudo") m.decodificador = "jpegdec";
    return m;
}

int main() {
    Entrada base;
    base.id = "cam";
    base.lienzo = {1920, 1080};
    base.fps = 30;
    base.origen = {1920, 1080};
    base.origenFps = 30;
    base.seVe = {{"Principal", {1920, 1080}}};

    // 1) La cadena del usuario en Windows: PTZ → quarktv → superres, a pantalla completa.
    {
        Entrada e = base;
        e.cadenaGpu = true;
        e.cadena = {tarjeta("ptz", "baptzd3d12", {1920, 1080}, 1, false, true), tarjeta("disolver", "quarktv", {1920, 1080}, 1, true),
                    tarjeta("sr", "basuperres", {3840, 2160}, 1, true)};
        const json p = planear(e);
        std::printf("%s\n", p.dump(1, ' ', false, json::error_handler_t::replace).c_str());
        const json* h = de(p, "superres_sobra");
        CHECK(h && (*h)["tarjeta"] == "sr" && (*h)["nivel"] == "mejorable" && (*h)["arreglo"]["accion"] == "apagar",
              "la superres que agranda 1920 para verse a 1920: amarilla, con «apagar»");
        CHECK(p["niveles"].value("sr", json::object()).value("nivel", "") == "mejorable", "el nivel queda en la tarjeta");
        CHECK(!de(p, "parte_la_cadena"), "quarktv no parte la cadena: lo que sigue (la superres) también es RAM");
        CHECK(p["llega"]["ancho"] == 3840 && p["demanda"]["ancho"] == 1920, "llega 3840, se ve 1920");
    }
    // 1b) La superres que no agranda (el motor no acepta ese tamaño): lo dice así.
    {
        Entrada e = base;
        e.cadena = {tarjeta("sr", "basuperres", {1920, 1080})};
        const json p = planear(e);
        const json* h = de(p, "superres_no_agranda");
        CHECK(h && (*h)["texto"].get<std::string>().find("960×540") != std::string::npos && !de(p, "superres_sobra"),
              "superres que entra y sale 1920: «no está agrandando», con lo que tendría que recibir (960×540)");
    }
    // 2) El PTZ después de la superres.
    {
        Entrada e = base;
        e.origen = {960, 540};
        e.cadena = {tarjeta("sr", "basuperres", {1920, 1080}), tarjeta("ptz", "baptz", {1920, 1080}, 2)};
        const json p = planear(e);
        const json* h = de(p, "orden_ptz_superres");
        CHECK(h && (*h)["nivel"] == "mal_armado" && (*h)["tarjeta"] == "ptz" && (*h)["arreglo"]["antes_de"] == "sr",
              "PTZ después de la superres: naranja, con «reordenar antes de la superres»");
    }
    // 3) Zoom sin superres: 3 = amarillo; 4 = rojo.
    {
        Entrada e = base;
        e.cadena = {tarjeta("ptz", "baptz", {1920, 1080}, 3)};
        const json p = planear(e);
        const json* h = de(p, "zoom_sin_detalle");
        CHECK(h && (*h)["nivel"] == "mejorable" && (*h)["texto"].get<std::string>().find("640×360") != std::string::npos,
              "zoom 3 sobre 1080p: 640×360 reales estirados (amarillo)");
        CHECK(p["pasos"][0]["real"]["ancho"] == 640, "el paso dice el detalle real");
        CHECK(p["fuente"]["necesita"]["ancho"] == 5760, "para verse a 1920 con zoom 3, la cámara tendría que dar 5760");
        e.cadena = {tarjeta("ptz", "baptz", {1920, 1080}, 4)};
        const json p4 = planear(e); // de() apunta adentro: el plan tiene que seguir vivo
        const json* h4 = de(p4, "zoom_sin_detalle");
        CHECK(h4 && (*h4)["nivel"] == "mal", "zoom 4: menos de un tercio del detalle (rojo)");
        e.cadena = {tarjeta("ptz", "baptz", {1920, 1080}, 2), tarjeta("sr", "basuperres", {3840, 2160})};
        CHECK(!de(planear(e), "zoom_sin_detalle"), "zoom 2 con superres después: es lo esperado");
    }
    // 4) Un PiP de 640: la cámara entrega de más y tiene un modo que alcanza.
    {
        Entrada e = base;
        e.seVe = {{"Entrevista", {640, 360}}};
        e.esCamara = true;
        e.modos = {modo(1920, 1080, 30, "mjpeg"), modo(1280, 720, 30, "mjpeg"), modo(640, 360, 30, "crudo")};
        const json p = planear(e);
        const json* h = de(p, "fuente_de_mas");
        CHECK(h && (*h)["nivel"] == "mejorable" && (*h)["arreglo"]["valor"] == "640x360@30" && !h->contains("tarjeta"),
              "PiP de 640: la cámara podría dar 640×360 crudo (amarillo, en la entrada)");
        CHECK(p["niveles"].value("entrada", json::object()).value("nivel", "") == "mejorable", "el nivel va a la entrada");
    }
    // 5) Pocos cps, con el modo que los da.
    {
        Entrada e = base;
        e.esCamara = true;
        e.origenFps = 5;
        e.modos = {modo(1920, 1080, 5, "crudo"), modo(1920, 1080, 30, "mjpeg")};
        const json p5 = planear(e);
        const json* h = de(p5, "pocos_cps");
        CHECK(h && (*h)["nivel"] == "mal" && (*h)["arreglo"]["valor"] == "1920x1080@30:mjpeg", "5 cps contra 30: rojo, con el MJPEG a 30");
    }
    // 6) Un filtro de CPU entre dos de GPU.
    {
        Entrada e = base;
        e.cadenaGpu = true;
        e.cadena = {tarjeta("ptz", "baptzd3d12", {1920, 1080}, 1, false, true), tarjeta("viejo", "agingtv", {1920, 1080}, 1, true),
                    tarjeta("color", "glcolorbalance", {1920, 1080}, 1, false, true)};
        const json p6 = planear(e);
        const json* h = de(p6, "parte_la_cadena");
        CHECK(h && (*h)["tarjeta"] == "viejo" && (*h)["nivel"] == "mal_armado", "agingtv entre dos de GPU: naranja");
    }
    // 6b) Un invitado de 1280×720 en un rectángulo de 960×389 (recortado): se achica a 0,75, no es «de más».
    {
        Entrada e = base;
        e.origen = {1280, 720};
        e.seVe = {{"Sala", {960, 389}}};
        e.cadena = {tarjeta("ptz", "baptz", {1280, 720})};
        const json p = planear(e);
        CHECK(!de(p, "llega_de_mas") && !de(p, "ptz_saca_de_mas"), "1280 visto a 960 (recortado): nada");
        e.seVe = {{"PiP", {480, 270}}};
        const json p2 = planear(e);
        CHECK(de(p2, "ptz_saca_de_mas"), "1280 visto a 480: el PTZ saca de más");
    }
    // 6c) Una imagen fija a 25 cps: los cps no importan.
    {
        Entrada e = base;
        e.fija = true;
        e.origenFps = 25;
        CHECK(!de(planear(e), "pocos_cps"), "una imagen fija no tiene «pocos cps»");
    }
    // 7) Sin escena.
    {
        Entrada e = base;
        e.seVe.clear();
        const json p = planear(e);
        CHECK(p.contains("nota") && p["hallazgos"].empty(), "no se ve en ninguna escena: sin hallazgos, con la nota");
    }
    std::printf("test_resolucion: %s\n", g_fallas == 0 ? "OK" : "FALLAS");
    return g_fallas == 0 ? 0 : 1;
}
