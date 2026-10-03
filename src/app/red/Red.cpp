// src/app/red/Red.cpp — ver Red.hpp

#include "red/Red.hpp"

#include "net/Credentials.hpp"
#include "ptz/ControlCamaras.hpp"
#include "ptz/OnvifBusqueda.hpp"
#include "ptz/PtzProtocolos.hpp"
#include "red/Plan.hpp"
#include "red/Rtsp.hpp"
#include "video/CatalogoGst.hpp"
#include "video/VideoGrafos.hpp"

#include <obs.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>
#include <set>
#include <thread>
#include <vector>

#if defined(BA_HAVE_GST)
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#endif

namespace ba::app::red {

using json = nlohmann::json;

namespace {

/// Los puertos que dicen «esto es de video»: RTSP, web, SDKs propios, RTMP, NDI.
const std::vector<int> kPuertos = {554, 8554, 80, 88, 8080, 8000, 37777, 1935, 5960};
constexpr int kConcurrentes = 256;
constexpr int kEsperaConexionMs = 600;

std::string minusculas(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

QVariant aVariant(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json{{"v", j}}.dump())).toVariant().toMap().value("v");
}

/// La tabla ARP del sistema: ip -> MAC. (macOS/Linux: `arp -an`; Windows: `arp -a`.)
std::map<std::string, std::string> tablaArp() {
    std::map<std::string, std::string> out;
    QProcess p;
#ifdef _WIN32
    p.start("arp", {"-a"});
#else
    p.start("arp", {"-an"});
#endif
    if (!p.waitForFinished(3000)) return out;
    const QString texto = QString::fromLocal8Bit(p.readAllStandardOutput());
    static const QRegularExpression reIp(R"((\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}))");
    static const QRegularExpression reMac(R"(((?:[0-9a-fA-F]{1,2}[:-]){5}[0-9a-fA-F]{1,2}))");
    for (const QString& l : texto.split('\n')) {
        const auto mi = reIp.match(l), mm = reMac.match(l);
        if (mi.hasMatch() && mm.hasMatch()) out[mi.captured(1).toStdString()] = mm.captured(1).toStdString();
    }
    return out;
}

/// Lo que se lee de las respuestas ONVIF que no cubre ptz/PtzProtocolos.
json leerHojas(const QByteArray& xml, const QString& dentroDe) {
    // Todas las hojas (texto) debajo del elemento `dentroDe`, por su ruta.
    json out = json::object();
    QXmlStreamReader r(xml);
    QStringList ruta;
    int dentro = -1;
    QString texto;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            ruta << r.name().toString();
            if (dentro < 0 && r.name() == dentroDe) dentro = static_cast<int>(ruta.size());
            texto.clear();
        } else if (r.isCharacters()) {
            texto += r.text();
        } else if (r.isEndElement()) {
            if (dentro > 0 && ruta.size() > dentro && !texto.trimmed().isEmpty())
                out[ruta.mid(dentro).join('.').toStdString()] = texto.trimmed().toStdString();
            texto.clear();
            if (static_cast<int>(ruta.size()) == dentro) dentro = -1;
            ruta.removeLast();
        }
    }
    return out;
}

/// Los perfiles de Media (GetProfiles) con lo de su codificador.
json leerPerfiles(const QByteArray& xml) {
    json out = json::array();
    QXmlStreamReader r(xml);
    json p;
    QString seccion, campo;
    while (!r.atEnd()) {
        r.readNext();
        const QString n = r.name().toString();
        if (r.isStartElement()) {
            if (n == "Profiles") {
                p = json{{"token", r.attributes().value("token").toString().toStdString()}, {"ptz", false}};
                seccion.clear();
            } else if (n.endsWith("Configuration") && !p.is_null()) {
                seccion = n;
                if (n == "PTZConfiguration") p["ptz"] = true;
            }
            campo = n;
        } else if (r.isCharacters() && !p.is_null()) {
            const QString t = r.text().toString().trimmed();
            if (t.isEmpty()) continue;
            if (campo == "Name" && seccion.isEmpty() && !p.contains("nombre")) p["nombre"] = t.toStdString();
            if (seccion == "VideoSourceConfiguration" && campo == "SourceToken") p["fuente"] = t.toStdString();
            if (seccion == "VideoEncoderConfiguration") {
                if (campo == "Encoding") p["codec"] = t.toUpper().toStdString();
                if (campo == "Width") p["ancho"] = t.toInt();
                if (campo == "Height") p["alto"] = t.toInt();
                if (campo == "FrameRateLimit") p["fps"] = t.toDouble();
                if (campo == "BitrateLimit") p["bitrate_kbps"] = t.toInt();
                if (campo == "GovLength") p["gop"] = t.toInt();
            }
        } else if (r.isEndElement()) {
            if (n.endsWith("Configuration")) seccion.clear();
            if (n == "Profiles" && !p.is_null()) {
                out.push_back(p);
                p = json();
            }
        }
    }
    return out;
}

