// src/app/audio/Calibracion.cpp — ver el .hpp
#include "audio/Calibracion.hpp"

#include "audio/AudioRack.hpp"
#include "audio/CalibracionAnalisis.hpp"
#include "audio/LazoMedicion.hpp"
#include "audio/LspDsp.hpp"
#include "audio/Modules.hpp"
#include "audio/RackManager.hpp"
#include <QCoreApplication>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace ba::app::audio {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr int kFrecuencia = 48000;

QVariantMap aMapa(const json& j) {
    if (!j.is_object()) return {};
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, json::error_handler_t::replace)))
        .object()
        .toVariantMap();
}

void escribirWav(const fs::path& ruta, const std::vector<float>& x, int sr) {
    std::ofstream f(ruta, std::ios::binary);
    if (!f) return;
    const auto u32 = [&f](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    const auto u16 = [&f](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t datos = static_cast<uint32_t>(x.size() * 4);
    f.write("RIFF", 4);
    u32(36 + datos);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(3); // IEEE float
    u16(1);
    u32(static_cast<uint32_t>(sr));
    u32(static_cast<uint32_t>(sr * 4));
    u16(4);
    u16(32);
    f.write("data", 4);
    u32(datos);
    f.write(reinterpret_cast<const char*>(x.data()), static_cast<std::streamsize>(datos));
}

std::string ahora() { return QDateTime::currentDateTime().toString(Qt::ISODate).toStdString(); }

json* buscarEn(json& lista, const std::string& id) {
    if (!lista.is_array()) return nullptr;
    for (auto& x : lista)
        if (x.value("id", "") == id) return &x;
    return nullptr;
}

json calibracionDe(const QString& tipo, const QString& modelo, const QString& archivo, QString* error) {
    json c = {{"tipo", tipo.isEmpty() ? "modelo" : tipo.toStdString()}};
    if (tipo == "modelo" || tipo.isEmpty()) {
        c["modelo"] = modelo.isEmpty() ? "desconocido" : modelo.toStdString();
    } else if (tipo == "archivo") {
        std::ifstream f(fs::u8path(archivo.toStdString()));
        std::stringstream ss;
        ss << f.rdbuf();
        const auto t = calib::leerArchivoCalibracion(ss.str());
        if (t.empty()) {
            *error = QCoreApplication::translate("calibracion", "no se pudo leer el archivo de calibración (se esperan líneas «frecuencia dB»)");
            return {};
        }
        json p = json::array();
        for (const auto& [f0, db] : t) p.push_back({f0, db});
        c["puntos"] = p;
        c["archivo"] = fs::u8path(archivo.toStdString()).filename().u8string();
    } else if (tipo != "plano") {
        *error = QCoreApplication::translate("calibracion", "tipo de calibración: plano, modelo o archivo");
        return {};
    }
    return c;
}

} // namespace

Calibracion::Calibracion(fs::path dataRoot, bool conSimulada, QObject* parent)
    : QObject(parent), root_(std::move(dataRoot)), conSimulada_(conSimulada || qEnvironmentVariableIsSet("BA_SALA_SIMULADA")) {
    refrescarDispositivos();
}

Calibracion::~Calibracion() {
    if (lazo_) lazo_->cancelar();
    if (hilo_.joinable()) hilo_.join();
    if (med_ != nullptr)
        if (const auto* L = lspdsp::api()) L->medicionLiberar(med_);
}

bool Calibracion::disponible() const { return lspdsp::api() != nullptr; }

void Calibracion::setVista(const QString& v) {
    if (v != "respuesta" && v != "impulso" && v != "rt" && v != "propuesta") return;
    if (v == vista_) return;
    vista_ = v;
    emit vistaCambio();
}

QVariantMap Calibracion::sesion() const { return aMapa(sesionJson(false)); }

json Calibracion::sesionJson(bool conCurvas) const {
    if (!s_.is_object()) return json::object();
    json j = s_;
    if (!conCurvas)
        for (auto& m : j["mediciones"]) {
            m.erase("curva");
            m.erase("etc");
            m.erase("ir_vista");
        }
    return j;
}

QVariantList Calibracion::sesiones() const {
    QVariantList r;
    std::error_code ec;
    const fs::path base = root_ / "Mediciones" / "Calibraciones";
    for (const auto& e : fs::directory_iterator(base, ec)) {
        std::ifstream f(e.path() / "sesion.json");
        if (!f) continue;
        const json j = json::parse(f, nullptr, false);
        if (!j.is_object()) continue;
        r.append(QVariantMap{{"id", QString::fromStdString(j.value("id", ""))},
                             {"nombre", QString::fromStdString(j.value("nombre", ""))},
                             {"fecha", QString::fromStdString(j.value("creada", ""))},
                             {"mediciones", static_cast<int>(j.value("mediciones", json::array()).size())}});
    }
    std::sort(r.begin(), r.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value("fecha").toString() > b.toMap().value("fecha").toString();
    });
    return r;
}

