// ============================================================================
// src/app/ui/FrameService.cpp — ver FrameService.hpp
// ============================================================================

#include "ui/FrameService.hpp"

#include "TimbreVistas.hpp"
#include "Vistas.hpp" // motor: Cuadro y Pixeles
#include "emision/ApiMotor.hpp"
#include "ipc/SpoutTransport.hpp"
#include "platform/Hilo.hpp"

#include "ui/LugaresCambiados.hpp"

#include <QQmlPropertyMap>
#include <QSet>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace ba::app {

namespace {
/// Un cuadro del motor como QImage, SIN COPIARLO (F3-11): la imagen se queda con una
/// referencia al buffer de GStreamer (motor::Pixeles) y lo suelta cuando la sueltan
/// todos. Antes se copiaba entero en el hilo de la interfaz, cada cuadro de cada vista.
///
/// RGB32 y no ARGB32: los cuadros son opacos (las composiciones tienen fondo
/// negro; invitados y cámaras son video) y Qt Quick sube RGB32 tal cual a la GPU
/// (BGRA8). Con ARGB32 lo convertía en CADA cuadro de CADA video a RGBA
/// premultiplicado, repartido en su pool de hilos: en Windows eran 14 hilos
/// despertándose sin parar (un cuarto de núcleo, visto con BA_PERFILAR).
QImage envolver(const motor::Cuadro& c) {
    auto* p = new motor::Pixeles(c.bgra);
    // Con los datos const: si alguien la quisiera escribir, Qt la copia antes.
    return QImage(
        static_cast<const uchar*>(p->data()), c.ancho, c.alto, c.stride, QImage::Format_RGB32,
        [](void* q) { delete static_cast<motor::Pixeles*>(q); }, p);
}

std::int64_t ahoraUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

// ============================================================================
// Canales: los canales entre procesos, leídos en su hilo (F3-11)
//
// Lo que antes hacía poll en el hilo de la interfaz con cada canal: suscribirse (y
// resuscribirse si el productor se reinició), copiar el cuadro nuevo y armar la
// QImage. Ahora lo hace este hilo y deja la imagen en un buzón; poll la recoge.
// Duerme hasta el próximo cuadro esperado del canal mirado que antes publique
// (proximaMirada, F2-14), con un tope de 50 ms para ver los pedidos nuevos.
// ============================================================================

// ============================================================================
// Despertador: poll APENAS llega un cuadro, no en la próxima vuelta del temporizador
//
// El temporizador de 16 ms (StudioViewModel) no tiene la fase de nadie: un video de 30
// parejo se tomaba a veces 17 ms y a veces 50 ms después del anterior (2026-11-05, el
// programa: lo que avanza el video, 33,3 exactos; cuándo se tomaba, p95 48,6 ms) y se veía a
// saltos. Las vistas del motor (motor/TimbreVistas.hpp) y el hilo de los canales tocan este
// timbre con cada cuadro nuevo; los toques se juntan (uno pendiente a la vez) y poll corre en
// el hilo de la interfaz. El temporizador sigue para lo demás (pedir y soltar vistas).
// ============================================================================

struct FrameService::Despertador {
    std::mutex mu;
    FrameService* fs = nullptr; ///< bajo mu: nullptr cuando se va
    std::atomic<bool> pendiente{false};
    /// SONDA: cada toque («ui:timbre», dato 1 si ya había uno en camino) y cada vez que poll
    /// corre por el timbre («ui:despierta», dato: cuánto esperó en la cola de la interfaz).
    BA_SONDA(std::atomic<std::int64_t> tocadoUs{0};
             const std::shared_ptr<ba::diag::TrazaCuadros> trTimbre = ba::diag::traza("ui:timbre");
             const std::shared_ptr<ba::diag::TrazaCuadros> trDespierta = ba::diag::traza("ui:despierta");)

