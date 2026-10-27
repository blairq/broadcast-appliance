// SPDX-License-Identifier: GPL-2.0-or-later
#include "i18n/I18n.hpp"
#include "control/SatelliteServidor.hpp"

#include "control/Botonera.hpp"
#include "control/CarasBotonera.hpp"

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QHostAddress>
#include <QImage>
#include <QPainter>
#include <QTcpServer>
#include <QTcpSocket>

#include <fstream>

namespace ba::app::control {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

/// La IP como texto, sin el «::ffff:» de IPv4 mapeada.
std::string ipDe(const QTcpSocket* s) {
    QHostAddress a = s->peerAddress();
    bool ok = false;
    const quint32 v4 = a.toIPv4Address(&ok);
    if (ok) return QHostAddress(v4).toString().toStdString();
    return a.toString().toStdString();
}

bool ipLocal(const std::string& ip) {
    const QHostAddress a(QString::fromStdString(ip));
    return a.isLoopback() || a.isPrivateUse() || a.isLinkLocal() || a.isUniqueLocalUnicast();
}

QColor colorDe(const json& v, const QColor& siNo) {
    if (!v.is_string()) return siNo;
    const QColor c(QString::fromStdString(v.get<std::string>()));
    return c.isValid() ? c : siNo;
}

} // namespace

std::map<std::string, std::string> SatelliteServidor::argumentos(const std::string& l, std::string* comando) {
    std::map<std::string, std::string> out;
    std::size_t i = 0;
    while (i < l.size() && l[i] != ' ') ++i;
    if (comando != nullptr) *comando = l.substr(0, i);
    while (i < l.size()) {
        while (i < l.size() && l[i] == ' ') ++i;
        const std::size_t ini = i;
        while (i < l.size() && l[i] != '=' && l[i] != ' ') ++i;
        const std::string k = l.substr(ini, i - ini);
        std::string v = "true";
        if (i < l.size() && l[i] == '=') {
            ++i;
            if (i < l.size() && l[i] == '"') {
                ++i;
                v.clear();
                while (i < l.size() && l[i] != '"') {
                    if (l[i] == '\\' && i + 1 < l.size()) ++i;
                    v += l[i++];
                }
                ++i;
            } else {
                const std::size_t vi = i;
                while (i < l.size() && l[i] != ' ') ++i;
                v = l.substr(vi, i - vi);
            }
        }
        if (!k.empty()) out[k] = v;
    }
    return out;
}

SatelliteServidor::SatelliteServidor(const fs::path& datos, Botonera& botonera, QObject* parent)
    : QObject(parent), archivo_(datos / "Config" / "botonera-satellite.json"), datos_(datos), bot_(botonera) {
    cargar();
    connect(&titilar_, &QTimer::timeout, this, [this]() {
        fase_ = !fase_;
        for (auto& [s, c] : clientes_)
            for (auto& [id, a] : c.aparatos) dibujar(c, a, false);
    });
    titilar_.start(500);
    connect(&animar_, &QTimer::timeout, this, [this]() {
        for (auto& [s, c] : clientes_)
            for (auto& [id, a] : c.aparatos)
                if (a.ultimo.contains("celdas") &&
                    std::any_of(a.ultimo["celdas"].begin(), a.ultimo["celdas"].end(),
                                [](const json& cel) { return cel.contains("cara") && cel["cara"].value("cuadros", 1) > 1; }))
                    dibujar(c, a, false);
    });
    animar_.start(125);
    if (activo_) abrir();
}

SatelliteServidor::~SatelliteServidor() { cerrar(); }

void SatelliteServidor::cargar() {
    std::ifstream f(archivo_);
    const json j = json::parse(f, nullptr, false);
    if (!j.is_object()) return;
    activo_ = j.value("activo", false);
    puerto_ = j.value("puerto", 16622);
    for (const auto& ip : j.value("ip_permitidas", json::array()))
        if (ip.is_string()) permitidas_.insert(ip.get<std::string>());
}

void SatelliteServidor::guardar() const {
    std::error_code ec;
    fs::create_directories(archivo_.parent_path(), ec);
    json ips = json::array();
    for (const auto& ip : permitidas_) ips.push_back(ip);
    std::ofstream o(archivo_);
    o << json{{"activo", activo_}, {"puerto", puerto_}, {"ip_permitidas", ips}}.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
}