QVariantMap Calibracion::analisis() const { return aMapa(analisis_); }
QVariantMap Calibracion::propuesta() const { return aMapa(propuesta_); }
QVariantMap Calibracion::verificacion() const { return aMapa(verificacion_); }
QVariantMap Calibracion::ultima() const { return aMapa(ultima_); }

QVariantList Calibracion::grilla() const {
    QVariantList r;
    for (double f : calib::grilla()) r.append(f);
    return r;
}

QVariantList Calibracion::modelosMicrofono() const {
    return QJsonDocument::fromJson(QByteArray::fromStdString(calib::modelosMicrofono().dump())).array().toVariantList();
}

void Calibracion::refrescarDispositivos() {
    salidas_ = dispositivosAudio(true, conSimulada_);
    entradas_ = dispositivosAudio(false, conSimulada_);
    emit dispositivosCambio();
}

fs::path Calibracion::carpeta() const {
    return root_ / "Mediciones" / "Calibraciones" / fs::u8path(s_.value("id", "sin-id"));
}

void Calibracion::guardar() {
    if (!s_.is_object()) return;
    std::error_code ec;
    fs::create_directories(carpeta(), ec);
    const fs::path tmp = carpeta() / "sesion.json.tmp";
    {
        std::ofstream f(tmp);
        f << s_.dump(1);
    }
    fs::rename(tmp, carpeta() / "sesion.json", ec);
}

json* Calibracion::buscar(const std::string& id) {
    if (!s_.is_object()) return nullptr;
    for (const char* k : {"parlantes", "microfonos", "puntos", "mediciones"})
        if (json* x = buscarEn(s_[k], id)) return x;
    return nullptr;
}
const json* Calibracion::buscar(const std::string& id) const { return const_cast<Calibracion*>(this)->buscar(id); }

std::string Calibracion::idNuevo(const std::string& prefijo) const {
    int max = 0;
    for (const char* k : {"parlantes", "microfonos", "puntos", "mediciones"})
        for (const auto& x : s_.value(k, json::array())) {
            const std::string id = x.value("id", "");
            if (id.rfind(prefijo, 0) == 0) {
                try {
                    max = std::max(max, std::stoi(id.substr(prefijo.size())));
                } catch (...) {
                }
            }
        }
    return prefijo + std::to_string(max + 1);
}

void Calibracion::setEstado(const QString& e, const QString& m) {
    estado_ = e;
    mensaje_ = m;
    emit estadoCambio();
}

void Calibracion::rehacerAnalisis() {
    if (!s_.is_object()) {
        analisis_ = propuesta_ = verificacion_ = json::object();
    } else {
        analisis_ = calib::analizar(s_);
        propuesta_ = calib::proponer(s_, analisis_);
        verificacion_ = calib::verificar(s_, analisis_);
    }
    emit analisisCambio();
}

QString Calibracion::nueva(const QString& nombre, double largo, double ancho, double alto) {
    if (estado_ == "midiendo") return tr("hay una medición en curso");
    if (largo <= 0 || ancho <= 0 || alto <= 0) return tr("las medidas de la sala van en metros, mayores que cero");
    const QDateTime t = QDateTime::currentDateTime();
    s_ = {{"id", "c" + t.toString("yyyyMMdd-HHmmss").toStdString()},
          {"nombre", nombre.isEmpty() ? "Sala" : nombre.toStdString()},
          {"creada", ahora()},
          {"sala", {{"largo", largo}, {"ancho", ancho}, {"alto", alto}}},
          {"objetivo", {{"pendiente_db_oct", -0.8}}},
          {"parlantes", json::array()},
          {"microfonos", json::array()},
          {"puntos", json::array()},
          {"mediciones", json::array()},
          {"aplicado", json::array()}};
    ultima_ = json::object();
    guardar();
    rehacerAnalisis();
    emit sesionCambio();
    emit ultimaCambio();
    return {};
}

