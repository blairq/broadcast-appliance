// ============================================================================
// src/app/agent/Voz.cpp — ver Voz.hpp
// ============================================================================
#include "agent/Voz.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QTime>

#include <algorithm>
#include <chrono>

#include <nlohmann/json.hpp>

#include "agent/ModelosLocales.hpp"
#include "AppRuntime.hpp"
#include "audio/MonitorVoz.hpp"
#if defined(BA_HAVE_CLAP)
#include "audio/RackManager.hpp"
#endif
#if defined(BA_HAVE_VOZ)
#include "audio/Escucha.hpp"
#include "audio/Habla.hpp"
#endif

namespace ba::app::agent {

namespace {
QString deRuta(const std::filesystem::path& p) { return QString::fromStdWString(p.wstring()); }
} // namespace

Voz::Voz(AppRuntime& rt, ModelosLocales& modelos, QObject* parent)
    : QObject(parent), rt_(rt), modelos_(modelos) {
#if defined(BA_HAVE_VOZ)
    escucha_ = std::make_unique<audio::Escucha>();
    habla_ = std::make_unique<audio::Habla>();
    monitor_ = std::make_unique<audio::MonitorVoz>();
    leerAjustes();
    // Las frases de la transcripción continua (llegan en el hilo de Escucha).
    escucha_->setFrase([this](const std::string& fuente, const audio::Escucha::Resultado& r) {
        QMetaObject::invokeMethod(this, [this, fuente, r]() {
            const QString f = QString::fromStdString(fuente), t = QString::fromStdString(r.texto);
            transcripcion_.push_back(QVariantMap{{"fuente", f},
                                                 {"texto", t},
                                                 {"hora", QTime::currentTime().toString("HH:mm:ss")},
                                                 {"segundos", r.segundosDeAudio}});
            while (transcripcion_.size() > 200)
                transcripcion_.removeFirst();
            emit transcripcionCambio();
            emit frase(f, t);
        });
    });
    connect(&modelos_, &ModelosLocales::cambio, this, &Voz::revisarModelos);
    connect(&modelos_, &ModelosLocales::cambio, this, &Voz::revisarVoz);
    qApp->installEventFilter(this);
    revisarModelos();
    revisarVoz();
#else
    estado_ = "Esta compilación no trae el motor de voz (make deps-sherpa-onnx).";
#endif
}

Voz::~Voz() {
#if defined(BA_HAVE_VOZ) && defined(BA_HAVE_CLAP)
    if (racks_ != nullptr) audio::RackManager::setTextoSink({}); // el audio ya no llama acá
#endif
#if defined(BA_HAVE_VOZ)
    if (habla_) habla_->callar();
    habla_.reset(); // antes que el monitor: su hilo le escribe
#endif
    rt_.operatorFeed().setPushToTalk(false);
    rt_.operatorFeed().setDictado({});
    if (qApp != nullptr) qApp->removeEventFilter(this);
}

bool Voz::disponible() const {
#if defined(BA_HAVE_VOZ)
    return true;
#else
    return false;
#endif
}

void Voz::setEstado(const QString& e) {
    estado_ = e;
    emit cambio();
}

void Voz::revisarModelos() {
#if defined(BA_HAVE_VOZ)
    const QString voz = modelos_.instaladoDeTipo("voz");
    const QString vad = modelos_.instaladoDeTipo("vad");
    if (voz.isEmpty() || vad.isEmpty()) {
        lista_ = false;
        cargados_.clear();
        setEstado(voz.isEmpty() ? "Falta bajar el reconocedor de voz (Configuración → Modelos locales)."
                                : "Falta bajar el detector de voz (Silero VAD).");
        return;
    }
    if (cargados_ == voz + "|" + vad) return;
    cargados_ = voz + "|" + vad;
    lista_ = false;
    setEstado("Cargando el reconocedor de voz…");
    const std::filesystem::path d = modelos_.carpetaInstalada(voz);
    const std::filesystem::path v = modelos_.carpetaInstalada(vad);
    audio::Escucha::Modelos m;
    m.encoder = (d / "encoder.int8.onnx").string();
    m.decoder = (d / "decoder.int8.onnx").string();
    m.joiner = (d / "joiner.int8.onnx").string();
    m.tokens = (d / "tokens.txt").string();
    m.vad = (v / "silero_vad.onnx").string();
    escucha_->cargar(m, [this](const std::string& error) {
        QMetaObject::invokeMethod(this, [this, error]() {
            lista_ = escucha_->cargado();
            setEstado(!lista_         ? "No cargó el reconocedor: " + QString::fromStdString(error)
                      : error.empty() ? "Lista: mantené " + tecla() + " (o el micrófono) y hablá."
                                      : QString::fromStdString(error));
        });
    });
#endif
}

void Voz::apretar() {
#if defined(BA_HAVE_VOZ)
    if (grabando_) return;
    if (!lista_) {
        emit notify(estado_, "warn");
        return;
    }
    OperatorFeed& feed = rt_.operatorFeed();
    // ¿Hay fuentes con voz a texto en modo dictado? Si no, se le pone solo al
    // micrófono del operador (así funciona sin pasos extra).
    bool porRack = false;
    if (racks_ != nullptr) {
        for (const auto& f : racks_->fuentesConTexto())
            if (f.value("modo", std::string()) == "dictado") porRack = true;
        const QString mic = microfono();
        if (!porRack && !mic.isEmpty() && racks_->setTexto(mic.toStdString(), "dictado").empty())
            porRack = true;
    }
    if (!porRack && !feed.hayMicrofono()) {
        emit notify("No hay micrófono del operador: elegilo en Configuración → Audio y voz.", "warn");
        return;
    }
    callar(); // el operador habla: el asistente se calla
    {
        std::lock_guard<std::mutex> l(tomasLock_);
        tomas_.clear();
    }
    // Sin racks: el micrófono del operador, directo de OperatorFeed.
    if (!porRack)
        feed.setDictado([this](const float* m, std::size_t n, std::uint32_t f) {
            alAudio("micrófono del operador", 1, m, static_cast<std::uint32_t>(n), f);
        });
    tomando_ = true;
    feed.setPushToTalk(true); // desde acá el lobby ya no oye al operador
    grabando_ = true;
    setEstado("Escuchando… (soltá para mandar)");
#endif
}

void Voz::soltar() {
#if defined(BA_HAVE_VOZ)
    if (!grabando_) return;
    tomando_ = false;
    OperatorFeed& feed = rt_.operatorFeed();
    feed.setPushToTalk(false); // el lobby vuelve a oírlo
    feed.setDictado({});
    grabando_ = false;
    // La fuente donde más se habló (si hay dos micrófonos con dictado, el
    // otro solo trajo ruido de fondo).
    Toma elegida;
    std::string de;
    {
        std::lock_guard<std::mutex> l(tomasLock_);
        for (auto& [fuente, t] : tomas_)
            if (t.energia > elegida.energia) {
                elegida = std::move(t);
                de = fuente;
            }
        tomas_.clear();
    }
    if (elegida.audio.empty()) {
        setEstado("Lista · no llegó audio (¿el micrófono está muteado o sin señal?)");
        emit notify("Voz: no llegó audio del micrófono.", "warn");
        return;
    }
    transcribiendo_ = true;
    setEstado("Transcribiendo…");
    escucha_->transcribir(
        std::move(elegida.audio), elegida.frecuencia, [this, de](const audio::Escucha::Resultado& r) {
            QMetaObject::invokeMethod(this, [this, r, de]() {
                transcribiendo_ = false;
                if (!r.error.empty()) {
                    setEstado("Lista · " + QString::fromStdString(r.error));
                    emit notify("Voz: " + QString::fromStdString(r.error), "warn");
                    return;
                }
                ultimo_ = QString::fromStdString(r.texto);
                setEstado(QString("Lista · %1 s de habla (%2) en %3 ms")
                              .arg(r.segundosDeAudio, 0, 'f', 1)
                              .arg(QString::fromStdString(de))
                              .arg(r.ms, 0, 'f', 0));
                atenderDe(ultimo_, "dictado",
                          nlohmann::json{
                              {"fuente", de}, {"segundos", r.segundosDeAudio}, {"ms_transcripcion", r.ms}});
            });
        });
#endif
}

void Voz::cancelar() {
#if defined(BA_HAVE_VOZ)
    if (!grabando_) return;
    tomando_ = false;
    rt_.operatorFeed().setPushToTalk(false);
    rt_.operatorFeed().setDictado({});
    {
        std::lock_guard<std::mutex> l(tomasLock_);
        tomas_.clear();
    }
    grabando_ = false;
    setEstado("Lista · cancelado");
#endif
}

void Voz::alAudio(const std::string& fuente, int modo, const float* m, std::uint32_t n, std::uint32_t f) {
#if defined(BA_HAVE_VOZ)
    if (modo == 2) {
        escucha_->alimentarContinuo(fuente, m, n, f);
        return;
    }
    if (modo != 1 || !tomando_) return;
    std::lock_guard<std::mutex> l(tomasLock_);
    Toma& t = tomas_[fuente];
    if (t.audio.size() > static_cast<std::size_t>(audio::Escucha::kMaxSegundos * f)) return;
    t.frecuencia = f;
    t.audio.insert(t.audio.end(), m, m + n);
    for (std::uint32_t i = 0; i < n; ++i)
        t.energia += static_cast<double>(m[i]) * m[i];
#else
    Q_UNUSED(fuente);
    Q_UNUSED(modo);
    Q_UNUSED(m);
    Q_UNUSED(n);
    Q_UNUSED(f);
#endif
}

// --- Micrófono y transcripción ------------------------------------------------------

void Voz::setRacks(audio::RackManager* racks) {
#if defined(BA_HAVE_VOZ) && defined(BA_HAVE_CLAP)
    racks_ = racks;
    if (racks_ == nullptr) return;
    audio::RackManager::setTextoSink([this](const std::string& fuente, int modo, const float* m,
                                            std::uint32_t n,
                                            std::uint32_t f) { alAudio(fuente, modo, m, n, f); });
    connect(racks_, &audio::RackManager::changed, this, [this]() {
        revisarCorrientes();
        emit cambio();
    });
    revisarCorrientes();
#else
    Q_UNUSED(racks);
#endif
}

void Voz::revisarCorrientes() {
#if defined(BA_HAVE_VOZ) && defined(BA_HAVE_CLAP)
    if (racks_ == nullptr) return;
    std::set<std::string> ahora;
    for (const auto& f : racks_->fuentesConTexto())
        if (f.value("modo", std::string()) == "continuo") ahora.insert(f.value("fuente", std::string()));
    for (const auto& c : continuas_)
        if (!ahora.contains(c)) escucha_->cerrarContinuo(c);
    for (const auto& c : ahora)
        if (!continuas_.contains(c)) escucha_->abrirContinuo(c);
    continuas_ = std::move(ahora);
#endif
}

QString Voz::microfono() const { return QString::fromStdString(rt_.config().operatorAudio); }

QString Voz::setMicrofono(const QString& fuente) {
    if (!rt_.setOperatorFeed(rt_.config().operatorVideo, fuente.toStdString()))
        return "No existe la fuente «" + fuente + "».";
    emit cambio();
    return {};
}

QVariantList Voz::fuentesTexto() const {
    QVariantList out;
#if defined(BA_HAVE_CLAP)
    if (racks_ != nullptr)
        for (const auto& f : racks_->fuentesConTexto())
            out.push_back(QVariantMap{{"fuente", QString::fromStdString(f.value("fuente", std::string()))},
                                      {"modo", QString::fromStdString(f.value("modo", std::string()))}});
#endif
    return out;
}

QString Voz::setTexto(const QString& fuente, const QString& modo) {
#if defined(BA_HAVE_CLAP)
    if (racks_ == nullptr) return "Esta compilación no tiene racks de audio.";
    if (modo.isEmpty()) {
        audio::AudioRack* r = racks_->rack(fuente.toStdString(), false);
        if (r == nullptr) return {};
        std::string e;
        r->remove("voz-a-texto", &e);
        racks_->markDirty();
        emit racks_->changed();
        return {};
    }
    if (modo != "dictado" && modo != "continuo") return "El modo es «dictado» o «continuo».";
    return QString::fromStdString(racks_->setTexto(fuente.toStdString(), modo.toStdString()));
#else
    Q_UNUSED(fuente);
    Q_UNUSED(modo);
    return "Esta compilación no tiene racks de audio.";
#endif
}

QVariantList Voz::transcripcionDe(const QString& fuente, int n) const {
    QVariantList out;
    for (auto it = transcripcion_.crbegin(); it != transcripcion_.crend() && out.size() < n; ++it)
        if (fuente.isEmpty() || it->toMap().value("fuente").toString() == fuente) out.prepend(*it);
    return out;
}

void Voz::borrarTranscripcion() {
    transcripcion_.clear();
    emit transcripcionCambio();
}

// --- Contestar hablando ----------------------------------------------------------

namespace {
QString archivoAjustes(const std::filesystem::path& raiz) { return deRuta(raiz / "Modelos" / "voz.json"); }
} // namespace

// --- Lo dictado, por el modelo local ------------------------------------------------------

void Voz::setHerramientas(Ficha ficha, Ejecutor ejecutor) {
    ficha_ = std::move(ficha);
    ejecutor_ = std::move(ejecutor);
}

void Voz::setUsarModeloLocal(bool on) {
    if (usarModeloLocal_ == on) return;
    usarModeloLocal_ = on;
    guardarAjustes();
    emit cambio();
}

QVariantList Voz::automaticas() const {
    QVariantList out;
    for (const QString& h : automaticas_) {
        const FichaHerramienta f = ficha_ ? ficha_(h) : FichaHerramienta{};
        out << QVariantMap{{"herramienta", h}, {"titulo", f.titulo.isEmpty() ? h : f.titulo}};
    }
    return out;
}

void Voz::setAutomatica(const QString& herramienta, bool on) {
    const QString h = herramienta.trimmed();
    if (h.isEmpty() || automaticas_.contains(h) == on) return;
    if (on)
        automaticas_ << h;
    else
        automaticas_.removeAll(h);
    automaticas_.sort();
    guardarAjustes();
    emit cambio();
}

void Voz::atender(const QString& frase) { atenderDe(frase, "manual", nlohmann::json::object()); }

void Voz::atenderDe(const QString& frase, const QString& origen, const nlohmann::json& meta) {
    const QString f = frase.trimmed();
    const int id = ++ultimoId_;
    nlohmann::json rec = meta;
    rec["origen"] = origen.toStdString();
    rec["texto"] = f.toStdString();
    bitacora(id, "reconocido", rec);
    if (f.isEmpty()) return;
    // Sin modelo local (o apagado a propósito): al asistente, como siempre.
    if (!usarModeloLocal_ || !modelos_.listo() || !ficha_) {
        bitacora(id, "al_asistente",
                 {{"motivo", !usarModeloLocal_ ? "modelo local apagado en Configuración"
                             : !ficha_         ? "sin herramientas MCP"
                                       : "modelo local no listo (" + modelos_.estado().toStdString() + ")"},
                  {"texto", f.toStdString()}});
        emit dictado(f);
        return;
    }
    bitacora(id, "modelo_pedido",
             {{"op", "elegir"}, {"frase", f.toStdString()}, {"modelo", modelos_.activo().toStdString()}});
    const auto t0 = std::chrono::steady_clock::now();
    modelos_.elegir(f, [this, f, id, t0](const nlohmann::json& j) {
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        QMetaObject::invokeMethod(this, [this, f, j, id, ms]() {
            bitacora(id, "modelo_respuesta", {{"respuesta", j}, {"ms_ida_y_vuelta", ms}});
            const QString h = QString::fromStdString(j.value("herramienta", std::string()));
            const FichaHerramienta ficha = h.isEmpty() ? FichaHerramienta{} : ficha_(h);
            // `derivar`, un error o algo que no existe: es del asistente.
            if (j.contains("error") || h.isEmpty() || h == "derivar" || !ficha.existe) {
                bitacora(id, "al_asistente",
                         {{"motivo", j.contains("error") ? "error del modelo"
                                     : h == "derivar"    ? "derivar"
                                                         : "herramienta inexistente: " + h.toStdString()},
                          {"texto", f.toStdString()}});
                emit dictado(f);
                return;
            }
            if (automaticas_.contains(h)) {
                bitacora(id, "automatica", {{"herramienta", h.toStdString()}});
                hacer(id, f, h, ficha);
                return;
            }
            propuesta_ = QVariantMap{{"id", id},
                                     {"frase", f},
                                     {"herramienta", h},
                                     {"titulo", ficha.titulo.isEmpty() ? h : ficha.titulo},
                                     {"prob", j.value("prob", 0.0)},
                                     {"conDatos", ficha.requiereDatos}};
            bitacora(id, "propuesta",
                     {{"herramienta", h.toStdString()},
                      {"titulo", ficha.titulo.toStdString()},
                      {"con_datos", ficha.requiereDatos}});
            emit propuestaCambio();
        });
    });
}

void Voz::hacer(int id, const QString& frase, const QString& herramienta, const FichaHerramienta& f) {
    const QString titulo = f.titulo.isEmpty() ? herramienta : f.titulo;
    if (f.requiereDatos || !ejecutor_) {
        // Faltan los datos (qué fuente, cuánto): los completa el asistente, con
        // la herramienta ya elegida para que no tenga que buscarla.
        const QString texto = frase + " (herramienta: " + herramienta + ")";
        bitacora(id, "al_asistente",
                 {{"motivo", "la herramienta pide datos"}, {"texto", texto.toStdString()}});
        emit dictado(texto);
        return;
    }
    bitacora(id, "ejecutar",
             {{"herramienta", herramienta.toStdString()}, {"argumentos", nlohmann::json::object()}});
    ejecutor_(herramienta, [this, id, titulo, herramienta](bool ok, const QString& texto) {
        bitacora(id, "resultado",
                 {{"herramienta", herramienta.toStdString()}, {"ok", ok}, {"texto", texto.toStdString()}});
        emit notify((ok ? "Hecho: " : "No se pudo: ") + titulo + (texto.isEmpty() ? "" : " — " + texto),
                    ok ? "ok" : "warn");
    });
}

void Voz::confirmar(bool noVolverAPreguntar) {
    if (propuesta_.isEmpty()) return;
    const QVariantMap p = propuesta_;
    propuesta_.clear();
    emit propuestaCambio();
    const QString h = p.value("herramienta").toString();
    const int id = p.value("id").toInt();
    bitacora(id, "decision", {{"decision", noVolverAPreguntar ? "hacer_y_no_volver_a_preguntar" : "hacer"}});
    if (noVolverAPreguntar) setAutomatica(h, true);
    hacer(id, p.value("frase").toString(), h, ficha_ ? ficha_(h) : FichaHerramienta{});
}

void Voz::descartar() {
    if (propuesta_.isEmpty()) return;
    bitacora(propuesta_.value("id").toInt(), "decision", {{"decision", "no"}});
    propuesta_.clear();
    emit propuestaCambio();
}

void Voz::alAsistente() {
    if (propuesta_.isEmpty()) return;
    bitacora(propuesta_.value("id").toInt(), "decision", {{"decision", "al_asistente"}});
    const QString frase = propuesta_.value("frase").toString();
    propuesta_.clear();
    emit propuestaCambio();
    emit dictado(frase);
}

void Voz::bitacora(int id, const std::string& evento, nlohmann::json datos) const {
    const QString dir = QString::fromStdString((rt_.dataRoot() / "Logs").string());
    QDir().mkpath(dir);
    const QString ruta = dir + "/voz-modelo.jsonl";
    // Un archivo por vez, de hasta 20 MB; el anterior queda como .1.
    if (QFileInfo(ruta).size() > 20 * 1024 * 1024) {
        QFile::remove(ruta + ".1");
        QFile::rename(ruta, ruta + ".1");
    }
    datos["ts"] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toStdString();
    datos["id"] = id;
    datos["evento"] = evento;
    QFile f(ruta);
    if (f.open(QIODevice::Append | QIODevice::WriteOnly))
        f.write(QByteArray::fromStdString(
            datos.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n"));
}

void Voz::leerAjustes() {
    QFile f(archivoAjustes(rt_.dataRoot()));
    if (!f.open(QIODevice::ReadOnly)) return;
    const nlohmann::json j = nlohmann::json::parse(f.readAll().toStdString(), nullptr, false);
    if (!j.is_object()) return;
    vozElegida_ = QString::fromStdString(j.value("voz", std::string()));
    hablante_ = j.value("hablante", 0);
    velocidad_ = std::clamp(j.value("velocidad", 1.0), 0.7, 1.5);
    usarModeloLocal_ = j.value("modeloLocal", true);
    automaticas_.clear();
    for (const auto& h : j.value("automaticas", nlohmann::json::array()))
        if (h.is_string()) automaticas_ << QString::fromStdString(h.get<std::string>());
}

void Voz::guardarAjustes() const {
    nlohmann::json autos = nlohmann::json::array();
    for (const QString& h : automaticas_)
        autos.push_back(h.toStdString());
    const nlohmann::json j{{"voz", vozElegida_.toStdString()},
                           {"hablante", hablante_},
                           {"velocidad", velocidad_},
                           {"modeloLocal", usarModeloLocal_},
                           {"automaticas", autos}};
    const QString ruta = archivoAjustes(rt_.dataRoot());
    QDir().mkpath(QFileInfo(ruta).absolutePath());
    QFile f(ruta);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QByteArray::fromStdString(j.dump(1)));
}

QVariantList Voz::voces() const {
    QVariantList out;
    for (const QVariant& v : modelos_.catalogo()) {
        QVariantMap m = v.toMap();
        if (m.value("tipo").toString() != "tts") continue;
        m["elegida"] = m.value("repo").toString() == vozElegida_;
        out.push_back(m);
    }
    return out;
}

void Voz::revisarVoz() {
#if defined(BA_HAVE_VOZ)
    // Sin elegir (o borrada): la primera bajada.
    if (vozElegida_.isEmpty() || modelos_.carpetaInstalada(vozElegida_).empty()) {
        const QString primera = modelos_.instaladoDeTipo("tts");
        if (primera != vozElegida_) {
            vozElegida_ = primera;
            hablante_ = 0;
            guardarAjustes();
        }
    }
    const std::filesystem::path d =
        vozElegida_.isEmpty() ? std::filesystem::path() : modelos_.carpetaInstalada(vozElegida_);
    if (d.empty()) {
        vozLista_ = false;
        vozCargada_.clear();
        emit cambio();
        return;
    }
    if (vozCargada_ == vozElegida_) {
        emit cambio();
        return;
    }
    vozCargada_ = vozElegida_;
    vozLista_ = false;
    QString modelo;
    for (const QVariant& v : modelos_.catalogo())
        if (v.toMap().value("repo").toString() == vozElegida_) modelo = v.toMap().value("modelo").toString();
    audio::Habla::Voz v;
    v.modelo = (d / modelo.toStdString()).string();
    v.tokens = (d / "tokens.txt").string();
    v.datos = (d / "espeak-ng-data").string();
    emit cambio();
    habla_->cargar(v, [this](const std::string& error) {
        QMetaObject::invokeMethod(this, [this, error]() {
            vozLista_ = habla_->cargada();
            hablantes_ = habla_->hablantes();
            if (hablante_ >= hablantes_) hablante_ = 0;
            if (!error.empty()) emit notify("La voz no cargó: " + QString::fromStdString(error), "warn");
            emit cambio();
        });
    });
#endif
}

void Voz::elegirVoz(const QString& repo) {
    if (repo == vozElegida_) return;
    if (modelos_.carpetaInstalada(repo).empty()) {
        emit notify("Primero hay que bajar esa voz.", "warn");
        return;
    }
    callar();
    vozElegida_ = repo;
    hablante_ = 0;
    guardarAjustes();
    revisarVoz();
}

void Voz::setHablante(int h) {
    if (h == hablante_ || h < 0) return;
    hablante_ = h;
    guardarAjustes();
    emit cambio();
}

void Voz::setVelocidad(double v) {
    v = std::clamp(v, 0.7, 1.5);
    if (v == velocidad_) return;
    velocidad_ = v;
    guardarAjustes();
    emit cambio();
}

void Voz::decir(const QString& texto) {
#if defined(BA_HAVE_VOZ)
    const QString t = texto.trimmed();
    if (t.isEmpty()) return;
    if (!vozLista_) {
        emit notify("No hay una voz cargada: bajá una en Configuración → Modelos locales.", "warn");
        return;
    }
    audio::MonitorVoz* m = monitor_.get();
    ++pendientes_;
    hablando_ = true;
    emit cambio();
    habla_->decir(
        t.toStdString(), hablante_, static_cast<float>(velocidad_),
        [m](const float* x, std::size_t n, std::uint32_t f) { m->reproducir(x, n, f); },
        [this]() {
            QMetaObject::invokeMethod(this, [this]() {
                if (pendientes_ > 0) --pendientes_;
                hablando_ = pendientes_ > 0;
                emit cambio();
            });
        });
#else
    Q_UNUSED(texto);
#endif
}

void Voz::probarVoz() {
    decir("Hola, soy la voz del asistente. Esto solo lo escuchás vos, por los auriculares.");
}

void Voz::callar() {
#if defined(BA_HAVE_VOZ)
    if (habla_) habla_->callar();
    if (monitor_) monitor_->callar();
    pendientes_ = 0;
    if (hablando_) {
        hablando_ = false;
        emit cambio();
    }
#endif
}

bool Voz::eventFilter(QObject* obj, QEvent* ev) {
    const auto t = ev->type();
    if (t == QEvent::KeyPress || t == QEvent::KeyRelease) {
        auto* k = static_cast<QKeyEvent*>(ev);
        if (k->key() == Qt::Key_F9) {
            // El filtro ve el mismo evento por cada objeto de la cadena: solo
            // cuenta el que va a la ventana, y nunca las repeticiones.
            if (!k->isAutoRepeat() && obj->isWindowType()) {
                if (t == QEvent::KeyPress)
                    apretar();
                else
                    soltar();
            }
            return true;
        }
    }
    // Si la app pierde el foco con la tecla apretada, no queda grabando (ni
    // el lobby callado).
    if (t == QEvent::ApplicationDeactivate && grabando_) soltar();
    return QObject::eventFilter(obj, ev);
}

} // namespace ba::app::agent
