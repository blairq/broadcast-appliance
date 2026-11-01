// test_oradores_sala.cpp — lo que la interfaz y el MCP comparten de los oradores (sala::OradoresSala)
//
//   la regla del reloj (los últimos 10 s avisan, pasado cero se excede, lo que queda se
//   redondea para arriba), el texto «m:ss» / «+m:ss», la lectura de un tiempo escrito
//   (un número solo son MINUTOS, en la interfaz y en el MCP: lo que escribe el operador)
//   y la fila en orden.

#include <cstdio>
#include <string>

#include "sala/OradoresSala.hpp"

#include <QJsonDocument>

using ba::app::sala::OradoresSala;

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

int leer(const std::string& t, int actual, std::string* error = nullptr) {
    int s = -1;
    const std::string e = OradoresSala::leerTiempo(t, actual, s);
    if (error != nullptr) *error = e;
    return e.empty() ? s : -1;
}
} // namespace

int main() {
    // --- El reloj ---------------------------------------------------------------------------
    {
        auto r = OradoresSala::reloj(60, 1000, 11000);
        CHECK(r.restanteMs == 50000 && r.estado == "ok" && r.texto == "0:50", "reloj: 50 s quedan, ok");
        r = OradoresSala::reloj(60, 1000, 52000);
        CHECK(r.estado == "aviso" && r.texto == "0:09", "reloj: los últimos 10 s avisan");
        r = OradoresSala::reloj(60, 0, 59500);
        CHECK(r.estado == "aviso" && r.texto == "0:01", "reloj: medio segundo se muestra como 0:01 (para arriba)");
        r = OradoresSala::reloj(60, 0, 60000);
        CHECK(r.estado == "aviso" && r.texto == "0:00", "reloj: justo en cero todavía no se pasó");
        r = OradoresSala::reloj(60, 0, 65000);
        CHECK(r.estado == "excedido" && r.texto == "+0:05", "reloj: pasado cero cuenta para adelante, excedido");
        r = OradoresSala::reloj(120, 0, 0);
        CHECK(r.texto == "2:00" && r.estado == "ok", "reloj: recién al aire");
    }
    // --- El texto ------------------------------------------------------------------------------
    CHECK(OradoresSala::textoTiempo(150000) == "2:30", "texto: 2:30");
    CHECK(OradoresSala::textoTiempo(-61000) == "+1:01", "texto: negativo con +");
    CHECK(OradoresSala::textoTiempo(0) == "0:00", "texto: cero");
    // --- Leer un tiempo --------------------------------------------------------------------
    CHECK(leer("2:30", 0) == 150, "leer: m:ss");
    CHECK(leer("3", 0) == 180, "leer: un número solo son minutos");
    CHECK(leer("1,5", 0) == 90, "leer: 1,5 son minutos y medio");
    CHECK(leer("45s", 0) == 45, "leer: 45s son segundos");
    CHECK(leer(" 90s ", 0) == 90, "leer: 90s (con espacios)");
    CHECK(leer("3m", 0) == 180, "leer: 3m");
    CHECK(leer("1,5m", 0) == 90, "leer: 1,5m (coma)");
    CHECK(leer("1.5 min", 0) == 90, "leer: 1.5 min");
    CHECK(leer("+2:00", 60) == 180, "leer: +2:00 suma a lo que tenía");
    CHECK(leer("-30s", 60) == 30, "leer: -30s resta segundos");
    CHECK(leer("+1", 60) == 120, "leer: +1 suma un minuto");
    CHECK(leer("=2:30", 60) == 150, "leer: =2:30 lo deja en eso");
    std::string e;
    CHECK(leer("-90s", 60, &e) == -1 && !e.empty(), "leer: no puede quedar en cero o menos");
    CHECK(leer("0", 0, &e) == -1 && !e.empty(), "leer: cero no vale");
    CHECK(leer("abc", 0, &e) == -1 && !e.empty(), "leer: lo que no se entiende es error");
    CHECK(leer("99999999999999", 0, &e) == -1, "leer: un número enorme es error (no excepción)");
    // --- Un tiempo en JSON: un ENTERO son segundos (lo que manda un programa), un texto
    // sigue la regla del operador -------------------------------------------------------------
    {
        const auto json = [](const nlohmann::json& v, int actual, std::string* error = nullptr) {
            int s = -1;
            const std::string e = OradoresSala::leerTiempoJson(v, actual, s);
            if (error != nullptr) *error = e;
            return e.empty() ? s : -1;
        };
        CHECK(json(120, 0) == 120, "json: 120 entero son 120 segundos (dos minutos, no dos horas)");
        CHECK(json(90, 600) == 90, "json: un entero es absoluto, no suma");
        CHECK(json("3", 0) == 180, "json: el texto «3» son tres minutos (regla del operador)");
        CHECK(json("45s", 0) == 45 && json("0:45", 0) == 45, "json: «45s» y «0:45» son segundos");
        CHECK(json("+2", 60) == 180 && json("-30s", 60) == 30, "json: «+2» y «-30s» suman y restan");
        CHECK(json(0, 0, &e) == -1 && !e.empty(), "json: cero no vale");
        CHECK(json(-5, 0, &e) == -1 && !e.empty(), "json: negativo no vale");
        CHECK(json(1.5, 0, &e) == -1 && !e.empty(), "json: un número con decimales no se adivina");
        CHECK(json(true, 0, &e) == -1 && !e.empty(), "json: un booleano es error");
    }
    // --- Sobre qué se suma: el tiempo propio, o el general ----------------------------------------
    {
        const QJsonObject foto = QJsonDocument::fromJson(R"({"speakers": {"defaultSec": 300},
            "participants": [{"id": "a", "allottedSec": 120}, {"id": "b", "allottedSec": 0}, {"id": "", "allottedSec": 9}]})").object();
        CHECK(OradoresSala::tiempoActual(foto, "a") == 120, "actual: el suyo si tiene");
        CHECK(OradoresSala::tiempoActual(foto, "b") == 300, "actual: el general si el suyo es 0");
        CHECK(OradoresSala::tiempoActual(foto, "zz") == 300, "actual: el general si no está");
        CHECK(OradoresSala::tiempoActual(foto, "") == 300, "actual: sin id, el general");
    }
    // --- La fila -----------------------------------------------------------------------------
    {
        const nlohmann::json foto{
            {"order", {"b", "a", "x", "c"}},
            {"participants",
             {{{"id", "a"}, {"name", "Ana"}, {"speaker", "queued"}},
              {{"id", "b"}, {"name", "Beto"}, {"speaker", "onair"}},
              {{"id", "c"}, {"name", "Caro"}, {"speaker", "queued"}},
              {{"id", "d"}, {"name", "Dani"}, {"speaker", "done"}}}}};
        const nlohmann::json f = OradoresSala::fila(foto);
        CHECK(f.size() == 3, "fila: solo los que están en el orden y existen");
        CHECK(f.size() == 3 && f[0]["id"] == "b" && f[1]["id"] == "a" && f[2]["id"] == "c", "fila: en el orden de la sala");
        CHECK(f.size() == 3 && f[0]["position"] == 0 && f[1]["position"] == 1 && f[2]["position"] == 2,
              "fila: los que esperan se numeran desde 1; el que habla, 0");
        CHECK(f.size() == 3 && f[1]["name"] == "Ana", "fila: cada uno tal cual (con sus datos)");
        CHECK(OradoresSala::fila(nlohmann::json::object()).empty(), "fila: sin sala, vacía");
    }
    std::printf(g_fallas == 0 ? "\nTODO BIEN\n" : "\n%d FALLAS\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