QString Calibracion::abrir(const QString& id) {
    if (estado_ == "midiendo") return tr("hay una medición en curso");
    std::ifstream f(root_ / "Mediciones" / "Calibraciones" / fs::u8path(id.toStdString()) / "sesion.json");
    if (!f) return tr("no está la sesión %1").arg(id);
    json j = json::parse(f, nullptr, false);
    if (!j.is_object()) return tr("la sesión %1 está dañada").arg(id);
    s_ = std::move(j);
    for (const char* k : {"parlantes", "microfonos", "puntos", "mediciones", "aplicado"})
        if (!s_[k].is_array()) s_[k] = json::array();
    ultima_ = s_["mediciones"].empty() ? json::object() : s_["mediciones"].back();
    rehacerAnalisis();
    emit sesionCambio();
    emit ultimaCambio();
    return {};
}

QString Calibracion::setSala(double largo, double ancho, double alto) {
    if (!s_.is_object()) return tr("no hay sesión abierta");
    if (largo <= 0 || ancho <= 0 || alto <= 0) return tr("las medidas van en metros, mayores que cero");
    s_["sala"] = {{"largo", largo}, {"ancho", ancho}, {"alto", alto}};
    guardar();
    rehacerAnalisis();
    emit sesionCambio();
    return {};
}

QString Calibracion::setObjetivo(double pendiente) {
    if (!s_.is_object()) return tr("no hay sesión abierta");
    s_["objetivo"]["pendiente_db_oct"] = std::clamp(pendiente, -3.0, 1.0);
    guardar();
    rehacerAnalisis();
    emit sesionCambio();
    return {};
}

QString Calibracion::agregarParlante(const QString& nombre, const QString& dispositivo, int canal, double x, double y) {
    if (!s_.is_object()) return tr("no hay sesión abierta");
    if (dispositivo.isEmpty()) return tr("falta la salida (dispositivo)");
    const std::string id = idNuevo("p");
    s_["parlantes"].push_back({{"id", id},
                               {"nombre", nombre.isEmpty() ? "Parlante " + id.substr(1) : nombre.toStdString()},
                               {"salida", {{"dispositivo", dispositivo.toStdString()}, {"canal", canal}}},
                               {"x", x},
                               {"y", y},
                               {"z", 1.8}});
    guardar();
    rehacerAnalisis();
    emit sesionCambio();
    ultimoId_ = id;
    return {};
}

QString Calibracion::agregarMicrofono(const QString& nombre, const QString& dispositivo, int canal, const QString& tipo,
                                      const QString& modelo, const QString& archivo) {
    if (!s_.is_object()) return tr("no hay sesión abierta");
    if (dispositivo.isEmpty()) return tr("falta la entrada (dispositivo)");
    QString e;
    const json cal = calibracionDe(tipo, modelo, archivo, &e);
    if (!e.isEmpty()) return e;
    const std::string id = idNuevo("mic");
    s_["microfonos"].push_back({{"id", id},
                                {"nombre", nombre.isEmpty() ? "Micrófono " + id.substr(3) : nombre.toStdString()},
                                {"entrada", {{"dispositivo", dispositivo.toStdString()}, {"canal", canal}}},
                                {"calibracion", cal}});
    guardar();
    rehacerAnalisis();
    emit sesionCambio();
    ultimoId_ = id;
    return {};
}

QString Calibracion::setCalibracionMicrofono(const QString& id, const QString& tipo, const QString& modelo,
                                             const QString& archivo) {
    json* m = s_.is_object() ? buscarEn(s_["microfonos"], id.toStdString()) : nullptr;
    if (m == nullptr) return tr("no está el micrófono %1").arg(id);
    QString e;
    const json cal = calibracionDe(tipo, modelo, archivo, &e);
    if (!e.isEmpty()) return e;
    (*m)["calibracion"] = cal;
    guardar();
    rehacerAnalisis();
    emit sesionCambio();
    return {};
}

