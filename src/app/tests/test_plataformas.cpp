// ============================================================================
// src/app/tests/test_plataformas.cpp — La fábrica de las plataformas (mcp/McpPlataformas)
//
// Con tres plataformas falsas (PlataformaViewModel sin red) y un McpCore de verdad:
//   - registra *_status, *_setup, *_login, *_logout y *_videos de las tres, con su
//     categoría, clase y marcas (borrar con clear, mundo abierto en el login);
//   - cada herramienta pasa por la plataforma (estado, guardar, conectar, desconectar,
//     videos) y el guardado por la puerta del formulario (guardarCredencial);
//   - lo que falta para usarla (app, cuenta, Página) y el paso siguiente (accounts_status);
//   - los errores llevan el nombre de la plataforma UNA vez (Twitch los traía dos);
//   - el login con código espera la señal codigoListo (no sondea);
//   - lo que no viene en los argumentos vale el `default` del esquema (el puerto de Meta);
//   - ultimosMensajes y conNombre.
// ============================================================================

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QTimer>

#include <cstdio>
#include <optional>
#include <string>

#include "mcp/McpCore.hpp"
#include "mcp/McpPlataformas.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/CapturaError.hpp"
#include "ui/PlataformaViewModel.hpp"

using namespace ba::app;
using namespace ba::app::mcp;

// Las conversiones de McpService.cpp (que arrastra la app entera): las mismas, por JSON.
namespace ba::app::mcp {
json toJson(const QJsonObject& o) { return json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString()); }
json toJson(const QJsonArray& a) { return json::parse(QJsonDocument(a).toJson(QJsonDocument::Compact).toStdString()); }
QJsonObject toQt(const json& j) {
    return j.is_object() ? QJsonDocument::fromJson(QByteArray::fromStdString(j.dump())).object() : QJsonObject();
}
} // namespace ba::app::mcp

namespace {

int g_fallas = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallas;                                                                                      \
        }                                                                                                    \
    } while (0)

/// Una plataforma sin red: lo que se le pide queda en `llamadas`.
class Falsa : public PlataformaViewModel {
   public:
    Falsa(QString id, QString nombre) : id_(std::move(id)), nombre_(std::move(nombre)) {}
    QString plataforma() const override { return id_; }
    QString nombre() const override { return nombre_; }
    bool configured() const override { return configurada; }
    bool loggedIn() const override { return conectada; }
    bool loginPending() const override { return pendiente; }
    QString cuentaNombre() const override { return conectada ? QStringLiteral("Canal de prueba") : QString(); }
    Falta falta() const override {
        const Falta f = PlataformaViewModel::falta();
        return f == Falta::Nada && pidePagina ? Falta::Pagina : f;
    }
    QJsonObject estado() const override { return {{"plataforma", id_}, {"configured", configurada}}; }
    QStringList permisos() const override { return {"leer", "transmitir"}; }
    bool guardarCredencial(const QVariantMap&) override { return true; }
    void clearClient() override { llamadas << "clear"; }
    using PlataformaViewModel::login;
    void login(Done listo) override {
        llamadas << "login";
        if (conCodigo) {
            QTimer::singleShot(20, this, [this]() {
                codigo_ = "WXYZ-1234";
                emit codigoListo();
            });
            return;
        }
        if (listo) listo(true, {{"title", "Canal de prueba"}}, {});
    }
    bool loginConCodigo() const override { return conCodigo; }
    QString codigo() const override { return codigo_; }
    QString codigoUri() const override { return conCodigo ? QStringLiteral("https://ejemplo/activar") : QString(); }
    QJsonObject alConectar() const override { return extraAlConectar; }
    void cancelLogin() override { llamadas << "cancelar"; }
    void logout() override { llamadas << "logout"; }
    void refresh() override { llamadas << "refresh"; }
    void listarVideos(int max, Done done) override {
        llamadas << QString("videos %1").arg(max);
        done(errorVideos.isEmpty(), errorVideos.isEmpty() ? QJsonObject{{"items", QJsonArray{"v1"}}} : QJsonObject{},
             errorVideos);
    }
    void terminarVivo(const QString& id, Done done) override {
        llamadas << "terminar " + id;
        done(true, {}, {});
    }
    const QJsonArray& chatMessages() const override { return chat; }
    /// Una acción que falla (como saveClient con un ID malo).
    void fallar(const QString& e) { avisarError(e); }