    /// Desde cualquier hilo.
    static void tocar(const std::shared_ptr<Despertador>& d) {
        BA_SONDA(const std::int64_t t = ba::diag::TrazaCuadros::ahoraUs();)
        if (d->pendiente.exchange(true)) {
            BA_SONDA(d->trTimbre->anotar(t, 0, 1);)
            return; // ya hay uno en camino
        }
        BA_SONDA(d->tocadoUs.store(t, std::memory_order_relaxed); d->trTimbre->anotar(t, 0, 0);)
        std::lock_guard<std::mutex> l(d->mu);
        if (d->fs == nullptr) return;
        QMetaObject::invokeMethod(
            d->fs,
            [d]() {
                d->pendiente = false;
                BA_SONDA(const std::int64_t t = ba::diag::TrazaCuadros::ahoraUs();
                         d->trDespierta->anotar(t, 0, t - d->tocadoUs.load(std::memory_order_relaxed));)
                if (d->fs != nullptr) d->fs->poll(); // en el hilo de la interfaz: el destructor también
            },
            Qt::QueuedConnection);
    }
};

class FrameService::Canales {
   public:
    explicit Canales(std::function<void()> llego) : llego_(std::move(llego)), hilo_([this] { bucle(); }) {}
    ~Canales() {
        {
            std::lock_guard<std::mutex> l(mu_);
            seguir_ = false;
        }
        cv_.notify_all();
        hilo_.join();
    }
    Canales(const Canales&) = delete;
    Canales& operator=(const Canales&) = delete;

    /// Un canal a leer: cuándo lo miró alguien por última vez (µs; 0 = nunca) y cada cuánto
    /// quiere cuadros el que más quiere (0: todos; una celda chica de la sala, 15 por segundo).
    struct Pedido {
        QString clave;
        std::int64_t miradoUs = 0;
        std::int64_t intervaloUs = 0;
    };
    /// Qué canales leer, cuándo los miró alguien y a qué ritmo.
    void pedir(std::vector<Pedido> pedidos) {
        std::lock_guard<std::mutex> l(mu_);
        pedidos_ = std::move(pedidos);
    }
    /// Una imagen leída y la hora en que el productor la publicó (µs, reloj monotónico).
    struct Llegado {
        QImage imagen;
        std::int64_t origenUs = 0;
    };
    /// Lo que llegó desde la última vez (por clave, la última imagen).
    std::map<QString, Llegado> tomar() {
        std::lock_guard<std::mutex> l(mu_);
        return std::exchange(llegados_, {});
    }
    std::uint64_t leidos() const { return leidos_.load(); }
    std::uint64_t bytes() const { return bytes_.load(); }

   private:
    struct Lector {
        ba::ipc::SpoutReceiver r;
        std::int64_t intentoUs = 0; ///< el último intento de suscribirse
        std::int64_t leidoUs = 0;   ///< la última vez que se copió un cuadro
        std::int64_t origenUs = 0;  ///< cuándo publicó el productor el último que se copió
    };

    void bucle() {
        ba::platform::nombrarHilo("ui-canales");
        std::vector<Pedido> pedidos;
        for (;;) {
            {
                std::lock_guard<std::mutex> l(mu_);
                if (!seguir_) return;
                pedidos = pedidos_;
            }
            auto despertar = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
            for (const Pedido& p : pedidos) leer(p, despertar);
            std::unique_lock<std::mutex> l(mu_);
            const auto minimo = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
            cv_.wait_until(l, std::max(despertar, minimo), [this] { return !seguir_; });
        }
    }

