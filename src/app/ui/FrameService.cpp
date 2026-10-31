// ============================================================================
// src/app/ui/FrameService.cpp — ver FrameService.hpp
// ============================================================================

#include "ui/FrameService.hpp"

#include "emision/BroadcastEngine.hpp"
#include "ipc/SpoutTransport.hpp"

#include <QJsonObject>

#include <chrono>

namespace ba::app {

namespace {
/// Una QImage que se queda con el buffer (BGRA = RGB32 en memoria): sin la
/// copia de más que hacía `.copy()` en el hilo de la interfaz.
///
/// RGB32 y no ARGB32: los cuadros son opacos (las composiciones tienen fondo
/// negro; invitados y cámaras son video) y Qt Quick sube RGB32 tal cual a la GPU
/// (BGRA8). Con ARGB32 lo convertía en CADA cuadro de CADA video a RGBA
/// premultiplicado, repartido en su pool de hilos: en Windows eran 14 hilos
/// despertándose sin parar (un cuarto de núcleo, visto con BA_PERFILAR).
QImage envolver(std::vector<std::uint8_t>&& v, int w, int h, int stride) {
    auto* p = new std::vector<std::uint8_t>(std::move(v));
    return QImage(
        p->data(), w, h, stride, QImage::Format_RGB32,
        [](void* q) { delete static_cast<std::vector<std::uint8_t>*>(q); }, p);
}
} // namespace

FrameService::FrameService(BroadcastEngine& engine, int guests, QObject* parent)
    : QObject(parent), engine_(engine) {
    slots_["program"];
    slots_["scene"];
    setGuests(guests);
}

FrameService::~FrameService() = default;

QStringList FrameService::keys() const {
    // Lo que se puede elegir para fondos, monitores y ventanas de salida. Los
    // previews del lobby ("lobby:k") y el feed propio ("operator") tambien se
    // pueden mostrar por clave, pero no se ofrecen en los menus.
    QStringList out;
    out << "program";
    // Segunda, pegada al programa, porque son los dos buses de la mesa: lo que
    // sale y lo que va a salir. Se miran juntos o no se miran.
    out << "armada";
    // El programa de cada una de las demás salidas (PLAN-SALIDAS): el multiview las
    // muestra todas, cada una con su color.
    out << programas().mid(1);
    for (int i = 1; i <= guests_; ++i) {
        out << QString("guest:%1").arg(i);
    }
    for (int i = 1; i <= guests_; ++i) {
        out << QString("share:%1").arg(i);
    }
    return out;
}

QString FrameService::label(const QString& key) const {
    const auto nameOr = [this](int slot, const QString& fallback) {
        const QString n = slot >= 1 && slot <= names_.size() ? names_.at(slot - 1) : QString();
        return n.isEmpty() ? fallback : n;
    };
    if (key == "program") return "Programa";
    if (key == "armada") return tr("Armada (preview)");
    if (key.startsWith("program:") || key.startsWith("armada:")) {
        const QString id = key.section(':', 1);
        QString nombre = id;
        for (const auto& v : salidas_)
            if (v.toObject().value("id").toString() == id) nombre = v.toObject().value("nombre").toString();
        return (key.startsWith("program:") ? tr("Programa · %1") : tr("Armada · %1")).arg(nombre);
    }
    if (key == "operator") return tr("Tu cámara");
    if (key.startsWith("guest:")) return nameOr(key.mid(6).toInt(), tr("Invitado %1").arg(key.mid(6)));
    if (key.startsWith("share:"))
        return tr("%1 — pantalla").arg(nameOr(key.mid(6).toInt(), tr("Invitado %1").arg(key.mid(6))));
    if (key.startsWith("lobby:")) return tr("Lobby %1").arg(key.mid(6));
    if (key.startsWith("backstage:")) return tr("Fila %1").arg(key.mid(10));
    if (key.startsWith("registro:")) return tr("Tótem de registro %1").arg(key.mid(9));
    if (key.startsWith("entrada:")) return key.mid(8);
    if (key.startsWith("escena:")) return key.mid(7);
    return key;
}

void FrameService::setSalidas(const QJsonArray& salidas) {
    if (salidas == salidas_) return;
    salidas_ = salidas;
    // Con una sola salida no hay nada que distinguir: sin colores, los cuadros como siempre.
    colores_.clear();
    if (salidas_.size() > 1)
        for (const auto& v : salidas_) {
            const QJsonObject o = v.toObject();
            colores_[o.value("porOmision").toBool() ? QStringLiteral("program") : "program:" + o.value("id").toString()] =
                o.value("color").toString();
        }
    emit keysChanged();
}

QStringList FrameService::programas() const {
    QStringList out{QStringLiteral("program")};
    for (const auto& v : salidas_) {
        const QJsonObject o = v.toObject();
        if (!o.value("porOmision").toBool()) out << "program:" + o.value("id").toString();
    }
    return out;
}

QString FrameService::clavePropia(const QString& key) {
    static const QString po = QString::fromLatin1(BroadcastEngine::kSalidaPorOmision);
    if (key == "program:" + po) return QStringLiteral("program");
    if (key == "armada:" + po) return QStringLiteral("armada");
    return key;
}

void FrameService::setArmada(const QString& escena) {
    if (escena == armada_) return;
    armada_ = escena;
    // 960 de ancho: alcanza para un monitor de tablero o un fondo, y no cuesta
    // como renderizar 1080p de una escena que ni siquiera esta al aire.
    engine_.enableScenePreview("armada", escena.toStdString(), 960);
    if (escena.isEmpty()) slots_.erase("armada");
}

namespace {
/// "entrada:X" → lo que se le pide al motor: "X"; "entrada-entera:X" → "entera:X";
/// "escena:X" → "X" (el motor compone una escena o una fuente por su nombre).
std::string vistaDeEntrada(const QString& clave) {
    if (clave.startsWith("escena:")) return clave.mid(7).toStdString();
    return clave.startsWith("entrada-entera:") ? "entera:" + clave.mid(15).toStdString() : clave.mid(8).toStdString();
}
} // namespace

void FrameService::mirarEntrada(const QString& entrada, bool mirar, bool entera) {
    if (entrada.isEmpty()) return;
    // 960 de ancho, como la armada: para apuntar alcanza y sobra.
    mirarClave((entera ? "entrada-entera:" : "entrada:") + entrada, mirar, 960);
}

void FrameService::mirarEscena(const QString& escena, bool mirar) {
    if (escena.isEmpty()) return;
    // Clave propia (una escena y una entrada pueden llamarse igual) y 1280 de
    // ancho: va a un monitor entero, no a una tarjeta.
    mirarClave("escena:" + escena, mirar, 1280);
}

void FrameService::mirarClave(const QString& clave, bool mirar, int ancho) {
    // Cuenta pedidos: se renderiza mientras alguien la mire.
    int& n = entradas_[clave];
    const std::string canal = clave.toStdString();
    if (mirar) {
        if (n++ == 0) engine_.enableScenePreview(canal, vistaDeEntrada(clave), ancho);
        return;
    }
    if (n > 0 && --n == 0) {
        engine_.disableScenePreview(canal);
        entradas_.erase(clave);
        slots_.erase(clave);
    }
}

void FrameService::setGuests(int guests) {
    guests_ = guests;
    emit keysChanged();
}

void FrameService::setGuestNames(const QStringList& names) {
    if (names == names_) return;
    names_ = names;
    emit keysChanged(); // las etiquetas cambian
}

std::wstring FrameService::channelOf(const QString& key) {
    if (key == "operator") return L"BA.Operator";
    const std::wstring n = key.section(':', 1).toStdWString();
    if (key.startsWith("guest:")) return L"BA.Guest." + n;
    if (key.startsWith("share:")) return L"BA.GuestShare." + n;
    if (key.startsWith("lobby:")) return L"BA.Lobby." + n;
    if (key.startsWith("backstage:")) return L"BA.Backstage." + n;
    if (key.startsWith("registro:")) return L"BA.Registro." + n; // la cámara de un tótem de registro
    return {};
}

namespace {
std::int64_t ahoraUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

bool FrameService::latest(const QString& clave, QImage& out, std::uint64_t& serial) const {
    const QString key = clavePropia(clave);
    // La vista de otra salida se pide sola mientras alguien la lea (ver poll).
    if (key.startsWith("program:") || key.startsWith("armada:")) salidasPedidas_[key] = ahoraUs();
    auto it = slots_.find(key);
    if (it != slots_.end()) it->second.pedidoUs = ahoraUs(); // alguien lo mira: a todos sus cuadros
    if (it == slots_.end() || it->second.serial == serial || it->second.image.isNull()) {
        return false;
    }
    out = it->second.image; // copia implicita (COW): barata
    serial = it->second.serial;
    return true;
}

void FrameService::poll() {
    bool any = false;
    int w = 0;
    int h = 0;
    int stride = 0;
    std::vector<std::uint8_t> buf;
    // Un canal del motor: solo si trae un cuadro nuevo, y sin copiarlo de nuevo.
    const auto tomar = [&](const QString& key) {
        Slot& s = slots_[key];
        s.image = envolver(std::move(buf), w, h, stride);
        ++s.serial;
        any = true;
    };
    // Programa: vista previa del motor (BGRA, ya escalada en la GPU).
    if (engine_.latestPreview(buf, w, h, stride, slots_["program"].serieMotor)) tomar("program");
    // La escena ARMADA: la que va a salir con el proximo take, renderizada
    // aunque no este al aire. Canal propio del motor, aparte del editor: son
    // dos escenas distintas al mismo tiempo y con un solo canal se pisaban
    // —abrir el editor apagaba el monitor del director—.
    if (!armada_.isEmpty() && engine_.renderScenePreview("armada", buf, w, h, stride, slots_["armada"].serieMotor)) {
        tomar("armada");
    }
    // Editor de escenas: la escena que se esta editando, renderizada aunque no
    // este al aire (la activa StudioViewModel::setScenePreview).
    if (engine_.renderScenePreview("editor", buf, w, h, stride, slots_["scene"].serieMotor)) tomar("scene");
    ++vueltas_;
    // Entradas que pidió alguien (mirarEntrada). Si la entrada se borró y
    // volvió, el canal se vuelve a tomar.
    for (auto& [key, n] : entradas_) {
        const std::string canal = key.toStdString();
        if (engine_.scenePreviewName(canal).empty()) {
            if (vueltas_ % 30 != 0) continue; // una vez por segundo, no a 30 Hz
            engine_.enableScenePreview(canal, vistaDeEntrada(key), 960);
        }
        if (engine_.renderScenePreview(canal, buf, w, h, stride, slots_[key].serieMotor)) tomar(key);
    }
    // El programa y el preview de las demás salidas: mientras alguien los lea; a los
    // 3 s sin que nadie los mire se sueltan (cada uno es una vista más en la GPU).
    for (auto it = salidasPedidas_.begin(); it != salidasPedidas_.end();) {
        const QString& key = it->first;
        const std::string canal = key.toStdString();
        if (ahoraUs() - it->second > 3'000'000) {
            engine_.disableScenePreview(canal);
            slots_.erase(key);
            it = salidasPedidas_.erase(it);
            continue;
        }
        // Recién pedida, o se cayó (salida borrada, motor rearmado): de nuevo, una vez por segundo.
        if (engine_.scenePreviewName(canal).empty() && (slots_.count(key) == 0 || vueltas_ % 30 == 0)) {
            slots_[key];
            engine_.enableSalidaPreview(canal, key.startsWith("program:") ? "programa" : "preview", key.section(':', 1).toStdString(), 960);
        }
        if (engine_.renderScenePreview(canal, buf, w, h, stride, slots_[key].serieMotor)) tomar(key);
        ++it;
    }
    // Canales de memoria compartida (con resuscripcion si el escritor cambia):
    // invitados y sus pantallas (B), previews del lobby (B), feed propio (A).
    std::vector<QString> wanted;
    for (int i = 1; i <= guests_; ++i) {
        wanted.push_back(QString("guest:%1").arg(i));
        wanted.push_back(QString("share:%1").arg(i));
    }
    for (int k = 1; k <= lobbySlots_; ++k) {
        wanted.push_back(QString("lobby:%1").arg(k));
    }
    // Previews de la fila de oradores (B publica hasta 8; los que no existen
    // simplemente no se suscriben).
    for (int k = 1; k <= kBackstageSlots; ++k) {
        wanted.push_back(QString("backstage:%1").arg(k));
    }
    // Las cámaras de los tótems de registro (B publica una por canal de conversación,
    // 1..ipc::kCanalesRegistro, el mismo tope que el gateway). Se reconoce la cara ahí y se las puede mirar.
    for (int k = 1; k <= kRegistroSlots; ++k) {
        wanted.push_back(QString("registro:%1").arg(k));
    }
    wanted.push_back("operator");
    for (const QString& key : wanted) {
        Slot& s = slots_[key];
        if (!s.receiver) {
            s.receiver = std::make_unique<ba::ipc::SpoutReceiver>();
        }
        auto& r = *s.receiver;
        if (r.width() != 0 && r.stale()) {
            r = ba::ipc::SpoutReceiver{};
        }
        if (r.width() == 0) {
            // Nadie publica todavía: probar dos veces por segundo, no 30 (eran
            // ~20 canales abriendo memoria compartida en cada vuelta).
            if (vueltas_ % 15 != 0) continue;
            const std::wstring name = channelOf(key);
            if (name.empty() || !r.subscribe(name.c_str())) {
                continue;
            }
        }
        // Lo que nadie mira (una página que no se ve, un lugar vacío), una vez por segundo
        // y no a 60: se copiaba cada cuadro de cada invitado, pantalla, lobby y orador.
        // Uno por segundo alcanza para que una captura (MCP) no salga vieja.
        if (ahoraUs() - s.pedidoUs > 2'000'000 && vueltas_ % 60 != 0) continue;
        if (!r.hasData()) continue;
        // Directo al buffer que se queda la imagen (sin inicializar: se pisa entero).
        const std::size_t bytes = r.frameBytes();
        auto* p = new std::uint8_t[bytes];
        ba::ipc::FrameMeta meta{};
        if (!r.receive(p, bytes, meta)) {
            delete[] p;
            continue;
        }
        s.image = QImage(
            p, r.width(), r.height(), r.width() * 4, QImage::Format_RGB32,
            [](void* q) { delete[] static_cast<std::uint8_t*>(q); }, p);
        ++s.serial;
        any = true;
    }
    if (any) {
        emit framesUpdated();
    }
}

} // namespace ba::app