    bool configurada = true, conectada = true, pendiente = false, conCodigo = false, pidePagina = false;
    QString errorVideos;
    QJsonObject extraAlConectar;
    QJsonArray chat;
    QStringList llamadas;

   private:
    QString id_, nombre_, codigo_;
};

/// Corre una herramienta y gira el event loop hasta la respuesta.
std::optional<ToolResult> correr(McpCore& core, const std::string& nombre, const json& args = json::object()) {
    const Tool* t = core.findTool(nombre);
    if (t == nullptr) return std::nullopt;
    std::optional<ToolResult> out;
    t->run(args, [&out](ToolResult r) { out = std::move(r); });
    for (int i = 0; i < 400 && !out; ++i) {
        QEventLoop loop;
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return out;
}

std::string texto(const std::optional<ToolResult>& r) {
    std::string t;
    if (r)
        for (const auto& c : r->content)
            if (c.value("type", "") == "text") t += c.value("text", "");
    return t;
}

bool contiene(const std::string& s, const std::string& parte) { return s.find(parte) != std::string::npos; }

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::printf("== test_plataformas ==\n");

    Falsa yt("youtube", "YouTube"), tw("twitch", "Twitch"), fb("facebook", "Facebook");
    QString guardadaPlataforma;
    QVariantMap guardados;
    QString errorAlGuardar;
    int lecturasDeDescargas = 0;
    McpCore core;
    const size_t propias = core.tools().size(); // las del núcleo (help…)
    registrarPlataformas(core, Plataformas{yt, tw, fb,
                                           [&](const QString& p, const QVariantMap& campos) {
                                               guardadaPlataforma = p;
                                               guardados = campos;
                                               return errorAlGuardar;
                                           },
                                           [&](auto listo) {
                                               ++lecturasDeDescargas;
                                               QTimer::singleShot(5, [listo]() {
                                                   listo("/descargas/client_secret_123.json", "{\"installed\":{}}", {});
                                               });
                                           }});

    // --- El registro: 5 de cada una, con su categoría y sus marcas --------------------------
    for (const char* p : {"youtube", "twitch", "facebook"}) {
        for (const char* q : {"_status", "_setup", "_login", "_logout", "_videos"}) {
            const std::string n = std::string(p) + q;
            const Tool* t = core.findTool(n);
            CHECK(t != nullptr, ("registrada: " + n).c_str());
            if (t == nullptr) continue;
            CHECK(t->category == p, ("categoría: " + n).c_str());
            CHECK(!t->title.empty() && !t->description.empty(), ("título y descripción: " + n).c_str());
        }
        CHECK(core.findTool(std::string(p) + "_status")->clase == Clase::Lectura, "status es lectura");
        CHECK(core.findTool(std::string(p) + "_videos")->clase == Clase::Lectura, "videos es lectura");
        CHECK(core.findTool(std::string(p) + "_setup")->destructivaSi.clave == std::string("clear"), "setup borra con clear");
        CHECK(core.findTool(std::string(p) + "_login")->openWorld, "login abre el navegador");
        CHECK(core.findTool(std::string(p) + "_logout")->destructive, "logout es destructiva");
    }
    CHECK(core.tools().size() == propias + 15, "15 herramientas, ni una más");
    CHECK(core.findTool("youtube_videos")->inputSchema["properties"]["max"]["maximum"] == 50, "YouTube: hasta 50");
    CHECK(core.findTool("twitch_videos")->inputSchema["properties"]["max"]["maximum"] == 100, "Twitch: hasta 100");

    // --- *_status: el estado de la plataforma, tal cual ------------------------------------
    {
        const auto r = correr(core, "twitch_status");
        CHECK(r && !r->isError && r->structured.value("plataforma", "") == "twitch", "twitch_status = estado()");
    }

    // --- *_videos: pasa por la plataforma, con el máximo de fábrica --------------------------
    {
        const auto r = correr(core, "youtube_videos");
        CHECK(r && !r->isError && yt.llamadas.contains("videos 25"), "youtube_videos: max 25 de fábrica");
        correr(core, "facebook_videos", {{"max", 7}});
        CHECK(fb.llamadas.contains("videos 7"), "facebook_videos: el max pedido");
    }
    // Los errores: el nombre de la plataforma una sola vez.
    {
        tw.errorVideos = "Twitch: invalid client";
        auto r = correr(core, "twitch_videos");
        CHECK(r && r->isError && texto(r) == "Twitch: invalid client", "Twitch: sin el nombre dos veces");
        yt.errorVideos = "se agoto la cuota";
        r = correr(core, "youtube_videos");
        CHECK(r && r->isError && texto(r) == "YouTube: se agoto la cuota", "YouTube: con su nombre adelante");
        tw.errorVideos.clear();
        yt.errorVideos.clear();
    }
    // Lo que falta para usarla.
    {
        yt.configurada = false;
        auto r = correr(core, "youtube_videos");
        CHECK(r && r->isError && contiene(texto(r), "youtube_setup"), "sin credencial: youtube_setup");
        yt.configurada = true;
        tw.conectada = false;
        r = correr(core, "twitch_videos");
        CHECK(r && r->isError && contiene(texto(r), "twitch_login"), "sin cuenta: twitch_login");
        tw.conectada = true;
        fb.pidePagina = true;
        const int antes = static_cast<int>(fb.llamadas.size());
        r = correr(core, "facebook_videos");
        CHECK(r && r->isError && contiene(texto(r), "facebook_pages"), "sin Página: facebook_pages");
        CHECK(fb.llamadas.size() == antes, "sin Página no le pide nada a la plataforma");
        fb.pidePagina = false;
    }

    // --- accounts_status: el estado de la cuenta y el paso siguiente --------------------------
    {
        json s = estadoDeCuenta(yt);
        CHECK(s.value("account", "") == "Canal de prueba" && s.value("loggedIn", false), "estadoDeCuenta: la cuenta");
        CHECK(contiene(s.value("next", ""), "account_check"), "lista: account_check");
        tw.pendiente = true;
        tw.conectada = false;
        s = estadoDeCuenta(tw);
        CHECK(contiene(s.value("next", ""), "userCode"), "login en curso (Twitch): el código");
        tw.pendiente = false;
        s = estadoDeCuenta(tw);
        CHECK(contiene(s.value("next", ""), "twitch_login"), "sin cuenta: twitch_login");
        tw.conectada = true;
        fb.pidePagina = true;
        s = estadoDeCuenta(fb);
        CHECK(contiene(s.value("next", ""), "facebook_pages"), "sin Página: facebook_pages");
        fb.pidePagina = false;
        fb.configurada = false;
        s = estadoDeCuenta(fb);
        CHECK(contiene(s.value("next", ""), "account_credentials_request"), "sin app: el formulario");
        fb.configurada = true;
    }

    // --- *_setup: por la puerta del formulario ------------------------------------------------
    {
        auto r = correr(core, "twitch_setup", {{"clear", true}});
        CHECK(r && !r->isError && tw.llamadas.contains("clear") && texto(r) == "App de Twitch borrada.", "clear borra");
        r = correr(core, "facebook_setup", {{"appId", "123"}, {"appSecret", "s"}});
        CHECK(r && !r->isError && guardadaPlataforma == "facebook", "facebook_setup guarda por la puerta común");
        CHECK(guardados.value("port").toInt() == 8791 && guardados.value("appId").toString() == "123",
              "el puerto de fábrica sale del esquema");
        CHECK(!guardados.contains("clear"), "clear no se guarda");
        errorAlGuardar = "ID invalido";
        r = correr(core, "twitch_setup", {{"clientId", "x"}});
        CHECK(r && r->isError && texto(r) == "ID invalido", "el error de guardar, tal cual");
        errorAlGuardar.clear();
        r = correr(core, "youtube_setup", {{"fromDownloads", true}});
        CHECK(lecturasDeDescargas == 1, "fromDownloads lee afuera de la interfaz");
        CHECK(r && !r->isError && guardados.value("clientJson").toString() == "{\"installed\":{}}",
              "y guarda el contenido con la misma puerta");
        CHECK(contiene(texto(r), "client_secret_123.json") && contiene(texto(r), "youtube_login"),
              "la respuesta nombra el archivo");
    }

    // --- *_login -------------------------------------------------------------------------------
    {
        auto r = correr(core, "youtube_login", {{"cancel", true}});
        CHECK(r && yt.llamadas.contains("cancelar"), "cancel cancela");
        r = correr(core, "youtube_login");
        CHECK(r && !r->isError && contiene(texto(r), "navegador") && yt.llamadas.contains("login"),
              "sin wait: abre el navegador y vuelve");
        fb.extraAlConectar = {{"pages", QJsonArray{"Página 1"}}};
        r = correr(core, "facebook_login", {{"wait", true}});
        CHECK(r && r->structured.contains("connected") && r->structured["pages"].size() == 1,
              "wait: la cuenta y lo que suma la plataforma (las Páginas)");
        tw.conCodigo = true;
        r = correr(core, "twitch_login");
        CHECK(r && !r->isError && r->structured.value("userCode", "") == "WXYZ-1234",
              "con código: espera codigoListo y lo devuelve");
        CHECK(contiene(texto(r), "WXYZ-1234") && contiene(texto(r), "https://ejemplo/activar"), "el texto con el código");
        tw.configurada = false;
        r = correr(core, "twitch_login");
        CHECK(r && r->isError && contiene(texto(r), "twitch_setup"), "sin app: primero setup");
        tw.configurada = true;
    }

    // --- *_logout ------------------------------------------------------------------------------
    {
        const auto r = correr(core, "facebook_logout");
        CHECK(r && fb.llamadas.contains("logout") && texto(r) == "Cuenta de Facebook desconectada.", "logout");
    }

    // --- Lo común de la plataforma ---------------------------------------------------------
    {
        yt.chat = QJsonArray{"a", "b", "c"};
        CHECK(yt.ultimosMensajes(2) == (QJsonArray{"b", "c"}), "los últimos 2");
        CHECK(yt.ultimosMensajes(10).size() == 3 && yt.ultimosMensajes(0).isEmpty(), "ni más ni menos");
        CHECK(tw.conNombre("Twitch: x") == "Twitch: x" && tw.conNombre("x") == "Twitch: x", "conNombre una vez");
        // El contrato de errores (ui/CapturaError): quien llama desde C++ se lo queda; si no, la vista.
        QString aviso;
        QObject::connect(&tw, &PlataformaViewModel::notify, [&aviso](const QString& m, const QString&) { aviso = m; });
        CHECK(errorDe([&] { tw.fallar("ID malo"); }) == "ID malo" && tw.lastError().isEmpty() && aviso.isEmpty(),
              "capturado: ni lastError ni aviso");
        tw.fallar("ID malo");
        CHECK(tw.lastError() == "ID malo" && aviso == "Twitch: ID malo", "sin captura: a la vista, con el nombre");
    }

    std::printf(g_fallas == 0 ? "OK\n" : "%d FALLAS\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
