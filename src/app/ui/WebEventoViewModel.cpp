// ============================================================================
// src/app/ui/WebEventoViewModel.cpp — ver WebEventoViewModel.hpp
// ============================================================================
#include "ui/WebEventoViewModel.hpp"
#include "ui/ConferenceClient.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimeZone>

#include "ajustes/Ajustes.hpp"
#include "arranque/AppRuntime.hpp"
#include "evento/Documentos.hpp"
#include "evento/EventoWeb.hpp"
#include "persist/ColaEscritura.hpp"
#include "sala/ControlSala.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/CorridaViewModel.hpp"
#include "ui/EventoAbierto.hpp"
#include <QCoreApplication>
#include "ui/EventosListaViewModel.hpp"
#include "ui/PadronViewModel.hpp"

namespace ba::app {

namespace {
using json = nlohmann::json;
QVariantMap aMapa(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, json::error_handler_t::replace))).object().toVariantMap();
}
} // namespace

WebEventoViewModel::WebEventoViewModel(EventoAbierto& abierto, CorridaViewModel& corrida, PadronViewModel& padron,
                                       EventosListaViewModel& lista, AppRuntime& rt, ConferenceViewModel* sala, QObject* parent)
    : QObject(parent), ab_(abierto), corrida_(corrida), padron_(padron), lista_(lista), rt_(rt), sala_(sala) {
    fondo_.setMaxThreadCount(1);
    connect(&ab_, &EventoAbierto::abiertoCambio, this, [this]() {
        ultimo_.clear();
        leerSitio();
    });
    // Las secciones son los bloques: cambian con el guion.
    connect(&ab_, &EventoAbierto::guionCambio, this, [this]() {
        leerSitio();
        publicarWeb();
    });
    connect(&corrida_, &CorridaViewModel::correChanged, this, &WebEventoViewModel::publicarWeb);
    // Una sala que arrancó de nuevo (otro «epoch») no sabe lo del evento: se le vuelve a dar.
    if (sala_ != nullptr) sala_->client().registrarRepublicador("evento-web", this, [this]() { salaReiniciada(); });
    leerSitio();
}

WebEventoViewModel::~WebEventoViewModel() {
    // Lo que se arma en el fondo usa this: termina antes (su respuesta encolada ya no llega).
    fondo_.waitForDone();
}

QString WebEventoViewModel::carpetaWebAbs() const {
    if (!ab_.hay()) return {};
    return QString::fromStdWString(evento::carpetaWeb(ab_.dir()).wstring());
}

void WebEventoViewModel::leerSitio() {
    const int gen = ++genSitio_;
    if (!ab_.hay()) {
        hayPortada_ = false;
        web_ = QVariantMap{{"hay", false}};
        emit webChanged();
        return;
    }
    // Qué páginas hay y sitio.json: del disco, en el fondo.
    QPointer<WebEventoViewModel> yo(this);
    fondo_.start([yo, gen, e = ab_.evento(), dir = carpetaWebAbs()]() {
        ba::persist::cola().vaciar(); // una página recién guardada, ya en el disco
        const std::filesystem::path d = dir.toStdWString();
        std::error_code ec;
        const bool hay = std::filesystem::exists(d / "portada.html", ec);
        json paginas = json::array();
        paginas.push_back({{"pagina", "portada.html"}, {"nombre", QCoreApplication::translate("EventoViewModel", "Portada (antes de empezar)").toStdString()}, {"existe", hay}});
        for (std::size_t i = 0; i < e.bloques.size(); ++i) {
            const std::string p = evento::paginaDeBloque(e.bloques[i], i);
            paginas.push_back({{"pagina", p}, {"nombre", e.bloques[i].nombre}, {"existe", std::filesystem::exists(d / p, ec)}});
        }
        paginas.push_back({{"pagina", "cierre.html"},
                           {"nombre", QCoreApplication::translate("EventoViewModel", "Cierre (al terminar)").toStdString()},
                           {"existe", std::filesystem::exists(d / "cierre.html", ec)}});
        const json sitio = evento::SitioWeb::leer(d, e).aJson();
        QMetaObject::invokeMethod(
            yo,
            [yo, gen, hay, paginas, sitio, dir]() {
                if (!yo || gen != yo->genSitio_) return;
                const bool antes = yo->hayPortada_;
                yo->hayPortada_ = hay;
                yo->web_ = QVariantMap{{"hay", hay},
                                       {"carpeta", dir},
                                       {"paginas", QJsonDocument::fromJson(QByteArray::fromStdString(paginas.dump())).array().toVariantList()},
                                       {"sitio", aMapa(sitio)},
                                       {"aviso", yo->avisoWeb()}};
                emit yo->webChanged();
                if (hay != antes) yo->publicarWeb();
            },
            Qt::QueuedConnection);
    });
}

