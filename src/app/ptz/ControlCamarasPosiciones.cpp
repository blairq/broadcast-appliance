// ============================================================================
// src/app/ptz/ControlCamarasPosiciones.cpp — posiciones con nombre, presets y miniaturas
// (la misma clase ControlCamaras, en partes: ver ControlCamarasCamara.hpp)
// ============================================================================
#include "ptz/ControlCamarasCamara.hpp"
#include "platform/Paths.hpp"

#include "ajustes/Ajustes.hpp"
#include "arranque/AppRuntime.hpp"
#include "emision/BroadcastEngine.hpp"
#include "emision/InputSpecs.hpp"
#include "net/Credentials.hpp"
#include "ptz/Interlock.hpp"
#include "ptz/OnvifBusqueda.hpp"
#include "ptz/Posiciones.hpp"
#include "ptz/PtzBackend.hpp"
#include "ptz/PtzDigital.hpp"
#include "ptz/PtzOnvif.hpp"
#include "ptz/PtzPanasonic.hpp"
#include "ptz/PtzPlugin.hpp"
#include "ptz/PtzVisca.hpp"

#include "emision/FuentesApp.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QCoreApplication>
#include <QImage>
#include <QFileInfo>
#include <QUrl>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <cmath>

namespace ba::app::ptz {

using namespace camaras;

// ---------------------------------------------------------------------------
// Posiciones con nombre
// ---------------------------------------------------------------------------

QVariantList ControlCamaras::posiciones(const QString& entrada) const {
    auto it = camaras_.find(entrada.toStdString());
    if (it == camaras_.end()) return {};
    QVariantList todas = it->second->posiciones.json().toVariantList();
    QVariantList out;
    for (QVariant& v : todas) {
        QVariantMap m = v.toMap();
        if (m.value("interna").toBool()) continue; // la memoria de la foto del panel
        out << m;
    }
    for (QVariant& v : out) {
        QVariantMap m = v.toMap();
        const QString f = m.value("miniatura").toString();
        if (f.isEmpty()) continue;
        const QString ruta = QString::fromStdWString((carpetaMiniaturas() / f.toStdWString()).wstring());
        if (!QFile::exists(ruta)) continue;
        m["miniatura_ruta"] = ruta;
        // La fecha en la URL: si se regraba, QML no muestra la foto vieja del caché.
        m["miniatura_url"] = QUrl::fromLocalFile(ruta).toString() + "?v=" + m.value("modificada").toString();
        v = m;
    }
    return out;
}

std::filesystem::path ControlCamaras::carpetaMiniaturas() const {
    return archivo_.parent_path().parent_path() / "Miniaturas" / "ptz";
}

QString ControlCamaras::capturarMiniatura(const QString& entrada, const QString& idONombre) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    Posicion* p = c->posiciones.buscar(idONombre);
    if (p == nullptr) return QCoreApplication::translate("ptz", "No hay una posición «%1» en «%2»").arg(idONombre, entrada);
    // El cuadro llega aparte (la interfaz no lo espera); lo que sigue, cuando llega.
    const std::string fuente = c->entrada;
    const QString posicion = p->id;
    QPointer<ControlCamaras> yo(this);
    rt_.engine().capturarAparte(
        fuente, 320, [yo, entrada, posicion](const std::string& e, std::vector<std::uint8_t> rgba, int w, int h) {
            QImage img;
            if (e.empty() && w > 0 && h > 0)
                img = QImage(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888).convertToFormat(QImage::Format_RGB888);
            QMetaObject::invokeMethod(
                qApp,
                [yo, entrada, posicion, img]() {
                    if (!yo) return;
                    const QString err = yo->guardarMiniatura(entrada, posicion, img);
                    if (!err.isEmpty()) emit yo->aviso(err);
                },
                Qt::QueuedConnection);
        });
    return {};
}