QStringList SatelliteServidor::ipPermitidas() const {
    QStringList l;
    for (const auto& ip : permitidas_) l << QString::fromStdString(ip);
    return l;
}

QStringList SatelliteServidor::pendientes() const {
    QStringList l;
    for (const auto& [ip, p] : pendientes_) l << QString::fromStdString(ip);
    return l;
}

QVariantList SatelliteServidor::solicitudesUi() const {
    QVariantList l;
    for (const auto& [ip, p] : pendientes_)
        l << QVariantMap{{"id", QString::fromStdString(ip)},
                         {"ip", QString::fromStdString(ip)},
                         {"nombre", QString::fromStdString(p.aparato)},
                         {"ms", static_cast<double>(p.ms)}};
    return l;
}

bool SatelliteServidor::permitida(const std::string& ip) const {
    return ipLocal(ip) && (permitidas_.count(ip) > 0 || QHostAddress(QString::fromStdString(ip)).isLoopback());
}

QVariantList SatelliteServidor::clientesUi() const {
    QVariantList out;
    for (const auto& [s, c] : clientes_) {
        QStringList aps;
        for (const auto& [id, a] : c.aparatos) aps << QString::fromStdString(id) + " (" + QString::number(a.total) + " teclas)";
        out.append(QVariantMap{{"ip", QString::fromStdString(c.ip)}, {"aparatos", aps.join(", ")}});
    }
    return out;
}

json SatelliteServidor::estado() const {
    json cs = json::array();
    for (const auto& [s, c] : clientes_) {
        json aps = json::array();
        for (const auto& [id, a] : c.aparatos)
            aps.push_back({{"deviceid", id}, {"teclas", a.total}, {"por_fila", a.porFila}, {"bitmaps", a.bitmaps},
                           {"dispositivo", idApp(c, id)}});
        cs.push_back({{"ip", c.ip}, {"aparatos", aps}});
    }
    json p = json::array(), pe = json::array();
    for (const auto& ip : permitidas_) p.push_back(ip);
    for (const auto& [ip, pd] : pendientes_) pe.push_back(ip);
    return {{"activo", activo_}, {"escuchando", srv_ && srv_->isListening()}, {"puerto", puerto_},
            {"ip_permitidas", p}, {"pendientes", pe}, {"clientes", cs}, {"error", error_.toStdString()}};
}

QString SatelliteServidor::configurar(bool activo, int puerto) {
    if (puerto < 1024 || puerto > 65535) return tr("puerto entre 1024 y 65535");
    const bool reabrir = activo_ != activo || puerto_ != puerto;
    activo_ = activo;
    puerto_ = puerto;
    guardar();
    if (reabrir) {
        cerrar();
        if (activo_) abrir();
    }
    bot_.auditar("escritorio", "satellite",
                 activo_ ? tr("prendido en %1").arg(puerto_).toStdString() : tr("apagado").toStdString());
    emit cambio();
    return error_;
}

void SatelliteServidor::permitir(const QString& ip, bool si) {
    const std::string i = ip.trimmed().toStdString();
    if (i.empty()) return;
    if (si) {
        permitidas_.insert(i);
        pendientes_.erase(i);
        rechazadas_.erase(i);
    } else {
        permitidas_.erase(i);
        // Lo que estaba conectado desde esa IP se corta.
        for (auto& [s, c] : clientes_)
            if (c.ip == i) s->disconnectFromHost();
    }
    guardar();
    bot_.auditar("escritorio", si ? "satellite_permitir" : "satellite_quitar", i);
    emit cambio();
}

void SatelliteServidor::rechazar(const QString& ip) {
    const std::string i = ip.trimmed().toStdString();
    if (i.empty() || pendientes_.erase(i) == 0) return;
    rechazadas_.insert(i);
    for (auto& [s, c] : clientes_)
        if (c.ip == i) s->disconnectFromHost();
    bot_.auditar("escritorio", "satellite_rechazar", i);
    emit cambio();
}

void SatelliteServidor::setIpPermitidas(const QStringList& ips) {
    std::set<std::string> nuevas;
    for (const QString& ip : ips)
        if (!ip.trimmed().isEmpty()) nuevas.insert(ip.trimmed().toStdString());
    for (const auto& vieja : permitidas_)
        if (!nuevas.count(vieja)) permitir(QString::fromStdString(vieja), false);
    for (const auto& n : nuevas) permitir(QString::fromStdString(n), true);
}