void WebEventoViewModel::cambio() {
    ++version_;
    ultimo_.clear();
    leerSitio();
    publicarWeb();
}

QVariantMap WebEventoViewModel::generarWeb(bool sobrescribir) {
    if (!ab_.hay()) return {{"error", QCoreApplication::translate("EventoViewModel", "No hay ningún evento abierto.")}};
    const evento::ResultadoWeb r = evento::generar(ab_.evento(), carpetaWebAbs().toStdWString(), sobrescribir);
    QStringList esc, sal;
    for (const auto& x : r.escritas) esc << QString::fromStdString(x);
    for (const auto& x : r.salteadas) sal << QString::fromStdString(x);
    cambio();
    if (!r.error.empty()) return {{"error", QString::fromStdString(r.error)}, {"escritas", esc}, {"salteadas", sal}};
    corrida_.anotar(QCoreApplication::translate("EventoViewModel", "Página web del evento: %1 archivos generados").arg(QString::number(esc.size())), "evento");
    return {{"escritas", esc}, {"salteadas", sal}, {"carpeta", carpetaWebAbs()}};
}

QString WebEventoViewModel::setWebSitio(const QVariantMap& campos) {
    if (!ab_.hay()) return QCoreApplication::translate("EventoViewModel", "No hay ningún evento abierto.");
    const std::filesystem::path dir = carpetaWebAbs().toStdWString();
    evento::SitioWeb s = evento::SitioWeb::leer(dir, ab_.evento());
    if (campos.contains("titulo")) s.titulo = campos.value("titulo").toString().toStdString();
    if (campos.contains("bajada")) s.bajada = campos.value("bajada").toString().toStdString();
    if (campos.contains("color")) s.color = campos.value("color").toString().toStdString();
    if (campos.contains("fondo")) s.fondo = campos.value("fondo").toString().toStdString();
    if (campos.contains("barra")) s.barra = campos.value("barra").toBool();
    if (campos.contains("direccion_al_iniciar")) s.direccionAlIniciar = campos.value("direccion_al_iniciar").toBool();
    if (campos.contains("participacion")) s.participacion = campos.value("participacion").toBool();
    if (campos.contains("vivos")) {
        const QVariantMap v = campos.value("vivos").toMap();
        for (auto it = v.begin(); it != v.end(); ++it) {
            const std::string k = it.key().toStdString();
            if (k != "youtube" && k != "twitch" && k != "facebook") continue;
            const std::string val = it.value().toString().trimmed().toStdString();
            if (val.empty()) s.vivos.erase(k);
            else s.vivos[k] = val;
        }
    }
    if (campos.contains("subdominio")) {
        const std::string sub = evento::subdominioDe(campos.value("subdominio").toString().toStdString());
        if (sub.empty() && !campos.value("subdominio").toString().trimmed().isEmpty())
            return QCoreApplication::translate("EventoViewModel", "El subdominio va solo con letras minúsculas y números.");
        s.subdominio = sub;
    }
    if (!s.guardar(dir)) return QCoreApplication::translate("EventoViewModel", "No se pudo guardar sitio.json.");
    cambio();
    return {};
}

QString WebEventoViewModel::leerPaginaWeb(const QString& pagina) const {
    if (!ab_.hay()) return {};
    const std::string rel = evento::resolverPagina(ab_.evento(), pagina.toStdString());
    if (rel.empty()) return {};
    std::string texto; // lo encolado para guardar cuenta (escribirPaginaWeb)
    return ba::persist::cola().leer((carpetaWebAbs() + "/" + QString::fromStdString(rel)).toStdWString(), texto)
               ? QString::fromStdString(texto)
               : QString();
}

