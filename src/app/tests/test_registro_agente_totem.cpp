// ============================================================================
// src/app/tests/test_registro_agente_totem.cpp — el ciclo entero contra un servidor FALSO
//
// Un servidor de /v1/chat/completions de mentira en 127.0.0.1 (QTcpServer) contesta en
// streaming (SSE, de a pedazos) lo que dice cada guion; AgenteTotem + ChatEnVivo +
// HttpsClient hacen lo de verdad:
//   1. Varios datos de una, con las herramientas llegando de a pedazos: el guion los confirma
//      juntos (y el pedido va con stream y herramientas).
//   2. Una pregunta sobre el evento: el texto llega de a pedazos, la voz recibe frase por
//      frase mientras escribe, la pantalla el texto entero; el guion retoma la pregunta.
//   3. Un dato que no valida: segunda vuelta con el resultado, y el modelo repregunta.
//   4. El servidor no contesta: a los N ms contesta el guion (lo entiende con sus
//      normalizadores); a la segunda, la conversación sigue con el guion solo.
//   5. Dos conversaciones a la vez con un solo lugar: la segunda espera en la cola, nunca hay
//      dos pedidos juntos, y las dos terminan.
// ============================================================================
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "agent/ChatEnVivo.hpp"
#include "registro/AgenteTotem.hpp"

using namespace ba::registro;
using ba::app::agent::ChatEnVivo;
using ba::app::agent::FuenteMirada;
using nlohmann::json;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        } else {                                                                                             \
            std::printf("ok: %s\n", msg);                                                                    \
        }                                                                                                    \
    } while (false)

bool contiene(const std::string& s, const std::string& x) { return s.find(x) != std::string::npos; }

/// Espera (con el bucle de eventos andando) a que `listo` sea cierto, hasta `ms`.
bool esperar(const std::function<bool()>& listo, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!listo() && t.elapsed() < ms) {
        QEventLoop l;
        QTimer::singleShot(10, &l, &QEventLoop::quit);
        l.exec();
    }
    return listo();
}

std::string sseTexto(const std::string& s) {
    return "data: " + json{{"choices", {{{"delta", {{"content", s}}}}}}}.dump() + "\n\n";
}
std::string sseHerramienta(int indice, const std::string& id, const std::string& nombre, const std::string& args) {
    json f = {{"arguments", args}};
    if (!nombre.empty()) f["name"] = nombre;
    json tc = {{"index", indice}, {"function", f}};
    if (!id.empty()) tc["id"] = id;
    return "data: " + json{{"choices", {{{"delta", {{"tool_calls", {tc}}}}}}}}.dump() + "\n\n";
}
const std::string kFin = "data: [DONE]\n\n";

/// Lo que contesta el servidor falso a un pedido.
struct Respuesta {
    int demoraMs = 0;
    std::vector<std::string> pedazos; ///< cada uno, un pedazo del chunked (con una pausa entre medio)
    bool nunca = false;               ///< acepta y no contesta (un modelo colgado)
};

class Falso : public QObject {
   public:
    QTcpServer srv;
    std::deque<Respuesta> guion;
    std::vector<std::string> pedidos;
    int simultaneos = 0, maxSimultaneos = 0;

    Falso() {
        srv.listen(QHostAddress::LocalHost, 0);
        connect(&srv, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* s = srv.nextPendingConnection()) {
                auto buf = std::make_shared<QByteArray>();
                connect(s, &QTcpSocket::readyRead, this, [this, s, buf]() {
                    *buf += s->readAll();
                    const int fin = buf->indexOf("\r\n\r\n");
                    if (fin < 0) return;
                    const QByteArray cab = buf->left(fin).toLower();
                    const int k = cab.indexOf("content-length:");
                    const int largo = k < 0 ? 0 : cab.mid(k + 15, cab.indexOf("\r\n", k) - k - 15).trimmed().toInt();
                    if (buf->size() < fin + 4 + largo) return;
                    pedidos.push_back(buf->mid(fin + 4, largo).toStdString());
                    buf->clear();
                    atender(s);
                });
            }
        });
    }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(srv.serverPort()) + "/v1"; }

   private:
    void atender(QTcpSocket* s) {
        Respuesta r = guion.empty() ? Respuesta{0, {kFin}} : guion.front();
        if (!guion.empty()) guion.pop_front();
        ++simultaneos;
        maxSimultaneos = std::max(maxSimultaneos, simultaneos);
        if (r.nunca) return; // la conexión queda abierta, sin contestar
        QTimer::singleShot(r.demoraMs, s, [this, s, r]() {
            s->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\n"
                     "Connection: close\r\n\r\n");
            mandar(s, r.pedazos, 0);
        });
    }
    void mandar(QTcpSocket* s, std::vector<std::string> pedazos, std::size_t i) {
        if (i == pedazos.size()) {
            s->write("0\r\n\r\n");
            s->flush();
            --simultaneos;
            s->disconnectFromHost();
            return;
        }
        const std::string& p = pedazos[i];
        char largo[32];
        std::snprintf(largo, sizeof(largo), "%zx\r\n", p.size());
        s->write(largo);
        s->write(p.data(), static_cast<qint64>(p.size()));
        s->write("\r\n");
        s->flush();
        // De a poco, como escribe un modelo.
        QTimer::singleShot(30, s, [this, s, pedazos = std::move(pedazos), i]() { mandar(s, pedazos, i + 1); });
    }
};

