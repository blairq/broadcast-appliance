// SPDX-License-Identifier: GPL-2.0-or-later
#include "legislatura/OrdenDelDia.hpp"

#include "control/BusEventos.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace ba::app::legislatura {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
std::int64_t ahoraMs() { return QDateTime::currentMSecsSinceEpoch(); }

QVariant aQv(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump())).object().value("v").toVariant();
}

std::string recortar(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\"");
    const auto b = s.find_last_not_of(" \t\r\"");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::string mmss(std::int64_t seg) {
    char b[32];
    if (seg >= 3600)
        std::snprintf(b, sizeof b, "%lld:%02lld:%02lld", static_cast<long long>(seg / 3600),
                      static_cast<long long>(seg / 60 % 60), static_cast<long long>(seg % 60));
    else
        std::snprintf(b, sizeof b, "%lld:%02lld", static_cast<long long>(seg / 60), static_cast<long long>(seg % 60));
    return b;
}

const std::set<std::string> kEstados{"pendiente", "en_tratamiento", "votado", "comision", "retirado"};
} // namespace

OrdenDelDia::OrdenDelDia(const fs::path& datos, control::BusEventos& bus, QObject* parent)
    : QObject(parent), archivo_(datos / "Config" / "orden-del-dia.json"), bus_(bus) {
    std::ifstream f(archivo_);
    const json j = json::parse(f, nullptr, false);
    if (j.is_object()) {
        puntos_ = j.value("puntos", json::array());
        tramos_ = j.value("tramos", json::array());
    }
    // Cada cambio del orador presencial al aire cierra el tramo del anterior.
    sub_ = bus_.suscribir("oradores/al_aire", [this](const std::string&, const json& v) {
        const std::int64_t ahora = ahoraMs();
        const std::string nombre = v.is_object() ? v.value("nombre", std::string()) : std::string();
        if (enCurso_.is_object() && enCurso_.value("nombre", std::string()) == nombre) return;
        cerrarTramo(ahora);
        if (!nombre.empty()) {
            const json a = actual();
            enCurso_ = {{"nombre", nombre},
                        {"banca", v.value("banca", std::string())},
                        {"desde_ms", ahora},
                        {"punto", a.is_object() && !a.empty() ? a.value("n", 0) : 0}};
        }
    });
}

void OrdenDelDia::cerrarTramo(std::int64_t ahora) {
    if (!enCurso_.is_object()) return;
    json t = enCurso_;
    t["hasta_ms"] = ahora;
    if (ahora - t.value("desde_ms", ahora) >= 1000) tramos_.push_back(t);
    enCurso_ = nullptr;
    guardar();
}

void OrdenDelDia::guardar() const {
    std::error_code ec;
    fs::create_directories(archivo_.parent_path(), ec);
    std::ofstream o(archivo_);
    o << json{{"puntos", puntos_}, {"tramos", tramos_}}.dump(1);
}

void OrdenDelDia::publicar() {
    guardar();
    bus_.publicar("sesion/punto", actual());
    emit cambio();
}