    void leer(const Pedido& pedido, std::chrono::steady_clock::time_point& despertar) {
        const QString& key = pedido.clave;
        const std::int64_t pedidoUs = pedido.miradoUs;
        const std::int64_t ahora = ahoraUs();
        Lector& lec = lectores_[key];
        auto& r = lec.r;
        if (r.width() != 0 && r.stale()) r = ba::ipc::SpoutReceiver{};
        if (r.width() == 0) {
            // Nadie publica todavía: probar dos veces por segundo (eran ~20 canales
            // abriendo memoria compartida en cada vuelta).
            if (ahora - lec.intentoUs < 500'000) return;
            lec.intentoUs = ahora;
            bool ok = false;
            for (const std::wstring& n : channelsOf(key)) {
                // Lo que muestra la interfaz es BGRA: un canal en otro formato no se lee.
                if ((ok = r.subscribe(n.c_str()) && r.formato() == ba::ipc::FormatoCanal::BGRA)) break;
                r = ba::ipc::SpoutReceiver{};
            }
            if (!ok) return;
        }
        // Lo que nadie mira (una página que no se ve, un lugar vacío), una vez por segundo
        // y no a cada cuadro: uno por segundo alcanza para que una captura (MCP) no salga vieja.
        const bool mirado = ahora - pedidoUs <= 2'000'000;
        // Y se le dice al productor (SpoutSender::demanda): el gateway no prepara los cuadros
        // de un invitado que nadie mira (convertirlo entero a BGRA para esta vista). Una celda
        // fuera de vista no llama a `ultimo`: a los 2 s su pedido deja de estar mirado y queda
        // «de vez en cuando». Se anota en la cabecera al mirar (hasData/receive).
        r.pedirTodos(mirado);
        if (!mirado && ahora - lec.leidoUs < 1'000'000) return;
        // Mirado, pero solo por celdas chicas (15 por segundo): no se copia cada cuadro del
        // productor (640×360, ~0,9 MB) para tirar la mitad. Se elige por lo que AVANZÓ el video
        // (cuándo publicó el productor el más nuevo, sin leerlo) desde el último copiado, con 3/4
        // del intervalo de margen: de un canal de 30, uno sí y uno no, parejo. Antes era por el
        // reloj de este hilo (el 90 % del intervalo desde la última copia): copiaba cada ~60 ms
        // el último publicado y lo que se veía avanzaba 33, 67, 67, 33… ms (a saltos). Si no
        // toca, se vuelve a mirar con el próximo que publique.
        if (mirado && pedido.intervaloUs > 0 && lec.origenUs != 0) {
            const auto envio = static_cast<std::int64_t>(r.ultimoEnvioNs() / 1000);
            const std::int64_t avanzo = envio != 0 ? envio - lec.origenUs : ahora - lec.leidoUs;
            if (avanzo < pedido.intervaloUs * 3 / 4) {
                despertar = std::min(despertar, r.proximaMirada(std::chrono::milliseconds(50)));
                return;
            }
        }
        if (mirado) despertar = std::min(despertar, r.proximaMirada(std::chrono::milliseconds(50)));
        if (!r.hasData()) return;
        // Directo al buffer que se queda la imagen (sin inicializar: se pisa entero).
        const std::size_t cap = r.frameBytes();
        auto* p = new std::uint8_t[cap];
        ba::ipc::FrameMeta meta{};
        if (!r.receive(p, cap, meta)) {
            delete[] p;
            return;
        }
        lec.leidoUs = ahora;
        lec.origenUs = meta.qpc != 0 ? static_cast<std::int64_t>(meta.qpc / 1000) : ahora;
        // Un invitado chico viaja a su tamaño (F2-13): las filas, juntas, de ancho×4.
        const int w = meta.ancho != 0 ? static_cast<int>(meta.ancho) : r.width();
        const int h = meta.alto != 0 ? static_cast<int>(meta.alto) : r.height();
        QImage img(
            p, w, h, w * 4, QImage::Format_RGB32, [](void* q) { delete[] static_cast<std::uint8_t*>(q); }, p);
        ++leidos_;
        bytes_ += static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) * 4;
        {
            std::lock_guard<std::mutex> l(mu_);
            llegados_[key] = Llegado{std::move(img), meta.qpc != 0 ? static_cast<std::int64_t>(meta.qpc / 1000) : ahora};
        }
        if (llego_) llego_(); // la interfaz lo toma ya (Despertador)
    }

