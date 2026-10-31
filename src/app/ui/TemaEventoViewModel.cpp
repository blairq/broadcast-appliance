// ============================================================================
// src/app/ui/TemaEventoViewModel.cpp — ver TemaEventoViewModel.hpp
// ============================================================================
#include "ui/TemaEventoViewModel.hpp"

#include <QJsonDocument>
#include <QPointer>
#include <QUrl>

#include "evento/EventoWeb.hpp"
#include "i18n/I18n.hpp"
#include "sala/Pedido.hpp"
#include "tema/TemaWeb.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EventoViewModel.hpp"

namespace ba::app {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

QVariantMap aMapa(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump())).object().toVariantMap();
}

std::string utf8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

/// Un texto acotado y sin espacios en las puntas.
std::string recorte(const json& v, std::size_t tope) {
    if (!v.is_string()) return {};
    QString s = QString::fromStdString(v.get<std::string>()).trimmed().left(static_cast<int>(tope));
    return s.toStdString();
}

bool nivelValido(const std::string& n) { return n == "nada" || n == "acento" || n == "todo"; }

} // namespace

TemaEventoViewModel::TemaEventoViewModel(EventoViewModel& evento, ConferenceViewModel& sala, QObject* parent)
    : QObject(parent), evento_(evento), sala_(sala) {
    // El evento abierto: guionChanged avisa también cuando se abre otro (ver `abierto`).
    connect(&evento_, &EventoViewModel::guionChanged, this, &TemaEventoViewModel::seguirEvento);
    // Los colores del sitio mandan mientras el tema no fije los suyos (paraPublicar).
    connect(&evento_, &EventoViewModel::webChanged, this, &TemaEventoViewModel::recargar);
    // La sala vuelve (o arranca de nuevo): no se acuerda del tema, se lo vuelve a dar.
    connect(&sala_, &ConferenceViewModel::onlineChanged, this, [this]() {
        if (!sala_.online()) return;
        mandado_.clear();
        publicar();
    });
    seguirEvento();
}

void TemaEventoViewModel::seguirEvento() {
    const fs::path p = evento_.carpetaAbierta();
    if (p == carpetaEvento_ && !p.empty()) return;
    carpetaEvento_ = p;
    recargar();
}

void TemaEventoViewModel::recargar() {
    tema_ = hayEvento() ? evento::TemaEvento::leer(carpetaEvento_) : evento::TemaEvento{};
    resuelto_ = tema_.existe ? ba::tema::resolver(tema_.paraPublicar(carpetaEvento_)) : json::object();
    emit cambio();
    publicar();
}

void TemaEventoViewModel::publicar() {
    if (!sala_.online()) return; // al volver la sala (onlineChanged) se manda
    const json cuerpo{{"tema", tema_.existe ? tema_.paraPublicar(carpetaEvento_) : json::object()},
                      {"carpeta", hayEvento() ? utf8(evento::carpetaTema(carpetaEvento_)) : std::string()}};
    const std::string d = cuerpo.dump();
    if (d == mandado_) return;
    mandado_ = d;
    QPointer<TemaEventoViewModel> yo(this);
    sala::pedir(sala_.client(), "POST", "/api/v1/tema-evento", cuerpo, [yo, d](const std::string& err, const json&) {
        if (yo && !err.empty() && yo->mandado_ == d) yo->mandado_.clear(); // se reintenta cuando la sala vuelva
    });
}

QString TemaEventoViewModel::carpeta() const {
    return hayEvento() ? QString::fromStdString(utf8(evento::carpetaTema(carpetaEvento_))) : QString();
}

QString TemaEventoViewModel::urlDe(const std::string& archivo) const {
    if (archivo.empty() || !hayEvento()) return {};
    return QUrl::fromLocalFile(QString::fromStdString(utf8(evento::carpetaTema(carpetaEvento_) / archivo))).toString();
}

QVariantMap TemaEventoViewModel::tema() const { return aMapa(tema_.aJson()); }

QVariantMap TemaEventoViewModel::resuelto() const { return aMapa(resuelto_); }

QStringList TemaEventoViewModel::avisos() const {
    QStringList out;
    for (const json& a : resuelto_.value("avisos", json::array())) {
        const QString c = QString::fromStdString(a.value("contraste", std::string()));
        const QString m = QString::fromStdString(a.value("minimo", std::string()));
        const std::string cod = a.value("codigo", std::string());
        if (cod == "texto") out << tr("El texto casi no se lee sobre el fondo (contraste %1:1; hace falta %2).").arg(c, m);
        else if (cod == "acentoTexto")
            out << tr("El texto encima del acento casi no se lee (contraste %1:1; hace falta %2).").arg(c, m);
        else if (cod == "acentoFondo")
            out << tr("El acento se confunde con el fondo (contraste %1:1; conviene %2 o más).").arg(c, m);
    }
    return out;
}