QString Calibracion::agregarPunto(const QString& nombre, const QString& tipo, double x, double y) {
    if (!s_.is_object()) return tr("no hay sesión abierta");
    const QString t = tipo.isEmpty() ? "escucha" : tipo;
    if (t != "escucha" && t != "cerca" && t != "pared" && t != "microfono")
        return tr("tipo de punto: escucha, cerca, pared o microfono");
    const std::string id = idNuevo("pt");
    s_["puntos"].push_back({{"id", id},
                            {"nombre", nombre.isEmpty() ? "Punto " + id.substr(2) : nombre.toStdString()},
                            {"tipo", t.toStdString()},
                            {"x", x},
                            {"y", y},
                            {"z", 1.2}});
    guardar();
    rehacerAnalisis();
    emit sesionCambio();
    ultimoId_ = id;
    return {};
}

QString Calibracion::mover(const QString& id, double x, double y) {
    json* o = buscar(id.toStdString());
    if (o == nullptr || o->contains("parlante")) return tr("no está %1 (parlante, micrófono o punto)").arg(id);
    const json sala = s_.value("sala", json::object());
    (*o)["x"] = std::clamp(x, 0.0, sala.value("largo", 100.0));
    (*o)["y"] = std::clamp(y, 0.0, sala.value("ancho", 100.0));
    guardar();
    emit sesionCambio();
    return {};
}

QString Calibracion::quitar(const QString& id) {
    if (!s_.is_object()) return tr("no hay sesión abierta");
    const std::string k = id.toStdString();
    for (const char* lista : {"parlantes", "microfonos", "puntos"}) {
        json& l = s_[lista];
        for (size_t i = 0; i < l.size(); ++i)
            if (l[i].value("id", "") == k) {
                l.erase(i);
                // Sus mediciones quedan (con la referencia colgando no entran al análisis).
                guardar();
                rehacerAnalisis();
                emit sesionCambio();
                return {};
            }
    }
    return tr("no está %1").arg(id);
}

QString Calibracion::borrarMedicion(const QString& id) {
    if (!s_.is_object()) return tr("no hay sesión abierta");
    json& l = s_["mediciones"];
    for (size_t i = 0; i < l.size(); ++i)
        if (l[i].value("id", "") == id.toStdString()) {
            std::error_code ec;
            fs::remove(carpeta() / fs::u8path(l[i].value("ir", "")), ec);
            l.erase(i);
            guardar();
            rehacerAnalisis();
            emit sesionCambio();
            return {};
        }
    return tr("no está la medición %1").arg(id);
}