    std::mutex mu_; // lo de abajo, hasta lectores_
    std::condition_variable cv_;
    bool seguir_ = true;
    std::vector<Pedido> pedidos_;
    std::map<QString, Llegado> llegados_;
    std::map<QString, Lector> lectores_; ///< solo el hilo
    std::atomic<std::uint64_t> leidos_{0}, bytes_{0};
    std::function<void()> llego_;
    std::thread hilo_; // el último: arranca con todo lo de arriba ya construido
};

namespace {
QStringList clavesConNombre(const QStringList& claves, const std::vector<QString>& canales, const QJsonArray& salidas) {
    QStringList out = claves;
    for (const QString& c : canales) out << c;
    for (const auto& v : salidas) out << "program:" + v.toObject().value("id").toString() << "armada:" + v.toObject().value("id").toString();
    return out;
}
} // namespace

FrameService::FrameService(ApiMotor& engine, Encargar encargar, int guests, QObject* parent)
    : ProveedorCuadros(parent), engine_(engine), encargar_(std::move(encargar)), pollUs_(3600, -1) {
    slots_["program"];
    slots_["scene"];
    // Los nombres de las claves, con cada cambio de claves o salidas (antes que las vistas:
    // esta conexión es la primera). Los de los invitados, en setGuestNames.
    rotulos_ = new QQmlPropertyMap(this);
    connect(this, &FrameService::keysChanged, this, &FrameService::rehacerRotulos);
    setGuests(guests);
    despertador_ = std::make_shared<Despertador>();
    despertador_->fs = this;
    canales_ = std::make_unique<Canales>([d = despertador_]() { Despertador::tocar(d); });
    motor::ponerTimbreVistas([d = despertador_]() { Despertador::tocar(d); });
}

FrameService::~FrameService() {
    motor::ponerTimbreVistas(nullptr);
    {
        std::lock_guard<std::mutex> l(despertador_->mu);
        despertador_->fs = nullptr;
    }
    canales_.reset();
}

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
    if (key == "program") return tr("Programa");
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
    static const QString po = QString::fromLatin1(ApiMotor::kSalidaPorOmision);
    if (key == "program:" + po) return QStringLiteral("program");
    if (key == "armada:" + po) return QStringLiteral("armada");
    return key;
}

void FrameService::setArmada(const QString& escena) {
    if (escena == armada_) return;
    armada_ = escena;
    // 960 de ancho: alcanza para un monitor de tablero o un fondo, y no cuesta
    // como renderizar 1080p de una escena que ni siquiera esta al aire.
    encargar_([e = escena.toStdString()](ApiMotor& m) { m.enableScenePreview("armada", e, 960); });
    if (escena.isEmpty()) slots_.erase("armada");
}

namespace {
/// "entrada:X" → lo que se le pide al motor: "X"; "entrada-entera:X" → "entera:X";
/// "escena:X" → "X" (el motor compone una escena o una fuente por su nombre); "mini:X"
/// queda así: la escena compuesta aparte a pocos cuadros por segundo.
std::string vistaDeEntrada(const QString& clave) {
    if (clave.startsWith("escena:")) return clave.mid(7).toStdString();
    if (clave.startsWith("mini:")) return clave.toStdString();
    return clave.startsWith("entrada-entera:") ? "entera:" + clave.mid(15).toStdString() : clave.mid(8).toStdString();
}
/// El ancho de cada vista: el monitor entero (escena), la tarjeta de apuntar (entrada) o
/// la caja de la línea de tiempo (mini). El mismo al pedirla y al volver a pedirla.
int anchoDe(const QString& clave) {
    if (clave.startsWith("mini:")) return 320;
    if (clave.startsWith("escena:")) return 1280;
    return 960;
}
} // namespace

void FrameService::usar(QObject* vista, bool si, const QString& que) {
    // Solo lo que una vista puede pedir (las miniaturas son de la línea, con su presupuesto).
    // La entrada a 960 de ancho (para apuntar alcanza y sobra); la escena, con clave propia (una
    // escena y una entrada pueden llamarse igual) y a 1280: va a un monitor entero.
    if (!(que.startsWith("entrada:") || que.startsWith("entrada-entera:") || que.startsWith("escena:"))) return;
    if (que.section(':', 1).isEmpty()) return;
    vistas_.usar(vista, si, que);
}

void FrameService::mirarMiniatura(const QString& escena, bool mirar) {
    if (escena.isEmpty()) return;
    // 320 de ancho: una caja de la escaleta o de la línea, no un monitor.
    mirarClave("mini:" + escena, mirar);
}

void FrameService::mirarClave(const QString& clave, bool mirar) {
    // Cuenta pedidos: se renderiza mientras alguien la mire.
    int& n = entradas_[clave];
    const std::string canal = clave.toStdString();
    if (mirar) {
        if (n++ == 0)
            encargar_([canal, v = vistaDeEntrada(clave), w = anchoDe(clave)](ApiMotor& m) { m.enableScenePreview(canal, v, w); });
        return;
    }
    if (n > 0 && --n == 0) {
        encargar_([canal](ApiMotor& m) { m.disableScenePreview(canal); });
        entradas_.erase(clave);
        slots_.erase(clave);
    }
}

