// ============================================================================
// src/app/ui/ExtensionesModel.cpp — ver ExtensionesModel.hpp
// ============================================================================
#include "ui/ExtensionesModel.hpp"

#include <vector>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include "AppRuntime.hpp"
#include "diag/DiarioDeArranque.hpp"
#include "diag/Registro.hpp"

namespace ba::app {
namespace {

/// Un id sirve para armar una ruta, así que se mira con desconfianza: nada de
/// separadores ni de «..». No es seguridad de verdad —la extensión ya corre
/// adentro del proceso— pero evita que un nombre raro escrito a mano termine
/// leyendo cualquier archivo del disco.
bool idSano(const QString& id) {
    if (id.isEmpty() || id.size() > 64) return false;
    for (const QChar c : id)
        if (!c.isLetterOrNumber() && c != '-' && c != '_') return false;
    return true;
}

} // namespace

struct ExtensionesModel::Impl {
    AppRuntime& rt;
    struct Ext {
        QString id, nombre, descripcion, vista, problema;
    };
    std::vector<Ext> ext;

    explicit Impl(AppRuntime& r) : rt(r) {}

    QString raiz() const { return QString::fromStdString((rt.dataRoot() / "Extensiones").string()); }
};

ExtensionesModel::ExtensionesModel(AppRuntime& rt, QObject* parent)
    : QAbstractListModel(parent), d_(std::make_unique<Impl>(rt)) {
    QDir().mkpath(d_->raiz());
    refrescar();
}

ExtensionesModel::~ExtensionesModel() = default;

QString ExtensionesModel::carpeta() const { return d_->raiz(); }

void ExtensionesModel::refrescar() {
    beginResetModel();
    d_->ext.clear();
    const QDir raiz(d_->raiz());
    for (const QString& id : raiz.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        Impl::Ext e;
        e.id = id;
        if (!idSano(id)) {
            // No se saltea en silencio: una carpeta que no se carga y nadie
            // dice por qué es media hora de buscar en el lugar equivocado.
            e.nombre = id;
            e.problema = "el nombre de la carpeta tiene caracteres que no se pueden usar";
            d_->ext.push_back(e);
            continue;
        }
        const QDir dir(raiz.filePath(id));
        QFile f(dir.filePath("extension.json"));
        if (!f.open(QIODevice::ReadOnly)) {
            e.nombre = id;
            e.problema = "le falta extension.json";
            d_->ext.push_back(e);
            continue;
        }
        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
        if (!doc.isObject()) {
            e.nombre = id;
            e.problema = "extension.json no es JSON válido: " + pe.errorString();
            d_->ext.push_back(e);
            continue;
        }
        const QJsonObject o = doc.object();
        e.nombre = o.value("nombre").toString(id);
        e.descripcion = o.value("descripcion").toString();
        const QString rel = o.value("tarjeta").toString("tarjeta.qml");
        // La vista tiene que estar ADENTRO de su carpeta: sin esto, un
        // "tarjeta": "../../otra/cosa.qml" cargaría cualquier archivo.
        if (rel.contains("..") || rel.contains('/') || rel.contains('\\')) {
            e.problema = "«tarjeta» tiene que ser un archivo de la propia carpeta";
        } else if (!QFile::exists(dir.filePath(rel))) {
            e.problema = "falta el archivo " + rel;
        } else {
            e.vista = QUrl::fromLocalFile(dir.filePath(rel)).toString();
        }
        d_->ext.push_back(e);
    }
    endResetModel();
    emit cambio();
}

int ExtensionesModel::rowCount(const QModelIndex&) const { return static_cast<int>(d_->ext.size()); }

QVariant ExtensionesModel::data(const QModelIndex& index, int role) const {
    if (index.row() < 0 || index.row() >= rowCount()) return {};
    const Impl::Ext& e = d_->ext[static_cast<std::size_t>(index.row())];
    switch (role) {
        case IdRole: return e.id;
        case NombreRole: return e.nombre;
        case DescripcionRole: return e.descripcion;
        case VistaRole: return e.vista;
        case ProblemaRole: return e.problema;
        case ApagadaRole: return ba::diag::diario().apagado((kPrefijo + e.id).toStdString());
        default: return {};
    }
}

QHash<int, QByteArray> ExtensionesModel::roleNames() const {
    return {{IdRole, "id"},       {NombreRole, "nombre"},     {DescripcionRole, "descripcion"},
            {VistaRole, "vista"}, {ProblemaRole, "problema"}, {ApagadaRole, "apagada"}};
}

QVariantMap ExtensionesModel::ficha(const QString& id) const {
    for (const Impl::Ext& e : d_->ext)
        if (e.id == id)
            return QVariantMap{{"id", e.id},
                               {"nombre", e.nombre},
                               {"descripcion", e.descripcion},
                               {"vista", e.vista},
                               {"problema", e.problema},
                               {"apagada", ba::diag::diario().apagado((kPrefijo + id).toStdString())}};
    return {};
}

QString ExtensionesModel::vista(const QString& id) const {
    for (const Impl::Ext& e : d_->ext)
        if (e.id == id) {
            // Una apagada NO se carga aunque alguien pida su vista: si no, el
            // diario apagaría algo que igual se vuelve a cargar y el programa
            // vuelve a morir en el mismo lugar.
            if (ba::diag::diario().apagado((kPrefijo + id).toStdString())) return {};
            // En vivo, nada nuevo: solo las que ya andaban en esta sesión.
            if (enVivo_ && enVivo_() && !yaCargadas_.count(id)) return {};
            yaCargadas_.insert(id);
            return e.vista;
        }
    return {};
}

QStringList ExtensionesModel::kinds() const {
    QStringList out;
    for (const Impl::Ext& e : d_->ext)
        if (e.problema.isEmpty() && !ba::diag::diario().apagado((kPrefijo + e.id).toStdString()))
            out << kPrefijo + e.id;
    return out;
}

QString ExtensionesModel::tituloDeKind(const QString& kind) const {
    if (!kind.startsWith(kPrefijo)) return kind;
    const QString id = kind.mid(static_cast<int>(std::char_traits<char>::length(kPrefijo)));
    for (const Impl::Ext& e : d_->ext)
        if (e.id == id) return e.nombre;
    return id;
}

void ExtensionesModel::cargando(const QString& id) {
    // Se anota ANTES de que el QML se evalúe. Si eso cuelga la interfaz, el
    // arranque no termina y la próxima vez esta extensión queda apagada.
    ba::diag::diario().empieza((kPrefijo + id).toStdString());
}

void ExtensionesModel::cargada(const QString& id, bool bien, const QString& error) {
    ba::diag::diario().termina((kPrefijo + id).toStdString());
    if (bien) return;
    // Un error de QML NO apaga la extensión: no rompió nada, solo no se
    // dibuja, y apagarla obligaría a prenderla a mano después de cada error de
    // tipeo. Se registra con el archivo y la línea, que es lo que se arregla.
    ba::diag::registro().linea(ba::diag::Nivel::Aviso, "extensiones", 0,
                               "la tarjeta «" + id.toStdString() + "» no cargó: " + error.toStdString());
    emit const_cast<ExtensionesModel*>(this)->notify(
        "La tarjeta «" + id + "» tiene un error (mirá el Registro)", "error");
}

QString ExtensionesModel::motivoNoCarga(const QString& id) const {
    if (enVivo_ && enVivo_() && !yaCargadas_.count(id))
        return "En vivo no se cargan extensiones nuevas: se carga al salir del modo en vivo.";
    return {};
}

void ExtensionesModel::tardo(const QString& id, int ms) {
    ba::diag::diario().termina((kPrefijo + id).toStdString());
    ba::diag::diario().setApagado((kPrefijo + id).toStdString(), true);
    ba::diag::registro().linea(ba::diag::Nivel::Aviso, "extensiones", 0,
                               "la tarjeta «" + id.toStdString() + "» tardó más de " + std::to_string(ms) +
                                   " ms en cargar: se cortó y quedó apagada (se prende desde el Registro)");
    emit notify("La tarjeta «" + id + "» tardaba demasiado en cargar: quedó apagada", "error");
}

void ExtensionesModel::prender(const QString& id) {
    ba::diag::diario().setApagado((kPrefijo + id).toStdString(), false);
    ba::diag::registro().linea(ba::diag::Nivel::Info, "extensiones", 0,
                               "«" + id.toStdString() + "» vuelve a cargarse");
    refrescar();
    emit notify("«" + id + "» se va a cargar de nuevo", "ok");
}

} // namespace ba::app
