// ============================================================================
// src/app/ptz/ControlCamarasFoto.cpp — la foto del panel PTZ y apuntar haciendo click en ella
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
// La foto del panel PTZ
// ---------------------------------------------------------------------------

QVariantMap ControlCamaras::sacarFoto(const QString& entrada) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return {{"ok", false}, {"error", error}};
    QString aviso;
    // Dónde está la cabeza, en una memoria de la cámara (una posición interna que se
    // regraba con cada foto): tocar la foto vuelve ahí antes de apuntar.
    c->foto.idMemoria.clear();
    if (c->hw) {
        Posicion* p = nullptr;
        for (const Posicion& x : c->posiciones.lista())
            if (x.interna) p = c->posiciones.buscar(x.id);
        if (p == nullptr) {
            Posicion n;
            n.nombre = kNombreFoto;
            n.interna = true;
            p = &c->posiciones.agregar(n);
        } else if (c->hw->usaTokens() && !p->token.isEmpty()) {
            c->hw->borrarPosicion(0, p->token); // ONVIF: se borra la vieja y se graba otra
            p->token.clear();
        }
        Posicion copia = *p;
        const QString e = registrar(*c, copia, aviso);
        Posicion* q = c->posiciones.buscar(copia.id);
        if (e.isEmpty() && q != nullptr) {
            const QString token = q->token; // registrar() pudo dejarlo pendiente: no se pisa
            *q = copia;
            if (q->token.isEmpty()) q->token = token;
            if (q->tieneCamara() || c->hw->usaTokens()) c->foto.idMemoria = q->id; // (ONVIF: el token llega después)
            guardarPronto();
        } else {
            aviso = QCoreApplication::translate("ptz", "La foto queda sin memoria de la cámara (%1): tocarla sirve mientras no se la mueva.")
                        .arg(e.isEmpty() ? QCoreApplication::translate("ptz", "no contestó") : e);
        }
    }
    c->foto.movimientos = c->movimientos;
    c->foto.cuando = Posiciones::ahora();
    // La imagen llega aparte; la interfaz se entera con cambio().
    const std::string fuente = c->entrada;
    capturar(fuente, 1280, [this, entrada](const QImage& img) {
        Camara* c = camara(entrada, nullptr);
        if (c == nullptr) return;
        if (img.isNull()) {
            emit this->aviso(QCoreApplication::translate("ptz", "La entrada «%1» no da imagen ahora: no hay foto").arg(entrada));
            return;
        }
        std::error_code ec;
        std::filesystem::create_directories(carpetaMiniaturas(), ec);
        const QString nombre = QString::fromStdString(nombreClave(c->entrada)) + "-foto.jpg";
        const QString ruta = QString::fromStdWString((carpetaMiniaturas() / nombre.toStdWString()).wstring());
        if (!img.save(ruta, "JPG", 85)) {
            emit this->aviso(QCoreApplication::translate("ptz", "No se pudo guardar la foto en %1").arg(ruta));
            return;
        }
        c->foto.archivo = nombre;
        ++c->foto.rev;
        emit cambio();
    });
    return {{"ok", true}, {"aviso", aviso}};
}

QVariantMap ControlCamaras::foto(const QString& entrada) const {
    auto it = camaras_.find(entrada.toStdString());
    if (it == camaras_.end() || it->second->foto.archivo.isEmpty()) return {{"hay", false}};
    const Camara& c = *it->second;
    const QString ruta = QString::fromStdWString((carpetaMiniaturas() / c.foto.archivo.toStdWString()).wstring());
    if (!QFile::exists(ruta)) return {{"hay", false}};
    return {{"hay", true},
            {"url", QUrl::fromLocalFile(ruta).toString() + "?v=" + QString::number(c.foto.rev)},
            {"conMemoria", !c.foto.idMemoria.isEmpty()},
            {"movida", c.movimientos != c.foto.movimientos},
            {"cuando", c.foto.cuando}};
}

QString ControlCamaras::apuntarEnFoto(const QString& entrada, double u, double v, double ancho, double alto) {
    QString error;
    Camara* c = camara(entrada, &error);
    if (c == nullptr) return error;
    if (c->foto.archivo.isEmpty()) return QCoreApplication::translate("ptz", "Primero sacá una foto");
    // Quieta desde la foto: la foto ES lo que ve; se apunta ya.
    if (c->movimientos == c->foto.movimientos) {
        const QString e = apuntar(entrada, u, v, ancho, alto);
        c->foto.movimientos = -1; // ya no mira lo de la foto
        return e;
    }
    if (c->foto.idMemoria.isEmpty())
        return QCoreApplication::translate("ptz", "La cámara se movió desde la foto y no tiene una memoria para volver: sacá otra foto.");
    const QVariantMap r = irAPosicion(entrada, c->foto.idMemoria, -1);
    if (!r.value("ok").toBool()) return r.value("error").toString();
    // Cuando llegó (la cabeza no avisa: se espera lo que tarda una típica), se apunta.
    // Si en el medio se la movió de otra forma, eso gana.
    const int marca = c->movimientos;
    QPointer<ControlCamaras> yo(this);
    QTimer::singleShot(ba::ajustes::entero("ptz.fotoVolverMs", 2500), this, [yo, entrada, u, v, ancho, alto, marca]() {
        if (!yo) return;
        Camara* c = yo->camara(entrada, nullptr);
        if (c == nullptr || c->movimientos != marca) return;
        const QString e = yo->apuntar(entrada, u, v, ancho, alto);
        c->foto.movimientos = -1;
        if (!e.isEmpty()) emit yo->aviso(e);
    });
    return {};
}


} // namespace ba::app::ptz