QString ControlCamaras::guardarMiniatura(const QString& entrada, const QString& idONombre, const QImage& img) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    Posicion* p = c->posiciones.buscar(idONombre);
    if (p == nullptr) return {}; // la borraron mientras tanto
    if (img.isNull()) return QCoreApplication::translate("ptz", "La entrada «%1» no da imagen ahora: la posición queda sin foto").arg(entrada);
    std::error_code ec;
    std::filesystem::create_directories(carpetaMiniaturas(), ec);
    const QString nombre = QString::fromStdString(nombreClave(c->entrada)) + "-" + p->id + ".jpg";
    const QString ruta = QString::fromStdWString((carpetaMiniaturas() / nombre.toStdWString()).wstring());
    if (!img.save(ruta, "JPG", 82)) return QCoreApplication::translate("ptz", "No se pudo guardar la foto en %1").arg(ruta);
    p->miniatura = nombre;
    p->modificada = Posiciones::ahora();
    guardarPronto();
    emit cambio();
    return {};
}
QString ControlCamaras::nombreFotoPreset(const Camara& c, bool digital, int n) const {
    return QString::fromStdString(nombreClave(c.entrada)) + (digital ? "-preset-dig-" : "-preset-cam-") +
           QString::number(n) + ".jpg";
}

QVariantMap ControlCamaras::datosPosicion(const QString& entrada, const QString& idONombre) const {
    auto it = camaras_.find(entrada.toStdString());
    if (it == camaras_.end()) return {{"existe", false}};
    const Camara& c = *it->second;
    const Posicion* p = c.posiciones.buscar(idONombre);
    if (p == nullptr || p->interna) return {{"existe", false}};
    return {{"existe", true},
            {"id", p->id},
            {"nombre", p->nombre},
            {"digital", p->digital},
            {"x", p->x},
            {"y", p->y},
            {"zoom", p->zoom},
            {"motorizada", c.hw != nullptr && p->tieneCamara()},
            {"recorteDigital", c.digital}}; // el filtro PTZ digital está puesto (hay toma entera)
}

QVariantList ControlCamaras::buscarPosicion(const QString& nombre) const {
    QVariantList out;
    const QString k = Posiciones::normalizar(nombre);
    if (k.isEmpty()) return out;
    for (const auto& [entrada, c] : camaras_)
        for (const Posicion& p : c->posiciones.lista())
            if (!p.interna && Posiciones::normalizar(p.nombre) == k)
                out << QVariantMap{{"entrada", qs(entrada)}, {"id", p.id}, {"nombre", p.nombre}};
    return out;
}

bool ControlCamaras::soloDigital(const QString& entrada) const {
    auto it = camaras_.find(entrada.toStdString());
    return it != camaras_.end() && it->second->hw == nullptr;
}

QString ControlCamaras::nombreLibre(const Camara& c, const QString& base) const {
    QString n = base;
    for (int i = 2; !c.posiciones.validarNombre(n).isEmpty(); ++i) n = QStringLiteral("%1 (%2)").arg(base).arg(i);
    return n;
}

bool ControlCamaras::migrarPresets(Camara& c, const QJsonObject& digitales) {
    bool hubo = false;
    const auto foto = [&](bool digital, int n) {
        const QString f = nombreFotoPreset(c, digital, n);
        std::error_code ec;
        return std::filesystem::exists(carpetaMiniaturas() / f.toStdWString(), ec) ? f : QString();
    };
    // Los digitales (x, y, zoom del recorte).
    for (auto it = digitales.begin(); it != digitales.end(); ++it) {
        const int n = it.key().toInt();
        const QJsonObject d = it.value().toObject();
        if (n < 1 || c.posiciones.porNumero(n) != nullptr) continue;
        Posicion p;
        p.numero = n;
        p.nombre = nombreLibre(c, QCoreApplication::translate("ptz", "Preset %1").arg(n));
        p.digital = true;
        p.x = d.value("x").toDouble(0.5);
        p.y = d.value("y").toDouble(0.5);
        p.zoom = d.value("zoom").toDouble(1.0);
        p.miniatura = foto(true, n);
        c.posiciones.agregar(p);
        hubo = true;
    }
    // Los de la cámara (VISCA, Panasonic: la memoria es el número) que tenían foto:
    // la foto dice que se grabaron desde acá.
    if (c.backend == "visca" || c.backend == "panasonic") {
        const QString prefijo = QString::fromStdString(nombreClave(c.entrada)) + "-preset-cam-";
        std::error_code ec;
        for (std::filesystem::directory_iterator it(carpetaMiniaturas(), ec), fin; !ec && it != fin; it.increment(ec)) {
            const QString f = QString::fromStdWString(it->path().filename().wstring());
            if (!f.startsWith(prefijo) || !f.endsWith(".jpg")) continue;
            const int n = f.mid(prefijo.size(), f.size() - prefijo.size() - 4).toInt();
            if (n < 1 || c.posiciones.porNumero(n) != nullptr) continue;
            Posicion p;
            p.numero = n;
            p.nombre = nombreLibre(c, QCoreApplication::translate("ptz", "Preset %1").arg(n));
            p.backend = c.backend;
            p.memoria = n;
            p.miniatura = f;
            c.posiciones.agregar(p);
            hubo = true;
        }
    }
    return hubo;
}

