// ============================================================================
// src/app/ui/EnlacesViewModel.cpp — ver EnlacesViewModel.hpp
// ============================================================================
#include "ui/EnlacesViewModel.hpp"

#include <vector>

#include <QPointer>

#include "sala/Direcciones.hpp"
#include "sala/EnlacesSala.hpp"

namespace ba::app {

/// Una fila del catálogo. `sala`: cómo se le pide el enlace a la sala ("" = no
/// se le pide: lo arma el estado de otro servicio o es por persona).
struct ServicioEnlace {
    const char* id;
    const char* grupo;
    const char* nombre;
    const char* para; ///< quién lo usa y para qué
    const char* sala;
    const char* pagina = "";    ///< id del riel donde se maneja en detalle
    bool porPersona = false;    ///< uno por integrante, televisor, invitación
    const char* accion = "";    ///< el texto del botón propio, si tiene
};

namespace {

const std::vector<ServicioEnlace>& catalogo() {
    static const std::vector<ServicioEnlace> c = {
        {"invitados", "Conferencia y público", "Ingreso de invitados",
         "Los que entran a la conferencia con cámara y micrófono (pasan por el lobby).", "ingreso", "sala"},
        {"tablero", "Conferencia y público", "Tablero de oradores",
         "Una tablet o un monitor que muestra quién habla y su tiempo (solo mirar).", "tablero", "sala"},
        {"publico", "Conferencia y público", "Página del público",
         "La gente de la sala o de afuera: preguntas, urna y votaciones. Se proyecta o se imprime.", "publico",
         "publico"},
        {"invitaciones", "Conferencia y público", "Invitaciones personales",
         "Una por persona del padrón: acredita al entrar. Se dan desde el evento.", "", "guion", true},
        {"registro", "Conferencia y público", "Registro de personas (tótems)",
         "Una TV con cámara o el celular de quien escanea el QR: se registra hablando. Uno o varios por evento.", "",
         "personas", true},
        {"prompter-control", "Producción", "Teleprompter: control",
         "El celular del operador o del orador: avanzar, pausar, velocidad.", "prompter-control", "prompter"},
        {"prompter-orador", "Producción", "Teleprompter: lo que lee el orador",
         "La tablet o el monitor del atril.", "prompter-orador", "prompter"},
        {"rotulos", "Producción", "Rótulos desde el celular",
         "Cambiar los textos de la escena al aire (nombres, cargos) desde un celular.", "rotulos", "rotulos"},
        {"correccion", "Producción", "Puesto de corrección de subtítulos",
         "Una persona que corrige los subtítulos en vivo desde otra computadora.", "correccion"},
        {"botonera", "Producción", "Botonera en una tablet",
         "Una tablet como superficie de control. Se empareja con un código de un solo uso (solo red local).", "",
         "botonera", false, "Emparejar"},
        {"control", "Producción", "Control web (dashboard)",
         "Una tablet, notebook o PC como consola completa en el navegador: escenas, PTZ, mezclador, salidas. "
         "Se empareja con un QR y una persona lo aprueba acá (solo red local).",
         "", "", false, "Emparejar (QR)"},
        {"companion", "Producción", "API para Companion (HTTP)",
         "Bitfocus Companion u otro programa que aprieta botones de la botonera por HTTP.", "", "botonera"},
        {"satellite", "Producción", "Companion Satellite",
         "Un Stream Deck conectado a Companion en otra máquina (TCP, no es una página web).", "", "botonera"},
        {"sesion", "Asamblea", "Sesión remota",
         "Cada integrante entra a su banca virtual con su credencial (una por persona).", "", "asamblea", true},
        {"presidencia", "Asamblea", "Consola de presidencia",
         "La presidencia maneja la sesión desde el navegador (una credencial por consola).", "", "asamblea", true},
        {"web", "Evento y pantallas", "Web del evento",
         "La página pública del evento abierto: portada, una por bloque y el cierre.", "", "guion"},
        {"pantallas", "Evento y pantallas", "Televisores de la sala",
         "Cada televisor recibe su propio enlace (con permiso de un solo uso) al mandarle algo.", "", "pantallas",
         true},
        {"tunel", "Internet", "Salida a internet (túnel)",
         "Lo que hace que los enlaces «de afuera» anden desde cualquier lugar.", "", "configuracion"},
    };
    return c;
}

const ServicioEnlace* buscar(const QString& id) {
    for (const ServicioEnlace& s : catalogo())
        if (id == QLatin1String(s.id)) return &s;
    return nullptr;
}

QVariantMap estado(const char* led, const QString& texto) { return {{"led", led}, {"estado", texto}}; }


} // namespace

EnlacesViewModel::EnlacesViewModel(Fuentes f, QObject* parent) : QObject(parent), f_(std::move(f)) {
    releerAprobaciones();
}

QVariantList EnlacesViewModel::catalogoUi() const {
    QVariantList out;
    int i = 0;
    for (const ServicioEnlace& s : catalogo())
        out << QVariantMap{{"id", QString::fromLatin1(s.id)}, {"grupo", QString::fromUtf8(s.grupo)}, {"indice", i++}};
    return out;
}

QStringList EnlacesViewModel::grupos() const {
    QStringList out;
    for (const ServicioEnlace& s : catalogo())
        if (!out.contains(QString::fromUtf8(s.grupo))) out << QString::fromUtf8(s.grupo);
    return out;
}

void EnlacesViewModel::releerAprobaciones() {
    QVariantMap nuevo;
    for (const auto& [id, a] : f_.aprobaciones)
        nuevo[QString::fromStdString(id)] =
            QVariantMap{{"solicitudes", a.pendientes ? a.pendientes() : QVariantList()},
                        {"perfilModo", a.perfilModo},
                        {"perfiles", a.perfilModo == "lista" && a.perfiles ? a.perfiles() : QVariantList()},
                        {"perfilAyuda", a.perfilAyuda},
                        {"textoAprobar", a.textoAprobar}};
    if (nuevo == aprobaciones_) return;
    aprobaciones_ = nuevo;
    emit aprobacionesCambio();
}

bool EnlacesViewModel::hayTunel() const {
    const sala::Direcciones* d = f_.direcciones ? f_.direcciones() : nullptr;
    return d != nullptr && !d->remoto().isEmpty();
}

QVariantList EnlacesViewModel::servicios() const {
    QVariantList out;
    for (const ServicioEnlace& s : catalogo()) out << fila(s);
    return out;
}

QVariantMap EnlacesViewModel::fila(const ServicioEnlace& s) const {
    const QString id = QString::fromLatin1(s.id);
    const bool enLinea = f_.salaEnLinea && f_.salaEnLinea();
    const QVariantMap t = f_.tunel ? f_.tunel() : QVariantMap();
    // Los enlaces salen de las direcciones (el gateway): la red local y, aparte, afuera.
    const sala::Direcciones* d = f_.direcciones ? f_.direcciones() : nullptr;
    const auto dir = [d](const char* servicio, bool afuera) -> QVariant {
        const std::string u = d != nullptr ? d->url(servicio, afuera) : std::string();
        return u.empty() ? QVariant() : QVariant(QString::fromStdString(u));
    };
    const auto porQue = [d, enLinea](const char* servicio) {
        if (!enLinea || d == nullptr) return QString("El servidor no responde");
        return QString::fromStdString(d->motivo(servicio, false));
    };
    const auto it = urls_.find(s.id);
    const QVariantMap pedido = it != urls_.end() ? it->second : QVariantMap();

    QVariantMap m{{"id", id},
                  {"grupo", QString::fromUtf8(s.grupo)},
                  {"nombre", QString::fromUtf8(s.nombre)},
                  {"para", QString::fromUtf8(s.para)},
                  {"pagina", QString::fromLatin1(s.pagina)},
                  {"porPersona", s.porPersona},
                  {"accion", QString::fromUtf8(s.accion)},
                  {"activable", false},
                  {"activo", false},
                  {"local", pedido.value("local")},
                  {"remoto", pedido.value("remoto")},
                  {"nota", pedido.value("nota")}};
    const auto poner = [&m](const QVariantMap& e) {
        for (auto i = e.begin(); i != e.end(); ++i) m[i.key()] = i.value();
    };
    // El estado según qué enlaces hay: el local de verdad (no 127.0.0.1) y el de afuera.
    // Sin la sala no anda nada.
    const auto segunEnlaces = [&]() {
        if (!enLinea) return estado("error", "El servidor no responde");
        const QString local = m.value("local").toString();
        const QString nota = m.value("nota").toString();
        const bool hayAfuera = !m.value("remoto").toString().isEmpty();
        if (!local.isEmpty()) return estado("ok", hayAfuera ? "Andando, en la red local y desde afuera" : "Andando en la red local");
        // Por qué no hay enlace en la red local: lo que dijo la sala.
        if (*s.sala != '\0') m["nota"] = QVariant(); // ya queda dicho en el estado: no se repite abajo
        if (nota.isEmpty()) return estado("aviso", "Pidiendo el enlace…");
        if (hayAfuera) return estado("aviso", "Anda desde afuera; en la red local no: " + nota);
        return estado("aviso", nota);
    };
    const auto deLaSala = segunEnlaces;

    if (id == "invitados") {
        const bool cerrada = f_.salaCerrada && f_.salaCerrada();
        m["activable"] = true;
        m["activo"] = enLinea && !cerrada;
        poner(enLinea && cerrada ? estado("apagado", "Cerrada con llave: nadie nuevo puede entrar") : deLaSala());
    } else if (std::string(s.sala).size() > 0) {
        poner(deLaSala());
    } else if (id == "botonera") {
        m["local"] = dir("botonera", false);
        m["nota"] = "Se abre con el QR de emparejar (Botonera → Generar QR); sin el código no entra.";
        poner(!enLinea                   ? estado("error", "El servidor no responde")
              : m.value("local").isNull() ? estado("aviso", "Ninguna tablet llega: " + porQue("botonera"))
                                          : estado("ok", "Lista para emparejar (solo red local)"));
    } else if (id == "control") {
        const QVariantMap c = f_.controlWeb ? f_.controlWeb() : QVariantMap();
        const int conectados = c.value("conectados").toInt();
        m["local"] = dir("control", false);
        m["remoto"] = dir("control", true); // solo si está habilitado desde afuera (lo decide el gateway)
        m["nota"] = QString("Sin emparejar no entra nadie. Aparatos emparejados: %1%2")
                        .arg(c.value("aparatos").toInt())
                        .arg(c.value("tunel").toBool() ? " · desde afuera, con PIN" : "");
        // El LED: verde si hay algún aparato conectado ahora.
        // (Los que esperan aprobación los pone abajo, como en todos los que aprueban.)
        poner(!enLinea                ? estado("error", "El servidor no responde")
              : m.value("local").isNull() ? estado("aviso", "Ningún aparato llega: " + porQue("control"))
              : conectados > 0        ? estado("ok", conectados == 1 ? QString("Un aparato conectado")
                                                                     : QString::number(conectados) + " aparatos conectados")
                                      : estado("apagado", "Ningún aparato conectado ahora"));
    } else if (id == "companion") {
        const QVariantMap a = f_.apiRemota ? f_.apiRemota() : QVariantMap();
        m["activable"] = true;
        m["activo"] = a.value("activo").toBool();
        m["local"] = a.value("url");
        m["nota"] = a.value("aviso").toString().isEmpty() ? QString("Cada programa usa su token (Botonera → API remota).")
                                                          : a.value("aviso").toString();
        poner(!a.value("activo").toBool()       ? estado("apagado", "Apagada")
              : !a.value("aviso").toString().isEmpty() ? estado("aviso", a.value("aviso").toString())
                                                       : estado("ok", "Escuchando en la red local"));
    } else if (id == "satellite") {
        const QVariantMap a = f_.satellite ? f_.satellite() : QVariantMap();
        m["activable"] = true;
        m["activo"] = a.value("activo").toBool();
        const QString host = d != nullptr ? d->ip() : QString();
        m["nota"] = "En Companion: Satellite → " + (host.isEmpty() ? QString("esta máquina") : host) + ", puerto " +
                    QString::number(a.value("puerto").toInt()) + " (TCP).";
        poner(!a.value("error").toString().isEmpty() ? estado("error", a.value("error").toString())
              : a.value("activo").toBool()           ? estado("ok", "Escuchando en el puerto " +
                                                                    QString::number(a.value("puerto").toInt()))
                                                     : estado("apagado", "Apagado"));
    } else if (id == "sesion" || id == "presidencia") {
        const char* sv = id == "sesion" ? "sesion" : "presidencia";
        m["local"] = dir(sv, false);
        m["remoto"] = dir(sv, true);
        m["nota"] = m.value("local").isNull() ? QVariant(porQue(sv))
                                              : QVariant("Sin credencial no entra nadie: se dan en Asamblea → Conexiones remotas.");
        poner(segunEnlaces());
    } else if (id == "web") {
        const QVariantMap w = f_.webEvento ? f_.webEvento() : QVariantMap();
        m["local"] = w.value("hay").toBool() ? dir("evento", false) : QVariant();
        m["remoto"] = w.value("hay").toBool() ? dir("evento", true) : QVariant();
        if (m.value("local").isNull()) m["nota"] = porQue("evento");
        if (!w.value("hay").toBool())
            poner(estado("apagado", w.value("evento").toString().isEmpty()
                                        ? QString("No hay un evento abierto")
                                        : "Sin generar: Evento → Web del evento"));
        else
            poner(segunEnlaces());
    } else if (id == "pantallas") {
        poner(enLinea ? estado("ok", "Cada televisor, desde Pantallas") : estado("error", "El servidor no responde"));
    } else if (id == "registro") {
        // Sin enlace acá: el de cada tótem lleva su token y se da desde Personas.
        poner(enLinea ? estado("ok", "Uno por tótem, desde Personas → Registro") : estado("error", "El servidor no responde"));
    } else if (id == "invitaciones") {
        poner(enLinea ? estado("ok", "Una por persona, desde el evento") : estado("error", "El servidor no responde"));
    } else if (id == "tunel") {
        const QString e = t.value("estado").toString();
        m["activable"] = true;
        m["activo"] = t.value("activo").toBool() || e == "listo" || e == "conectando";
        m["remoto"] = t.value("url").toString().isEmpty() ? QVariant() : t.value("url");
        m["nota"] = t.value("texto");
        poner(e == "listo"        ? estado("ok", "Conectado por " + t.value("via").toString())
              : e == "conectando" ? estado("aviso", "Conectando…")
              : e == "error"      ? estado("error", t.value("texto").toString().isEmpty() ? QString("No se pudo conectar")
                                                                                          : t.value("texto").toString())
                                  : estado("apagado", "Apagado: los enlaces de afuera no andan"));
    }

    // Los pedidos que esperan a una persona: en la fila, para aprobarlos acá. Mientras
    // haya alguno, el LED queda en ámbar y el estado lo dice (salvo que no ande).
    m["aprueba"] = false;
    m["pendientes"] = 0;
    m["solicitudes"] = QVariantList();
    if (const auto ap = f_.aprobaciones.find(s.id); ap != f_.aprobaciones.end()) {
        const Aprobaciones& a = ap->second;
        const QVariantList sol = a.pendientes ? a.pendientes() : QVariantList();
        m["aprueba"] = true;
        m["pendientes"] = static_cast<int>(sol.size());
        m["solicitudes"] = sol;
        if (!sol.isEmpty() && m.value("led").toString() != "error")
            poner(estado("aviso", sol.size() == 1 ? a.unoEspera : a.variosEsperan.arg(sol.size())));
    }
    return m;
}

void EnlacesViewModel::pedir(const std::string& id, bool remoto) {
    sala::EnlacesSala* e = f_.enlaces ? f_.enlaces() : nullptr;
    if (e == nullptr) return;
    QPointer<EnlacesViewModel> yo(this);
    const auto listo = [yo, id, remoto](const sala::Enlace& r) {
        if (!yo) return;
        QVariantMap& u = yo->urls_[id];
        const char* campo = remoto ? "remoto" : "local";
        u[campo] = r.ok ? QVariant(QString::fromStdString(r.url)) : QVariant();
        if (!remoto) u["nota"] = QString::fromStdString(r.mensaje);
        emit yo->cambio();
    };
    if (id == "invitados") e->ingreso(remoto, false, listo);
    else if (id == "tablero") e->tablero(remoto, listo);
    else if (id == "publico") e->publico(remoto, listo);
    else if (id == "prompter-control") e->prompter(remoto, "control", true, listo);
    else if (id == "prompter-orador") e->prompter(remoto, "orador", false, listo);
    else if (id == "rotulos") e->rotulos(remoto, true, listo);
    else if (id == "correccion") e->correccion(remoto, listo);
}

void EnlacesViewModel::refrescar() {
    const bool tunel = hayTunel();
    for (const ServicioEnlace& s : catalogo()) {
        if (std::string(s.sala).empty()) continue;
        pedir(s.id, false);
        if (tunel)
            pedir(s.id, true);
        else
            urls_[s.id].remove("remoto");
    }
    tuvoTunel_ = tunel;
    tuvoSala_ = f_.salaEnLinea && f_.salaEnLinea();
    releerAprobaciones();
    emit cambio();
}

void EnlacesViewModel::avisarCambio() {
    // Si recién llegó la sala o el túnel, los enlaces cambian: se vuelven a pedir.
    const bool sala = f_.salaEnLinea && f_.salaEnLinea();
    if (sala != tuvoSala_ || hayTunel() != tuvoTunel_) {
        refrescar();
        return;
    }
    releerAprobaciones();
    emit cambio();
}

QString EnlacesViewModel::activar(const QString& id, bool activo) {
    QString err;
    if (id == "invitados" && f_.cerrarSala) f_.cerrarSala(!activo);
    else if (id == "companion" && f_.activarApiRemota) f_.activarApiRemota(activo);
    else if (id == "satellite" && f_.activarSatellite) err = f_.activarSatellite(activo);
    else if (id == "tunel" && f_.activarTunel) err = f_.activarTunel(activo);
    else {
        const ServicioEnlace* s = buscar(id);
        err = s == nullptr ? "No hay un servicio «" + id + "»."
                           : "«" + QString::fromUtf8(s->nombre) + "» no se prende ni se apaga desde acá.";
    }
    emit cambio();
    return err;
}

void EnlacesViewModel::accion(const QString& id) {
    if (id == "control" && f_.emparejarControlWeb) f_.emparejarControlWeb();
}

QString EnlacesViewModel::resolver(const QString& servicio, const QString& solicitud, bool aprobar,
                                   const QString& perfil) {
    const auto ap = f_.aprobaciones.find(servicio.toStdString());
    if (ap == f_.aprobaciones.end() || !ap->second.resolver) return "«" + servicio + "» no pide aprobaciones.";
    const QString err = ap->second.resolver(solicitud, aprobar, perfil);
    avisarCambio(); // la fuente también avisa; por si el pedido ya no estaba
    return err;
}

} // namespace ba::app
