// ============================================================================
// src/app/ui/LectorDocumentos.cpp — ver LectorDocumentos.hpp
// ============================================================================
#include "ui/LectorDocumentos.hpp"

#include <QByteArray>
#include <QThread>

#include <algorithm>

#include "plugins/Plugins.hpp"
#if defined(BA_HAVE_CODIGOS)
#include "documentos/lector/LectorCodigos.hpp"
#endif

namespace ba::app {

using nlohmann::json;

namespace {

/// Cuánto se le espera a un plugin que traduzca un código. Corto: es una
/// cuenta sobre un texto, y la persona está parada frente a la cámara.
constexpr int kEsperaPluginMs = 3000;

bool aceptaFormato(const json& formatos, const std::string& formato) {
    if (!formatos.is_array() || formatos.empty() || formato.empty()) return true;
    return std::any_of(formatos.begin(), formatos.end(),
                       [&formato](const json& f) { return f.is_string() && f.get<std::string>() == formato; });
}

} // namespace

struct LectorDocumentos::Impl {
    QThread hilo;
    /// Vive en `hilo`: a quién se le mandan las lecturas para que corran allá.
    QObject* alla = nullptr;
    /// Cuántas lecturas hay dando vueltas. Solo se toca desde la interfaz.
    int enVuelo = 0;