QString Calibracion::medir(const QString& parlante, const QString& punto, const QString& microfono, const QString& etapa,
                           double duracionS, double nivelDb) {
    const auto* L = lspdsp::api();
    if (L == nullptr) return tr("falta ba-lspdsp: %1").arg(QString::fromStdString(lspdsp::error()));
    if (!s_.is_object()) return tr("no hay sesión abierta (calibracion_crear)");
    if (estado_ == "midiendo") return tr("ya hay una medición en curso");
    const json* pa = buscarEn(s_["parlantes"], parlante.toStdString());
    const json* pt = buscarEn(s_["puntos"], punto.toStdString());
    const json* mi = buscarEn(s_["microfonos"], microfono.toStdString());
    if (pa == nullptr) return tr("no está el parlante %1").arg(parlante);
    if (pt == nullptr) return tr("no está el punto %1").arg(punto);
    if (mi == nullptr) return tr("no está el micrófono %1").arg(microfono);
    const QString et = etapa.isEmpty() ? "antes" : etapa;
    if (et != "antes" && et != "verificacion" && et != "sustitucion") return tr("etapa: antes, verificacion o sustitucion");
    const QString salida = QString::fromStdString(pa->value("salida", json::object()).value("dispositivo", ""));
    const QString entrada = QString::fromStdString(mi->value("entrada", json::object()).value("dispositivo", ""));
    const bool simulada = salida == kSalaSimulada || entrada == kSalaSimulada;
    if (simulada && salida != entrada) return tr("la sala simulada va en la salida Y en la entrada (o en ninguna)");
    if (!simulada && alAire_ && alAire_()) return tr("hay una emisión o grabación en curso: el barrido saldría al aire");

    const float dur = static_cast<float>(std::clamp(duracionS > 0 ? duracionS : 4.0, 1.0, 15.0));
    // De fábrica -20 dBFS; nunca más de -6 (con el volumen del equipo se sube de a poco).
    const float amp = static_cast<float>(std::pow(10.0, std::clamp(nivelDb != 0 ? nivelDb : -20.0, -60.0, -6.0) / 20.0));
    if (med_ != nullptr) L->medicionLiberar(med_);
    med_ = L->medicionCrear(kFrecuencia, dur, 20.0f, 20000.0f, amp, 0.0f);
    if (med_ == nullptr) return tr("no se pudo armar el barrido");
    // El estímulo: lo que emite la medición sin nada en la entrada.
    std::vector<float> estimulo(L->medicionDuracionMuestras(med_)), cero(1024, 0.0f);
    L->medicionArrancar(med_);
    for (size_t i = 0; i < estimulo.size(); i += 1024)
        L->medicionProcesar(med_, estimulo.data() + i, cero.data(), std::min<size_t>(1024, estimulo.size() - i));

    if (simulada) {
        json sala = s_["sala"];
        sala["rt60"] = 0.6;
        // El micrófono simulado suena como su curva (con la corrección se cancela).
        std::vector<std::pair<double, double>> curva;
        const calib::Curva cm = calib::curvaMicrofono(*mi);
        for (size_t i = 0; i < cm.size(); i += 4) curva.emplace_back(calib::grilla()[i], cm[i]);
        lazo_ = lazoSimulado(sala, *pa, *pt, std::move(curva));
    } else {
        lazo_ = lazoReal(salida, pa->value("salida", json::object()).value("canal", 0), entrada,
                         mi->value("entrada", json::object()).value("canal", 0));
    }
    json base = {{"parlante", parlante.toStdString()},
                 {"punto", punto.toStdString()},
                 {"microfono", microfono.toStdString()},
                 {"etapa", et.toStdString()},
                 {"duracion_s", dur},
                 {"nivel_dbfs", std::round(20 * std::log10(amp) * 10) / 10},
                 {"simulada", simulada}};
    const int gen = ++generacion_;
    QPointer<Calibracion> yo(this);
    const QString e = lazo_->arrancar(estimulo, kFrecuencia, [this, yo, gen, base](std::vector<float> cap, QString err) {
        if (!yo || gen != generacion_) return;
        if (!err.isEmpty()) return terminarMedicion(base, {}, err);
        setEstado("analizando", tr("Calculando la respuesta impulsiva…"));
        if (hilo_.joinable()) hilo_.join();
        ba_medicion* m = med_;
        hilo_ = std::thread([this, yo, gen, base, m, cap = std::move(cap)]() mutable {
            std::vector<float> ir;
            json r = procesarCaptura(lspdsp::api(), m, kFrecuencia, cap, &ir);
            QMetaObject::invokeMethod(
                this,
                [this, yo, gen, base, r = std::move(r), ir = std::move(ir)]() mutable {
                    if (!yo || gen != generacion_) return;
                    if (r.contains("error"))
                        return terminarMedicion(base, {}, QString::fromStdString(r["error"].get<std::string>()));
                    json med = base;
                    med.update(r);
                    terminarMedicion(std::move(med), std::move(ir), {});
                },
                Qt::QueuedConnection);
        });
    });
    if (!e.isEmpty()) {
        lazo_.reset();
        return e;
    }
    const QString nom = QString::fromStdString(pa->value("nombre", "")) + " → " + QString::fromStdString(pt->value("nombre", ""));
    setEstado("midiendo", simulada ? tr("Midiendo %1 (sala simulada)…").arg(nom) : tr("Midiendo %1…").arg(nom));
    return {};
}

void Calibracion::cancelar() {
    if (estado_.isEmpty()) return;
    ++generacion_;
    if (lazo_) lazo_->cancelar();
    setEstado("", tr("Medición cancelada"));
}

