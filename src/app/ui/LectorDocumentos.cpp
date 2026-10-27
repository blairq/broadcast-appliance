// ============================================================================
// src/app/ui/LectorDocumentos.cpp — ver LectorDocumentos.hpp
// ============================================================================
#include "ui/LectorDocumentos.hpp"
#include "i18n/I18n.hpp"

#include <QByteArray>
#include <QThread>

#include <algorithm>

#include "agent/Mirada.hpp"
#include "documentos/Mrz.hpp"
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

LectorDocumentos::LectorDocumentos(plugins::Plugins* plugins, std::filesystem::path dataRoot, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>()), plugins_(plugins), dataRoot_(std::move(dataRoot)) {
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

QString LectorDocumentos::dorsoCon(QString* falta) const {
    QString f;
    if (vision_.disponible && vision_.disponible(&f)) return tr("modelo local");
    if (falta != nullptr) *falta = f;
    if (!dataRoot_.empty() && agent::modeloPropioListo(dataRoot_)) return tr("modelo propio");
    return {};
}

bool LectorDocumentos::dorsoDisponible() const { return !dorsoCon().isEmpty(); }

void LectorDocumentos::prepararDorso() {
    if (retenido_ != nullptr || !vision_.disponible || !vision_.disponible(nullptr) || !vision_.pedir) return;
    retenido_ = new QObject(this);
    vision_.pedir(retenido_, [](quint16, QString) {}); // solo que cargue: los pedidos lo piden de nuevo
}

void LectorDocumentos::soltarDorso() {
    if (retenido_ == nullptr) return;
    if (vision_.soltar) vision_.soltar(retenido_);
    delete retenido_;
    retenido_ = nullptr;
}

json LectorDocumentos::tipos() const {
    json out = json::array();
    for (const auto& t : catalogo_.tipos()) {
        const auto fs = t->formatos();
        out.push_back({{"id", t->id()},
                       {"titulo", ba::i18n::traducir("documentos", t->titulo().c_str())},
                       {"comoSeDice", ba::i18n::traducirEvento("documentos", t->comoSeDice().c_str())}, // va al público: idioma del evento
                       {"formatos", fs},
                       {"origen", "fabrica"},
                       {"pais", t->id() == "dni-ar" ? "AR" : ""},
                       {"dorso", std::find(fs.begin(), fs.end(), documentos::kFormatoMrz) != fs.end()}});
    }
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
    r.error = tr("Esta compilación no trae el lector de códigos de barras (zxing-cpp).");
    luego(std::move(r));
#endif
}

void LectorDocumentos::leerDorso(const QImage& img, std::vector<std::string> aceptados,
                                 std::shared_ptr<documentos::ConfirmadorMrz> confirmador, Respuesta luego) {
    QPointer<LectorDocumentos> yo = this;
    // Primero el local: la foto no sale de la máquina. Un `quien` por pedido: lo suelta
    // al terminar (si el tótem lo tiene retenido, el servidor sigue cargado).
    if (vision_.disponible && vision_.disponible(nullptr) && vision_.pedir) {
        auto* quien = new QObject(this);
        QPointer<QObject> q(quien);
        vision_.pedir(quien, [yo, q, img, aceptados, confirmador, luego](quint16 puerto, QString error) mutable {
            if (yo.isNull()) return;
            const auto soltar = [yo, q]() {
                if (q.isNull()) return;
                if (yo->vision_.soltar) yo->vision_.soltar(q.data());
                q->deleteLater();
            };
            if (puerto == 0) {
                soltar();
                return yo->dorsoConPropio(img, std::move(aceptados), std::move(confirmador), std::move(luego),
                                          tr("modelo local: %1; ").arg(error).toStdString());
            }
            agent::FuenteMirada f;
            f.url = "http://127.0.0.1:" + std::to_string(puerto) + "/v1";
            f.modelo = "local"; // el alias del llama-server
            f.nombre = tr("modelo local").toStdString();
            agent::mirarAparte(f, img, agent::pedidoDelDorso(), yo.data(),
                               [yo, soltar, img, aceptados = std::move(aceptados), confirmador = std::move(confirmador),
                                luego = std::move(luego)](std::string texto, std::string err) mutable {
                                   soltar();
                                   if (yo.isNull()) return;
                                   // Sin respuesta del local: el propio, si hay.
                                   if (texto.empty() && agent::modeloPropioListo(yo->dataRoot_))
                                       return yo->dorsoConPropio(img, std::move(aceptados), std::move(confirmador),
                                                                 std::move(luego),
                                                                 tr("modelo local: %1; ").arg(QString::fromStdString(err)).toStdString());
                                   yo->alTranscribir(std::move(texto), std::move(err), tr("modelo local").toStdString(),
                                                     std::move(aceptados), std::move(confirmador), std::move(luego));
                               });
        });
        return;
    }
    dorsoConPropio(img, std::move(aceptados), std::move(confirmador), std::move(luego), {});
}

void LectorDocumentos::dorsoConPropio(const QImage& img, std::vector<std::string> aceptados,
                                      std::shared_ptr<documentos::ConfirmadorMrz> confirmador, Respuesta luego,
                                      std::string antes) {
    agent::FuenteMirada f;
    std::string error;
    if (dataRoot_.empty() || !agent::fuentePropia(dataRoot_, f, &error)) {
        Resultado r;
        r.dorso = true;
        r.error = tr("No hay con qué leer el dorso (%1%2): hace falta la visión del modelo local o un modelo propio.")
                      .arg(QString::fromStdString(antes),
                           error.empty() ? tr("sin modelo propio") : QString::fromStdString(error));
        luego(std::move(r));
        return;
    }
    QPointer<LectorDocumentos> yo = this;
    agent::mirarAparte(f, img, agent::pedidoDelDorso(), this,
                       [yo, antes, aceptados = std::move(aceptados), confirmador = std::move(confirmador),
                        luego = std::move(luego)](std::string texto, std::string err) mutable {
                           if (yo.isNull()) return;
                           yo->alTranscribir(std::move(texto), antes + err, tr("modelo propio").toStdString(), std::move(aceptados),
                                             std::move(confirmador), std::move(luego));
                       });
}

void LectorDocumentos::alTranscribir(std::string texto, std::string error, std::string con,
                                     std::vector<std::string> aceptados,
                                     std::shared_ptr<documentos::ConfirmadorMrz> confirmador, Respuesta luego) {
    Resultado r;
    r.dorso = true;
    r.con = con;
    if (texto.empty()) {
        r.error = tr("El %1 no leyó el dorso: %2").arg(QString::fromStdString(con), QString::fromStdString(error));
        return luego(std::move(r));
    }
    // Lo que contestó NO va a ningún registro: son los datos de la persona.
    const documentos::LecturaMrz l = documentos::interpretarRespuestaMrz(texto);
    using E = documentos::LecturaMrz::Estado;
    const bool aceptada = confirmador ? confirmador->agregar(l) : l.estado == E::Leida;
    if (!aceptada) {
        r.noSeVe = l.estado == E::NoSeVe;
        r.dudosa = l.estado == E::Dudosa;
        r.detalle = r.dudosa ? tr("no cierra el verificador compuesto: hace falta otra foto que coincida").toStdString()
                             : l.motivo;
        return luego(std::move(r));
    }
    traducir({documentos::Codigo{documentos::kFormatoMrz, l.texto, {}}}, std::move(aceptados),
             [luego = std::move(luego), con](Resultado x) {
                 x.dorso = true;
                 x.con = con;
                 if (!x.leido) {
                     x.detalle = tr("los renglones no son de un documento de los aceptados").toStdString();
                     // Los renglones son los datos de la persona: no salen (MCP, logs).
                     for (auto& c : x.codigos) c.texto.clear();
                 }
                 luego(std::move(x));
             });
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