void SatelliteServidor::abrir() {
    error_.clear();
    srv_ = std::make_unique<QTcpServer>();
    connect(srv_.get(), &QTcpServer::newConnection, this, &SatelliteServidor::nueva);
    if (!srv_->listen(QHostAddress::Any, static_cast<quint16>(puerto_))) {
        error_ = tr("no pude abrir el puerto %1: %2").arg(puerto_).arg(srv_->errorString());
        srv_.reset();
    }
}

void SatelliteServidor::cerrar() {
    for (auto& [s, c] : clientes_) {
        for (const auto& [id, a] : c.aparatos) bot_.quitarLocal(idApp(c, id));
        s->disconnect(this);
        s->abort();
        s->deleteLater();
    }
    clientes_.clear();
    srv_.reset();
}

void SatelliteServidor::nueva() {
    while (srv_ && srv_->hasPendingConnections()) {
        QTcpSocket* s = srv_->nextPendingConnection();
        const std::string ip = ipDe(s);
        if (!ipLocal(ip)) {
            bot_.auditar(ip, "satellite_rechazado", tr("fuera de la red local").toStdString());
            s->abort();
            s->deleteLater();
            continue;
        }
        Cliente& c = clientes_[s];
        c.s = s;
        c.ip = ip;
        connect(s, &QTcpSocket::readyRead, this, [this, s]() { leer(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s]() {
            auto it = clientes_.find(s);
            if (it == clientes_.end()) return;
            for (const auto& [id, a] : it->second.aparatos) bot_.quitarLocal(idApp(it->second, id));
            bot_.auditar(it->second.ip, "satellite_desconectado", "");
            clientes_.erase(it);
            s->deleteLater();
            emit cambio();
        });
        // Nos presentamos como un Companion con la API 1.7 (modo simple).
        enviar(c, "BEGIN CompanionVersion=3.4.0 ApiVersion=1.7.0");
        bot_.auditar(ip, "satellite_conectado", "");
        emit cambio();
    }
}

void SatelliteServidor::enviar(Cliente& c, const std::string& l) {
    if (c.s != nullptr) c.s->write(QByteArray::fromStdString(l + "\n"));
}

void SatelliteServidor::leer(QTcpSocket* s) {
    auto it = clientes_.find(s);
    if (it == clientes_.end()) return;
    Cliente& c = it->second;
    c.buf += s->readAll();
    if (c.buf.size() > 1 << 20) { // nadie manda un mega sin cortar línea
        s->abort();
        return;
    }
    int nl;
    while ((nl = c.buf.indexOf('\n')) >= 0) {
        std::string l = c.buf.left(nl).toStdString();
        c.buf.remove(0, nl + 1);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty()) linea(c, l);
        if (clientes_.find(s) == clientes_.end()) return;
    }
}

