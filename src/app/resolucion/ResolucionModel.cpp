// ============================================================================
// src/app/resolucion/ResolucionModel.cpp — ver ResolucionModel.hpp
// ============================================================================
#include "resolucion/ResolucionModel.hpp"

#include "i18n/I18n.hpp"

#include <QJsonDocument>
#include <QJsonObject>

#include <regex>

namespace ba::app::resolucion {

using json = nlohmann::json;

namespace {

QVariant aVariant(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"x", j}}.dump(-1, ' ', false, json::error_handler_t::replace)))
        .object()
        .value("x")
        .toVariant();
}

/// El tamaño de unas caps escritas («video/x-raw, width=(int)1920, height=(int)1080, …»).
Tam tamDeCaps(const std::string& caps) {
    static const std::regex w(R"(width=\(int\)(\d+))"), h(R"(height=\(int\)(\d+))");
    std::smatch a, b;
    if (!std::regex_search(caps, a, w) || !std::regex_search(caps, b, h)) return {};
    return {std::stoi(a[1]), std::stoi(b[1])};
}

} // namespace

ResolucionModel::ResolucionModel(Fuentes f, QObject* parent) : QObject(parent), f_(std::move(f)) {
    reloj_.setInterval(2000);
    connect(&reloj_, &QTimer::timeout, this, [this]() { recalcular(); });
}

void ResolucionModel::mirar(bool si) {
    mirando_ = std::max(0, mirando_ + (si ? 1 : -1));
    if (mirando_ > 0 && !reloj_.isActive()) {
        recalcular(); // que la vista no arranque sin colores
        reloj_.start();
    } else if (mirando_ == 0) {
        reloj_.stop();
    }
}

const std::vector<ba::motor::ModoVideo>& ResolucionModel::modosDe(const std::string& camara) {
    auto& m = modos_[camara];
    const auto ahora = std::chrono::steady_clock::now();
    if (m.second.empty() || ahora - m.first > std::chrono::seconds(30)) {
        m.second = f_.modosDe ? f_.modosDe(camara) : std::vector<ba::motor::ModoVideo>{};
        m.first = ahora;
    }
    return m.second;
}

const json& ResolucionModel::interiorDe(const std::string& elemento) {
    auto it = interiores_.find(elemento);
    if (it == interiores_.end()) it = interiores_.emplace(elemento, f_.interior ? f_.interior(elemento) : json::object()).first;
    return it->second;
}

