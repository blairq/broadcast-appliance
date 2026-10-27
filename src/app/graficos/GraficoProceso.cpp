// ============================================================================
// src/app/graficos/GraficoProceso.cpp — ver GraficoProceso.hpp
// ============================================================================
#include "graficos/GraficoProceso.hpp"
#include "ajustes/Ajustes.hpp"
#include "platform/Segmento.hpp"

#include <cstring>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>

namespace ba::app::graficos {
namespace {

/// Un gráfico normal se dibuja en decenas de milisegundos. Este límite no es
/// para los lentos: es para los que NO VAN A TERMINAR (un bucle infinito en
/// el QML que alguien está escribiendo). Generoso a propósito, porque una tira
/// de doce cuadros con desenfoque sí puede tardar.
inline int kLimiteMs() { return ba::ajustes::sAMs("graficos.limiteS", 20); }

QImage dePngBase64(const QString& b64) {
    QImage img;
    img.loadFromData(QByteArray::fromBase64(b64.toLatin1()), "PNG");
    return img;
}

/// Donde el bundle deja `ba-grafico`: al lado de la propia app.
QString binarioPorDefecto() {
    const QString dir = QCoreApplication::applicationDirPath();
    for (const QString& c : {dir + "/ba-grafico", dir + "/ba-grafico.exe"})
        if (QFileInfo::exists(c)) return c;
    return dir + "/ba-grafico";
}

} // namespace

struct GraficoProceso::Impl {
    QString binario;
    std::unique_ptr<QProcess> proc;

    /// Levanta el proceso si hace falta. Devuelve el error o "".
    QString asegurar() {
        if (proc && proc->state() == QProcess::Running) return {};
        proc = std::make_unique<QProcess>();
        proc->setProgram(binario);
        proc->setProcessChannelMode(QProcess::SeparateChannels);
        proc->start();
        if (!proc->waitForStarted(5000)) {
            const QString e = QCoreApplication::translate("graficos", "no se pudo arrancar el renderizador de gráficos (%1)").arg(binario);
            proc.reset();
            return e;
        }
        return {};
    }