void SatelliteServidor::linea(Cliente& c, const std::string& l) {
    std::string cmd;
    const auto a = argumentos(l, &cmd);
    const auto arg = [&](const char* k, const std::string& siNo = {}) {
        auto it = a.find(k);
        return it != a.end() ? it->second : siNo;
    };
    const auto verdad = [&](const char* k, bool siNo) {
        const std::string v = arg(k);
        if (v.empty()) return siNo;
        return v == "true" || v == "1" || v == "hex" || v == "rgb";
    };
    const std::string dev = arg("DEVICEID");
    if (cmd == "PING") return enviar(c, "PONG " + l.substr(std::min(l.size(), std::size_t(5))));
    if (cmd == "PONG") return;
    if (cmd == "QUIT") {
        c.s->disconnectFromHost();
        return;
    }
    if (cmd == "ADD-DEVICE") {
        // El MESSAGE lo muestra Companion: va traducido (sin comillas adentro).
        if (dev.empty() || dev.size() > 64)
            return enviar(c, "ADD-DEVICE ERROR MESSAGE=\"" + tr("falta DEVICEID").toStdString() + "\"");
        if (!permitida(c.ip)) {
            if (rechazadas_.count(c.ip) > 0)
                return enviar(c, "ADD-DEVICE ERROR DEVICEID=" + dev + " MESSAGE=\"" + tr("IP rechazada").toStdString() + "\"");
            if (pendientes_.count(c.ip) == 0) {
                const std::string producto = arg("PRODUCT_NAME");
                pendientes_[c.ip] = {producto.empty() ? dev : producto.substr(0, 60),
                                     QDateTime::currentMSecsSinceEpoch()};
                bot_.auditar(c.ip, "satellite_pendiente", dev + " " + producto);
                emit cambio();
            }
            return enviar(c, "ADD-DEVICE ERROR DEVICEID=" + dev + " MESSAGE=\"" +
                                 ba::i18n::traducirEvento("botonera", "IP no permitida: aprobala en Servicios o en Botonera (Aparatos)") + "\"");
        }
        Aparato ap;
        ap.id = dev;
        ap.total = std::clamp(std::atoi(arg("KEYS_TOTAL", "32").c_str()), 1, 128);
        ap.porFila = std::clamp(std::atoi(arg("KEYS_PER_ROW", "8").c_str()), 1, 16);
        const std::string bm = arg("BITMAPS", "true");
        ap.bitmaps = bm == "true" ? 72 : (bm == "false" || bm == "0") ? 0 : std::clamp(std::atoi(bm.c_str()), 0, 400);
        ap.colores = verdad("COLORS", false);
        ap.texto = verdad("TEXT", false);
        c.aparatos[dev] = ap;
        const int filas = (ap.total + ap.porFila - 1) / ap.porFila;
        bot_.registrarLocal(idApp(c, dev), arg("PRODUCT_NAME", "Satellite"), "satellite",
                            {{"modelo", arg("PRODUCT_NAME")}, {"columnas", ap.porFila}, {"filas", filas},
                             {"teclas_lcd", ap.bitmaps > 0}, {"tamano_px", ap.bitmaps}, {"ip", c.ip}});
        enviar(c, "ADD-DEVICE OK DEVICEID=" + dev);
        enviar(c, "KEYS-CLEAR DEVICEID=" + dev);
        bot_.entrada(idApp(c, dev), {{"type", "hola"}});
        emit cambio();
        return;
    }
    auto ap = c.aparatos.find(dev);
    if (cmd == "REMOVE-DEVICE") {
        if (ap != c.aparatos.end()) {
            bot_.quitarLocal(idApp(c, dev));
            c.aparatos.erase(ap);
        }
        emit cambio();
        return enviar(c, "REMOVE-DEVICE OK DEVICEID=" + dev);
    }
    if (ap == c.aparatos.end())
        return enviar(c, cmd + " ERROR DEVICEID=" + dev + " MESSAGE=\"" + ba::i18n::traducirEvento("botonera", "aparato desconocido") + "\"");
    const int key = std::atoi(arg("KEY", "-1").c_str());
    if (key < 0 || key >= ap->second.total)
        return enviar(c, cmd + " ERROR DEVICEID=" + dev + " MESSAGE=\"" + tr("tecla fuera de rango").toStdString() + "\"");
    const int x = key % ap->second.porFila, y = key / ap->second.porFila;
    if (cmd == "KEY-PRESS") {
        const bool apretada = verdad("PRESSED", false);
        std::string tipo = apretada ? "presionar" : "soltar";
        // Confirmación de aire: apretar de nuevo la tecla que titila en amarillo.
        if (apretada) {
            const std::string conf = ap->second.ultimo.value("confirmar", std::string());
            for (const auto& cel : ap->second.ultimo.value("celdas", json::array())) {
                const int cx = cel.value("x", 0), cy = cel.value("y", 0);
                if (x >= cx && x < cx + cel.value("ancho", 1) && y >= cy && y < cy + cel.value("alto", 1) &&
                    conf == std::to_string(cx) + "," + std::to_string(cy))
                    tipo = "confirmar";
            }
        }
        bot_.entrada(idApp(c, dev), {{"type", tipo}, {"x", x}, {"y", y}});
        return enviar(c, "KEY-PRESS OK");
    }
    if (cmd == "KEY-ROTATE") {
        int d = std::atoi(arg("DIRECTION", "1").c_str());
        if (d == 0) d = -1; // el 0 viejo es un paso a la izquierda
        bot_.entrada(idApp(c, dev), {{"type", "girar"}, {"x", x}, {"y", y}, {"delta", d}});
        return enviar(c, "KEY-ROTATE OK");
    }
    enviar(c, "ERROR MESSAGE=\"Unknown command: " + cmd + "\"");
}

void SatelliteServidor::mensaje(const std::string& dispositivo, const json& msg) {
    for (auto& [s, c] : clientes_)
        for (auto& [id, a] : c.aparatos) {
            if (idApp(c, id) != dispositivo) continue;
            const std::string t = msg.value("type", std::string());
            if (t == "estado") {
                a.ultimo = msg;
                dibujar(c, a, false);
            } else if (t == "confirmar") {
                a.ultimo["confirmar"] = msg.value("celda", std::string());
                dibujar(c, a, false);
            }
        }
}