json ResolucionModel::recalcular() {
    json planes = json::object(), niveles = json::object();
    try {
        const json motor = f_.motor ? f_.motor() : json::object();
        const json entradas = motor.value("entradas", json::object());
        const Tam lienzo = f_.lienzo ? f_.lienzo() : Tam{1920, 1080};
        const double fps = f_.fps ? f_.fps() : 30.0;
        // Dónde se ve cada fuente.
        std::map<std::string, std::vector<std::pair<std::string, Tam>>> seVe;
        if (f_.escenas)
            for (const auto& [escena, items] : f_.escenas())
                for (const auto& [fuente, t] : items) {
                    auto& v = seVe[fuente];
                    bool ya = false;
                    for (auto& [e, tt] : v)
                        if (e == escena) {
                            tt.ancho = std::max(tt.ancho, t.ancho), tt.alto = std::max(tt.alto, t.alto);
                            ya = true;
                        }
                    if (!ya) v.push_back({escena, t});
                }
        // Los títulos de las tarjetas y el nodo «entrada» de cada grupo.
        std::map<std::string, std::string> titulos, nodoEntrada;
        if (f_.grafo)
            for (const auto& n : f_.grafo()) {
                titulos[n.id] = n.etiqueta;
                if (n.tipo == "entrada" && !n.grupo.empty()) nodoEntrada[n.grupo] = n.id;
            }
        for (const auto& [id, est] : entradas.items()) {
            if (!est.is_object() || !est.value("video", false)) continue;
            Entrada e;
            e.id = id;
            e.lienzo = lienzo;
            e.fps = fps;
            if (const auto it = seVe.find(id); it != seVe.end()) e.seVe = it->second;
            const json origen = est.value("origen", json::object());
            if (origen.is_object()) {
                e.origen = {origen.value("ancho", 0), origen.value("alto", 0)};
                e.origenFps = origen.value("fps", 0.0);
            }
            e.cadenaGpu = est.value("gpu", false);
            e.fija = est.value("fija", false);
            e.modoActual = est.value("modo_camara", json());
            const std::string camara = f_.camaraDe ? f_.camaraDe(id) : std::string();
            if (!camara.empty()) {
                e.esCamara = true;
                e.modos = modosDe(camara);
            }
            for (const auto& fl : est.value("filtros", json::array())) {
                if (!fl.is_object() || fl.value("audio", false)) continue;
                Tarjeta t;
                t.id = fl.value("id", std::string());
                t.elemento = fl.value("elemento", std::string());
                const auto ti = titulos.find(t.id);
                t.titulo = ti != titulos.end() && !ti->second.empty() ? ti->second : t.elemento;
                t.sale = tamDeCaps(fl.value("caps", std::string()));
                const json info = fl.value("info", json::object());
                if (info.is_object() && info.contains("zoom") && info["zoom"].is_number()) t.zoom = info["zoom"].get<double>();
                const json& in = interiorDe(t.elemento);
                bool alguna = false, todasRam = true, gpu = false;
                for (const auto& m : in.value("memorias", json::object()).value("entrada", json::array())) {
                    const std::string s = m.get<std::string>();
                    alguna = true;
                    todasRam = todasRam && s == "sistema";
                    gpu = gpu || s == "cualquiera" || (s != "sistema" && s != "dmabuf");
                }
                t.soloRam = alguna && todasRam;
                t.gpuPosible = gpu;
                e.cadena.push_back(std::move(t));
            }
            json p = planear(e);
            // Los niveles: los de cada tarjeta, y el de la fuente en «entrada:<nombre>» y en su nodo del grafo.
            // Con nombre: items() sobre un json temporal recorre memoria liberada.
            const json nivelesP = p.value("niveles", json::object());
            for (const auto& [k, n] : nivelesP.items()) {
                if (k == "entrada") {
                    niveles["entrada:" + id] = n;
                    if (const auto ne = nodoEntrada.find(id); ne != nodoEntrada.end()) niveles[ne->second] = n;
                } else {
                    // Por entrada (lo usa Latencias; dos entradas pueden tener una tarjeta «ptz»
                    // con el mismo id) y, si es un nodo del grafo («<grupo>.<tarjeta>», único), por su id.
                    niveles["tarjeta:" + id + ":" + k] = n;
                    if (k.find('.') != std::string::npos) niveles[k] = n;
                }
            }
            planes[id] = std::move(p);
        }
    } catch (const std::exception& ex) {
        // Una foto con algo inesperado no tumba la vista: queda lo último bueno.
        qWarning("[resolucion] %s",
                 ba::i18n::llenar(ba::i18n::traducir("resolucion", "no se pudo armar el plan: %1"), {ex.what()}).c_str());
        return ultimo_;
    }
    ultimo_ = {{"entradas", planes}, {"niveles", niveles}};
    const QVariantMap nv = aVariant(niveles).toMap(), pu = aVariant(planes).toMap();
    if (nv != niveles_ || pu != planesUi_) { // la vista se redibuja solo si cambió algo
        niveles_ = nv;
        planesUi_ = pu;
        emit cambio();
    }
    return ultimo_;
}

json ResolucionModel::plan(const std::string& entrada) {
    const json todo = recalcular();
    const json& e = todo.value("entradas", json::object());
    return e.contains(entrada) ? e[entrada] : json();
}

} // namespace ba::app::resolucion