// --- Cambiar ----------------------------------------------------------------------------

QString TemaEventoViewModel::cambiar(const QVariantMap& campos) {
    return cambiarJson(json::parse(QJsonDocument::fromVariant(campos).toJson(QJsonDocument::Compact).toStdString(), nullptr,
                                   false));
}

QString TemaEventoViewModel::cambiarJson(const json& c) {
    if (!hayEvento()) return tr("No hay ningún evento abierto.");
    if (!c.is_object()) return tr("No entiendo los cambios del tema.");
    evento::TemaEvento t = tema_;
    if (c.contains("modo")) {
        const std::string m = recorte(c["modo"], 10);
        if (!m.empty() && m != "claro" && m != "oscuro") return tr("El modo es «claro» u «oscuro» (o vacío: según el fondo).");
        t.modo = m;
    }
    if (c.contains("colores") && c["colores"].is_object()) {
        const std::pair<const char*, std::string*> colores[] = {
            {"acento", &t.colores.acento},         {"acentoTexto", &t.colores.acentoTexto}, {"fondo", &t.colores.fondo},
            {"superficie", &t.colores.superficie}, {"texto", &t.colores.texto},             {"textoSuave", &t.colores.textoSuave}};
        for (const auto& [clave, destino] : colores) {
            if (!c["colores"].contains(clave)) continue;
            const std::string v = recorte(c["colores"][clave], 20);
            if (v.empty()) {
                *destino = "";
                continue;
            }
            const auto rgb = ba::tema::leerColor(v);
            if (!rgb) return tr("«%1» no es un color (va #rrggbb).").arg(QString::fromStdString(v));
            *destino = ba::tema::hex(*rgb);
        }
    }
    if (c.contains("tipografia") && c["tipografia"].is_object()) {
        if (c["tipografia"].contains("texto")) t.tipografia.texto = recorte(c["tipografia"]["texto"], 60);
        if (c["tipografia"].contains("titulos")) t.tipografia.titulos = recorte(c["tipografia"]["titulos"], 60);
    }
    const json* radio = c.contains("radio") ? &c["radio"]
                        : c.contains("forma") && c["forma"].is_object() && c["forma"].contains("radio") ? &c["forma"]["radio"]
                                                                                                         : nullptr;
    if (radio != nullptr) {
        if (!radio->is_number() || radio->get<double>() < -1 || radio->get<double>() > 32)
            return tr("El radio va de 0 a 32 (o -1: el de cada página).");
        t.radio = static_cast<int>(radio->get<double>());
    }
    if (c.contains("marca") && c["marca"].is_object()) {
        const json& m = c["marca"];
        if (m.contains("nombre")) t.marca.nombre = recorte(m["nombre"], 120);
        // Los archivos solo se eligen entre los que ya están en Tema/ (se suben con subirLogo).
        const std::pair<const char*, std::string*> archivos[] = {
            {"logo", &t.marca.logo}, {"logoClaro", &t.marca.logoClaro}, {"favicon", &t.marca.favicon}};
        for (const auto& [clave, destino] : archivos) {
            if (!m.contains(clave)) continue;
            const std::string a = recorte(m[clave], 80);
            std::error_code ec;
            if (!a.empty() && (!ba::tema::esImagenDeMarca(a) || !fs::is_regular_file(evento::carpetaTema(carpetaEvento_) / a, ec)))
                return tr("«%1» no está en la carpeta Tema/ del evento (subilo primero).").arg(QString::fromStdString(a));
            *destino = a;
        }
    }
    if (c.contains("alcance") && c["alcance"].is_object()) {
        for (const char* k : {"publico", "control"}) {
            if (!c["alcance"].contains(k)) continue;
            const std::string n = recorte(c["alcance"][k], 10);
            if (!nivelValido(n)) return tr("El alcance es «nada», «acento» o «todo».");
            (std::string(k) == "publico" ? t.alcance.publico : t.alcance.control) = n;
        }
    }
    return guardar(std::move(t));
}