QByteArray SatelliteServidor::tecla(const json& cel, int lado, bool esperando, bool fase) const {
    QImage img(lado, lado, QImage::Format_RGB888);
    img.fill(Qt::black);
    if (cel.is_null()) return QByteArray(reinterpret_cast<const char*>(img.constBits()), img.sizeInBytes());
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QColor fondo = colorDe(cel.value("fondo", json()), QColor(43, 47, 58));
    if (cel.value("titilar", false) && !fase) fondo = fondo.darker(280);
    p.setBrush(fondo);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(0, 0, lado, lado), lado / 10.0, lado / 10.0);
    // La cara propia (Media/botonera/<id>/<n>.jpg, 288 px) escalada a esta tecla
    // y guardada así; si no, la data: URL de la celda. Apagada para que se lea el texto.
    if (cel.contains("cara") && cel["cara"].is_object()) {
        const std::string id = cel["cara"].value("id", std::string());
        const int n = cel.value("cara_cuadro", 0);
        const std::string clave = id + "/" + std::to_string(n) + "@" + std::to_string(lado);
        auto it = caras_.find(clave);
        if (it == caras_.end()) {
            QImage cara;
            if (!id.empty() && id.find("..") == std::string::npos && id.find('/') == std::string::npos) {
                const fs::path f = datos_ / "Media" / "botonera" / fs::u8path(id) / CarasBotonera::archivoCuadro(n);
                if (cara.load(QString::fromStdWString(f.wstring())))
                    cara = cara.scaled(lado, lado, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
            if (caras_.size() > 800) caras_.clear();
            it = caras_.emplace(clave, cara).first;
        }
        if (!it->second.isNull()) {
            p.setOpacity(0.6);
            p.drawImage(QRectF(0, 0, lado, lado), it->second);
            p.setOpacity(1.0);
        }
    } else if (const std::string im = cel.value("imagen", std::string()); im.rfind("data:", 0) == 0) {
        auto it = caras_.find(im);
        if (it == caras_.end()) {
            const auto coma = im.find(',');
            QImage cara;
            if (coma != std::string::npos)
                cara.loadFromData(QByteArray::fromBase64(QByteArray::fromStdString(im.substr(coma + 1))));
            if (caras_.size() > 800) caras_.clear();
            it = caras_.emplace(im, cara).first;
        }
        if (!it->second.isNull()) {
            p.setOpacity(0.6);
            p.drawImage(QRectF(0, 0, lado, lado), it->second);
            p.setOpacity(1.0);
        }
    }
    const std::string tipo = cel.value("tipo", std::string());
    if ((tipo == "fader" || tipo == "perilla" || tipo == "medidor") && cel.value("max", 0.0) != cel.value("min", 0.0)) {
        const double f = std::clamp((cel.value("valor", 0.0) - cel.value("min", 0.0)) /
                                        (cel.value("max", 1.0) - cel.value("min", 0.0)), 0.0, 1.0);
        p.setBrush(QColor(230, 230, 230));
        p.drawRect(QRectF(lado * 0.82, lado * (1 - f), lado * 0.15, lado * f));
    }
    // El borde propio (le toca, presidente), que puede titilar solo.
    if (const QColor borde = colorDe(cel.value("borde", json()), QColor());
        borde.isValid() && !esperando && (!cel.value("borde_titilar", false) || fase)) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(borde, std::max(3, lado / 12)));
        p.drawRoundedRect(QRectF(3, 3, lado - 6, lado - 6), lado / 10.0, lado / 10.0);
    }
    if (esperando || cel.value("activo", false)) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(esperando ? QColor(255, 204, 0) : QColor(Qt::white), std::max(2, lado / (esperando ? 14 : 24))));
        p.drawRoundedRect(QRectF(2, 2, lado - 4, lado - 4), lado / 10.0, lado / 10.0);
    }
    QString texto = QString::fromStdString(cel.value("texto", std::string()));
    const QString icono = QString::fromStdString(cel.value("icono", std::string()));
    if (!icono.isEmpty()) texto = icono + "\n" + texto;
    if (!texto.isEmpty()) {
        p.setPen(colorDe(cel.value("color", json()), Qt::white));
        QFont f;
        f.setBold(true);
        const QRectF caja(4, 4, lado - 8, lado - 8);
        // El tamaño más grande con el que entra.
        for (int px = lado / 4; px >= 8; px -= 1) {
            f.setPixelSize(px);
            const QRect r = QFontMetrics(f).boundingRect(caja.toRect(), Qt::AlignCenter | Qt::TextWordWrap, texto);
            if (r.width() <= caja.width() && r.height() <= caja.height()) break;
        }
        p.setFont(f);
        p.drawText(caja, Qt::AlignCenter | Qt::TextWordWrap, texto);
    }
    // La insignia (turno, «P») arriba a la derecha.
    if (const QString ins = QString::fromStdString(cel.value("insignia", std::string())); !ins.isEmpty()) {
        QFont f;
        f.setBold(true);
        f.setPixelSize(std::max(9, lado / 5));
        const QFontMetrics fm(f);
        const qreal w = std::max<qreal>(fm.height(), fm.horizontalAdvance(ins) + 8);
        const QRectF r(lado - w - 3, 3, w, fm.height());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 179, 0));
        p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
        p.setPen(Qt::black);
        p.setFont(f);
        p.drawText(r, Qt::AlignCenter, ins);
    }
    // Atenuada (sin unidad de la central, o sin central): todo más oscuro.
    if (cel.value("atenuar", false)) p.fillRect(QRectF(0, 0, lado, lado), QColor(0, 0, 0, 150));
    p.end();
    // RGB crudo, fila por fila (sin el relleno de alineación de QImage).
    QByteArray out;
    out.reserve(lado * lado * 3);
    for (int y = 0; y < lado; ++y) out.append(reinterpret_cast<const char*>(img.constScanLine(y)), lado * 3);
    return out;
}