void Calibracion::terminarMedicion(json med, std::vector<float> ir, QString error) {
    lazo_.reset();
    if (!error.isEmpty() || !s_.is_object()) {
        setEstado("", tr("No se pudo medir: %1").arg(error));
        emit medicionTerminada({}, error);
        return;
    }
    const std::string id = idNuevo("m");
    med["id"] = id;
    med["fecha"] = ahora();
    med["ir"] = "ir-" + id + ".wav";
    std::error_code ec;
    fs::create_directories(carpeta(), ec);
    escribirWav(carpeta() / fs::u8path(med["ir"].get<std::string>()), ir, kFrecuencia);
    s_["mediciones"].push_back(med);
    ultima_ = med;
    ultimoId_ = id;
    guardar();
    rehacerAnalisis();
    const double snr = med.value("snr_db", 0.0);
    QString aviso;
    const QString directo = QString::number(med.value("directo_ms", 0.0), 'f', 1);
    const QString rt60 = QString::number(med.value("rt60_s", 0.0), 'f', 2);
    if (snr < 40)
        setEstado("", tr("Medido: directo a %1 ms, RT60 %2 s — relación señal/ruido baja (%3 dB): subí el nivel o "
                         "bajá el ruido y repetí")
                          .arg(directo, rt60, QString::number(snr, 'f', 0)));
    else
        setEstado("", tr("Medido: directo a %1 ms, RT60 %2 s").arg(directo, rt60));
    emit sesionCambio();
    emit ultimaCambio();
    emit medicionTerminada(QString::fromStdString(id), {});
}

QString Calibracion::calibrarPorSustitucion(const QString& microfono, const QString& conReferencia, const QString& conMicrofono) {
    json* mic = s_.is_object() ? buscarEn(s_["microfonos"], microfono.toStdString()) : nullptr;
    const json* mr = buscar(conReferencia.toStdString());
    const json* mm = buscar(conMicrofono.toStdString());
    if (mic == nullptr) return tr("no está el micrófono %1").arg(microfono);
    if (mr == nullptr || !mr->contains("curva")) return tr("no está la medición de referencia %1").arg(conReferencia);
    if (mm == nullptr || !mm->contains("curva")) return tr("no está la medición con el micrófono %1").arg(conMicrofono);
    if (mr->value("parlante", "") != mm->value("parlante", "") || mr->value("punto", "") != mm->value("punto", ""))
        return tr("las dos mediciones tienen que ser del mismo parlante en el mismo punto");
    const json* ref = buscar(mr->value("microfono", ""));
    if (ref == nullptr) return tr("no está el micrófono de referencia");
    if (mr->value("microfono", "") == microfono.toStdString()) return tr("la referencia tiene que ser otro micrófono");
    const calib::Curva cr = calib::curvaMicrofono(*ref);
    calib::Curva d(calib::grilla().size());
    for (size_t i = 0; i < d.size(); ++i)
        d[i] = (*mm)["curva"][i].get<double>() - ((*mr)["curva"][i].get<double>() - cr[i]);
    d = calib::suavizar(d, 6);
    const double en1k = d[calib::indiceDe(1000)];
    json p = json::array();
    for (size_t i = 0; i < d.size(); i += 2) p.push_back({std::round(calib::grilla()[i] * 10) / 10, std::round((d[i] - en1k) * 100) / 100});
    (*mic)["calibracion"] = {{"tipo", "sustitucion"},
                             {"puntos", p},
                             {"referencia", ref->value("nombre", "")},
                             {"mediciones", {conReferencia.toStdString(), conMicrofono.toStdString()}},
                             {"fecha", ahora()}};
    guardar();
    rehacerAnalisis();
    emit sesionCambio();
    return {};
}

