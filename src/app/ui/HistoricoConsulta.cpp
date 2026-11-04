// ============================================================================
// src/app/ui/HistoricoConsulta.cpp — ver HistoricoConsulta.hpp
// ============================================================================
#include "ui/HistoricoConsulta.hpp"

#include <QDateTime>

#include <algorithm>

#include "evento/Bitacora.hpp"
#include "evento/CierreEvento.hpp"
#include "evento/EventoStore.hpp"
#include "evento/FotosConfig.hpp"
#include "evento/Historico.hpp"
#include "i18n/I18nTexto.hpp"
#include "platform/Paths.hpp"

namespace ba::app {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

fs::path deU8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string aU8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string hora(std::int64_t ms) {
    return ms == 0 ? std::string() : QDateTime::fromMSecsSinceEpoch(ms).toString("dd/MM/yyyy HH:mm:ss").toStdString();
}

/// Un nombre de configuración que no se sale de su carpeta.
bool nombreValido(const std::string& n) {
    return !n.empty() && n != "." && n != ".." && n.find_first_of("/\\") == std::string::npos;
}

} // namespace

HistoricoConsulta::HistoricoConsulta(fs::path configuraciones, std::string activa, fs::path eventos, fs::path config)
    : configuraciones_(std::move(configuraciones)), eventos_(std::move(eventos)), config_(std::move(config)),
      activa_(std::move(activa)) {}

HistoricoConsulta HistoricoConsulta::deLaApp(const fs::path& eventos, const fs::path& config) {
    const fs::path datos = ba::platform::dataRoot();
    return HistoricoConsulta(datos / "Configuraciones", ba::platform::configActiva(datos), eventos, config);
}

HistoricoConsulta::Lugar HistoricoConsulta::lugar(const std::string& carpeta) const {
    if (carpeta.rfind('@', 0) == 0) {
        const auto barra = carpeta.find('/', 1);
        const std::string ev = carpeta.substr(1, barra == std::string::npos ? std::string::npos : barra - 1);
        // La abierta nombrada con «@» es la abierta (con sus raíces de siempre).
        if (ev == activa_) return {eventos_, config_, barra == std::string::npos ? std::string() : carpeta.substr(barra + 1), ev, true};
        if (!nombreValido(ev)) return {{}, {}, {}, ev, false};
        const fs::path base = configuraciones_ / deU8(ev);
        return {base / "Eventos", base / "Config", barra == std::string::npos ? std::string() : carpeta.substr(barra + 1), ev, false};
    }
    return {eventos_, config_, carpeta, activa_, true};
}

fs::path HistoricoConsulta::carpeta(const std::string& carpeta) const {
    const Lugar l = lugar(carpeta);
    if (l.raiz.empty()) return {};
    return evento::EventoStore(l.raiz).carpetaExistente(l.rel);
}

std::vector<std::pair<std::string, fs::path>> HistoricoConsulta::otras() const {
    std::vector<std::pair<std::string, fs::path>> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(configuraciones_, ec)) {
        const std::string n = aU8(e.path().filename());
        if (!e.is_directory(ec) || n.empty() || n[0] == '.' || n == activa_) continue;
        out.emplace_back(n, e.path() / "Eventos");
    }
    return out;
}

std::string HistoricoConsulta::noHay(const std::string& carpeta) {
    return carpeta.empty() ? ba::i18n::traducir("mcp", "no hay un evento en curso") : ba::i18n::llenar(ba::i18n::traducir("mcp", "no hay un evento «%1»"), {carpeta});
}