    /// Manda un pedido y espera la respuesta. Si el proceso no contesta a
    /// tiempo lo mata: el pedido siguiente arranca uno limpio.
    QJsonObject pedir(const QJsonObject& pedido, QString* error) {
        const auto fallar = [error](const QString& e) {
            if (error != nullptr) *error = e;
            return QJsonObject{};
        };
        const QString arranque = asegurar();
        if (!arranque.isEmpty()) return fallar(arranque);

        proc->write(QJsonDocument(pedido).toJson(QJsonDocument::Compact) + "\n");
        if (!proc->waitForBytesWritten(3000)) return fallar(QCoreApplication::translate("graficos", "el renderizador no aceptó el pedido"));

        QByteArray linea;
        while (!linea.endsWith('\n')) {
            if (!proc->waitForReadyRead(kLimiteMs())) {
                // No contestó: o se colgó (bucle infinito en el gráfico) o se
                // murió. En los dos casos se lo mata y se dice qué pasó.
                const bool vivo = proc->state() == QProcess::Running;
                proc->kill();
                proc->waitForFinished(2000);
                proc.reset();
                return fallar(vivo ? QCoreApplication::translate("graficos", "el gráfico se colgó (¿un bucle infinito?): se cortó el dibujo")
                                   : QCoreApplication::translate("graficos", "el renderizador de gráficos se murió"));
            }
            linea += proc->readLine();
        }

        const QJsonDocument doc = QJsonDocument::fromJson(linea);
        if (!doc.isObject()) return fallar(QCoreApplication::translate("graficos", "el renderizador contestó algo que no entiendo"));
        const QJsonObject r = doc.object();
        if (!r.value("ok").toBool())
            return fallar(r.value("error").toString(QCoreApplication::translate("graficos", "el renderizador falló sin decir por qué")));
        if (error != nullptr) error->clear();
        return r;
    }
};

GraficoProceso::GraficoProceso(QString binario, QObject* padre)
    : QObject(padre), d_(std::make_unique<Impl>()) {
    d_->binario = binario.isEmpty() ? binarioPorDefecto() : std::move(binario);
}

GraficoProceso::~GraficoProceso() { detener(); }

bool GraficoProceso::disponible() const { return QFileInfo(d_->binario).isExecutable(); }
QString GraficoProceso::binario() const { return d_->binario; }

void GraficoProceso::detener() {
    if (!d_->proc) return;
    // Se le pide salir por el mismo canal; si no hace caso, se lo mata.
    d_->proc->write("{\"op\":\"salir\"}\n");
    d_->proc->waitForBytesWritten(500);
    if (!d_->proc->waitForFinished(2000)) d_->proc->kill();
    d_->proc.reset();
}

QString GraficoProceso::validar(const QJsonObject& pedido) {
    QJsonObject p = pedido;
    p["op"] = "validar";
    QString error;
    d_->pedir(p, &error);
    return error;
}

QImage GraficoProceso::dibujar(QJsonObject pedido, int ms, QString* error) {
    pedido["op"] = "dibujar";
    pedido["ms"] = ms;
    const QJsonObject r = d_->pedir(pedido, error);
    if (r.isEmpty()) return {};
    return dePngBase64(r.value("png").toString());
}

QVector<Cuadro> GraficoProceso::tira(QJsonObject pedido, const QVector<int>& momentos, QString* error) {
    pedido["op"] = "tira";
    QJsonArray ms;
    for (const int m : momentos)
        ms.append(m);
    pedido["momentos"] = ms;
    // Los píxeles crudos por memoria compartida (ver src/grafico/main.cpp): sin PNG
    // ni base64, que se comían el tiempo de una animación al aire.
    pedido["shm"] = true;

    const QJsonObject r = d_->pedir(pedido, error);
    QVector<Cuadro> out;
    if (r.contains("shm")) {
        const int ancho = r.value("ancho").toInt(), alto = r.value("alto").toInt(), bpl = r.value("bpl").toInt();
        const QJsonArray cuadros = r.value("cuadros").toArray();
        const qsizetype porCuadro = static_cast<qsizetype>(bpl) * alto;
        ba::platform::Segmento seg;
        std::string err;
        if (ancho <= 0 || alto <= 0 || bpl < ancho * 4 || !seg.abrir(r.value("shm").toString().toStdString(), &err) ||
            seg.tam() < static_cast<size_t>(porCuadro * cuadros.size())) {
            if (error != nullptr)
                *error = QCoreApplication::translate("graficos", "no se pudieron leer los cuadros del renderizador: %1")
                             .arg(QString::fromStdString(err));
            return out;
        }
        const auto* base = static_cast<const uchar*>(seg.datos());
        for (qsizetype i = 0; i < cuadros.size(); ++i) {
            QImage img(ancho, alto, QImage::Format_ARGB32_Premultiplied);
            const uchar* src = base + porCuadro * i;
            for (int y = 0; y < alto; ++y)
                std::memcpy(img.scanLine(y), src + static_cast<qsizetype>(bpl) * y, static_cast<size_t>(ancho) * 4);
            out.append(Cuadro{cuadros.at(i).toInt(), std::move(img)});
        }
        return out;
    }
    for (const QJsonValue& v : r.value("cuadros").toArray()) {
        const QJsonObject c = v.toObject();
        out.append(Cuadro{c.value("ms").toInt(), dePngBase64(c.value("png").toString())});
    }
    return out;
}

QJsonObject GraficoProceso::repertorio(QString* error) {
    const QJsonObject r = d_->pedir(QJsonObject{{"op", "repertorio"}}, error);
    return r.value("repertorio").toObject();
}

} // namespace ba::app::graficos
