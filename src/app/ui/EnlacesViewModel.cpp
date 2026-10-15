// ============================================================================
// src/app/ui/EnlacesViewModel.cpp — ver EnlacesViewModel.hpp
// ============================================================================
#include "ui/EnlacesViewModel.hpp"

#include <vector>

#include <QPointer>

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
        {"invitados", "Sala y público", "Ingreso de invitados",
         "Los que entran a la sala con cámara y micrófono (pasan por el lobby).", "ingreso", "sala"},
        {"tablero", "Sala y público", "Tablero de oradores",
         "Una tablet o un monitor que muestra quién habla y su tiempo (solo mirar).", "tablero", "sala"},
        {"publico", "Sala y público", "Página del público",
         "La gente de la sala o de afuera: preguntas, urna y votaciones. Se proyecta o se imprime.", "publico",
         "publico"},
        {"invitaciones", "Sala y público", "Invitaciones personales",
         "Una por persona del padrón: acredita al entrar. Se dan desde el evento.", "", "guion", true},
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
        {"consola", "Internet", "Consola web del operador",
         "La sala en el navegador, solo en esta máquina (entra con un código de un uso).", "", "sala", false,
         "Abrir"},
    };
    return c;
}

const ServicioEnlace* buscar(const QString& id) {
    for (const ServicioEnlace& s : catalogo())
        if (id == QLatin1String(s.id)) return &s;
    return nullptr;
}

QVariantMap estado(const char* led, const QString& texto) { return {{"led", led}, {"estado", texto}}; }

/// Un enlace que solo abre en esta máquina (la sala escucha en 127.0.0.1): para
/// otro dispositivo no sirve.
bool soloEstaMaquina(const QString& url) {
    return url.contains("://127.0.0.1") || url.contains("://localhost");
}

const char* kSoloEsta = "la sala escucha solo en esta máquina (Configuración → Gateway → Acceso: «Red local»)";

} // namespace

EnlacesViewModel::EnlacesViewModel(Fuentes f, QObject* parent) : QObject(parent), f_(std::move(f)) {}

bool EnlacesViewModel::hayTunel() const {
    return f_.tunel && f_.tunel().value("estado").toString() == "listo";
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
    const bool tunel = t.value("estado").toString() == "listo";
    const QString afuera = tunel ? t.value("url").toString() : QString();
    const QString red = f_.origenLocal ? f_.origenLocal() : QString();
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
        if (!enLinea) return estado("error", "La sala no responde");
        const QString local = m.value("local").toString();
        const QString nota = m.value("nota").toString();
        const bool hayLocal = !local.isEmpty() && !soloEstaMaquina(local);
        const bool hayAfuera = !m.value("remoto").toString().isEmpty();
        if (hayLocal) return estado("ok", hayAfuera ? "Andando, en la red local y desde afuera" : "Andando en la red local");
        // Por qué no hay enlace en la red local: lo de 127.0.0.1 se dice claro; si no, lo que dijo la sala.
        const bool soloAca = soloEstaMaquina(local) || soloEstaMaquina(red);
        const QString porQue = soloAca ? QString(kSoloEsta) : nota;
        if (*s.sala != '\0') m["nota"] = QVariant(); // ya queda dicho en el estado: no se repite abajo
        if (porQue.isEmpty() && local.isEmpty()) return estado("aviso", "Pidiendo el enlace…");
        if (hayAfuera) return estado("aviso", "Anda desde afuera; en la red local no: " + porQue);
        if (!local.isEmpty()) return estado("aviso", "Solo en esta máquina: " + porQue);
        return estado("aviso", porQue);
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
        m["local"] = red.isEmpty() ? QVariant() : QVariant(red + "/botonera/");
        m["nota"] = "Se abre con el QR de emparejar (Botonera → Generar QR); sin el código no entra.";
        poner(!enLinea              ? estado("error", "La sala no responde")
              : soloEstaMaquina(red) ? estado("aviso", QString("Ninguna tablet llega: ") + kSoloEsta)
                                     : estado("ok", "Lista para emparejar (solo red local)"));
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
        const QString host = QString(red).remove("http://").remove("https://").section(':', 0, 0);
        m["nota"] = "En Companion: Satellite → " + (host.isEmpty() ? QString("esta máquina") : host) + ", puerto " +
                    QString::number(a.value("puerto").toInt()) + " (TCP).";
        poner(!a.value("error").toString().isEmpty() ? estado("error", a.value("error").toString())
              : a.value("activo").toBool()           ? estado("ok", "Escuchando en el puerto " +
                                                                    QString::number(a.value("puerto").toInt()))
                                                     : estado("apagado", "Apagado"));
    } else if (id == "sesion" || id == "presidencia") {
        const QString ruta = id == "sesion" ? "/sesion/" : "/presidencia/";
        m["local"] = red.isEmpty() ? QVariant() : QVariant(red + ruta);
        m["remoto"] = afuera.isEmpty() ? QVariant() : QVariant(afuera + ruta);
        m["nota"] = "Sin credencial no entra nadie: se dan en Asamblea → Conexiones remotas.";
        poner(segunEnlaces());
    } else if (id == "web") {
        const QVariantMap w = f_.webEvento ? f_.webEvento() : QVariantMap();
        m["local"] = red.isEmpty() || !w.value("hay").toBool() ? QVariant() : QVariant(red + "/");
        m["remoto"] = afuera.isEmpty() || !w.value("hay").toBool() ? QVariant() : QVariant(afuera + "/");
        if (!w.value("hay").toBool())
            poner(estado("apagado", w.value("evento").toString().isEmpty()
                                        ? QString("No hay un evento abierto")
                                        : "Sin generar: Evento → Web del evento"));
        else
            poner(segunEnlaces());
    } else if (id == "pantallas") {
        poner(enLinea ? estado("ok", "Cada televisor, desde Pantallas") : estado("error", "La sala no responde"));
    } else if (id == "invitaciones") {
        poner(enLinea ? estado("ok", "Una por persona, desde el evento") : estado("error", "La sala no responde"));
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
    } else if (id == "consola") {
        poner(enLinea ? estado("ok", "Solo en esta máquina") : estado("error", "La sala no responde"));
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
    emit cambio();
}

void EnlacesViewModel::avisarCambio() {
    // Si recién llegó la sala o el túnel, los enlaces cambian: se vuelven a pedir.
    const bool sala = f_.salaEnLinea && f_.salaEnLinea();
    if (sala != tuvoSala_ || hayTunel() != tuvoTunel_) {
        refrescar();
        return;
    }
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
    if (id == "consola" && f_.abrirConsolaWeb) f_.abrirConsolaWeb();
}

} // namespace ba::app