void FrameService::setGuests(int guests) {
    guests_ = guests;
    rehacerCanales();
    emit keysChanged();
}

void FrameService::setLobbySlots(int n) {
    lobbySlots_ = n;
    rehacerCanales();
}

void FrameService::rehacerCanales() {
    // Canales de memoria compartida (con resuscripcion si el escritor cambia):
    // invitados y sus pantallas (B), previews del lobby (B), feed propio (A).
    std::vector<QString>& wanted = canalesPedidos_;
    wanted.clear();
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
}

QObject* FrameService::rotulosQml() const { return rotulos_; }

void FrameService::ponerRotulo(const QString& clave) {
    const QString l = label(clave);
    const QVariant actual = rotulos_->value(clave);
    if (actual.isValid() && actual.toString() == l) return;
    rotulos_->insert(clave, l); // avisa solo esta clave
    ++rotulosEscritos_;
}

void FrameService::rehacerRotulos() {
    const QStringList claves = clavesConNombre(keys(), canalesPedidos_, salidas_);
    const QSet<QString> quedan(claves.cbegin(), claves.cend());
    // Las que ya no están quedan sin valor (la vista muestra la clave tal cual).
    for (const QString& k : rotulos_->keys())
        if (!quedan.contains(k) && rotulos_->value(k).isValid()) rotulos_->clear(k);
    for (const QString& k : claves) ponerRotulo(k);
}

void FrameService::setGuestNames(const QStringList& names) {
    if (names == names_) return;
    const QStringList antes = names_;
    names_ = names;
    // Solo los rótulos de los lugares cuyo nombre cambió: las claves no cambian (sin
    // keysChanged: eso hacía releer las claves, los colores y TODOS los rótulos a cada vista
    // con cada foto de la sala en que cambiaba un nombre).
    const std::vector<int> lugares = lugaresCambiados(antes, names);
    for (const int lugar : lugares)
        for (const QString& k : {QStringLiteral("guest:%1").arg(lugar), QStringLiteral("share:%1").arg(lugar)})
            if (rotulos_->contains(k)) ponerRotulo(k);
    if (!lugares.empty()) emit rotulosCambio(); // (el catálogo del escritorio, las fuentes de Personas)
}

std::vector<std::wstring> FrameService::channelsOf(const QString& key) {
    if (key == "operator") return {L"BA.Operator"};
    const std::wstring n = key.section(':', 1).toStdWString();
    // El invitado: la vista chica que B publica para la interfaz (F3-11) y, si no está
    // (un B de antes), el canal entero.
    if (key.startsWith("guest:")) return {L"BA.GuestUi." + n, L"BA.Guest." + n};
    if (key.startsWith("share:")) return {L"BA.GuestShare." + n};
    if (key.startsWith("lobby:")) return {L"BA.Lobby." + n};
    if (key.startsWith("backstage:")) return {L"BA.Backstage." + n};
    if (key.startsWith("registro:")) return {L"BA.Registro." + n}; // la cámara de un tótem de registro
    return {};
}

bool FrameService::ultimo(const QString& clave, QImage& out, std::uint64_t& serial, std::int64_t intervaloUs) const {
    // Lo llaman todas las vistas de video en cada vuelta con cuadros nuevos (120 celdas de
    // la sala, 60 veces por segundo): sin armar cadenas ni leer el reloj de más.
    const bool deSalida = clave.startsWith(QLatin1String("program:")) || clave.startsWith(QLatin1String("armada:"));
    const QString key = deSalida ? clavePropia(clave) : clave;
    const std::int64_t ahora = ahoraUs();
    leidoUs_ = ahora;
    // La vista de otra salida se pide sola mientras alguien la lea (ver poll).
    if (deSalida && (key.startsWith(QLatin1String("program:")) || key.startsWith(QLatin1String("armada:"))))
        salidasPedidas_[key] = ahora;
    auto it = slots_.find(key);
    if (it != slots_.end()) {
        const Slot& s = it->second; // lo que se toca es mutable
        s.pedidoUs = ahora; // alguien lo mira: a sus cuadros (al ritmo que pida)
        // El intervalo del canal: el más corto de los que leen. Uno más largo lo reemplaza
        // recién cuando el más corto deja de leer un segundo.
        const std::int64_t pedido = std::max<std::int64_t>(0, intervaloUs);
        if (pedido <= s.intervaloUs || ahora - s.intervaloVistoUs > 1'000'000) {
            s.intervaloUs = pedido;
            s.intervaloVistoUs = ahora;
        }
    }
    if (it == slots_.end() || it->second.serial == serial || it->second.image.isNull()) {
        return false;
    }
    out = it->second.image; // copia implicita (COW): barata
    serial = it->second.serial;
    return true;
}