QString cuerpoInfo() { return R"(<GetDeviceInformation xmlns="http://www.onvif.org/ver10/device/wsdl"/>)"; }
QString cuerpoUri(const QString& perfil) {
    return QString(R"(<GetStreamUri xmlns="http://www.onvif.org/ver10/media/wsdl"><StreamSetup><Stream xmlns="http://www.onvif.org/ver10/schema">RTP-Unicast</Stream><Transport xmlns="http://www.onvif.org/ver10/schema"><Protocol>RTSP</Protocol></Transport></StreamSetup><ProfileToken>%1</ProfileToken></GetStreamUri>)")
        .arg(perfil.toHtmlEscaped());
}
QString cuerpoNodos() { return R"(<GetNodes xmlns="http://www.onvif.org/ver20/ptz/wsdl"/>)"; }
QString cuerpoImagen(const QString& fuente) {
    return QString(R"(<GetImagingSettings xmlns="http://www.onvif.org/ver20/imaging/wsdl"><VideoSourceToken>%1</VideoSourceToken></GetImagingSettings>)")
        .arg(fuente.toHtmlEscaped());
}

} // namespace

// --- El escaneo en curso --------------------------------------------------------------

struct Red::Escaneo {
    Listo listo;
    QElapsedTimer reloj;
    std::vector<std::pair<QString, int>> pendientes;
    size_t siguiente = 0;
    int activos = 0;
    std::map<std::string, std::set<int>> abiertos;
    std::unique_ptr<ptz::OnvifBusqueda> onvif;
    bool onvifListo = false, barridoListo = false, cerrando = false;
    QStringList subredes;
    std::set<std::string> propias;
    std::map<std::string, json> equipos;
    int trabajos = 0; ///< web y RTSP en vuelo (fase 2)
};

Red::Red(const std::filesystem::path& datos, const QString& dirCredenciales, video::VideoGrafos* video,
         ptz::ControlCamaras* camaras, QObject* parent)
    : QObject(parent), video_(video), camaras_(camaras), dirCred_(dirCredenciales) {
    biblioteca_.leer((datos / "camaras-urls.json").string());
    oui_.leer((datos / "oui.tsv").string());
    http_ = new QNetworkAccessManager(this);
}

Red::~Red() = default;

QVariantList Red::equiposQml() const { return aVariant(equipos_).toList(); }

json Red::equipo(const std::string& ip) const {
    for (const auto& e : equipos_)
        if (e.value("ip", std::string()) == ip) return e;
    return json::object(); // vacío: no está en el último escaneo
}

// --- Credenciales ------------------------------------------------------------------------

bool Red::guardarClave(const std::string& ip, const QString& usuario, const QString& clave) {
    const std::string nombre = "camara-" + ip;
    return cred::write(dirCred_, nombre.c_str(), (usuario + "\n" + clave).toUtf8());
}

bool Red::tieneClave(const std::string& ip) const {
    const std::string nombre = "camara-" + ip;
    return cred::exists(dirCred_, nombre.c_str());
}

bool Red::clave(const std::string& ip, QString& usuario, QString& clave) const {
    const std::string nombre = "camara-" + ip;
    QByteArray d;
    if (!cred::read(dirCred_, nombre.c_str(), d)) return false;
    const QString s = QString::fromUtf8(d);
    usuario = s.section('\n', 0, 0);
    clave = s.section('\n', 1);
    return !usuario.isEmpty();
}

QString Red::guardarCredenciales(const QString& ip, const QString& usuario, const QString& clave) {
    if (ip.isEmpty() || usuario.trimmed().isEmpty()) return "Falta el usuario.";
    if (!guardarClave(ip.toStdString(), usuario.trimmed(), clave)) return "No se pudo guardar (Credenciales/).";
    // Con la clave, se vuelven a probar sus URLs y se rehace la ficha.
    probarUrls(ip.toStdString(), [](json) {});
    return {};
}

// --- Escanear --------------------------------------------------------------------------------

void Red::escanear(Listo listo) {
    if (escaneando_) return listo({{"error", "ya hay un escaneo en curso"}});
    escaneando_ = true;
    esc_ = std::make_unique<Escaneo>();
    esc_->listo = std::move(listo);
    esc_->reloj.start();
    // Las subredes de cada placa con IPv4 (hasta /24: una red más grande se
    // limita al /24 propio, que es donde suelen estar las cámaras).
    for (const QNetworkInterface& i : QNetworkInterface::allInterfaces()) {
        if (!(i.flags() & QNetworkInterface::IsUp) || (i.flags() & QNetworkInterface::IsLoopBack)) continue;
        // Solo placas de verdad (cable o Wi-Fi): las virtuales (puentes de
        // máquinas virtuales, VPN) no tienen cámaras y duplican el tiempo.
        if (i.type() != QNetworkInterface::Ethernet && i.type() != QNetworkInterface::Wifi) continue;
        const QString nombreIf = i.name().toLower();
        if (nombreIf.startsWith("bridge") || nombreIf.startsWith("vmnet") || nombreIf.startsWith("utun") ||
            nombreIf.startsWith("vboxnet") || nombreIf.startsWith("docker") || nombreIf.startsWith("veth"))
            continue;
        for (const QNetworkAddressEntry& e : i.addressEntries()) {
            const QHostAddress ip = e.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol || ip.isLoopback() || ip.isLinkLocal()) continue;
            esc_->propias.insert(ip.toString().toStdString());
            const int prefijo = std::max(24, e.prefixLength());
            const quint32 mascara = prefijo >= 32 ? 0xFFFFFFFFu : ~((1u << (32 - prefijo)) - 1);
            const quint32 red = ip.toIPv4Address() & mascara;
            esc_->subredes << QHostAddress(red).toString() + "/" + QString::number(prefijo);
            for (quint32 h = red + 1; h < (red | ~mascara); ++h) {
                const QString host = QHostAddress(h).toString();
                if (host == ip.toString()) continue;
                for (int p : kPuertos) esc_->pendientes.emplace_back(host, p);
            }
        }
    }
    progreso_ = "Buscando en " + esc_->subredes.join(", ") + "…";
    emit cambio();
    // ONVIF, en paralelo.
    esc_->onvif = std::make_unique<ptz::OnvifBusqueda>();
    connect(esc_->onvif.get(), &ptz::OnvifBusqueda::terminada, this, [this]() {
        if (!esc_) return;
        esc_->onvifListo = true;
        if (esc_->barridoListo) terminarBarrido();
    });
    esc_->onvif->buscar(3500);
    // El barrido: muchas conexiones a la vez, cada una con poco tiempo.
    auto lanzarPtr = std::make_shared<std::function<void()>>();
    *lanzarPtr = [this, lanzarPtr]() {
        if (!esc_) return;
        while (esc_->activos < kConcurrentes && esc_->siguiente < esc_->pendientes.size()) {
            const auto [host, puerto] = esc_->pendientes[esc_->siguiente++];
            ++esc_->activos;
            auto* s = new QTcpSocket(this);
            auto hecho = std::make_shared<bool>(false);
            const auto fin = [this, s, hecho, lanzarPtr]() {
                if (*hecho) return;
                *hecho = true;
                s->abort();
                s->deleteLater();
                if (!esc_) return;
                --esc_->activos;
                if (esc_->siguiente % 200 == 0) {
                    progreso_ = QString("Probando puertos: %1 de %2…").arg(esc_->siguiente).arg(esc_->pendientes.size());
                    emit cambio();
                }
                if (esc_->siguiente >= esc_->pendientes.size() && esc_->activos == 0) {
                    esc_->barridoListo = true;
                    if (esc_->onvifListo) terminarBarrido();
                    return;
                }
                (*lanzarPtr)();
            };
            connect(s, &QTcpSocket::connected, this, [this, host = host, puerto = puerto, fin]() {
                if (esc_) esc_->abiertos[host.toStdString()].insert(puerto);
                fin();
            });
            connect(s, &QTcpSocket::errorOccurred, this, [fin](QAbstractSocket::SocketError) { fin(); });
            QTimer::singleShot(kEsperaConexionMs, s, fin);
            s->connectToHost(host, static_cast<quint16>(puerto));
        }
    };
    if (esc_->pendientes.empty()) {
        esc_->barridoListo = true;
        if (esc_->onvifListo) terminarBarrido();
    } else {
        (*lanzarPtr)();
    }
}

void Red::terminarBarrido() {
    if (!esc_ || esc_->cerrando) return;
    esc_->cerrando = true;
    progreso_ = "Reconociendo equipos…";
    emit cambio();
    // Los equipos: lo que tiene algún puerto de video abierto o contestó ONVIF.
    const auto arp = tablaArp();
    std::map<std::string, json> onvif;
    for (const QVariant& v : esc_->onvif->encontradas()) {
        const QVariantMap m = v.toMap();
        // El device_service puede traer varias direcciones (una 169.254…,
        // autoasignada): la que cae en una de las subredes que se barren.
        // La IP de la que vino la respuesta es la de verdad.
        QString host = m["origen"].toString();
        if (host.isEmpty()) host = m["host"].toString().section(':', 0, 0);
        if (m["origen"].toString().isEmpty())
            for (const QString& d : m["direccion"].toString().split(' ', Qt::SkipEmptyParts)) {
            const QHostAddress h(QUrl(d).host());
            if (h.isNull() || h.isLinkLocal()) continue;
            for (const QString& sr : esc_->subredes) {
                const auto par = QHostAddress::parseSubnet(sr);
                if (h.isInSubnet(par)) host = h.toString();
            }
            }
        onvif[host.toStdString()] = {{"direccion", m["direccion"].toString().toStdString()},
                                     {"nombre", m["nombre"].toString().toStdString()},
                                     {"modelo", m["modelo"].toString().toStdString()}};
    }
    std::set<std::string> ips;
    for (const auto& [ip, _] : esc_->abiertos) ips.insert(ip);
    for (const auto& [ip, _] : onvif) ips.insert(ip);
    for (const std::string& ip : ips) {
        if (esc_->propias.count(ip)) continue;
        json e{{"ip", ip}};
        const auto pu = esc_->abiertos[ip];
        e["puertos"] = json(std::vector<int>(pu.begin(), pu.end()));
        if (arp.count(ip)) {
            e["mac"] = arp.at(ip);
            e["fabricante_mac"] = oui_.fabricante(arp.at(ip));
        }
        if (onvif.count(ip)) e["onvif"] = onvif[ip];
        e["credenciales_guardadas"] = tieneClave(ip);
        // ¿Ya está conectada como entrada?
        if (video_ != nullptr) {
            const json ya = video_->entradasDeHost(ip);
            if (!ya.empty()) e["ya_conectada"] = ya;
        }
        esc_->equipos[ip] = e;
    }
    if (esc_->equipos.empty()) return cerrarEscaneo();
    // Fase 2: la web de cada uno, la marca y sus URLs RTSP.
    esc_->trabajos = static_cast<int>(esc_->equipos.size());
    for (auto& [ip, e] : esc_->equipos) {
        const std::set<int> pu = esc_->abiertos[ip];
        const int web = pu.count(80) ? 80 : pu.count(88) ? 88 : pu.count(8080) ? 8080 : 0;
        if (web == 0) {
            reconocer(ip);
            continue;
        }
        QNetworkRequest pedido(QUrl(QString("http://%1:%2/").arg(QString::fromStdString(ip)).arg(web)));
        pedido.setTransferTimeout(2500);
        pedido.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* r = http_->get(pedido);
        connect(r, &QNetworkReply::finished, this, [this, r, ip = ip]() {
            r->deleteLater();
            if (!esc_) return;
            const QByteArray cuerpo = r->read(65536);
            const auto t = QRegularExpression("<title[^>]*>([^<]*)</title>", QRegularExpression::CaseInsensitiveOption)
                               .match(QString::fromUtf8(cuerpo));
            esc_->equipos[ip]["web"] = {{"server", r->rawHeader("Server").toStdString()},
                                        {"titulo", t.hasMatch() ? t.captured(1).trimmed().toStdString() : std::string()},
                                        {"codigo", r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()}};
            reconocer(ip);
        });
    }
}

void Red::reconocer(const std::string& ip) {
    if (!esc_) return;
    json& e = esc_->equipos[ip];
    Pistas pi;
    if (e.contains("onvif")) {
        pi.onvifNombre = e["onvif"].value("nombre", std::string());
        pi.onvifModelo = e["onvif"].value("modelo", std::string());
    }
    pi.server = e.value("web", json::object()).value("server", std::string());
    pi.titulo = e.value("web", json::object()).value("titulo", std::string());
    pi.fabricanteMac = e.value("fabricante_mac", std::string());
    for (const auto& p : e["puertos"]) pi.puertos.insert(p.get<int>());
    Marca m = biblioteca_.reconocer(pi);
    // Una marca de CÁMARA por la MAC o por un puerto solo vale si el equipo
    // tiene video (RTSP u ONVIF): si no, es el router o una PC de esa marca.
    const bool video = pi.puertos.count(554) || pi.puertos.count(8554) || e.contains("onvif");
    if (!video && m.confianza < 2)
        m = {"", pi.fabricanteMac.empty() ? std::string("desconocido") : pi.fabricanteMac, "no tiene video (ni RTSP ni ONVIF)", 0};
    e["marca"] = {{"id", m.id}, {"nombre", m.nombre}, {"porque", m.porque}, {"confianza", m.confianza}};
    const std::string modelo = minusculas(pi.onvifNombre + " " + pi.onvifModelo);
    const bool grabador = modelo.find("dvr") != std::string::npos || modelo.find("nvr") != std::string::npos ||
                          modelo.find("ds-7") != std::string::npos || modelo.find("ds-9") != std::string::npos;
    e["tipo"] = grabador ? "grabador (DVR/NVR)"
                : (pi.puertos.count(554) || pi.puertos.count(8554) || e.contains("onvif")) ? "cámara"
                : pi.puertos.count(1935)                                                   ? "servidor RTMP"
                : pi.puertos.count(5960)                                                   ? "NDI"
                                                                                            : "equipo con web";
    const json ficha = biblioteca_.ficha(m.id);
    for (const char* k : {"notas", "ptz"})
        if (ficha.contains(k)) e[std::string(k == std::string("ptz") ? "ptz_segun_marca" : k)] = ficha[k];
    if (ficha.contains("usuario")) e["usuario_de_fabrica"] = ficha["usuario"];
    const int puertoRtsp = pi.puertos.count(554) ? 554 : pi.puertos.count(8554) ? 8554 : 0;
    if (puertoRtsp == 0) {
        e["siguiente_paso"] = siguientePaso(e);
        return trabajoListo();
    }
    // Un grabador: cuántos canales sugiere el modelo (DS-7208 = 8, DS-7616 = 16).
    int canales = grabador ? 4 : 1;
    if (grabador) {
        static const QRegularExpression re(R"(ds-?[79]\d(\d\d))", QRegularExpression::CaseInsensitiveOption);
        const auto mm = re.match(QString::fromStdString(pi.onvifModelo + " " + pi.onvifNombre));
        if (mm.hasMatch() && mm.captured(1).toInt() > 0) {
            e["canales_segun_modelo"] = mm.captured(1).toInt();
            canales = std::min(mm.captured(1).toInt(), 16);
        }
    }
    json cands = json::array();
    // Se prueba el principal de cada canal (el secundario, solo del primero).
    for (const auto& c : biblioteca_.candidatas(m.id, ip, puertoRtsp, canales))
        if (!grabador || c.perfil.find("secundario") == std::string::npos || c.url.find("/102") != std::string::npos)
            cands.push_back({{"url", c.url}, {"perfil", c.perfil}});
    sondearRtsp(ip, cands, [this, ip](json resultados) {
        if (!esc_) return;
        json& e = esc_->equipos[ip];
        e["rtsp"] = resultados;
        e["siguiente_paso"] = siguientePaso(e);
        trabajoListo();
    });
}

void Red::trabajoListo() {
    if (!esc_) return;
    progreso_ = QString("Reconociendo equipos (faltan %1)…").arg(esc_->trabajos - 1);
    emit cambio();
    if (--esc_->trabajos <= 0) cerrarEscaneo();
}

void Red::cerrarEscaneo() {
    if (!esc_) return;
    json lista = json::array();
    for (auto& [ip, e] : esc_->equipos) lista.push_back(e);
    // Primero lo que más interesa: cámaras y grabadores.
    std::stable_sort(lista.begin(), lista.end(), [](const json& a, const json& b) {
        const auto peso = [](const json& x) {
            const std::string t = x.value("tipo", std::string());
            return t == "cámara" ? 0 : t.rfind("grabador", 0) == 0 ? 1 : 2;
        };
        return peso(a) < peso(b);
    });
    equipos_ = lista;
    json r{{"equipos", lista},
           {"subredes", json(esc_->subredes.join(", ").toStdString())},
           {"segundos", std::round(static_cast<double>(esc_->reloj.elapsed()) / 100.0) / 10.0}};
    Listo listo = std::move(esc_->listo);
    esc_.reset();
    escaneando_ = false;
    progreso_ = QString("%1 equipos").arg(lista.size());
    emit cambio();
    if (listo) listo(r);
}

json Red::siguientePaso(const json& e) const {
    const std::string ip = e.value("ip", std::string());
    bool seVe = false, pideClave = false;
    for (const auto& r : e.value("rtsp", json::array())) {
        seVe = seVe || r.value("codigo", 0) == 200;
        pideClave = pideClave || r.value("codigo", 0) == 401;
    }
    const bool conClave = e.value("credenciales_guardadas", false);
    const bool onvif = e.contains("onvif");
    if (e.contains("ya_conectada") && !e["ya_conectada"].empty()) {
        const std::string n = e["ya_conectada"][0].value("entrada", std::string());
        return "YA ESTÁ CONECTADA como la entrada «" + n + "»: para tenerla en otra escena, camara_agregar {\"ip\": \"" + ip +
               "\", \"escena\": …, \"reusar\": true} (la misma entrada, no otro stream)" +
               (onvif && !conClave ? ". Para su PTZ ONVIF falta la clave: camara_credenciales {\"ips\": [\"" + ip + "\"], \"pedir\": true}" : "");
    }
    if (onvif && !conClave)
        return "pedir usuario y clave (camara_credenciales {\"ip\": \"" + ip + "\", \"pedir\": true}; usuario de fábrica: " +
               e.value("usuario_de_fabrica", std::string("admin")) + ") y después camara_ficha: ONVIF da la URL exacta, "
               "el PTZ y la imagen" + (seVe ? " (ya hay una URL que se ve sin clave)" : "");
    if (onvif && conClave) return "camara_ficha {\"ip\": \"" + ip + "\"} (ONVIF completo + análisis + plan) y camara_agregar";
    if (seVe) return "camara_ficha {\"ip\": \"" + ip + "\"} (analiza la URL que se ve) y camara_agregar";
    if (pideClave)
        return "pedir usuario y clave (camara_credenciales {\"ip\": \"" + ip + "\", \"pedir\": true}): la URL existe pero pide clave";
    if (e.value("tipo", std::string()) == "cámara")
        return "ninguna URL típica contestó: revisar las notas de la marca (habilitar RTSP) o pedir la URL al operador";
    return "no parece una cámara (no tiene RTSP ni ONVIF)";
}

// --- RTSP: probar URLs ------------------------------------------------------------------------

void Red::sondearRtsp(const std::string& ip, json candidatas, Listo listo) {
    QString usuario, pass;
    const bool conClave = clave(ip, usuario, pass);
    auto resultados = std::make_shared<json>(json::array());
    auto i = std::make_shared<size_t>(0);
    auto vistas = std::make_shared<int>(0);
    auto probar = std::make_shared<std::function<void()>>();
    *probar = [this, candidatas, conClave, usuario, pass, resultados, i, vistas, probar, listo]() {
        // Alcanza con dos que se vean (principal y secundario), o se cortan si no contesta nada.
        // (En un grabador se prueban todos los canales: cada uno es una cámara.)
        if (*i >= candidatas.size() || (*vistas >= 2 && candidatas.size() <= 6) ||
            (*i >= 2 && std::all_of(resultados->begin(), resultados->end(), [](const json& r) { return r.value("codigo", 0) == 0; }))) {
            // El ciclo (esta función se tiene a sí misma) se corta en la
            // vuelta siguiente: borrarla ACÁ sería borrar lo que se está
            // ejecutando.
            QTimer::singleShot(0, this, [probar]() { *probar = nullptr; });
            return listo(*resultados);
        }
        const json c = candidatas[(*i)++];
        const QString url = QString::fromStdString(c.value("url", std::string()));
        const rtsp::Url u = rtsp::partir(url);
        // Un DESCRIBE; si pide clave y hay, otro con la autorización.
        auto pedir = std::make_shared<std::function<void(QString, int)>>();
        *pedir = [this, url, u, c, conClave, usuario, pass, resultados, vistas, probar, pedir](QString autz, int cseq) {
            auto* s = new QTcpSocket(this);
            auto buf = std::make_shared<QByteArray>();
            auto hecho = std::make_shared<bool>(false);
            const auto terminar = [=](rtsp::Respuesta r) {
                if (*hecho) return;
                *hecho = true;
                s->abort();
                s->deleteLater();
                if (r.codigo == 401 && conClave && autz.isEmpty()) {
                    (*pedir)(rtsp::autorizacion(r, url, usuario, pass), cseq + 1);
                    return;
                }
                json x{{"url", url.toStdString()}, {"perfil", c.value("perfil", std::string())}, {"codigo", r.codigo},
                       {"estado", rtsp::estadoDe(r.codigo).toStdString()}, {"con_clave", !autz.isEmpty()}};
                if (!r.codec.isEmpty()) x["codec"] = r.codec.toStdString();
                if (r.ancho > 0) {
                    x["ancho"] = r.ancho;
                    x["alto"] = r.alto;
                }
                if (r.codigo == 200) ++*vistas;
                resultados->push_back(x);
                QTimer::singleShot(0, this, [pedir]() { *pedir = nullptr; }); // ver abajo
                (*probar)();
            };
            connect(s, &QTcpSocket::connected, this, [=]() { s->write(rtsp::describe(url, cseq, autz)); });
            connect(s, &QTcpSocket::readyRead, this, [=]() {
                *buf += s->readAll();
                const rtsp::Respuesta r = rtsp::leer(*buf);
                if (r.codigo != 0 && r.completa) terminar(r);
            });
            connect(s, &QTcpSocket::errorOccurred, this, [=](QAbstractSocket::SocketError) { terminar(rtsp::leer(*buf)); });
            QTimer::singleShot(2500, s, [=]() { terminar(rtsp::leer(*buf)); });
            s->connectToHost(u.host, static_cast<quint16>(u.puerto));
        };
        (*pedir)(QString(), 1);
    };
    (*probar)();
}

void Red::probarUrls(const std::string& ip, Listo listo) {
    json e = equipo(ip);
    if (e.empty()) return listo({{"error", "«" + ip + "» no está en el último escaneo: red_escanear primero"}});
    std::set<int> pu;
    for (const auto& p : e.value("puertos", json::array())) pu.insert(p.get<int>());
    const int puertoRtsp = pu.count(554) ? 554 : pu.count(8554) ? 8554 : 554;
    const bool grabador = e.value("tipo", std::string()).rfind("grabador", 0) == 0;
    json cands = json::array();
    for (const auto& c : biblioteca_.candidatas(e["marca"].value("id", std::string("generica")), ip, puertoRtsp, grabador ? 4 : 1))
        cands.push_back({{"url", c.url}, {"perfil", c.perfil}});
    sondearRtsp(ip, cands, [this, ip, listo](json resultados) {
        for (auto& x : equipos_)
            if (x.value("ip", std::string()) == ip) {
                x["rtsp"] = resultados;
                x["credenciales_guardadas"] = tieneClave(ip);
                x["siguiente_paso"] = siguientePaso(x);
                emit cambio();
                return listo(x);
            }
        listo({{"rtsp", resultados}});
    });
}

// --- Analizar con GStreamer ------------------------------------------------------------------

void Red::analizar(const QString& uri, Listo listo) {
    QPointer<Red> yo = this;
    std::thread([yo, uri, listo]() {
        json r;
#if defined(BA_HAVE_GST)
        std::string err;
        if (!video::iniciarGStreamer(&err)) {
            r = {{"error", "GStreamer: " + err}};
        } else {
            GError* e = nullptr;
            GstDiscoverer* d = gst_discoverer_new(10 * GST_SECOND, &e);
            GstDiscovererInfo* info = d != nullptr ? gst_discoverer_discover_uri(d, uri.toUtf8().constData(), &e) : nullptr;
            if (e != nullptr) {
                r = {{"error", e->message}};
                g_error_free(e);
            }
            if (info != nullptr && gst_discoverer_info_get_result(info) == GST_DISCOVERER_OK) {
                r = json::object();
                GList* vs = gst_discoverer_info_get_video_streams(info);
                if (vs != nullptr) {
                    auto* v = GST_DISCOVERER_VIDEO_INFO(vs->data);
                    json o{{"ancho", gst_discoverer_video_info_get_width(v)},
                           {"alto", gst_discoverer_video_info_get_height(v)},
                           {"entrelazado", gst_discoverer_video_info_is_interlaced(v) != FALSE}}; // gboolean: a bool
                    const guint fn = gst_discoverer_video_info_get_framerate_num(v), fd = gst_discoverer_video_info_get_framerate_denom(v);
                    o["fps"] = fn > 0 && fd > 0 ? static_cast<double>(fn) / fd : 0.0;
                    o["fps_variable"] = fn == 0;
                    const guint pn = gst_discoverer_video_info_get_par_num(v), pd = gst_discoverer_video_info_get_par_denom(v);
                    o["pixel"] = pn == pd || pd == 0 ? std::string("cuadrado")
                                                     : std::to_string(pn) + "/" + std::to_string(pd) + " (no cuadrado)";
                    o["bitrate_kbps"] = gst_discoverer_video_info_get_bitrate(v) / 1000;
                    if (GstCaps* caps = gst_discoverer_stream_info_get_caps(GST_DISCOVERER_STREAM_INFO(v))) {
                        gchar* desc = gst_pb_utils_get_codec_description(caps);
                        if (desc != nullptr) o["codec"] = desc;
                        g_free(desc);
                        const GstStructure* st = gst_caps_get_structure(caps, 0);
                        if (const char* col = gst_structure_get_string(st, "colorimetry")) o["colorimetria"] = col;
                        if (const char* prof = gst_structure_get_string(st, "profile")) o["perfil_codec"] = prof;
                        gst_caps_unref(caps);
                    }
                    r["video"] = o;
                }
                gst_discoverer_stream_info_list_free(vs);
                GList* as = gst_discoverer_info_get_audio_streams(info);
                if (as != nullptr) {
                    auto* a = GST_DISCOVERER_AUDIO_INFO(as->data);
                    json o{{"canales", gst_discoverer_audio_info_get_channels(a)},
                           {"frecuencia", gst_discoverer_audio_info_get_sample_rate(a)}};
                    if (GstCaps* caps = gst_discoverer_stream_info_get_caps(GST_DISCOVERER_STREAM_INFO(a))) {
                        gchar* desc = gst_pb_utils_get_codec_description(caps);
                        if (desc != nullptr) o["codec"] = desc;
                        g_free(desc);
                        gst_caps_unref(caps);
                    }
                    r["audio"] = o;
                }
                gst_discoverer_stream_info_list_free(as);
            } else if (info != nullptr && !r.contains("error")) {
                const GstDiscovererResult res = gst_discoverer_info_get_result(info);
                r = {{"error", res == GST_DISCOVERER_TIMEOUT ? "no contestó a tiempo" : res == GST_DISCOVERER_URI_INVALID ? "URL inválida"
                                                                                       : "no se pudo abrir el stream"}};
            }
            if (info != nullptr) gst_discoverer_info_unref(info);
            if (d != nullptr) g_object_unref(d);
        }
#else
        r = {{"error", "esta compilación no trae GStreamer"}};
#endif
        QMetaObject::invokeMethod(qApp, [yo, r, listo]() {
            if (!yo) return;
            // Lo que haga el que pidió el análisis corre en el bucle de la
            // interfaz: una excepción acá tumbaría la app entera.
            try {
                listo(r);
            } catch (const std::exception& e) {
                listo({{"error", std::string("falló al procesar el análisis: ") + e.what()}});
            }
        }, Qt::QueuedConnection);
    }).detach();
}

// --- La ficha ONVIF -----------------------------------------------------------------------------

void Red::ficha(const std::string& ip, Listo listo) {
    json e = equipo(ip);
    QString usuario, pass;
    const bool conClave = clave(ip, usuario, pass);
    // El device_service: el que contestó al descubrimiento, o el de siempre.
    QString servicio = QString::fromStdString(e.value("onvif", json::object()).value("direccion", std::string()));
    for (const QString& d : servicio.split(' ', Qt::SkipEmptyParts))
        if (QUrl(d).host() == QString::fromStdString(ip)) servicio = d;
    if (servicio.isEmpty() || servicio.contains(' ')) servicio = "http://" + QString::fromStdString(ip) + "/onvif/device_service";
    auto f = std::make_shared<json>(json{{"ip", ip}, {"marca", e.value("marca", json::object())}});
    auto desfase = std::make_shared<qint64>(0);
    auto listoUnaVez = std::make_shared<bool>(false);
    const auto terminar = [this, f, listo, ip, listoUnaVez]() {
        if (*listoUnaVez) return;
        *listoUnaVez = true;
        fichas_[ip] = *f;
        emit cambio();
        listo(*f);
    };
    // Un pedido SOAP: con clave si hay. Lo que pase en la respuesta (un dato
    // inesperado) termina la ficha con el error: nunca tumba la app ni la
    // deja esperando.
    const auto soap = [this, usuario, pass, conClave, desfase, f, terminar](const QString& url, const QString& cuerpo, bool autent,
                                                                             std::function<void(bool, QByteArray)> cb) {
        QNetworkRequest pedido{QUrl(url)};
        pedido.setHeader(QNetworkRequest::ContentTypeHeader, "application/soap+xml; charset=utf-8");
        pedido.setTransferTimeout(6000);
        const QByteArray datos = autent && conClave ? ptz::onvif::sobre(cuerpo, usuario, pass, *desfase) : ptz::onvif::sobre(cuerpo);
        QNetworkReply* r = http_->post(pedido, datos);
        connect(r, &QNetworkReply::finished, this, [r, cb, f, terminar]() {
            r->deleteLater();
            const QByteArray xml = r->isOpen() ? r->readAll() : QByteArray(); // cortada por tiempo: ya está cerrada
            try {
                cb(r->error() == QNetworkReply::NoError && ptz::onvif::leerFalla(xml).isEmpty(), xml);
            } catch (const std::exception& ex) {
                (*f)["error"] = std::string("falló al leer la respuesta ONVIF: ") + ex.what();
                terminar();
            }
        });
    };
    // 1) La hora (sin clave): la autenticación usa la de la cámara.
    soap(servicio, ptz::onvif::getSystemDateAndTime(), false, [=](bool, QByteArray xml) {
        const QDateTime hora = ptz::onvif::leerHora(xml);
        if (hora.isValid()) *desfase = QDateTime::currentDateTimeUtc().secsTo(hora);
        // 2) El equipo.
        soap(servicio, cuerpoInfo(), true, [=](bool ok, QByteArray xml) {
            if (!ok) {
                const QString falla = ptz::onvif::leerFalla(xml);
                (*f)["error"] = conClave ? "ONVIF rechazó el usuario y la clave guardados" + (falla.isEmpty() ? std::string() : ": " + falla.toStdString())
                                         : "ONVIF pide usuario y clave: camara_credenciales {\"ip\": \"" + ip + "\", \"pedir\": true}";
                (*f)["onvif"] = false;
                // Sin ONVIF: lo que se sabe del escaneo (una URL que se ve), analizada.
                for (const auto& r : equipo(ip).value("rtsp", json::array()))
                    if (r.value("codigo", 0) == 200) {
                        (*f)["url"] = r["url"];
                        break;
                    }
                if (!f->contains("url")) return terminar();
                QString u, c;
                const QString uri = clave(ip, u, c) ? rtsp::conClave(QString::fromStdString((*f)["url"]), u, c)
                                                    : QString::fromStdString((*f)["url"]);
                return analizar(uri, [=](json a) {
                    (*f)["analisis"] = a;
                    obs_video_info ovi{};
                    obs_get_video_info(&ovi);
                    (*f)["plan"] = planDe(*f, a, {{"ancho", ovi.base_width}, {"alto", ovi.base_height},
                                                 {"fps", ovi.fps_den ? static_cast<double>(ovi.fps_num) / ovi.fps_den : 30.0}});
                    terminar();
                });
            }
            (*f)["onvif"] = true;
            (*f)["equipo"] = leerHojas(xml, "GetDeviceInformationResponse");
            // 3) Qué servicios tiene.
            soap(servicio, ptz::onvif::getCapabilities(), true, [=](bool, QByteArray xml) {
                const ptz::onvif::Servicios sv = ptz::onvif::leerCapacidades(xml);
                (*f)["servicios"] = {{"media", !sv.media.isEmpty()}, {"ptz", !sv.ptz.isEmpty()}, {"imagen", !sv.imaging.isEmpty()}};
                const QString media = sv.media.isEmpty() ? servicio : sv.media;
                // 4) Los perfiles de video.
                soap(media, ptz::onvif::getProfiles(), true, [=](bool, QByteArray xml) {
                    auto perfiles = std::make_shared<json>(leerPerfiles(xml));
                    auto k = std::make_shared<size_t>(0);
                    // 5) La URL de cada perfil (uno por vez).
                    auto uri = std::make_shared<std::function<void()>>();
                    *uri = [=]() {
                        if (*k < perfiles->size()) {
                            const QString tok = QString::fromStdString((*perfiles)[*k].value("token", std::string()));
                            return soap(media, cuerpoUri(tok), true, [=](bool, QByteArray x) {
                                const json h = leerHojas(x, "MediaUri");
                                if (h.contains("Uri")) (*perfiles)[*k]["uri"] = rtsp::sinClave(QString::fromStdString(h["Uri"])).toStdString();
                                ++*k;
                                (*uri)();
                            });
                        }
                        QTimer::singleShot(0, this, [uri]() { *uri = nullptr; }); // ver sondearRtsp
                        (*f)["perfiles"] = *perfiles;
                        // 6) El PTZ.
                        const auto seguirImagen = [=]() {
                            // 7) La imagen (la de la primera fuente de video).
                            const std::string fuente = perfiles->empty() ? std::string() : (*perfiles)[0].value("fuente", std::string());
                            const auto analizarYPlan = [=]() {
                                obs_video_info ovi{};
                                obs_get_video_info(&ovi);
                                const json lienzo{{"ancho", ovi.base_width}, {"alto", ovi.base_height},
                                                  {"fps", ovi.fps_den ? static_cast<double>(ovi.fps_num) / ovi.fps_den : 30.0}};
                                const int i = elegirPerfil(*perfiles, static_cast<int>(ovi.base_height));
                                if (i < 0) {
                                    (*f)["plan"] = planDe(*f, json::object(), lienzo);
                                    return terminar();
                                }
                                QString u, c;
                                clave(ip, u, c);
                                const QString url = QString::fromStdString((*perfiles)[static_cast<size_t>(i)].value("uri", std::string()));
                                analizar(rtsp::conClave(url, u, c), [=](json a) {
                                    (*f)["analisis"] = a;
                                    (*f)["plan"] = planDe(*f, a.contains("error") ? json::object() : a, lienzo);
                                    terminar();
                                });
                            };
                            if (sv.imaging.isEmpty() || fuente.empty()) return analizarYPlan();
                            soap(sv.imaging, cuerpoImagen(QString::fromStdString(fuente)), true, [=](bool ok, QByteArray x) {
                                if (ok) (*f)["imagen"] = leerHojas(x, "ImagingSettings");
                                analizarYPlan();
                            });
                        };
                        if (sv.ptz.isEmpty()) {
                            (*f)["ptz"] = {{"tiene", false}};
                            return seguirImagen();
                        }
                        soap(sv.ptz, cuerpoNodos(), true, [=](bool ok, QByteArray x) {
                            json ptz{{"tiene", true}};
                            if (ok) {
                                const json h = leerHojas(x, "PTZNode");
                                ptz["nodo"] = h;
                                ptz["home"] = h.value("HomeSupported", std::string()) == "true";
                                ptz["max_presets"] = h.value("MaximumNumberOfPresets", std::string());
                            }
                            // El perfil que tiene PTZ.
                            for (const auto& p : *perfiles)
                                if (p.value("ptz", false)) {
                                    ptz["perfil"] = p.value("token", std::string());
                                    break;
                                }
                            if (!ptz.contains("perfil")) {
                                (*f)["ptz"] = ptz;
                                return seguirImagen();
                            }
                            soap(sv.ptz, ptz::onvif::getPresets(QString::fromStdString(ptz["perfil"])), true, [=](bool, QByteArray x) {
                                json pr = ptz;
                                json lista = json::array();
                                for (const auto& p : ptz::onvif::leerPresets(x))
                                    lista.push_back({{"token", p.token.toStdString()}, {"nombre", p.nombre.toStdString()}});
                                pr["presets"] = lista;
                                (*f)["ptz"] = pr;
                                seguirImagen();
                            });
                        });
                    };
                    (*uri)();
                });
            });
        });
    });
}

void Red::pedirFicha(const QString& ip) {
    ficha(ip.toStdString(), [](json) {});
}

QVariantMap Red::fichaDe(const QString& ip) const {
    const auto it = fichas_.find(ip.toStdString());
    return it == fichas_.end() ? QVariantMap() : aVariant(it->second).toMap();
}

// --- Agregar al estudio ------------------------------------------------------------------------

std::string Red::agregar(const std::string& ip, const json& op, json* hecho) {
    if (video_ == nullptr) return "el video no está disponible";
    // Solo el PTZ de una entrada que ya existe (cuando llega la clave después).
    if (op.value("solo_ptz", false)) {
        const std::string entrada = op.value("entrada", std::string());
        if (entrada.empty()) return "con `solo_ptz` hace falta `entrada` (la que ya muestra esta cámara)";
        QString u, c;
        if (!clave(ip, u, c)) return "falta la clave ONVIF: camara_credenciales primero";
        if (!camaras_) return "el PTZ no está disponible";
        const json fi = fichas_.count(ip) ? fichas_.at(ip) : json::object();
        QVariantMap conf{{"host", QString::fromStdString(ip)}, {"usuario", u}, {"clave", c}};
        const std::string pp = fi.value("ptz", json::object()).value("perfil", std::string());
        if (!pp.empty()) conf["perfil"] = QString::fromStdString(pp);
        const QString e = camaras_->configurar(QString::fromStdString(entrada), "onvif", conf);
        if (!e.isEmpty()) return e.toStdString();
        if (hecho != nullptr) *hecho = {{"entrada", entrada}, {"ptz", "ONVIF configurado"}};
        return {};
    }
    const std::string escena = op.value("escena", std::string());
    if (escena.empty() && !op.value("solo_ptz", false)) return "falta `escena` (preguntale al operador en cuál va)";
    QString usuario, pass;
    const bool conClave = clave(ip, usuario, pass);
    const json fi = fichas_.count(ip) ? fichas_.at(ip) : json::object();
    const json e = equipo(ip);
    // La URL: la pedida, la del perfil ONVIF pedido (o el del plan), o la que se vio en el escaneo.
    std::string url = op.value("url", std::string());
    std::string perfil = op.value("perfil", std::string());
    if (url.empty() && perfil.empty() && fi.contains("plan")) perfil = fi["plan"].value("argumentos", json::object()).value("perfil", std::string());
    if (url.empty() && !perfil.empty())
        for (const auto& p : fi.value("perfiles", json::array()))
            if (p.value("token", std::string()) == perfil) url = p.value("uri", std::string());
    if (url.empty() && fi.contains("url")) url = fi["url"];
    if (url.empty())
        for (const auto& r : e.value("rtsp", json::array()))
            if (r.value("codigo", 0) == 200) {
                url = r.value("url", std::string());
                break;
            }
    if (url.empty()) return "no hay una URL que ande: camara_credenciales y camara_ficha primero (o `url` a mano)";
    // ¿Ya hay una entrada con esta misma URL? Otra copia abriría otro stream
    // (doble ancho de banda, doble decodificación).
    for (const auto& ya : video_->entradasDeHost(ip))
        if (ya.value("url", std::string()) == rtsp::sinClave(QString::fromStdString(url)).toStdString() && !op.value("duplicar", false)) {
            const std::string n = ya.value("entrada", std::string());
            if (!op.value("reusar", false))
                return "ya existe la entrada «" + n + "» con esta misma URL: `reusar: true` la suma a «" + escena +
                       "» (la misma, sin abrir otro stream) o `duplicar: true` si de verdad hace falta otra";
            const std::string e2 = video_->sumarAEscena(n, escena);
            if (!e2.empty()) return e2;
            if (hecho != nullptr) *hecho = {{"entrada", n}, {"reusada", true}, {"escena", escena}};
            return {};
        }
    // El nombre: el pedido, el de ONVIF o «Marca ip».
    std::string nombre = op.value("nombre", std::string());
    if (nombre.empty()) nombre = e.value("onvif", json::object()).value("nombre", std::string());
    if (nombre.empty()) nombre = e.value("marca", json::object()).value("nombre", std::string("Cámara")) + " " + ip;
    const bool normalizar = op.value("normalizar", fi.value("plan", json::object()).value("argumentos", json::object()).value("normalizar", true));
    const int latencia = op.value("latencia_ms", 200);
    const QString uri = conClave ? rtsp::conClave(QString::fromStdString(url), usuario, pass) : QString::fromStdString(url);
    const std::string err = video_->crearEntradaRed(nombre, uri.toStdString(), latencia, escena, 1, normalizar ? 1 : 0);
    if (!err.empty()) return err;
    json h{{"entrada", nombre}, {"url", url}, {"normalizar", normalizar}, {"latencia_ms", latencia}, {"escena", escena}};
    // El PTZ por ONVIF, si lo tiene (y no se pidió que no).
    const bool tienePtz = fi.value("ptz", json::object()).value("tiene", false);
    if (op.value("ptz", tienePtz) && tienePtz && camaras_) {
        QVariantMap conf{{"host", QString::fromStdString(ip)}, {"usuario", usuario}, {"clave", pass}};
        const std::string pp = fi["ptz"].value("perfil", std::string());
        if (!pp.empty()) conf["perfil"] = QString::fromStdString(pp);
        const QString e2 = camaras_->configurar(QString::fromStdString(nombre), "onvif", conf);
        h["ptz"] = e2.isEmpty() ? json("ONVIF configurado") : json("no se pudo configurar: " + e2.toStdString());
    } else if (tienePtz) {
        h["ptz"] = "no (se pidió sin PTZ)";
    } else if (e.contains("onvif") && (!conClave || fi.empty() || fi.contains("error"))) {
        // No se sabe: ONVIF no se pudo consultar (falta la clave), no que no tenga.
        h["ptz"] = "PENDIENTE: la cámara anuncia ONVIF («" + e["onvif"].value("nombre", std::string()) +
                   "») pero no se pudo consultar (falta su usuario y clave ONVIF). Con la clave: camara_ficha y "
                   "camara_agregar {\"ip\": \"" + ip + "\", \"solo_ptz\": true, \"entrada\": \"" + nombre + "\"}. Mientras, PTZ digital.";
    } else {
        h["ptz"] = "no tiene PTZ por ONVIF (queda el PTZ digital: recorte y zoom en la GPU)";
    }
    if (hecho != nullptr) *hecho = h;
    return {};
}

QString Red::agregarDesdeUi(const QString& ip, const QString& nombre, const QString& escena) {
    json h;
    json op{{"escena", escena.toStdString()}};
    if (!nombre.trimmed().isEmpty()) op["nombre"] = nombre.trimmed().toStdString();
    return QString::fromStdString(agregar(ip.toStdString(), op, &h));
}

QVariantList Red::entradasDe(const QString& ip) const {
    if (video_ == nullptr) return {};
    const json e = video_->entradasDeHost(ip.toStdString());
    return QJsonDocument::fromJson(QByteArray::fromStdString(e.dump())).toVariant().toList();
}

QString Red::ptzDesdeUi(const QString& ip, const QString& entrada) {
    json h;
    return QString::fromStdString(agregar(ip.toStdString(), json{{"solo_ptz", true}, {"entrada", entrada.toStdString()}}, &h));
}

QString Red::reusarDesdeUi(const QString& ip, const QString& escena) {
    json h;
    return QString::fromStdString(agregar(ip.toStdString(), json{{"reusar", true}, {"escena", escena.toStdString()}}, &h));
}

// --- El PTZ real desde la página PTZ ---------------------------------------------------------------

namespace {
std::string ipDeEntrada(const QString& entrada) {
    obs_source_t* src = obs_get_source_by_name(entrada.toUtf8().constData());
    if (src == nullptr) return {};
    obs_data_t* st = obs_source_get_settings(src);
    const QString uri = QString::fromUtf8(obs_data_get_string(st, "uri"));
    obs_data_release(st);
    obs_source_release(src);
    return QUrl(uri).host().toStdString();
}
} // namespace

QVariantMap Red::camaraDeEntrada(const QString& entrada) const {
    const std::string ip = ipDeEntrada(entrada);
    if (ip.empty()) return {};
    const json e = equipo(ip);
    const json fi = fichas_.count(ip) ? fichas_.at(ip) : json::object();
    json r{{"ip", ip}, {"tieneClave", tieneClave(ip)}};
    std::string marca = e.value("marca", json::object()).value("nombre", std::string());
    if (marca.empty()) marca = fi.value("marca", json::object()).value("nombre", std::string());
    const json eq = fi.value("equipo", json::object());
    if (eq.is_object() && !eq.value("Manufacturer", std::string()).empty()) marca = eq.value("Manufacturer", std::string());
    r["marca"] = marca;
    r["modelo"] = eq.is_object() ? eq.value("Model", e.value("onvif", json::object()).value("nombre", std::string()))
                                 : e.value("onvif", json::object()).value("nombre", std::string());
    r["onvif"] = !e.value("onvif", json::object()).empty() || fi.value("onvif", false);
    if (fi.contains("ptz") && fi["ptz"].is_object()) r["ptz"] = fi["ptz"].value("tiene", false);
    else r["ptz"] = nullptr;
    QString u, c;
    r["usuario"] = clave(ip, u, c) ? u.toStdString() : e.value("usuario_de_fabrica", std::string());
    r["notas"] = e.value("notas", json::array());
    if (fi.contains("error")) r["error"] = fi["error"];
    return aVariant(r).toMap();
}

void Red::activarPtzReal(const QString& entrada, const QString& usuario, const QString& clave) {
    const std::string ip = ipDeEntrada(entrada);
    if (ip.empty()) return emit ptzListo(entrada, false, "«" + entrada + "» no es una cámara de red");
    if (!usuario.trimmed().isEmpty() && !clave.isEmpty() && !guardarClave(ip, usuario.trimmed(), clave))
        return emit ptzListo(entrada, false, "No se pudo guardar la clave");
    QPointer<Red> yo(this);
    activarPtz(ip, entrada, [yo, entrada](bool ok, QString texto) {
        if (yo) emit yo->ptzListo(entrada, ok, texto);
    });
}

void Red::activarPtz(const std::string& ip, const QString& entrada, std::function<void(bool, QString)> listo) {
    QPointer<Red> yo(this);
    ficha(ip, [yo, ip, entrada, listo](json f) {
        if (!yo) return;
        if (!f.value("onvif", false))
            return listo(false, QString::fromStdString(f.value("error", std::string("La cámara no contestó por ONVIF"))) +
                                    ". Revisá el usuario y la clave (en una Axis: un usuario ONVIF, que se crea en la web de la "
                                    "cámara, Sistema → Usuarios → ONVIF) y que la cámara tenga la hora bien.");
        const json ptz = f.value("ptz", json::object());
        if (!ptz.value("tiene", false))
            return listo(false, "La cámara contestó por ONVIF pero no ofrece PTZ (o ese usuario no tiene permiso de PTZ). Queda el PTZ digital.");
        json h;
        const std::string e = yo->agregar(ip, json{{"solo_ptz", true}, {"entrada", entrada.toStdString()}}, &h);
        if (!e.empty()) return listo(false, QString::fromStdString(e));
        const size_t presets = ptz.value("presets", json::array()).size();
        listo(true, "PTZ real activado por ONVIF" + (presets ? QString(" (%1 presets de la cámara)").arg(presets) : QString()) +
                        ". El joystick ahora mueve la cámara.");
    });
}

} // namespace ba::app::red