void ControlCamaras::importarPresetsDeLaCamara(Camara& c) {
    if (!c.hw) return;
    bool hubo = false;
    for (const auto& [token, nombre] : c.hw->presetsConNombre()) {
        if (token.isEmpty()) continue;
        const bool ya = std::any_of(c.posiciones.lista().begin(), c.posiciones.lista().end(),
                                    [&](const Posicion& p) { return p.token == token; });
        if (ya) continue;
        Posicion p;
        p.nombre = nombreLibre(c, nombre.trimmed().isEmpty() ? QCoreApplication::translate("ptz", "Preset de la cámara") : nombre.trimmed());
        p.comentario = QCoreApplication::translate("ptz", "Traído de la cámara");
        p.backend = c.backend;
        p.token = token;
        c.posiciones.agregar(p);
        hubo = true;
    }
    if (hubo) guardarPronto();
}

void ControlCamaras::miniaturaPronto(const QString& entrada, const QString& id) {
    // El recorte digital se mueve en ~0,3 s; la cabeza motorizada tarda más,
    // pero al registrar ya está quieta (se registra donde está).
    QTimer::singleShot(600, this, [this, entrada, id]() { capturarMiniatura(entrada, id); });
}

QString ControlCamaras::registrar(Camara& c, Posicion& p, QString& aviso) {
    // La definición de la entrada ahora.
    int w = 0, h = 0;
    rt_.engine().sourceSize(c.entrada, w, h);
    p.ancho = w;
    p.alto = h;
    // El recorte digital.
    p.digital = false;
    if (c.digital && c.dig) {
        const QVariantMap d = c.dig->posicion();
        if (!d.isEmpty()) {
            p.digital = true;
            p.x = d["x"].toDouble();
            p.y = d["y"].toDouble();
            p.zoom = d["zoom"].toDouble();
        }
    }
    // La cabeza: se graba en una memoria de la cámara.
    QStringList faltas;
    if (c.hw) {
        if (!c.hw->listo()) {
            faltas << QCoreApplication::translate("ptz", "la cámara no contesta: no se grabó en ella");
        } else if (c.hw->memorias() <= 0) {
            faltas << QCoreApplication::translate("ptz", "la cámara no tiene memorias");
        } else if (c.hw->usaTokens()) {
            p.backend = c.backend;
            p.memoria = 0;
            const QString entrada = qs(c.entrada), id = p.id;
            // ONVIF: la cámara contesta el token después.
            c.hw->grabarPosicion(0, p.nombre, [this, entrada, id](bool ok, QString token) {
                auto it = camaras_.find(entrada.toStdString());
                if (it == camaras_.end()) return;
                if (Posicion* q = it->second->posiciones.buscar(id)) {
                    q->token = ok ? token : QString();
                    if (!ok) q->backend.clear();
                    guardarPronto();
                    emit cambio();
                }
            });
        } else {
            const int m = p.memoria > 0 ? p.memoria : c.posiciones.memoriaLibre(c.hw->memorias());
            if (m <= 0) {
                faltas << QCoreApplication::translate("ptz", "no quedan memorias libres en la cámara");
            } else {
                p.backend = c.backend;
                p.memoria = m;
                c.hw->grabarPosicion(m, p.nombre, [](bool, QString) {});
            }
        }
    }
    aviso = faltas.join("; ");
    if (!p.digital && !p.tieneCamara() && !(c.hw && c.hw->usaTokens() && c.hw->listo()))
        return QCoreApplication::translate("ptz", "No hay nada que registrar: prendé el PTZ digital o configurá la cámara (%1)")
            .arg(aviso);
    return {};
}