json HistoricoConsulta::resumen(const std::string& carpeta, bool conDetalle) const {
    const fs::path dir = this->carpeta(carpeta);
    if (dir.empty()) return {{"error", noHay(carpeta)}};
    json m = evento::Historico::resumen(dir);
    const Lugar lu = lugar(carpeta);
    const evento::DatosDelEvento d = evento::EventoStore(lu.raiz).datos(lu.rel);
    m["evento"] = lu.evento;
    m["abierto"] = lu.abierto;
    m["carpeta"] = carpeta;
    m["ruta"] = dir.string();
    m["estado"] = d.estado;
    m["desde_ms"] = d.iniciadoMs;
    m["hasta_ms"] = d.terminadoMs;
    m["desde"] = hora(d.iniciadoMs);
    m["hasta"] = hora(d.terminadoMs);
    m["lugar"] = d.lugar;
    m["notas"] = d.notas;
    m["automatico"] = d.automatico;
    m["etiquetas"] = d.etiquetas;
    if (!conDetalle) return m;
    // El acta, el informe y los capítulos que genera la página Evento al cerrar.
    json docs = json::array();
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string n = e.path().filename().string();
        const bool esDoc = (n.rfind("acta-", 0) == 0 && e.path().extension() == ".md") || n.rfind("informe-", 0) == 0 ||
                           n == "capitulos.txt";
        if (e.is_regular_file(ec) && esDoc) docs.push_back({{"archivo", n}, {"ruta", e.path().string()}});
    }
    std::sort(docs.begin(), docs.end(), [](const json& a, const json& b) { return a["archivo"] > b["archivo"]; });
    m["documentos"] = docs;
    // Por tipo, cuántas líneas tiene la bitácora (para los filtros de la línea de tiempo).
    json porTipo = json::object();
    for (const auto& l : evento::Bitacora::leer(dir / "bitacora.jsonl", 0, 0, {}, {}, 0)) {
        const std::string t = l.value("tipo", std::string());
        porTipo[t] = porTipo.value(t, 0) + 1;
    }
    m["porTipo"] = porTipo;
    return m;
}

json HistoricoConsulta::bitacora(const std::string& carpeta, std::int64_t desdeMs, std::int64_t hastaMs,
                                 const std::set<std::string>& tipos, const std::string& texto, std::size_t limite) const {
    const fs::path dir = this->carpeta(carpeta);
    if (dir.empty()) return {{"error", noHay(carpeta)}};
    const fs::path archivo = dir / "bitacora.jsonl";
    json out = json::array();
    for (json x : evento::Bitacora::leer(archivo, desdeMs, hastaMs, tipos, texto, limite)) {
        x["hora"] = QDateTime::fromMSecsSinceEpoch(x.value("t", std::int64_t{0})).toString("HH:mm:ss").toStdString();
        out.push_back(std::move(x));
    }
    const std::string enCurso = carpeta.empty() ? evento::EventoStore(eventos_).enCurso() : carpeta;
    return {{"evento", enCurso}, {"lineas", out}, {"total_en_archivo", evento::Bitacora::leer(archivo, 0, 0, {}, {}, 0).size()}};
}

json HistoricoConsulta::grabaciones(const std::string& carpeta) const {
    const fs::path dir = this->carpeta(carpeta);
    if (dir.empty()) return {{"error", noHay(carpeta)}};
    json g = evento::Historico::grabaciones(dir);
    for (const char* k : {"grabaciones", "transmisiones"})
        for (auto& x : g[k]) {
            x["desde"] = hora(x.value("desde_ms", std::int64_t{0}));
            if (x.value("hasta_ms", std::int64_t{0}) != 0) x["hasta"] = hora(x.value("hasta_ms", std::int64_t{0}));
        }
    g["carpeta"] = dir.string();
    return g;
}

json HistoricoConsulta::archivos(const std::string& carpeta) const {
    const fs::path dir = this->carpeta(carpeta);
    if (dir.empty()) return {{"error", noHay(carpeta)}};
    return evento::Historico::archivos(dir);
}

namespace {
json cargarFoto(const fs::path& dir, const std::string& foto) {
    return evento::FotosConfig::cargar(dir / "config" / ((foto.empty() ? std::string("ultima") : foto) + ".json"));
}
std::string sinFoto(const std::string& foto) {
    return ba::i18n::llenar(ba::i18n::traducir("mcp", "ese evento no tiene foto «%1»"), {foto.empty() ? std::string("ultima") : foto});
}
} // namespace