std::string OrdenDelDia::cargar(const std::string& texto, int* cuantos) {
    json nuevos = json::array();
    std::istringstream is(texto);
    std::string l;
    // «3. Expte. 123/24 – Título», «3) Título», «3;123/24;Título», «Título» (numerado solo).
    // std::regex trabaja por BYTES: los guiones largos y «Nº» (UTF-8 de varios
    // bytes) se normalizan antes, y las expresiones quedan en ASCII.
    static const std::regex numerado(R"(^\s*(\d+)\s*[\.\)\-:]\s*(.*)$)");
    static const std::regex expte(R"(^(?:expte\.?|expediente|exp\.?)\s*(?:n\.?\s*)?([\w/\.-]+)\s*[\-:,]?\s*(.*)$)",
                                  std::regex::icase);
    const auto normalizar = [](std::string x) {
        for (const char* r : {"\xE2\x80\x93", "\xE2\x80\x94"}) // – —
            for (auto p = x.find(r); p != std::string::npos; p = x.find(r)) x.replace(p, 3, "-");
        for (const char* r : {"N\xC2\xBA", "N\xC2\xB0", "n\xC2\xBA", "n\xC2\xB0"}) // Nº N°
            for (auto p = x.find(r); p != std::string::npos; p = x.find(r)) x.replace(p, 3, "N");
        return x;
    };
    int auto_n = 0;
    while (std::getline(is, l)) {
        l = recortar(normalizar(l));
        if (l.empty()) continue;
        int n = 0;
        std::string expediente, titulo;
        if (l.find(';') != std::string::npos) {
            std::vector<std::string> c;
            std::string x;
            std::istringstream cs(l);
            while (std::getline(cs, x, ';')) c.push_back(recortar(x));
            if (!c.empty() && std::all_of(c[0].begin(), c[0].end(), ::isdigit) && !c[0].empty()) {
                n = std::stoi(c[0]);
                expediente = c.size() > 2 ? c[1] : "";
                titulo = c.size() > 2 ? c[2] : (c.size() > 1 ? c[1] : "");
            } else {
                // Encabezado de un CSV («n;expediente;titulo»): se saltea.
                continue;
            }
        } else {
            std::smatch m;
            std::string resto = l;
            if (std::regex_match(l, m, numerado)) {
                n = std::stoi(m[1].str());
                resto = m[2].str();
            }
            std::smatch e;
            if (std::regex_match(resto, e, expte)) {
                expediente = e[1].str();
                titulo = recortar(e[2].str());
            } else {
                titulo = resto;
            }
        }
        if (titulo.empty()) continue;
        if (n == 0) n = auto_n + 1;
        auto_n = n;
        nuevos.push_back({{"n", n}, {"expediente", expediente}, {"titulo", titulo}, {"estado", "pendiente"},
                          {"inicio_ms", 0}, {"fin_ms", 0}, {"resultado", ""}});
    }
    if (nuevos.empty()) return "no encontré puntos en el texto (una línea por punto: «1. Expte. 123/24 – Título»)";
    puntos_ = nuevos;
    if (cuantos != nullptr) *cuantos = static_cast<int>(nuevos.size());
    publicar();
    return {};
}

json OrdenDelDia::actual() const {
    for (const auto& p : puntos_)
        if (p.value("estado", std::string()) == "en_tratamiento") return p;
    return json::object();
}

std::string OrdenDelDia::punto(int n, const std::string& estado, const std::string& resultado) {
    if (!kEstados.count(estado)) return "estado: pendiente, en_tratamiento, votado, comision o retirado";
    json* p = nullptr;
    for (auto& x : puntos_)
        if (x.value("n", 0) == n) p = &x;
    if (p == nullptr) return "no hay un punto " + std::to_string(n);
    const std::int64_t ahora = ahoraMs();
    if (estado == "en_tratamiento") {
        // Uno solo a la vez: el que estaba en tratamiento pasa a «votado» si nadie dijo otra cosa.
        for (auto& x : puntos_)
            if (&x != p && x.value("estado", std::string()) == "en_tratamiento") {
                x["estado"] = "votado";
                x["fin_ms"] = ahora;
            }
        if (p->value("inicio_ms", 0LL) == 0) (*p)["inicio_ms"] = ahora;
        // El orador que está hablando sigue, pero desde acá su tiempo cuenta para este punto.
        if (enCurso_.is_object()) {
            json sigue = enCurso_;
            cerrarTramo(ahora);
            sigue["desde_ms"] = ahora;
            sigue["punto"] = n;
            enCurso_ = sigue;
        }
    } else if (p->value("estado", std::string()) == "en_tratamiento") {
        (*p)["fin_ms"] = ahora;
    }
    (*p)["estado"] = estado;
    if (!resultado.empty()) (*p)["resultado"] = resultado;
    publicar();
    return {};
}

std::string OrdenDelDia::siguiente() {
    for (const auto& p : puntos_)
        if (p.value("estado", std::string()) == "pendiente") return punto(p.value("n", 0), "en_tratamiento", "");
    return "no quedan puntos pendientes";
}