    /// Un tipo a probar (de fábrica o de un plugin).
    struct Tipo {
        std::string id;
        bool fabrica = false;
        std::string plugin, nombre, pais;
        json formatos;
    };
    /// Una traducción en curso: por qué código y qué tipo va.
    struct Recorrido {
        std::vector<documentos::Codigo> codigos;
        std::vector<Tipo> tipos;
        std::size_t ci = 0, ti = 0;
        Respuesta luego;
    };
    static void paso(QPointer<LectorDocumentos> yo, std::shared_ptr<Recorrido> s);
    static void terminar(const std::shared_ptr<Recorrido>& s, bool leido, json datos);
};

LectorDocumentos::LectorDocumentos(plugins::Plugins* plugins, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>()), plugins_(plugins) {
    d_->alla = new QObject;
    d_->alla->moveToThread(&d_->hilo);
    d_->hilo.setObjectName("ba-documentos");
    d_->hilo.start();
    if (plugins != nullptr) connect(plugins, &plugins::Plugins::cambio, this, &LectorDocumentos::tiposCambio);
}

LectorDocumentos::~LectorDocumentos() {
    // Primero el hilo: ninguna lectura queda a medias. Las respuestas que ya
    // estén en la cola de la interfaz las cubre el QPointer de `leer`.
    d_->hilo.quit();
    d_->hilo.wait();
    delete d_->alla;
}

bool LectorDocumentos::disponible() {
#if defined(BA_HAVE_CODIGOS)
    return true;
#else
    return false;
#endif
}

json LectorDocumentos::tipos() const {
    json out = json::array();
    for (const auto& t : catalogo_.tipos())
        out.push_back({{"id", t->id()},
                       {"titulo", t->titulo()},
                       {"comoSeDice", t->comoSeDice()},
                       {"formatos", t->formatos()},
                       {"origen", "fabrica"},
                       {"pais", t->id() == "dni-ar" ? "AR" : ""}});
    if (plugins_)
        for (json p : plugins_->tiposDocumento()) {
            p["origen"] = "plugin";
            out.push_back(std::move(p));
        }
    return out;
}

void LectorDocumentos::leer(const QImage& img, std::vector<std::string> aceptados, bool descartable, Respuesta luego) {
#if defined(BA_HAVE_CODIGOS)
    if (descartable && d_->enVuelo > 0) return; // ver el encabezado: no se encola
    ++d_->enVuelo;
    QPointer<LectorDocumentos> yo = this;
    QMetaObject::invokeMethod(
        d_->alla,
        [yo, img, aceptados = std::move(aceptados), luego = std::move(luego)]() mutable {
            // ARGB32 en memoria es B, G, R, A en una máquina little-endian (las tres de acá).
            const QImage b = img.convertToFormat(QImage::Format_ARGB32);
            std::vector<documentos::Codigo> codigos;
            if (!b.isNull())
                codigos = documentos::leerCodigos(b.constBits(), b.width(), b.height(),
                                                  static_cast<int>(b.bytesPerLine()));
            if (yo.isNull()) return;
            QMetaObject::invokeMethod(
                yo.data(),
                [yo, codigos = std::move(codigos), aceptados = std::move(aceptados), luego = std::move(luego)]() mutable {
                    if (yo.isNull()) return;
                    --yo->d_->enVuelo;
                    yo->traducir(std::move(codigos), std::move(aceptados), std::move(luego));
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
#else
    (void)img;
    (void)aceptados;
    (void)descartable;
    Resultado r;
    r.error = "Esta compilación no trae el lector de códigos de barras (zxing-cpp).";
    luego(std::move(r));
#endif
}

void LectorDocumentos::traducir(std::vector<documentos::Codigo> codigos, std::vector<std::string> aceptados,
                                Respuesta luego) {
    auto s = std::make_shared<Impl::Recorrido>();
    s->codigos = std::move(codigos);
    s->luego = std::move(luego);
    // Los tipos a probar, en orden: los aceptados o, si no se dice, todos (de fábrica primero).
    const json todos = tipos();
    const auto agregar = [&s](const json& t) {
        Impl::Tipo x;
        x.id = t.value("id", std::string());
        x.fabrica = t.value("origen", std::string()) == "fabrica";
        x.plugin = t.value("plugin", std::string());
        x.nombre = t.value("nombre", std::string());
        x.pais = t.value("pais", std::string());
        x.formatos = t.value("formatos", json::array());
        s->tipos.push_back(std::move(x));
    };
    if (aceptados.empty()) {
        for (const json& t : todos) agregar(t);
    } else {
        for (const std::string& id : aceptados)
            for (const json& t : todos)
                if (t.value("id", std::string()) == id) agregar(t);
    }
    Impl::paso(this, std::move(s));
}

void LectorDocumentos::Impl::terminar(const std::shared_ptr<Recorrido>& s, bool leido, json datos) {
    Resultado r;
    r.codigos = s->codigos;
    r.leido = leido;
    r.datos = std::move(datos);
    if (s->luego) s->luego(std::move(r));
}

void LectorDocumentos::Impl::paso(QPointer<LectorDocumentos> yo, std::shared_ptr<Recorrido> s) {
    // Código por código, tipo por tipo. Los de plugin contestan después: el
    // recorrido sigue desde su respuesta (el pedido tiene `s`, y con él la cuenta).
    while (!yo.isNull() && s->ci < s->codigos.size()) {
        if (s->ti >= s->tipos.size()) {
            ++s->ci;
            s->ti = 0;
            continue;
        }
        const documentos::Codigo& c = s->codigos[s->ci];
        const Tipo t = s->tipos[s->ti++];
        if (!aceptaFormato(t.formatos, c.formato)) continue;
        if (t.fabrica) {
            documentos::Datos d;
            if (yo->catalogo_.leer(c, {t.id}, d)) return terminar(s, true, d.aJson());
            continue;
        }
        if (!yo->plugins_ || t.plugin.empty()) continue;
        json args{{"nombre", t.nombre}, {"formato", c.formato}, {"texto", c.texto}};
        if (!c.bytes.empty())
            args["bytes_b64"] =
                QByteArray(reinterpret_cast<const char*>(c.bytes.data()), static_cast<qsizetype>(c.bytes.size()))
                    .toBase64()
                    .toStdString();
        yo->plugins_->pedir(
            t.plugin, "documento", args,
            [yo, s, t](json r) {
                if (yo.isNull()) return;
                const json res = r.value("resultado", json());
                const json datos = res.is_object() ? res.value("datos", json()) : json();
                if (r.value("ok", false) && datos.is_object()) {
                    documentos::Datos d = documentos::Datos::deJson(datos);
                    // Sin número no es un documento leído: el plugin dijo que sí de más.
                    if (!d.numero.empty()) {
                        d.tipo = t.id;
                        if (d.pais.empty()) d.pais = t.pais;
                        if (d.crudo.empty()) d.crudo = s->codigos[s->ci].texto;
                        return terminar(s, true, d.aJson());
                    }
                }
                paso(yo, s);
            },
            kEsperaPluginMs);
        return;
    }
    if (!yo.isNull()) terminar(s, false, json());
}

} // namespace ba::app