QString WebEventoViewModel::escribirPaginaWeb(const QString& pagina, const QString& contenido) {
    if (!ab_.hay()) return QCoreApplication::translate("EventoViewModel", "No hay ningún evento abierto.");
    const std::string rel = evento::resolverPagina(ab_.evento(), pagina.toStdString());
    if (rel.empty())
        return QCoreApplication::translate("EventoViewModel", "No conozco la página «%1»: portada, cierre, una sección o un .html propio.").arg(pagina);
    // El escritor único, por su cola (entero o nada, fuera de la interfaz; crea la carpeta):
    // lo que lee la página (leerPaginaWeb) y lo que arma la web en el fondo ya lo ven.
    ba::persist::cola().escribir((carpetaWebAbs() + "/" + QString::fromStdString(rel)).toStdWString(),
                                 contenido.toUtf8().toStdString());
    cambio();
    return {};
}

void WebEventoViewModel::setAvisoWeb(const QString& texto, const QString& nivel) {
    const QString t = texto.trimmed().left(280);
    aviso_ = t.isEmpty() ? json::object()
                         : json{{"texto", t.toStdString()},
                                {"nivel", nivel == "urgente" ? "urgente" : "info"},
                                {"desdeMs", QDateTime::currentMSecsSinceEpoch()}};
    if (!t.isEmpty()) corrida_.anotar(QCoreApplication::translate("EventoViewModel", "Aviso en la página del evento: %1").arg(t), "evento");
    web_["aviso"] = avisoWeb();
    emit webChanged();
    publicarWeb();
}

QVariantMap WebEventoViewModel::avisoWeb() const { return aMapa(aviso_); }

void WebEventoViewModel::asegurarBase() {
    if (!ab_.hay()) return;
    // Solo lo que falta (una sección nueva, la portada de un evento recién creado): lo que
    // ya está —editado a mano o por el asistente— no se toca.
    const evento::ResultadoWeb r = evento::generar(ab_.evento(), carpetaWebAbs().toStdWString(), false);
    if (!r.escritas.empty()) cambio();
}

void WebEventoViewModel::salaReiniciada() {
    ultimo_.clear();
    publicarWeb();
}

std::string WebEventoViewModel::subdominioAlIniciar(const std::string& carpeta) const {
    // Su página web con dirección propia: que la dirección pública pase a ser esa.
    const std::filesystem::path dir = evento::carpetaWeb(ab_.store().carpetaDe(carpeta));
    std::error_code ec;
    if (!std::filesystem::exists(dir / "portada.html", ec)) return {};
    const evento::Evento ev = QString::fromStdString(carpeta) == ab_.carpeta() ? ab_.evento() : ab_.leer(carpeta, nullptr, nullptr);
    const evento::SitioWeb s = evento::SitioWeb::leer(dir, ev);
    return s.direccionAlIniciar ? s.subdominio : std::string();
}

void WebEventoViewModel::publicarWeb() {
    if (sala_ == nullptr) return;
    if (!ab_.hay() || !hayPortada_) {
        publicarCuerpo(json{{"activo", false}});
        return;
    }
    // Uno a la vez: lo que se pide mientras se arma sale cuando termina (con lo de ese momento).
    if (armando_) {
        otraVez_ = true;
    } else {
        armando_ = true;
        armarEnElFondo(carpetaWebAbs());
    }
}

