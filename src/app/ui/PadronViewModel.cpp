// ============================================================================
// src/app/ui/PadronViewModel.cpp — ver PadronViewModel.hpp
// ============================================================================
// Cada cambio se guarda EN EL MOMENTO. El guion tiene "sin guardar" porque se
// lo escribe con tiempo y uno quiere poder deshacer; el padrón se toca en la
// puerta, con cola, y ahí nadie se va a acordar de apretar guardar. La escritura
// es barata y atómica (ver guardarPadron).
#include "ui/PadronViewModel.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

#include <chrono>

#include "ajustes/Ajustes.hpp"
#include "i18n/I18n.hpp"
#include "sala/ControlSala.hpp"
#include "sala/EnlacesSala.hpp"
#include "ui/ConferenceViewModel.hpp"
#include "ui/EventoAbierto.hpp"
#include <QCoreApplication>

namespace ba::app {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

/// Pasa un mapa de QML/MCP a un asistente, y de paso dice QUÉ claves vinieron:
/// no es lo mismo "poner la nota en vacío" que "no tocar la nota".
evento::Asistente desdeMapa(const QVariantMap& m, std::vector<std::string>& queCampos) {
    evento::Asistente a;
    const auto vino = [&m, &queCampos](const char* c) {
        if (!m.contains(c)) return false;
        queCampos.emplace_back(c);
        return true;
    };
    if (vino("personaId")) a.personaId = m.value("personaId").toString().toStdString();
    if (vino("nombre")) a.nombre = m.value("nombre").toString().toStdString();
    if (vino("contacto")) a.contacto = m.value("contacto").toString().toStdString();
    if (vino("nota")) a.nota = m.value("nota").toString().toStdString();
    if (vino("dejarPasarSolo")) a.dejarPasarSolo = m.value("dejarPasarSolo").toBool();
    if (vino("vota")) a.vota = m.value("vota").toBool();
    if (vino("registro")) a.registro = m.value("registro").toString().toStdString();
    if (vino("datos")) {
        const QJsonObject o = QJsonObject::fromVariantMap(m.value("datos").toMap());
        a.datos = nlohmann::json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
        if (!a.datos.is_object()) queCampos.pop_back();
    }
    // Una palabra que no se entiende no se ignora en silencio: se saca de la
    // lista de campos y queda el valor que había. Si no, "papel: moderdor" con
    // un dedazo convertiría a un orador en público sin que nadie lo note.
    if (vino("papel") && !evento::papelDeNombre(m.value("papel").toString().toStdString(), a.papel)) queCampos.pop_back();
    if (vino("canal") && !evento::canalDeNombre(m.value("canal").toString().toStdString(), a.canal)) queCampos.pop_back();
    if (vino("estado") && !evento::estadoDeNombre(m.value("estado").toString().toStdString(), a.estado)) queCampos.pop_back();
    return a;
}

QVariantMap aMapa(const evento::Asistente& a) {
    return QVariantMap{{"id", qs(a.id)},
                       {"personaId", qs(a.personaId)},
                       {"nombre", qs(a.nombre)},
                       {"papel", QString::fromUtf8(evento::nombreDePapel(a.papel))},
                       {"canal", QString::fromUtf8(evento::nombreDeCanal(a.canal))},
                       {"estado", QString::fromUtf8(evento::nombreDeEstado(a.estado))},
                       {"contacto", qs(a.contacto)},
                       {"nota", qs(a.nota)},
                       {"dejarPasarSolo", a.dejarPasarSolo},
                       {"vota", a.vota},
                       {"codigo", qs(a.codigo)},
                       {"llegoMs", static_cast<qint64>(a.llegoMs)},
                       {"porDonde", qs(a.porDonde)},
                       {"registro", qs(a.registro)},
                       {"datos", QJsonDocument::fromJson(QByteArray::fromStdString(
                                                             a.datos.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
                                     .object()
                                     .toVariantMap()}};
}

} // namespace

PadronViewModel::PadronViewModel(EventoAbierto& abierto, ConferenceViewModel* sala, QObject* parent)
    : QObject(parent), ab_(abierto), sala_(sala) {
    // Cada dos segundos: el que escanea en la puerta espera mirando el
    // teléfono, y diez segundos ahí parado se sienten eternos.
    buzon_.setInterval(ba::ajustes::entero("evento.buzonMs", 2000));
    connect(&buzon_, &QTimer::timeout, this, &PadronViewModel::recogerAcreditaciones);
}

QVariantList PadronViewModel::padron() const {
    QVariantList out;
    for (const evento::Asistente& a : padron_.gente()) out.append(aMapa(a));
    return out;
}

QVariantMap PadronViewModel::resumenPadron() const {
    const evento::Resumen r = padron_.resumen();
    return QVariantMap{{"total", r.total},     {"presentes", r.presentes},      {"presenciales", r.presenciales},
                       {"enLinea", r.enLinea}, {"confirmados", r.confirmados},  {"faltan", r.faltan},
                       {"conVoto", r.conVoto}, {"votoPresente", r.votoPresente}, {"sinDato", r.sinDato}};
}

std::optional<evento::Padron::Votantes> PadronViewModel::votantes() const {
    if (!ab_.hay()) return std::nullopt;
    return padron_.votantes();
}

void PadronViewModel::cargar() {
    if (!ab_.hay()) {
        buzon_.stop();
        padron_ = {};
        emit padronChanged();
        return;
    }
    // El padrón viene con el evento. Que no se pueda leer NO impide abrirlo: el guion es
    // lo que hay que poder correr, y quedarse sin evento por un padrón roto sería perder
    // el acto entero por la lista de invitados.
    std::string err;
    bool ok = false;
    padron_ = evento::cargarPadron(ab_.dir(), &ok, &err);
    if (!ok) emit notify(QCoreApplication::translate("EventoViewModel", "El padron de este evento no se pudo leer: %1").arg(qs(err)), "error");
    // TODO(G): cuando el gateway avise por su canal de eventos (SSE) quién escaneó su
    // invitación, esto deja de preguntar cada 2 s y escucha ese aviso.
    buzon_.start();
    emit padronChanged();
}

bool PadronViewModel::guardarAhora() {
    if (!ab_.hay()) return false;
    std::string err;
    if (!evento::guardarPadron(ab_.dir(), padron_, &err)) {
        emit notify(QCoreApplication::translate("EventoViewModel", "No se pudo guardar el padron: %1").arg(qs(err)), "error");
        return false;
    }
    emit padronChanged();
    return true;
}

std::string PadronViewModel::motivoEntrarSolo(const QVariantMap& datos, const QString& id) const {
    // Dejar pasar solo a alguien EXIGE que su ficha exista de verdad. Una entrada con un
    // id que no existe no deja pasar a nadie y nadie se entera hasta el día del evento,
    // cuando esa persona se queda esperando en el lobby y el operador no sabe por qué.
    const evento::Asistente* ya = id.isEmpty() ? nullptr : padron_.porId(id.toStdString());
    const bool solo = datos.contains("dejarPasarSolo") ? datos.value("dejarPasarSolo").toBool() : ya != nullptr && ya->dejarPasarSolo;
    if (!solo || (!datos.contains("dejarPasarSolo") && !datos.contains("personaId"))) return {};
    const std::string persona = datos.contains("personaId") ? datos.value("personaId").toString().toStdString()
                                : ya != nullptr               ? ya->personaId
                                                              : std::string();
    if (persona.empty())
        return ba::i18n::traducir("mcp", "Para que alguien entre solo hace falta su ficha de rostro (`personaId`): sin cara no "
                                         "hay con que reconocerlo, y entraria por haber ESCRITO un nombre, que es lo que no se "
                                         "quiere. `rostro_listar` da las fichas.");
    const auto [existe, nombre] = fichaDe_ ? fichaDe_(persona) : std::pair{true, std::string()};
    if (!existe)
        return ba::i18n::llenar(ba::i18n::traducir("mcp", "No hay ninguna ficha con id '%1' en la libreta de rostros. `rostro_listar` los da; si "
                                                          "esa persona todavia no esta, dale de alta con `rostro_guardar` primero."),
                                {persona});
    return {};
}

QString PadronViewModel::anotar(const QVariantMap& datos) {
    if (!ab_.hay()) return {};
    std::vector<std::string> queCampos;
    const evento::Asistente a = desdeMapa(datos, queCampos);
    if (a.nombre.empty()) {
        emit notify(QCoreApplication::translate("EventoViewModel", "Para anotar a alguien hace falta al menos su nombre."), "error");
        return {};
    }
    if (const std::string m = motivoEntrarSolo(datos); !m.empty()) {
        emit notify(qs(m), "error");
        return {};
    }
    const QString id = qs(padron_.agregar(a));
    guardarAhora();
    return id;
}

bool PadronViewModel::cambiarAsistente(const QString& id, const QVariantMap& datos) {
    if (!ab_.hay()) return false;
    if (const std::string m = motivoEntrarSolo(datos, id); !m.empty()) {
        emit notify(qs(m), "error");
        return false;
    }
    std::vector<std::string> queCampos;
    const evento::Asistente a = desdeMapa(datos, queCampos);
    if (!padron_.cambiar(id.toStdString(), a, queCampos)) return false;
    return guardarAhora();
}

bool PadronViewModel::quitarAsistente(const QString& id) {
    if (!ab_.hay() || !padron_.quitar(id.toStdString())) return false;
    return guardarAhora();
}

bool PadronViewModel::acreditar(const QString& id, const QString& porDonde) {
    if (!ab_.hay() || !padron_.acreditar(id.toStdString(), porDonde.toStdString(), ahoraMs())) return false;
    return guardarAhora();
}

bool PadronViewModel::despedir(const QString& id) {
    if (!ab_.hay() || !padron_.despedir(id.toStdString())) return false;
    return guardarAhora();
}

void PadronViewModel::recogerAcreditaciones() {
    if (!ab_.hay() || sala_ == nullptr) return;
    QPointer<PadronViewModel> yo(this);
    sala_->control().acreditaciones([this, yo](const std::string& err, const nlohmann::json& rj) {
        if (!yo || !err.empty()) return;
        const nlohmann::json cs = rj.value("codigos", nlohmann::json::array());
        if (!cs.is_array() || cs.empty()) return;
        int hechos = 0;
        QStringList desconocidos;
        for (const auto& v : cs) {
            const std::string codigo = v.is_string() ? v.get<std::string>() : std::string();
            const evento::Asistente* a = padron_.porCodigo(codigo);
            // Un código que no está en el padrón NO se anota solo: quien entra sin estar
            // invitado es una decisión del que organiza. Pero se avisa, porque del otro
            // lado hay alguien parado esperando.
            if (a == nullptr) {
                desconocidos << qs(codigo);
                continue;
            }
            if (acreditar(qs(a->id), "qr")) ++hechos;
        }
        if (hechos > 0) emit notify(QCoreApplication::translate("EventoViewModel", "%n persona llegó (escanearon su invitación).", "", hechos), "info");
        if (!desconocidos.isEmpty())
            emit notify(QCoreApplication::translate("EventoViewModel", "Escanearon códigos que no están en el padrón: %1. Si corresponde, anotalos con el padrón.")
                            .arg(desconocidos.join(", ")),
                        "warn");
    });
}

bool PadronViewModel::migrarAutoAceptar(const std::vector<evento::Invitado>& lista) {
    // La lista blanca vieja del lobby (`Evento::autoAceptar`) y el permiso del padrón
    // (`dejarPasarSolo`) decían lo mismo. Dos listas para lo mismo se desincronizan —se
    // agrega a alguien en una y el otro día no entra—, así que la vieja se vuelca en el
    // padrón y se apaga. Se hace al abrir y una sola vez.
    if (lista.empty()) return false;
    int movidos = 0;
    for (const evento::Invitado& i : lista) {
        if (i.personaId.empty()) continue;
        if (const evento::Asistente* ya = padron_.porPersona(i.personaId); ya != nullptr) {
            evento::Asistente prender;
            prender.dejarPasarSolo = true;
            padron_.cambiar(ya->id, prender, {"dejarPasarSolo"});
        } else {
            evento::Asistente a;
            a.personaId = i.personaId;
            a.nombre = i.nombre;
            a.nota = i.nota;
            a.dejarPasarSolo = true;
            // Estaba en la lista de entrar solo, así que alguien lo decidió a propósito:
            // es invitado de verdad, no público anónimo.
            a.papel = evento::Papel::Panelista;
            a.canal = evento::Canal::Online;
            a.estado = evento::Estado::Confirmo;
            padron_.agregar(std::move(a));
        }
        ++movidos;
    }
    guardarAhora();
    if (movidos > 0) emit notify(QCoreApplication::translate("EventoViewModel", "%1 de la lista de auto-aceptar pasaron al padrón del evento.").arg(movidos), "info");
    return true;
}

QString PadronViewModel::registrarDesdeTotem(const QString& personaId, const QString& nombre, const QString& totem,
                                             const nlohmann::json& datos) {
    if (!ab_.hay()) return {};
    // La ficha de rostro manda (como buscarEnPadron); sin ficha, el nombre dicho solo
    // engancha a alguien que ya estaba en la lista armada de antemano.
    const evento::Asistente* a = padron_.porPersona(personaId.toStdString());
    if (a == nullptr && !nombre.trimmed().isEmpty()) a = padron_.porNombre(nombre.toStdString());
    std::string id;
    if (a != nullptr) {
        id = a->id;
        evento::Asistente c;
        std::vector<std::string> que{"datos", "registro"};
        c.datos = datos.is_object() ? datos : nlohmann::json::object();
        c.registro = totem.toStdString();
        if (a->personaId.empty() && !personaId.isEmpty()) {
            c.personaId = personaId.toStdString();
            que.emplace_back("personaId");
        }
        if (a->nombre.empty() && !nombre.trimmed().isEmpty()) {
            c.nombre = nombre.trimmed().toStdString();
            que.emplace_back("nombre");
        }
        if (a->canal == evento::Canal::SinDefinir) {
            c.canal = evento::Canal::Presencial;
            que.emplace_back("canal");
        }
        padron_.cambiar(id, c, que);
    } else {
        evento::Asistente n;
        n.personaId = personaId.toStdString();
        n.nombre = nombre.trimmed().isEmpty() ? QCoreApplication::translate("EventoViewModel", "(sin nombre)").toStdString() : nombre.trimmed().toStdString();
        n.canal = evento::Canal::Presencial; // se paró frente a un tótem del lugar (o escaneó su QR ahí)
        n.datos = datos.is_object() ? datos : nlohmann::json::object();
        n.registro = totem.toStdString();
        // El contacto legible sale de lo que dijo, si dijo un correo o un teléfono.
        for (const char* k : {"correo", "email", "mail", "telefono", "celular"})
            if (n.contacto.empty() && n.datos.contains(k) && n.datos[k].is_string()) n.contacto = n.datos[k].get<std::string>();
        id = padron_.agregar(std::move(n));
    }
    padron_.acreditar(id, "registro", ahoraMs());
    guardarAhora();
    return qs(id);
}

bool PadronViewModel::registradoPorTotem(const QString& personaId) const {
    const evento::Asistente* a = padron_.porPersona(personaId.toStdString());
    return a != nullptr && !a->registro.empty();
}

bool PadronViewModel::puedeEntrarSolo(const QString& personaId) const {
    const evento::Asistente* a = padron_.porPersona(personaId.toStdString());
    return a != nullptr && a->dejarPasarSolo;
}

bool PadronViewModel::acreditarPorPersona(const QString& personaId, const QString& porDonde) {
    const evento::Asistente* a = padron_.porPersona(personaId.toStdString());
    return a != nullptr && acreditar(qs(a->id), porDonde);
}

QString PadronViewModel::buscarEnPadron(const QString& personaId, const QString& nombre) const {
    // La ficha de rostro manda sobre el nombre: el nombre lo escribe cualquiera
    // y hay dos González en cualquier lista de cuarenta.
    const evento::Asistente* a = padron_.porPersona(personaId.toStdString());
    if (a == nullptr) a = padron_.porNombre(nombre.toStdString());
    return a == nullptr ? QString() : qs(a->id);
}

void PadronViewModel::pedirInvitacion(const QVariantMap& quien, bool remoto, std::function<void(const QVariantMap&)> luego) {
    if (sala_ == nullptr) return luego(QVariantMap{{"ok", false}, {"motivo", QCoreApplication::translate("EventoViewModel", "El servidor no está disponible.")}});
    const QString codigo = quien.value("codigo").toString();
    const evento::Evento& e = ab_.evento();
    // El nombre y la fecha salen del evento; el enlace, del gateway.
    const QString nombreEvento = ab_.nombre();
    // La invitación la lee el invitado: en el idioma del evento abierto. El idioma se toma
    // ahora: la respuesta del gateway llega después.
    const ba::i18n::En ev(ba::i18n::idiomaDelEvento());
    QString cuandoTexto = qs(e.fecha);
    if (!e.inicio.empty()) cuandoTexto = qs(ba::i18n::llenar(ba::i18n::traducirEvento("evento", "%1 a las %2"), {e.fecha, e.inicio}));
    if (!e.huso.empty()) cuandoTexto += " (" + qs(e.huso) + ")";
    const QString instante = ab_.cuando();

    // El mismo enlace de la página del público (modelo: sala::EnlacesSala).
    sala_->enlaces().publico(remoto, [luego, quien, codigo, nombreEvento, cuandoTexto, instante, ev](const sala::Enlace& en) {
        if (!en.ok)
            return luego(QVariantMap{{"ok", false}, {"motivo", QCoreApplication::translate("EventoViewModel", "No pude pedirle el enlace a la conferencia. Fijate `room_status`.")}});
        const auto t1 = [&ev](const char* texto, const QString& dato) {
            return qs(ba::i18n::llenar(ev.traducir("evento", texto), {dato.toStdString()}));
        };
        // El código va en el FRAGMENTO, con la clave: no viaja en la petición ni queda en
        // los registros de ningún servidor por el que pase.
        const QString url = qs(en.url) + "&c=" + codigo;
        QString t;
        t += t1(QT_TRANSLATE_NOOP("evento", "Te esperamos en %1."), nombreEvento) + "\n\n";
        if (cuandoTexto.trimmed().isEmpty()) t += qs(ev.traducir("evento", "(El evento todavía no tiene fecha en el guion.)")) + "\n\n";
        else t += t1(QT_TRANSLATE_NOOP("evento", "Cuándo: %1"), cuandoTexto.trimmed()) + "\n";
        if (!instante.isEmpty()) t += t1(QT_TRANSLATE_NOOP("evento", "En tu huso horario: %1"), instante) + "\n";
        t += "\n" + qs(ev.traducir("evento", "Este es tu enlace personal:")) + "\n" + url + "\n\n";
        t += qs(ev.traducir("evento", "Escanealo o abrilo al llegar: queda registrado que llegaste, y desde ahí podés "
                                      "preguntar y votar.")) +
             "\n";
        t += t1(QT_TRANSLATE_NOOP("evento", "Tu código, por si te lo piden: %1"), codigo) + "\n";
        if (!quien.value("nota").toString().isEmpty())
            t += "\n" + t1(QT_TRANSLATE_NOOP("evento", "Nota: %1"), quien.value("nota").toString()) + "\n";
        QVariantList modulos;
        for (const std::string& fila : en.filas) modulos.append(qs(fila));
        luego(QVariantMap{{"ok", true}, {"texto", t}, {"url", url}, {"modulos", modulos}});
    });
}

} // namespace ba::app