json HistoricoConsulta::config(const std::string& carpeta, const std::string& foto) const {
    const fs::path dir = this->carpeta(carpeta);
    if (dir.empty()) return {{"error", noHay(carpeta)}};
    const json f = cargarFoto(dir, foto);
    if (f.is_null()) return {{"error", sinFoto(foto)}};
    json lista = json::array();
    const json archivos = f.value("archivos", json::object()); // no .items() de un temporal
    for (const auto& [k, v] : archivos.items()) lista.push_back({{"archivo", k}, {"bytes", v.value("bytes", 0)}});
    return {{"hora", hora(f.value("t", std::int64_t{0}))}, {"archivos", lista}};
}

json HistoricoConsulta::contenidoConfig(const std::string& carpeta, const std::string& foto, const std::string& archivo) const {
    const fs::path dir = this->carpeta(carpeta);
    if (dir.empty()) return {{"error", noHay(carpeta)}};
    const json f = cargarFoto(dir, foto);
    if (f.is_null()) return {{"error", sinFoto(foto)}};
    const json a = f.value("archivos", json::object());
    if (!a.contains(archivo)) return {{"error", ba::i18n::llenar(ba::i18n::traducir("mcp", "la foto no tiene «%1»"), {archivo})}};
    // El contenido se lee por su sha del Eventos/.objetos de SU configuración.
    const Lugar lu = lugar(carpeta);
    const evento::FotosConfig fotos(lu.config, lu.raiz / ".objetos");
    return {{"archivo", archivo}, {"contenido", fotos.leer(a[archivo].value("sha", std::string()))}};
}

json HistoricoConsulta::comparar(const std::string& carpeta, const std::string& foto, const std::string& otro,
                                 const std::string& otraFoto) const {
    // El contenido de cada foto se lee por su sha del Eventos/.objetos de SU configuración:
    // se comparan fotos de la misma («actual» es la configuración de ahora de la abierta).
    const Lugar la = lugar(carpeta);
    const bool actual = otro.empty() && otraFoto == "actual";
    const Lugar lb = actual ? lugar(std::string()) : lugar(otro);
    if (la.raiz != lb.raiz)
        return {{"error", ba::i18n::traducir("mcp", "se comparan fotos de eventos de la misma configuración (cada una guarda sus contenidos)")}};
    const fs::path dir = this->carpeta(carpeta);
    if (dir.empty()) return {{"error", noHay(carpeta)}};
    const json a = cargarFoto(dir, foto);
    if (a.is_null()) return {{"error", sinFoto(foto)}};
    evento::FotosConfig fotos(la.config, la.raiz / ".objetos");
    json b;
    if (actual) {
        if (!la.abierto) return {{"error", ba::i18n::traducir("mcp", "«actual» es la configuración abierta: ese evento es de otra")}};
        b = fotos.foto(QDateTime::currentMSecsSinceEpoch());
    } else {
        const fs::path otroDir = this->carpeta(otro);
        if (otroDir.empty()) return {{"error", noHay(otro)}};
        b = cargarFoto(otroDir, otraFoto);
        if (b.is_null()) return {{"error", sinFoto(otraFoto)}};
    }
    const json c = fotos.comparar(a, b);
    return {{"cambios", c}, {"archivos_distintos", c.size()}};
}

std::string HistoricoConsulta::exportar(const std::string& carpeta, const fs::path& destino, bool conGrabaciones,
                                        fs::path* copia) const {
    const fs::path dir = this->carpeta(carpeta);
    if (dir.empty()) return noHay(carpeta);
    const Lugar lu = lugar(carpeta);
    return evento::Historico::exportar(dir, lu.raiz / ".objetos", destino.empty() ? carpetaDeExportar() : destino,
                                       conGrabaciones, copia);
}

json HistoricoConsulta::sellos(const std::string& carpeta, const std::atomic<bool>* cancelar) const {
    const fs::path dir = this->carpeta(carpeta);
    if (dir.empty()) return {{"error", noHay(carpeta)}};
    json r = evento::CierreEvento::verificar(dir, cancelar);
    const Lugar lu = lugar(carpeta);
    if (dir == evento::EventoStore(lu.raiz).carpetaExistente({}))
        r["aviso"] = ba::i18n::traducir("mcp", "está en curso: se sigue escribiendo (bitácora, ficha…), así que lo que cambió desde su último cierre "
                        "es normal; los sellos valen cuando deje de estar en curso.");
    return r;
}

} // namespace ba::app
