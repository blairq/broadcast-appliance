// src/app/agent/VocesRack.cpp — ver VocesRack.hpp

#include "agent/VocesRack.hpp"

#include "agent/FlujosTexto.hpp"
#include "agent/ModelosLocales.hpp"

#if defined(BA_HAVE_CLAP)
#include "audio/RackManager.hpp"
#endif
#if defined(BA_HAVE_VOZ)
#include "audio/Habla.hpp"
#include "dsp/Resampler.hpp"
#endif

#include "comun/HoraMonotonica.hpp"

#include <QMetaObject>
#include <QSet>

#include <vector>

namespace ba::app::agent {

struct VocesRack::Cargada {
#if defined(BA_HAVE_VOZ)
    std::unique_ptr<audio::Habla> habla = std::make_unique<audio::Habla>();
#endif
    bool lista = false;
    QString error;
    /// Lo que se pidió mientras cargaba.
    std::vector<std::pair<nlohmann::json, QString>> pendientes;
};

VocesRack::VocesRack(ModelosLocales& modelos, QObject* parent) : QObject(parent), modelos_(modelos) {
    connect(&modelos_, &ModelosLocales::cambio, this, &VocesRack::cambio);
}

VocesRack::~VocesRack() = default;

void VocesRack::setFlujos(FlujosTexto* flujos) {
    flujos_ = flujos;
    if (flujos_ != nullptr) connect(flujos_, &FlujosTexto::frase, this, &VocesRack::alFrase);
}

QString VocesRack::nombreIdioma(const QString& c) {
    static const std::map<QString, QString> n{{"es", "Castellano"}, {"en", "Inglés"},     {"pt", "Portugués"},
                                              {"fr", "Francés"},    {"it", "Italiano"},   {"de", "Alemán"},
                                              {"ca", "Catalán"},    {"nl", "Neerlandés"}, {"pl", "Polaco"}};
    auto it = n.find(c);
    return it == n.end() ? c : it->second;
}

QVariantList VocesRack::voces() const {
    QVariantList out;
    for (const QVariant& v : modelos_.catalogo()) {
        const QVariantMap m = v.toMap();
        if (m.value("tipo").toString() != "tts") continue;
        const QString idioma =
            m.value("idioma").toString().isEmpty() ? QStringLiteral("es") : m.value("idioma").toString();
        out << QVariantMap{{"repo", m.value("repo")},
                           {"nombre", m.value("nombre")},
                           {"idioma", idioma},
                           {"idiomaNombre", nombreIdioma(idioma)},
                           {"acento", m.value("acento")},
                           {"genero", m.value("genero")},
                           {"instalada", !modelos_.carpetaInstalada(m.value("repo").toString()).empty()}};
    }
    return out;
}

QVariantList VocesRack::idiomas() const {
    QVariantList out;
    QSet<QString> vistos;
    for (const QVariant& v : voces()) {
        const QString c = v.toMap().value("idioma").toString();
        if (vistos.contains(c)) continue;
        vistos.insert(c);
        out << QVariantMap{{"codigo", c}, {"nombre", nombreIdioma(c)}};
    }
    return out;
}

void VocesRack::bajar(const QString& repo) { modelos_.descargar(repo); }

VocesRack::Cargada* VocesRack::voz(const QString& repo) {
#if defined(BA_HAVE_VOZ)
    auto it = cargadas_.find(repo);
    if (it != cargadas_.end()) return it->second.get();
    const std::filesystem::path d = modelos_.carpetaInstalada(repo);
    if (d.empty()) return nullptr;
    QString modelo;
    for (const QVariant& v : modelos_.catalogo())
        if (v.toMap().value("repo").toString() == repo) modelo = v.toMap().value("modelo").toString();
    auto c = std::make_unique<Cargada>();
    Cargada* p = c.get();
    cargadas_[repo] = std::move(c);
    audio::Habla::Voz v;
    v.modelo = (d / modelo.toStdString()).string();
    v.tokens = (d / "tokens.txt").string();
    v.datos = (d / "espeak-ng-data").string();
    p->habla->cargar(v, [this, repo](const std::string& error) {
        QMetaObject::invokeMethod(this, [this, repo, error]() {
            auto it = cargadas_.find(repo);
            if (it == cargadas_.end()) return;
            Cargada& c = *it->second;
            c.lista = error.empty();
            c.error = QString::fromStdString(error);
            auto pendientes = std::move(c.pendientes);
            c.pendientes.clear();
            if (c.lista)
                for (const auto& [tarjeta, texto] : pendientes)
                    decirCon(tarjeta, texto);
            emit cambio();
        });
    });
    emit cambio();
    return p;
#else
    Q_UNUSED(repo);
    return nullptr;
#endif
}

QString VocesRack::estado(const QString& fuente, const QString& nodo) const {
#if defined(BA_HAVE_CLAP) && defined(BA_HAVE_VOZ)
    if (racks_ == nullptr) return {};
    for (const auto& h : racks_->hablas()) {
        if (QString::fromStdString(h.value("fuente", "")) != fuente ||
            QString::fromStdString(h.value("nodo", "")) != nodo)
            continue;
        QString repo = QString::fromStdString(h.value("voz", ""));
        if (repo.isEmpty() && vozSistema_) repo = vozSistema_();
        if (repo.isEmpty()) return QStringLiteral("Sin voz: elegí una (o bajá una en Modelos locales)");
        if (modelos_.carpetaInstalada(repo).empty()) return QStringLiteral("Falta bajar la voz");
        auto it = cargadas_.find(repo);
        if (it == cargadas_.end()) return QStringLiteral("Lista (se carga al hablar)");
        if (!it->second->error.isEmpty()) return "No cargó: " + it->second->error;
        return it->second->lista ? QStringLiteral("Lista") : QStringLiteral("Cargando la voz…");
    }
    return QStringLiteral("No existe la tarjeta");
#else
    Q_UNUSED(fuente);
    Q_UNUSED(nodo);
    return QStringLiteral("Esta compilación no tiene voz");
#endif
}

QString VocesRack::decir(const QString& fuente, const QString& nodo, const QString& texto) {
#if defined(BA_HAVE_CLAP) && defined(BA_HAVE_VOZ)
    if (racks_ == nullptr) return QStringLiteral("No hay racks de audio");
    if (texto.trimmed().isEmpty()) return QStringLiteral("No hay nada que decir");
    for (const auto& h : racks_->hablas())
        if (QString::fromStdString(h.value("fuente", "")) == fuente &&
            (nodo.isEmpty() || QString::fromStdString(h.value("nodo", "")) == nodo)) {
            decirCon(h, texto);
            return {};
        }
    return QStringLiteral("«%1» no tiene una tarjeta de texto a voz%2")
        .arg(fuente, nodo.isEmpty() ? "" : " «" + nodo + "»");
#else
    Q_UNUSED(fuente);
    Q_UNUSED(nodo);
    Q_UNUSED(texto);
    return QStringLiteral("Esta compilación no tiene voz");
#endif
}

void VocesRack::alFrase(const QString& flujo, const QString& texto) {
#if defined(BA_HAVE_CLAP) && defined(BA_HAVE_VOZ)
    if (racks_ == nullptr) return;
    for (const auto& h : racks_->hablas())
        if (!h.value("flujo", std::string()).empty() && QString::fromStdString(h.value("flujo", "")) == flujo)
            decirCon(h, texto);
#else
    Q_UNUSED(flujo);
    Q_UNUSED(texto);
#endif
}

void VocesRack::decirCon(const nlohmann::json& tarjeta, const QString& texto) {
#if defined(BA_HAVE_CLAP) && defined(BA_HAVE_VOZ)
    QString repo = QString::fromStdString(tarjeta.value("voz", ""));
    if (repo.isEmpty() && vozSistema_) repo = vozSistema_();
    Cargada* c = voz(repo);
    if (c == nullptr) return; // no está bajada: estado() lo dice
    if (!c->lista) {
        c->pendientes.emplace_back(tarjeta, texto);
        return;
    }
    const std::string fuente = tarjeta.value("fuente", ""), nodo = tarjeta.value("nodo", "");
    std::shared_ptr<audio::anillo::Ring> ring = racks_->anilloHabla(fuente, nodo);
    if (!ring) return;
    const std::string clave = fuente + "\n" + nodo;
    const int hablante = tarjeta.value("hablante", 0);
    const float velocidad = static_cast<float>(std::clamp(tarjeta.value("velocidad", 1.0), 0.5, 2.0));
    c->habla->decir(
        texto.toStdString(), hablante, velocidad,
        [this, ring, clave](const float* m, std::size_t n, std::uint32_t f) {
            // A 48 kHz estéreo (el rack y libobs van a 48 kHz).
            constexpr int kSalida = 48000;
            std::vector<float> out(static_cast<std::size_t>(static_cast<double>(n) * kSalida / f) + 1024);
            std::size_t hechos = 0;
            ba::dsp::Resampler rs;
            rs.configure(static_cast<int>(f), kSalida, 1, ba::dsp::Quality::SincMedium);
            if (!rs.resample(m, n, out.data(), out.size(), &hechos) || hechos == 0) return;
            // Pegado a lo anterior de esta tarjeta; si no suena nada, un poco adelante.
            const std::uint64_t ahora = ba::app::ahoraNs();
            std::uint64_t ts;
            {
                std::lock_guard<std::mutex> l(proximoLock_);
                ts = std::max<std::uint64_t>(proximo_[clave], ahora + 60'000'000ULL);
                proximo_[clave] = ts + static_cast<std::uint64_t>(hechos) * 1'000'000'000ULL / kSalida;
            }
            ring->pushEn(out.data(), out.data(), static_cast<std::uint32_t>(hechos), ts, ahora, kSalida);
        });
#else
    Q_UNUSED(tarjeta);
    Q_UNUSED(texto);
#endif
}

} // namespace ba::app::agent