QString TemaEventoViewModel::guardar(evento::TemaEvento t) {
    t.existe = true;
    t.delSitio = false;
    if (!t.guardar(carpetaEvento_)) return tr("No se pudo guardar tema.json.");
    evento::limpiarRecursos(carpetaEvento_, t);
    recargar();
    return {};
}

QString TemaEventoViewModel::subirLogo(const QString& archivo, const QString& cual) {
    if (!hayEvento()) return tr("No hay ningún evento abierto.");
    if (cual != "logo" && cual != "logoClaro" && cual != "favicon") return tr("Es «logo», «logoClaro» o «favicon».");
    const QString ruta = archivo.startsWith("file:") ? QUrl(archivo).toLocalFile() : archivo;
    const evento::RecursoCopiado r = evento::copiarRecurso(carpetaEvento_, fs::path(ruta.toStdWString()), "logo");
    if (!r.error.empty()) return QString::fromStdString(ba::i18n::traducir("evento", r.error.c_str()));
    evento::TemaEvento t = tema_;
    (cual == "logo" ? t.marca.logo : cual == "logoClaro" ? t.marca.logoClaro : t.marca.favicon) = r.archivo;
    return guardar(std::move(t));
}

QString TemaEventoViewModel::subirFuente(const QString& archivo, const QString& familia, const QString& peso,
                                         const QString& estilo) {
    if (!hayEvento()) return tr("No hay ningún evento abierto.");
    const std::string fam = familia.trimmed().left(60).toStdString();
    if (fam.empty()) return tr("Falta el nombre de la familia (cómo se llama la letra).");
    const QString ruta = archivo.startsWith("file:") ? QUrl(archivo).toLocalFile() : archivo;
    const evento::RecursoCopiado r = evento::copiarRecurso(carpetaEvento_, fs::path(ruta.toStdWString()), "fuente");
    if (!r.error.empty()) return QString::fromStdString(ba::i18n::traducir("evento", r.error.c_str()));
    evento::TemaEvento t = tema_;
    t.tipografia.archivos.push_back({fam, r.archivo, peso.trimmed().isEmpty() ? "400" : peso.trimmed().toStdString(),
                                     estilo == "italic" ? "italic" : "normal"});
    if (t.tipografia.texto.empty()) t.tipografia.texto = fam;
    return guardar(std::move(t));
}

QString TemaEventoViewModel::quitar(const QString& que) {
    if (!hayEvento()) return tr("No hay ningún evento abierto.");
    evento::TemaEvento t = tema_;
    if (que == "logo") t.marca.logo.clear();
    else if (que == "logoClaro") t.marca.logoClaro.clear();
    else if (que == "favicon") t.marca.favicon.clear();
    else if (que == "fuentes") {
        // Las familias que venían de esos archivos dejan de existir: la letra vuelve a la de cada página.
        for (const auto& f : t.tipografia.archivos) {
            if (t.tipografia.texto == f.familia) t.tipografia.texto.clear();
            if (t.tipografia.titulos == f.familia) t.tipografia.titulos.clear();
        }
        t.tipografia.archivos.clear();
    } else return tr("Es «logo», «logoClaro», «favicon» o «fuentes».");
    return guardar(std::move(t));
}

QString TemaEventoViewModel::restablecer() {
    if (!hayEvento()) return tr("No hay ningún evento abierto.");
    std::error_code ec;
    fs::remove(carpetaEvento_ / "tema.json", ec);
    if (ec) return tr("No se pudo borrar tema.json.");
    evento::limpiarRecursos(carpetaEvento_, evento::TemaEvento{});
    recargar();
    return {};
}

QVariantMap TemaEventoViewModel::migrarSitio() {
    if (!hayEvento()) return {{"error", tr("No hay ningún evento abierto.")}};
    QStringList cambiados;
    for (const std::string& a : evento::migrarSitio(evento::carpetaWeb(carpetaEvento_))) cambiados << QString::fromStdString(a);
    return {{"cambiados", cambiados}};
}

json TemaEventoViewModel::estado() const {
    json avisosTexto = json::array();
    for (const QString& a : avisos()) avisosTexto.push_back(a.toStdString());
    return {{"hayEvento", hayEvento()},
            {"existe", tema_.existe},
            {"delSitio", tema_.delSitio},
            {"tema", tema_.existe ? tema_.aJson() : json::object()},
            {"resuelto", resuelto_},
            {"avisos", avisosTexto},
            {"carpeta", carpeta().toStdString()},
            {"archivos", tema_.archivos()}};
}

} // namespace ba::app