json OrdenDelDia::tiemposPalabra(const json& remotos) const {
    std::map<std::string, json> por;
    const auto sumar = [&](const std::string& nombre, const std::string& banca, std::int64_t seg, int punto) {
        json& o = por[nombre];
        if (o.is_null()) o = {{"nombre", nombre}, {"banca", banca}, {"segundos", 0}, {"por_punto", json::object()}};
        o["segundos"] = o.value("segundos", 0LL) + seg;
        if (punto > 0) {
            const std::string k = std::to_string(punto);
            o["por_punto"][k] = o["por_punto"].value(k, 0LL) + seg;
        }
    };
    json todos = tramos_;
    if (enCurso_.is_object()) {
        json t = enCurso_;
        t["hasta_ms"] = ahoraMs();
        todos.push_back(t);
    }
    for (const auto& t : todos)
        sumar(t.value("nombre", std::string()), t.value("banca", std::string()),
              (t.value("hasta_ms", 0LL) - t.value("desde_ms", 0LL)) / 1000, t.value("punto", 0));
    if (remotos.is_array())
        for (const auto& r : remotos)
            if (r.value("segundos", 0LL) > 0) sumar(r.value("nombre", std::string()), "", r.value("segundos", 0LL), 0);
    json out = json::array();
    for (auto& [k, v] : por) out.push_back(v);
    std::sort(out.begin(), out.end(), [](const json& a, const json& b) { return a.value("segundos", 0) > b.value("segundos", 0); });
    return out;
}

json OrdenDelDia::capitulos(std::int64_t recInicioMs) const {
    json out = json::array();
    for (const auto& p : puntos_) {
        const std::int64_t ini = p.value("inicio_ms", 0LL);
        if (ini <= 0 || ini < recInicioMs) continue;
        const std::int64_t seg = (ini - recInicioMs) / 1000;
        out.push_back({{"seg", seg}, {"marca", mmss(seg)},
                       {"titulo", "Punto " + std::to_string(p.value("n", 0)) + ": " + p.value("titulo", std::string())}});
    }
    return out;
}

std::string OrdenDelDia::anexoActa(const json& remotos) const {
    std::string t;
    if (!puntos_.empty()) {
        t += "## Orden del día\n\n| N.º | Expediente | Punto | Estado | Tratamiento | Resultado |\n|---|---|---|---|---|---|\n";
        for (const auto& p : puntos_) {
            std::string dur = "—";
            const std::int64_t ini = p.value("inicio_ms", 0LL), fin = p.value("fin_ms", 0LL);
            if (ini > 0) {
                const std::int64_t hasta = fin > 0 ? fin : ahoraMs();
                dur = QDateTime::fromMSecsSinceEpoch(ini).toString("HH:mm").toStdString() + " (" + mmss((hasta - ini) / 1000) + ")";
            }
            std::string est = p.value("estado", std::string());
            if (est == "en_tratamiento") est = "en tratamiento";
            if (est == "comision") est = "a comisión";
            t += "| " + std::to_string(p.value("n", 0)) + " | " + p.value("expediente", std::string()) + " | " +
                 p.value("titulo", std::string()) + " | " + est + " | " + dur + " | " + p.value("resultado", std::string()) + " |\n";
        }
        t += "\n";
    }
    const json tp = tiemposPalabra(remotos);
    if (!tp.empty()) {
        t += "## Uso de la palabra\n\n| Orador | Banca | Tiempo | Por punto |\n|---|---|---|---|\n";
        for (const auto& o : tp) {
            std::string pp;
            const json porPunto = o.value("por_punto", json::object()); // en una variable: items() no alarga el temporal
            for (const auto& [k, v] : porPunto.items())
                pp += (pp.empty() ? "" : ", ") + std::string("P") + k + " " + mmss(v.get<std::int64_t>());
            t += "| " + o.value("nombre", std::string()) + " | " + o.value("banca", std::string()) + " | " +
                 mmss(o.value("segundos", 0LL)) + " | " + pp + " |\n";
        }
        t += "\n";
    }
    return t;
}

void OrdenDelDia::limpiar() {
    puntos_ = json::array();
    tramos_ = json::array();
    enCurso_ = nullptr;
    publicar();
}

QVariantList OrdenDelDia::puntosUi() const { return aQv(puntos_).toList(); }
QVariantMap OrdenDelDia::actualUi() const { return aQv(actual()).toMap(); }
QString OrdenDelDia::cargarUi(const QString& texto) {
    int n = 0;
    const std::string e = cargar(texto.toStdString(), &n);
    return QString::fromStdString(e);
}
QString OrdenDelDia::puntoUi(int n, const QString& estado, const QString& resultado) {
    return QString::fromStdString(punto(n, estado.toStdString(), resultado.toStdString()));
}

} // namespace ba::app::legislatura