QString Calibracion::aplicar(const QString& parlante, const QString& fuente) {
    if (racks_ == nullptr) return tr("no hay racks de audio");
    const json prop = propuesta_.value("parlantes", json::object()).value(parlante.toStdString(), json::object());
    if (!prop.contains("filtros")) return tr("no hay propuesta para %1 (medí y analizá primero)").arg(parlante);
    std::string e;
    AudioRack* r = racks_->rack(fuente.toStdString(), true, &e);
    if (r == nullptr) return tr("no hay rack para «%1»: %2").arg(fuente, QString::fromStdString(e));
    const ModuleType* eq = findModule("eq");
    std::string nodo = "eq-sala";
    if (r->plugin(nodo) == nullptr) {
        nodo = r->addPlugin(eq->pluginId, "EQ de sala", "", &e, "eq-sala");
        if (nodo.empty()) return tr("no se pudo agregar el EQ: %1").arg(QString::fromStdString(e));
    }
    ClapInstance* inst = r->plugin(nodo);
    if (inst == nullptr) return tr("el EQ no quedó cargado");
    json bandas = json::array();
    int b = 0;
    for (const auto& f : prop["filtros"]) {
        if (b >= eq->bandCount) break;
        const std::string t = f.value("tipo", "pico");
        bandas.push_back({{"banda", b++},
                          {"tipo", t == "estante_graves" ? "Lo-shelf" : t == "estante_agudos" ? "Hi-shelf" : "Bell"},
                          {"frecuencia_hz", f.value("f", 1000.0)},
                          {"ganancia_db", f.value("ganancia_db", 0.0)},
                          {"q", f.value("q", 1.0)}});
    }
    for (; b < eq->bandCount; ++b) bandas.push_back({{"banda", b}, {"tipo", "Off"}});
    const json res = applyModule(*inst, eq, {{"bandas", bandas}});
    racks_->markDirty();
    if (res.contains("errores") && !res["errores"].empty())
        return tr("el EQ quedó a medias: %1").arg(QString::fromStdString(res["errores"].dump()));
    s_["aplicado"].push_back({{"fecha", ahora()}, {"parlante", parlante.toStdString()}, {"fuente", fuente.toStdString()},
                              {"nodo", nodo}, {"filtros", prop["filtros"]}});
    guardar();
    emit sesionCambio();
    setEstado("", tr("Aplicado en el rack de «%1» (nodo %2). Ahora: verificar (medir los mismos puntos con etapa "
                     "verificacion).")
                      .arg(fuente, QString::fromStdString(nodo)));
    return {};
}

QString Calibracion::revertir(const QString& fuente) {
    if (racks_ == nullptr) return tr("no hay racks de audio");
    AudioRack* r = racks_->rack(fuente.toStdString(), false);
    if (r == nullptr || r->plugin("eq-sala") == nullptr) return tr("«%1» no tiene EQ de sala").arg(fuente);
    std::string e;
    if (!r->remove("eq-sala", &e)) return QString::fromStdString(e);
    racks_->markDirty();
    if (s_.is_object()) {
        s_["aplicado"].push_back({{"fecha", ahora()}, {"fuente", fuente.toStdString()}, {"revertido", true}});
        guardar();
        emit sesionCambio();
    }
    setEstado("", tr("Sacado el EQ de sala de «%1»").arg(fuente));
    return {};
}

json Calibracion::medicionJson(const std::string& id) const {
    const json* m = buscar(id);
    return m != nullptr && m->contains("parlante") ? *m : json();
}

QVariantMap Calibracion::medicion(const QString& id) const { return aMapa(medicionJson(id.toStdString())); }

QVariantMap Calibracion::curvas(const QString& parlante) const {
    json r = json::object();
    if (!s_.is_object()) return {};
    const std::string pid = parlante.toStdString();
    const json a = analisis_.value("parlantes", json::object()).value(pid, json::object());
    const json p = propuesta_.value("parlantes", json::object()).value(pid, json::object());
    const json v = verificacion_.value(pid, json::object());
    // Cada punto de escucha: la última medición «antes», corregida por su micrófono y suavizada a 1/6.
    json puntos = json::array();
    for (const auto& ref : a.value("mediciones", json::array())) {
        const json* m = buscar(ref.value("medicion", ""));
        const json* pt = buscar(ref.value("punto", ""));
        if (m == nullptr || !m->contains("curva")) continue;
        calib::Curva c(calib::grilla().size());
        for (size_t i = 0; i < c.size(); ++i) c[i] = (*m)["curva"][i].get<double>();
        if (const json* mic = buscar(m->value("microfono", ""))) {
            const calib::Curva cm = calib::curvaMicrofono(*mic);
            for (size_t i = 0; i < c.size(); ++i) c[i] -= cm[i];
        }
        c = calib::suavizar(c, 6);
        json cj = json::array();
        for (double x : c) cj.push_back(std::round(x * 10) / 10);
        puntos.push_back({{"punto", ref.value("punto", "")}, {"nombre", pt ? pt->value("nombre", "") : ""}, {"curva", cj}});
    }
    r["puntos"] = puntos;
    for (const char* k : {"promedio", "modos", "pozos", "confianza"})
        if (a.contains(k)) r[k] = a[k];
    for (const char* k : {"objetivo", "previsto", "filtros", "no_se_corrige"})
        if (p.contains(k)) r[k] = p[k];
    if (v.contains("despues")) r["despues"] = v["despues"];
    return aMapa(r);
}

} // namespace ba::app::audio