Totem totem() {
    Totem t;
    t.id = "hall";
    t.nombre = "Hall";
    t.foto = false;
    t.ficha = "Es en la sala A. Empieza a las 19.";
    Campo n;
    n.clave = "nombre";
    n.etiqueta = "Nombre";
    n.tipo = TipoCampo::Nombre;
    n.obligatorio = true;
    Campo c;
    c.clave = "correo";
    c.etiqueta = "Correo";
    c.tipo = TipoCampo::Correo;
    Campo f;
    f.clave = "telefono";
    f.etiqueta = "Teléfono";
    f.tipo = TipoCampo::Telefono;
    t.campos = {n, c, f};
    return t;
}

/// Lo que fue pasando en una conversación.
struct Visto {
    std::vector<std::string> frases;
    std::string texto;
    int listos = 0;
    Conversacion::Respuesta ultima;
    bool porGuion = false;
    std::string caida;
};

void enganchar(AgenteTotem& a, Visto& v) {
    a.alTexto = [&v](const std::string& t) { v.texto = t; };
    a.alFrase = [&v](const std::string& f) { v.frases.push_back(f); };
    a.alListo = [&v](const Conversacion::Respuesta& r, bool porGuion) {
        v.ultima = r;
        v.porGuion = porGuion;
        ++v.listos;
    };
    a.alCaer = [&v](const std::string& m) { v.caida = m; };
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Falso falso;
    FuenteMirada f;
    f.url = falso.url();
    f.modelo = "local";
    f.nombre = "modelo falso";
    AgenteTotem::Tiempos tiempos;
    tiempos.primeraMs = 2000;
    tiempos.turnoMs = 6000;
    tiempos.fallasMax = 2;

    Conversacion conv(totem(), "el Congreso");
    conv.empezar({});
    AgenteTotem a(f, "Sos el asistente de {evento}.\n{campos}\n{ficha}", conv, tiempos);
    Visto v;
    enganchar(a, v);

    // --- 1. Varios datos de una -----------------------------------------------------------
    falso.guion.push_back({0,
                           {sseHerramienta(0, "c1", "registro_poner", "{\"campo\":\"nom"),
                            sseHerramienta(0, "", "", "bre\",\"valor\":\"Jorge Ortega\"}"),
                            sseHerramienta(1, "c2", "registro_poner", "{\"campo\":\"correo\",\"valor\":\"jorge@gmail.com\"}"),
                            kFin}});
    CHECK(a.oido("soy Jorge Ortega y mi correo es jorge arroba gmail punto com"), "lo oído va al modelo");
    CHECK(a.ocupado(), "pensando");
    CHECK(esperar([&]() { return v.listos == 1; }), "terminó el turno");
    CHECK(!v.porGuion && contiene(v.ultima.decir, "Anoté nombre Jorge Ortega y correo jorge arroba gmail punto com"),
          "los dos datos, confirmados juntos por el guion");
    CHECK(v.frases.empty(), "el modelo no dijo nada (anotó y listo)");
    {
        const json p = json::parse(falso.pedidos.back());
        CHECK(p.value("stream", false) && p["tools"].size() == 2 && p.value("model", "") == "local", "el pedido: streaming y herramientas");
    }
    CHECK(!a.oido("sí, es correcto"), "el «sí» suelto lo contesta el guion, sin el modelo");
    auto r = conv.oido("sí, es correcto");
    CHECK(contiene(r.decir, "teléfono") || contiene(r.decir, "telefono") || contiene(conv.paso(), "Teléfono"),
          "el guion sigue con el teléfono");

    // --- 2. Una pregunta sobre el evento, frase por frase -----------------------------------
    falso.guion.push_back({0, {sseTexto("Es en la "), sseTexto("sala A. Empie"), sseTexto("za a las 19."), kFin}});
    CHECK(a.oido("¿dónde es la charla?"), "la pregunta va al modelo");
    CHECK(esperar([&]() { return !v.frases.empty(); }), "la primera frase sale mientras escribe");
    CHECK(v.frases[0] == "Es en la sala A." && v.listos == 1, "entera, antes de que termine el turno");
    CHECK(esperar([&]() { return v.listos == 2; }), "terminó");
    CHECK(v.frases.size() == 2 && v.frases[1] == "Empieza a las 19.", "la segunda frase");
    CHECK(v.texto == "Es en la sala A. Empieza a las 19.", "la pantalla, con el texto entero");
    CHECK(!v.porGuion && v.ultima.decir == conv.retomar(), "y el guion retoma la pregunta del teléfono");

    // --- 3. Un dato que no valida: el modelo repregunta -----------------------------------
    const std::size_t antes = falso.pedidos.size();
    falso.guion.push_back({0, {sseHerramienta(0, "t1", "registro_poner", "{\"campo\":\"telefono\",\"valor\":\"once\"}"), kFin}});
    falso.guion.push_back({0, {sseTexto("Perdón, ¿me repetís el teléfono?"), kFin}});
    v.frases.clear();
    CHECK(a.oido("mi teléfono es once"), "al modelo");
    CHECK(esperar([&]() { return v.listos == 3; }), "terminó");
    CHECK(falso.pedidos.size() == antes + 2, "dos vueltas");
    CHECK(contiene(falso.pedidos.back(), "\"role\":\"tool\"") && contiene(falso.pedidos.back(), "t1"),
          "la segunda lleva el resultado de la herramienta");
    CHECK(v.frases.size() == 1 && contiene(v.frases[0], "repetís el teléfono") && v.ultima.decir.empty(),
          "repregunta el modelo y el guion no agrega nada");
    CHECK(!conv.datos().contains("telefono"), "lo que no validó no se anotó");

    // --- 4. El servidor no contesta: el guion, y a la segunda el guion solo ----------------
    {
        Conversacion c2(totem(), "x");
        c2.empezar({});
        AgenteTotem::Tiempos rapido = tiempos;
        rapido.primeraMs = 300;
        AgenteTotem b(f, "{campos}", c2, rapido);
        Visto w;
        enganchar(b, w);
        falso.guion.push_back({0, {}, true});
        CHECK(b.oido("Jorge Ortega"), "al modelo");
        CHECK(esperar([&]() { return w.listos == 1; }, 3000), "pasado el tiempo, contesta igual");
        CHECK(w.porGuion && contiene(w.ultima.decir, "Anoté Jorge Ortega"), "contestó el guion, con su normalizador");
        CHECK(!b.caido() && w.caida.empty(), "una falla no alcanza para dejarlo");
        CHECK(!b.oido("sí, es correcto"), "el sí suelto, guion");
        c2.oido("sí, es correcto");
        falso.guion.push_back({0, {}, true});
        CHECK(b.oido("jorge punto ortega arroba gmail punto com"), "al modelo otra vez");
        CHECK(esperar([&]() { return w.listos == 2; }, 3000), "otra vez el tiempo");
        CHECK(w.porGuion && contiene(w.ultima.decir, "jorge punto ortega arroba gmail punto com"), "el guion entendió el correo");
        CHECK(b.caido() && !w.caida.empty(), "dos fallas seguidas: el guion solo");
        CHECK(!b.oido("once cuatro cinco seis siete ocho nueve uno dos tres"), "ya no va al modelo");
        CHECK(c2.datos().value("nombre", "") == "Jorge Ortega", "nada de lo anotado se perdió");
    }

    // --- 5. Dos conversaciones a la vez, un solo lugar ------------------------------------
    {
        ChatEnVivo::el().setParalelo(1);
        // Esperar a que se vayan las conexiones colgadas de arriba (el cliente las corta al
        // cancelar; el servidor falso cuenta las que contestó).
        falso.simultaneos = 0;
        falso.maxSimultaneos = 0;
        Conversacion c3(totem(), "x"), c4(totem(), "x");
        c3.empezar({});
        c4.empezar({});
        AgenteTotem p3(f, "{campos}", c3, tiempos), p4(f, "{campos}", c4, tiempos);
        Visto w3, w4;
        enganchar(p3, w3);
        enganchar(p4, w4);
        falso.guion.push_back({300, {sseHerramienta(0, "a", "registro_poner", "{\"campo\":\"nombre\",\"valor\":\"Ana Paz\"}"), kFin}});
        falso.guion.push_back({0, {sseHerramienta(0, "b", "registro_poner", "{\"campo\":\"nombre\",\"valor\":\"Luis Paz\"}"), kFin}});
        p3.oido("me llamo Ana Paz");
        p4.oido("me llamo Luis Paz");
        CHECK(ChatEnVivo::el().enCola(f.url) == 1, "la segunda espera en la cola");
        CHECK(esperar([&]() { return w3.listos == 1 && w4.listos == 1; }), "las dos terminan");
        CHECK(falso.maxSimultaneos == 1, "nunca dos pedidos juntos");
        CHECK(contiene(w3.ultima.decir, "Ana Paz") && contiene(w4.ultima.decir, "Luis Paz"), "cada una con lo suyo");
    }

    std::printf(g_fallos ? "\n%d fallos\n" : "\ntodo ok\n", g_fallos);
    return g_fallos ? 1 : 0;
}