std::map<QString, std::shared_ptr<ba::diag::RitmoCuadros>> FrameService::ritmos() const {
    std::map<QString, std::shared_ptr<ba::diag::RitmoCuadros>> out;
    BA_SONDA(const std::int64_t ahora = ahoraUs();
             for (const auto& [k, s] : slots_) if (s.ritmo && ahora - s.ritmo->ultimoUs() < 5'000'000) out[k] = s.ritmo;)
    return out;
}

std::int64_t FrameService::origenUs(const QString& key) const {
    const bool deSalida = key.startsWith(QLatin1String("program:")) || key.startsWith(QLatin1String("armada:"));
    const auto it = slots_.find(deSalida ? clavePropia(key) : key);
    return it != slots_.end() ? it->second.origenUs : 0;
}

bool FrameService::mirado() const { return ahoraUs() - leidoUs_ <= 1'000'000; }

void FrameService::pollDelTemporizador() {
    if (ahoraUs() - ultimoPollUs_ < 12'000) return;
    poll();
}

void FrameService::poll() {
    const std::int64_t t0 = ahoraUs();
    ultimoPollUs_ = t0;
    bool any = false;
    motor::Cuadro c;
    // Un canal del motor: solo si trae un cuadro nuevo, y sin copiarlo (envolver).
    const auto tomar = [&](const QString& key) {
        Slot& s = slots_[key];
        s.image = envolver(c);
        s.origenUs = c.contenidoQpc != 0 ? static_cast<std::int64_t>(c.contenidoQpc / 1000) : t0;
        ++s.serial;
        BA_SONDA(s.ritmo->cuadro(ahoraUs()); if (!s.traza) s.traza = ba::diag::traza("ui:" + key.toStdString() + ":tomado");
                 s.traza->anotar(ba::diag::TrazaCuadros::ahoraUs(), s.origenUs, static_cast<std::int64_t>(c.numero));)
        any = true;
    };
    // Programa: vista previa del motor (BGRA, ya escalada en la GPU).
    if (engine_.latestPreview(c, slots_["program"].serieMotor)) tomar("program");
    // La escena ARMADA: la que va a salir con el proximo take, renderizada
    // aunque no este al aire. Canal propio del motor, aparte del editor: son
    // dos escenas distintas al mismo tiempo y con un solo canal se pisaban
    // —abrir el editor apagaba el monitor del director—.
    if (!armada_.isEmpty() && engine_.renderScenePreview("armada", c, slots_["armada"].serieMotor)) {
        tomar("armada");
    }
    // Editor de escenas: la escena que se esta editando, renderizada aunque no
    // este al aire (la activa StudioViewModel::setScenePreview).
    if (engine_.renderScenePreview("editor", c, slots_["scene"].serieMotor)) tomar("scene");
    ++vueltas_;
    // Entradas que pidió alguien (mirarEntrada). Si la entrada se borró y
    // volvió, el canal se vuelve a tomar.
    for (auto& [key, n] : entradas_) {
        const std::string canal = key.toStdString();
        if (engine_.scenePreviewName(canal).empty()) {
            if (vueltas_ % 30 != 0) continue; // una vez por segundo, no a 30 Hz
            encargar_([canal, v = vistaDeEntrada(key), w = anchoDe(key)](ApiMotor& m) { m.enableScenePreview(canal, v, w); });
        }
        // Las miniaturas de la línea ya vienen a 5 cuadros por segundo del motor: el
        // serial solo cambia cuando hay uno nuevo.
        if (engine_.renderScenePreview(canal, c, slots_[key].serieMotor)) tomar(key);
    }
    // El programa y el preview de las demás salidas: mientras alguien los lea; a los
    // 3 s sin que nadie los mire se sueltan (cada uno es una vista más en la GPU).
    for (auto it = salidasPedidas_.begin(); it != salidasPedidas_.end();) {
        const QString& key = it->first;
        const std::string canal = key.toStdString();
        if (t0 - it->second > 3'000'000) {
            encargar_([canal](ApiMotor& m) { m.disableScenePreview(canal); });
            slots_.erase(key);
            it = salidasPedidas_.erase(it);
            continue;
        }
        // Recién pedida, o se cayó (salida borrada, motor rearmado): de nuevo, una vez por segundo.
        if (engine_.scenePreviewName(canal).empty() && (slots_.count(key) == 0 || vueltas_ % 30 == 0)) {
            slots_[key];
            encargar_([canal, bus = std::string(key.startsWith("program:") ? "programa" : "preview"),
                       salida = key.section(':', 1).toStdString()](ApiMotor& m) { m.enableSalidaPreview(canal, bus, salida, 960); });
        }
        if (engine_.renderScenePreview(canal, c, slots_[key].serieMotor)) tomar(key);
        ++it;
    }
    // Los canales entre procesos los lee su hilo (Canales): acá, decirle cuáles y cuándo
    // los miró alguien, y recoger lo que llegó.
    {
        std::vector<Canales::Pedido> pedidos;
        pedidos.reserve(canalesPedidos_.size());
        for (const QString& key : canalesPedidos_) {
            const auto it = slots_.find(key);
            if (it == slots_.end()) pedidos.push_back({key, 0, 0});
            else pedidos.push_back({key, it->second.pedidoUs, it->second.intervaloUs});
        }
        canales_->pedir(std::move(pedidos));
        for (auto& [key, llegado] : canales_->tomar()) {
            Slot& s = slots_[key];
            s.image = std::move(llegado.imagen);
            s.origenUs = llegado.origenUs;
            ++s.serial;
            BA_SONDA(s.ritmo->cuadro(ahoraUs());)
            any = true;
        }
    }
    if (any) {
        emit framesUpdated();
    }
    // SONDA: cada vuelta de poll («ui:poll»: cuándo empezó y, de dato, lo que tardó con lo que
    // hicieron los que escuchan framesUpdated).
    BA_SONDA(static const auto trPoll = ba::diag::traza("ui:poll"); trPoll->anotar(t0, 0, ahoraUs() - t0);)
    // Lo que tardó esta vuelta en la UI (con lo que hagan los que escuchan framesUpdated).
    pollUs_[pollPos_] = static_cast<std::int32_t>(std::min<std::int64_t>(ahoraUs() - t0, INT32_MAX));
    pollPos_ = (pollPos_ + 1) % pollUs_.size();
    ++polls_;
}

QJsonObject FrameService::medidas() const {
    std::vector<std::int32_t> v;
    v.reserve(pollUs_.size());
    for (std::int32_t x : pollUs_)
        if (x >= 0) v.push_back(x);
    std::sort(v.begin(), v.end());
    const auto pct = [&v](double p) {
        if (v.empty()) return 0.0;
        const auto k = std::min(v.size() - 1, static_cast<std::size_t>(p / 100.0 * static_cast<double>(v.size())));
        return v[k] / 1000.0;
    };
    return QJsonObject{{"vueltas", static_cast<double>(polls_)},
                       {"ultimo_minuto", QJsonObject{{"muestras", static_cast<int>(v.size())},
                                                     {"p50_ms", pct(50)},
                                                     {"p99_ms", pct(99)},
                                                     {"max_ms", v.empty() ? 0.0 : v.back() / 1000.0}}},
                       {"canales", QJsonObject{{"leidos", static_cast<double>(canales_->leidos())},
                                               {"bytes", static_cast<double>(canales_->bytes())}}}};
}

} // namespace ba::app

#include "moc_FrameService.cpp"