void WebEventoViewModel::armarEnElFondo(const QString& dir) {
    // Lo que es de la interfaz (la corrida, la sala, las preguntas, el padrón), acá.
    const CorridaViewModel::Publico pub = corrida_.publico();
    std::string est = pub.estado;
    const int bloque = pub.bloque;
    const evento::Evento& e = ab_.evento();
    if (est == "proximo" && lista_.datos(ab_.carpeta().toStdString()).estado == "terminado") est = "terminado";
    if ((est == "en_vivo" || est == "pausa") && arranqueMs_ == 0) arranqueMs_ = QDateTime::currentMSecsSinceEpoch();
    if (est == "proximo") arranqueMs_ = 0;
    qint64 inicioMs = 0;
    if (!e.fecha.empty() && !e.inicio.empty()) {
        const QTimeZone tz = e.huso.empty() ? QTimeZone::systemTimeZone() : QTimeZone(QByteArray::fromStdString(e.huso));
        const QDateTime dt(QDate::fromString(QString::fromStdString(e.fecha), "yyyy-MM-dd"),
                           QTime::fromString(QString::fromStdString(e.inicio), "HH:mm"), tz.isValid() ? tz : QTimeZone::systemTimeZone());
        if (dt.isValid()) inicioMs = dt.toMSecsSinceEpoch();
    }
    evento::ExtrasVivo x;
    x.aviso = aviso_;
    if (preguntas_) x.preguntas = preguntas_();
    if (enLaSala_) x.enLaSala = enLaSala_();
    if (datosSala_) x.sala = datosSala_();
    {
        // La asistencia sale del padrón del evento (cuántos se anotaron y cuántos llegaron).
        const QVariantMap r = padron_.resumenPadron();
        if (r.value("total").toInt() > 0)
            x.sala["asistencia"] = {{"total", r.value("total").toInt()}, {"presentes", r.value("presentes").toInt()}};
    }
    // Cuándo empezó de verdad cada sección (para el horario estimado de las que siguen).
    if (est == "proximo") inicioReal_.clear();
    if (bloque >= 0 && !inicioReal_.count(bloque)) inicioReal_[bloque] = QDateTime::currentMSecsSinceEpoch();
    for (const auto& [b, ms] : inicioReal_) x.inicioReal[b] = ms;
    if (pub.restanteActualSeg >= 0) {
        x.restanteActualSeg = pub.restanteActualSeg;
        x.habla = pub.habla;
    }
    // Lo que lee el disco (el sitio, la documentación) y arma el JSON, en el fondo: con un
    // sitio armado tardaba ~50 ms en la interfaz.
    fondo_.start([this, dir, e, est, bloque, inicioMs, arranco = arranqueMs_, x = std::move(x),
                  plataformas = plataformas_ ? plataformas_() : json::object(), version = version_, raiz = rt_.dataRoot()]() mutable {
        ba::persist::cola().vaciar(); // una página recién guardada, ya en el disco
        const evento::SitioWeb s = evento::SitioWeb::leer(dir.toStdWString(), e);
        x.vivos = evento::armarVivos(s, plataformas);
        json estado = evento::estadoPublico(e, s, est, bloque, inicioMs, arranco, 0, x);
        estado["version"] = version;
        // La documentación pública del evento (docs/): la web la muestra y la sala la sirve.
        const evento::Documentos docs(raiz);
        estado["documentos"] = docs.publicos();
        // Para la sala (no se publica): el archivo de cada documento público, por su id.
        json docsArchivos = json::object();
        for (const auto& d : docs.lista())
            if (d.value("publico", true)) docsArchivos[d.value("id", std::string())] = d.value("archivo", std::string());
        const std::u8string u = docs.carpeta().u8string();
        json cuerpo = {{"activo", true},
                       {"dir", dir.toStdString()},
                       {"docs_archivos", docsArchivos},
                       {"docs_dir", std::string(u.begin(), u.end())},
                       {"estado", estado}};
        QMetaObject::invokeMethod(
            this,
            [this, cuerpo = std::move(cuerpo)]() {
                armando_ = false;
                publicarCuerpo(cuerpo);
                if (otraVez_) {
                    otraVez_ = false;
                    publicarWeb();
                }
            },
            Qt::QueuedConnection);
    });
}

void WebEventoViewModel::publicarCuerpo(const json& cuerpo) {
    if (sala_ == nullptr) return;
    const QByteArray b = QByteArray::fromStdString(cuerpo.dump());
    if (b == ultimo_) return;
    ultimo_ = b;
    // La hora de la sala va aparte (cambia siempre y no tiene que forzar el reenvío).
    json conHora = cuerpo;
    if (conHora.contains("estado")) conHora["estado"]["ahoraMs"] = QDateTime::currentMSecsSinceEpoch();
    sala_->control().publicarEventoWeb(conHora);
}

} // namespace ba::app