QVariantMap ControlCamaras::crearPosicion(const QString& entrada, const QString& nombre,
                                          const QString& comentario, double velocidad, double segundos, int numero) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return {{"ok", false}, {"error", error}};
    error = c->posiciones.validarNombre(nombre);
    if (!error.isEmpty()) return {{"ok", false}, {"error", error}};
    Posicion nueva;
    nueva.nombre = nombre.trimmed();
    nueva.comentario = comentario.trimmed();
    nueva.velocidad = qBound(0.0, velocidad, 1.0);
    nueva.segundos = segundos < 0 ? -1 : qMin(segundos, 10.0);
    nueva.numero = numero; // ocupado o 0: el primero libre (agregar)
    Posicion& p = c->posiciones.agregar(nueva);
    QString aviso;
    error = registrar(*c, p, aviso);
    const QString id = p.id;
    if (!error.isEmpty()) {
        c->posiciones.borrar(id);
        return {{"ok", false}, {"error", error}};
    }
    guardarPronto();
    emit cambio();
    miniaturaPronto(entrada, id);
    return {{"ok", true}, {"id", id}, {"aviso", aviso}};
}

QVariantMap ControlCamaras::editarPosicion(const QString& entrada, const QString& idONombre,
                                           const QVariantMap& cambios) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return {{"ok", false}, {"error", error}};
    Posicion* p = c->posiciones.buscar(idONombre);
    if (p == nullptr)
        return {{"ok", false},
                {"error", QCoreApplication::translate("ptz", "No hay una posición «%1» en «%2»").arg(idONombre, entrada)}};
    // Todo revisado antes de cambiar nada (con el número mal, el nombre quedaba cambiado igual).
    const bool cambiaNombre = cambios.contains("nombre") && cambios["nombre"].toString().trimmed() != p->nombre;
    if (cambiaNombre) {
        error = c->posiciones.validarNombre(cambios["nombre"].toString(), p->id);
        if (!error.isEmpty()) return {{"ok", false}, {"error", error}};
    }
    const bool cambiaNumero = cambios.contains("numero") && cambios["numero"].toInt() != p->numero;
    if (cambiaNumero) {
        const int n = cambios["numero"].toInt();
        if (n < 1) return {{"ok", false}, {"error", QCoreApplication::translate("ptz", "El número de preset va desde 1")}};
        if (const Posicion* otra = c->posiciones.porNumero(n))
            return {{"ok", false},
                    {"error", QCoreApplication::translate("ptz", "El número %1 ya es de «%2»").arg(n).arg(otra->nombre)}};
    }
    if (cambiaNombre) p->nombre = cambios["nombre"].toString().trimmed();
    if (cambios.contains("comentario")) p->comentario = cambios["comentario"].toString().trimmed();
    if (cambiaNumero) p->numero = cambios["numero"].toInt();
    if (cambios.contains("velocidad")) p->velocidad = qBound(0.0, cambios["velocidad"].toDouble(), 1.0);
    if (cambios.contains("segundos"))
        p->segundos = cambios["segundos"].toDouble() < 0 ? -1 : qMin(cambios["segundos"].toDouble(), 10.0);
    QString aviso;
    if (cambios.value("regrabar").toBool()) {
        // Con ONVIF se borra la memoria vieja y se graba una nueva.
        if (c->hw && c->hw->usaTokens() && !p->token.isEmpty()) {
            c->hw->borrarPosicion(0, p->token);
            p->token.clear();
        }
        Posicion copia = *p;
        error = registrar(*c, copia, aviso);
        if (!error.isEmpty()) return {{"ok", false}, {"error", error}};
        // registrar() pudo dejar pendiente el token: se copia sin pisarlo.
        Posicion* q = c->posiciones.buscar(copia.id);
        const QString token = q->token;
        *q = copia;
        if (q->token.isEmpty()) q->token = token;
        p = q;
    }
    p->modificada = Posiciones::ahora();
    guardarPronto();
    emit cambio();
    if (cambios.value("regrabar").toBool() || cambios.value("miniatura").toBool()) miniaturaPronto(entrada, p->id);
    return {{"ok", true}, {"aviso", aviso}};
}

