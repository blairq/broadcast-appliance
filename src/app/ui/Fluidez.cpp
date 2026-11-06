// ============================================================================
// src/app/ui/Fluidez.cpp — ver Fluidez.hpp
// ============================================================================
#include "ui/Fluidez.hpp"

#include "diag/RitmoCuadros.hpp"
#include "ui/ConferenceClient.hpp"
#include "ui/FrameService.hpp"
#include "ui/VideoItem.hpp"

#include <QJsonDocument>
#include <QJsonObject>

#include <chrono>
#include <cmath>

namespace ba::app {

using json = nlohmann::json;

namespace {

std::int64_t ahoraUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double r1(double v) { return std::round(v * 10) / 10; }

/// ¿A saltos? Con al menos 20 intervalos: más del kHuecosPct % de huecos o el p95 de más de 2 períodos.
bool vieneASaltos(int n, int huecos, double p95Ms, double esperadoMs) {
    if (n < 20 || esperadoMs <= 0) return false;
    return huecos * 100.0 / n > Fluidez::kHuecosPct || p95Ms > 2 * esperadoMs;
}

/// El ritmo de `r` contra `esperadoUs` (0: la mediana de la misma ventana). Vacío si hace
/// más de 2 s que no anota nada (no se mira).
json juzgar(const diag::RitmoCuadros& r, std::int64_t esperadoUs, std::int64_t ahora, bool& mal) {
    mal = false;
    if (r.ultimoUs() == 0 || ahora - r.ultimoUs() > 2'000'000) return nullptr;
    if (esperadoUs <= 0) esperadoUs = static_cast<std::int64_t>(r.resumen().p50Ms * 1000);
    const auto z = r.resumen(esperadoUs);
    if (z.n == 0) return nullptr;
    const double esperadoMs = static_cast<double>(esperadoUs) / 1000.0;
    mal = vieneASaltos(z.n, z.huecos, z.p95Ms, esperadoMs);
    return json{{"cps", r1(z.cps)},          {"esperado_cps", esperadoMs > 0 ? r1(1000.0 / esperadoMs) : 0.0},
                {"p50_ms", r1(z.p50Ms)},     {"p95_ms", r1(z.p95Ms)},
                {"max_ms", r1(z.maxMs)},     {"huecos", z.huecos},
                {"intervalos", z.n},         {"a_saltos", mal}};
}

} // namespace

Fluidez::Fluidez(FrameService& frames, ConferenceClient& sala, QObject* parent)
    : QObject(parent), frames_(frames), sala_(&sala) {}

bool Fluidez::vigilar(const std::string& clave, const std::string& nombre, bool mal) {
    Vigia& v = vigias_[clave];
    v.visto = true;
    if (mal) {
        ++v.malos;
        v.buenos = 0;
    } else {
        ++v.buenos;
        v.malos = 0;
    }
    if (!v.avisado && v.malos >= kSostenerS) {
        v.avisado = true;
        emit notify(tr("El video de «%1» se ve a saltos (llegan los cuadros desparejos). Ver video_fluidez.")
                        .arg(QString::fromStdString(nombre)),
                    "warn");
    } else if (v.avisado && v.buenos >= kSoltarS) {
        v.avisado = false;
    }
    return v.avisado;
}

void Fluidez::tic() {
    const std::int64_t ahora = ahoraUs();
    ++tics_;
    for (auto& [k, v] : vigias_) v.visto = false;
    int saltando = 0, videos = 0;
    json peor;
    double peorHuecos = -1;
    const auto anotarPeor = [&](const json& quien, const json& ritmo) {
        if (!ritmo.is_object()) return;
        const double h = ritmo.value("intervalos", 0) > 0 ? ritmo.value("huecos", 0) * 100.0 / ritmo.value("intervalos", 1) : 0;
        if (h > peorHuecos) {
            peorHuecos = h;
            peor = quien;
            peor["ritmo"] = ritmo;
        }
    };

    // Lo que llega a la interfaz, por clave.
    const auto ritmosFuente = frames_.ritmos();
    std::map<QString, double> p50Fuente;
    json fuentes = json::object();
    for (const auto& [clave, r] : ritmosFuente) {
        bool mal = false;
        json j = juzgar(*r, 0, ahora, mal);
        if (j.is_null()) continue;
        p50Fuente[clave] = j.value("p50_ms", 0.0);
        fuentes[clave.toStdString()] = std::move(j);
    }

    // Cada vista que se ve: lo que toma y lo que sube, contra el más lento entre su fuente y su tope.
    json vistas = json::array();
    for (const auto& v : VideoItem::ritmos()) {
        if (!v->activa.load(std::memory_order_relaxed)) continue;
        const int tope = v->topeCps.load(std::memory_order_relaxed);
        double esperadoMs = tope > 0 ? 1000.0 / tope : 0;
        if (const auto f = p50Fuente.find(v->fuente); f != p50Fuente.end()) esperadoMs = std::max(esperadoMs, f->second);
        bool malTomado = false, malSubido = false;
        json tomado = juzgar(v->tomado, static_cast<std::int64_t>(esperadoMs * 1000), ahora, malTomado);
        if (tomado.is_null()) continue;
        json subido = juzgar(v->subido, static_cast<std::int64_t>(esperadoMs * 1000), ahora, malSubido);
        // Cuánto avanza lo que se ve entre un cuadro y el siguiente (la hora de origen): el
        // que llega parejo pero salta 33, 100, 33 ms de video también se ve a saltos.
        bool malContenido = false;
        json contenido = juzgar(v->contenido, static_cast<std::int64_t>(esperadoMs * 1000), ahora, malContenido);
        const std::string fuente = v->fuente.toStdString();
        json x{{"fuente", fuente},
               {"nombre", frames_.label(v->fuente).toStdString()},
               {"lado", v->lado.load(std::memory_order_relaxed)},
               {"tope_cps", tope},
               {"tomados", tomado},
               {"dibujados", subido},
               {"contenido", contenido},
               {"salteados", v->salteados.load(std::memory_order_relaxed)}};
        ++videos;
        // Varias vistas de la misma fuente: un aviso por fuente.
        const bool mal = malTomado || malSubido || malContenido;
        x["a_saltos"] = vigilar("vista:" + fuente, x["nombre"].get<std::string>(), mal);
        if (x["a_saltos"].get<bool>()) ++saltando;
        anotarPeor({{"vista", fuente}}, subido.is_object() ? subido : tomado);
        vistas.push_back(std::move(x));
    }

    // Lo que dibuja la ventana.
    json ventanas = json::array();
    for (const auto& w : VideoItem::ritmosVentanas()) {
        bool mal = false;
        json j = juzgar(w->swaps, 0, ahora, mal);
        if (j.is_null()) continue;
        j["titulo"] = w->titulo.toStdString();
        ventanas.push_back(std::move(j));
    }

    // La grilla de personas: lo del gateway por celda (cada 3 s; la respuesta llega después).
    if (sala_ && !pidiendo_ && tics_ % 3 == 0) {
        pidiendo_ = true;
        sala_->get("/api/v1/grillas", [this, yo = QPointer<Fluidez>(this)](bool ok, const QJsonObject& o) {
            if (!yo) return;
            pidiendo_ = false;
            if (!ok) return;
            const json g = json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
            grilla_ = json::array();
            for (const auto& x : g.value("grillas", json::array())) {
                json celdas = json::array();
                for (const auto& c : x.value("celdas", json::array()))
                    celdas.push_back({{"persona", c.value("persona", std::string())},
                                      {"capa", c.value("capa", std::string())},
                                      {"ritmo", c.value("ritmo", json::object())}});
                grilla_.push_back({{"canal", x.value("canal", 0)},
                                   {"publicado", x.value("ritmo_publicado", json::object())},
                                   {"celdas", std::move(celdas)}});
            }
        });
    }
    for (auto& g : grilla_)
        for (auto& c : g["celdas"]) {
            const json& p = c["ritmo"].value("pintado", json::object());
            if (!p.is_object() || p.empty()) continue;
            const std::string capa = c.value("capa", std::string());
            const double esperadoMs = capa == "baja" ? 1000.0 / 15 : 1000.0 / 30;
            const bool mal = vieneASaltos(p.value("n", 0), p.value("huecos", 0), p.value("p95_ms", 0.0), esperadoMs);
            ++videos;
            const std::string persona = c.value("persona", std::string());
            c["a_saltos"] = vigilar("celda:" + persona, persona, mal);
            if (c["a_saltos"].get<bool>()) ++saltando;
            json pr = p;
            pr["intervalos"] = p.value("n", 0);
            anotarPeor({{"celda", persona}, {"capa", capa}}, pr);
        }

    // Los que ya no están se olvidan (si vuelven, de cero).
    for (auto it = vigias_.begin(); it != vigias_.end();) it = it->second.visto ? std::next(it) : vigias_.erase(it);

    estado_ = json{{"videos", videos},
                   {"a_saltos", saltando},
                   {"peor", peor},
                   {"criterio", {{"huecos_pct", kHuecosPct}, {"sostener_s", kSostenerS}, {"soltar_s", kSoltarS}}},
                   {"ventanas", ventanas},
                   {"fuentes", fuentes},
                   {"vistas", vistas},
                   {"grilla", grilla_}};
    estadoQml_ = QJsonDocument::fromJson(QByteArray::fromStdString(estado_.dump(-1, ' ', false, json::error_handler_t::replace)))
                     .object()
                     .toVariantMap();
    aSaltos_ = saltando;
    emit cambio();
}

} // namespace ba::app

#include "moc_Fluidez.cpp"