void SatelliteServidor::dibujar(Cliente& c, Aparato& a, bool forzar) {
    if (a.ultimo.is_null()) return;
    const std::string conf = a.ultimo.value("confirmar", std::string());
    const json& celdas = a.ultimo.contains("celdas") ? a.ultimo["celdas"] : json::array();
    for (int k = 0; k < a.total; ++k) {
        const int x = k % a.porFila, y = k / a.porFila;
        json cel;
        for (const auto& cc : celdas) {
            const int cx = cc.value("x", 0), cy = cc.value("y", 0);
            if (x >= cx && x < cx + cc.value("ancho", 1) && y >= cy && y < cy + cc.value("alto", 1)) {
                cel = cc;
                break;
            }
        }
        // Cara propia (foto o video corto): el cuadro de este momento; la firma
        // cambia con él y tecla() lo lee del disco a la medida de la tecla.
        if (cel.contains("cara") && cel["cara"].is_object()) {
            const json& k = cel["cara"];
            const int n = std::max(1, k.value("cuadros", 1)), fps = std::max(1, k.value("fps", 8));
            cel["cara_cuadro"] = n > 1 ? static_cast<int>((QDateTime::currentMSecsSinceEpoch() * fps / 1000) % n) : 0;
        }
        const bool esperando = !cel.is_null() && conf == std::to_string(cel.value("x", 0)) + "," + std::to_string(cel.value("y", 0));
        const bool titila = esperando || (!cel.is_null() && (cel.value("titilar", false) || cel.value("borde_titilar", false)));
        const QString firma = QString::fromStdString(cel.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)) + (esperando ? "E" : "") + (titila && !fase_ ? "0" : "1");
        if (!forzar && a.enviado.value(k) == firma) continue;
        a.enviado[k] = firma;
        std::string l = "KEY-STATE DEVICEID=" + a.id + " KEY=" + std::to_string(k) + " TYPE=BUTTON";
        if (a.bitmaps > 0)
            l += " BITMAP=" + tecla(cel, a.bitmaps, esperando, !titila || fase_).toBase64().toStdString();
        if (a.colores) {
            QColor f = cel.is_null() ? QColor(Qt::black) : colorDe(cel.value("fondo", json()), QColor(43, 47, 58));
            if (titila && !fase_) f = f.darker(280);
            if (esperando && fase_) f = QColor(255, 204, 0);
            l += " COLOR=" + f.name().toStdString();
        }
        if (a.texto)
            l += " TEXT=" + QByteArray::fromStdString(cel.is_null() ? std::string() : cel.value("texto", std::string()))
                                .toBase64()
                                .toStdString();
        enviar(c, l);
    }
}

} // namespace ba::app::control