QString ControlCamaras::borrarPosicion(const QString& entrada, const QString& idONombre) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    const Posicion* p = c->posiciones.buscar(idONombre);
    if (p == nullptr) return QCoreApplication::translate("ptz", "No hay una posición «%1» en «%2»").arg(idONombre, entrada);
    // Libera la memoria en la cámara (si es la misma cámara que la grabó).
    if (c->hw && p->tieneCamara() && p->backend == c->backend) c->hw->borrarPosicion(p->memoria, p->token);
    if (!p->miniatura.isEmpty()) {
        std::error_code ec;
        std::filesystem::remove(carpetaMiniaturas() / p->miniatura.toStdWString(), ec);
    }
    c->posiciones.borrar(p->id);
    guardarPronto();
    emit cambio();
    return {};
}

QVariantMap ControlCamaras::irAPosicion(const QString& entrada, const QString& idONombre, double segundos) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return {{"ok", false}, {"error", error}};
    const Posicion* p = c->posiciones.buscar(idONombre);
    if (p == nullptr) {
        QStringList nombres;
        for (const Posicion& x : c->posiciones.lista())
            if (!x.interna) nombres << x.nombre;
        return {{"ok", false},
                {"error", QCoreApplication::translate("ptz", "No hay una posición «%1» en «%2» (hay: %3)")
                              .arg(idONombre, entrada,
                                   nombres.isEmpty() ? QStringLiteral("ninguna") : nombres.join(", "))}};
    }
    ++c->movimientos;
    if (dejarDeSeguir_) dejarDeSeguir_(entrada); // a pedido: el seguimiento no le pelea
    QStringList avisos;
    bool algo = false;
    if (p->tieneCamara()) {
        if (!c->hw || c->backend != p->backend) {
            avisos << QCoreApplication::translate("ptz", "se grabó con el backend %1 y la cámara ahora tiene %2: la cabeza no se mueve")
                          .arg(p->backend, c->backend);
        } else {
            if (!c->hw->listo()) avisos << QCoreApplication::translate("ptz", "la cámara no contesta");
            c->hw->irAPosicionCon(p->memoria, p->token, p->velocidad);
            algo = true;
        }
    }
    if (p->digital) {
        if (!c->digital || !c->dig) {
            avisos << QCoreApplication::translate("ptz", "el PTZ digital está apagado: el recorte no se aplica");
        } else if (c->dig->ir(p->x, p->y, p->zoom,
                              qBound(0.0, segundos >= 0 ? segundos : (p->segundos >= 0 ? p->segundos : 1.0), 10.0))) {
            algo = true;
        }
    }
    int w = 0, h = 0;
    rt_.engine().sourceSize(c->entrada, w, h);
    if (p->ancho > 0 && w > 0 && (p->ancho != w || p->alto != h))
        avisos << QCoreApplication::translate("ptz", "se registró en %1 y la entrada ahora es %2x%3 (el recorte es proporcional: "
                                 "cae en el mismo lugar)")
                      .arg(p->definicion())
                      .arg(w)
                      .arg(h);
    if (!algo)
        return {{"ok", false},
                {"error", QCoreApplication::translate("ptz", "No se pudo ir a «%1»: %2").arg(p->nombre, avisos.join("; "))}};
    guardarPronto();
    emit cambio();
    return {{"ok", true}, {"nombre", p->nombre}, {"aviso", avisos.join("; ")}};
}

} // namespace ba::app::ptz
