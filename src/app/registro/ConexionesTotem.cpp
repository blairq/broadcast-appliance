// src/app/registro/ConexionesTotem.cpp — ver ConexionesTotem.hpp
#include "registro/ConexionesTotem.hpp"

#include <algorithm>
#include <sstream>

#include "diag/Registro.hpp"
#include "documentos/Documento.hpp"
#include "documentos/Mrz.hpp"
#include "i18n/I18n.hpp"
#include "registro/Totems.hpp"

namespace ba::registro {

using nlohmann::json;

namespace {

std::string texto(const json& j, const char* k) {
    return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string();
}

void anotar(diag::Nivel n, const std::string& t) { diag::registro().linea(n, "registro", 0, t); }

std::size_t palabras(const std::string& s) {
    std::istringstream in(s);
    std::size_t n = 0;
    for (std::string w; in >> w;) ++n;
    return n;
}

} // namespace

/// Una conexión de la página (un tótem puede tener varias: la TV y cada celular que
/// escaneó su QR).
struct ConexionesTotem::Conexion {
    std::string conn, totem, ip, lugar;
    std::int64_t desdeMs = 0;
    int canal = 0;
    bool audio = false;
    std::unique_ptr<Conversacion> conv;
    /// Cuándo: el fin del turno, la voz que termina, las esperas de la foto y del
    /// documento, el silencio, las gracias, el arranque sin cara (registro/RitmoTotem).
    RitmoTotem ritmo;
    /// Lo oído en este turno que todavía no se mandó: las frases se juntan hasta el fin del turno.
    std::string turno;
    bool sintetizando = false;
    // La cara.
    bool identificando = false;
    std::int64_t ultimaFotoMs = 0;
    std::string ultimaCara; ///< "" sin cara, "?" desconocida, o el id de la ficha
    std::string ultimoNombre;
    int vecesCara = 0;
    std::string mejorFoto; ///< la última foto (JPEG) con una cara que sirve (para el alta)
    // El paso de la foto de frente (Conversacion::Estado::Foto).
    std::string fotoSugerencia; ///< qué corregir, según la última toma que no sirvió
    // El paso del documento (Conversacion::Estado::Documento).
    std::int64_t ultimoDocMs = 0;
    bool leyendoDocumento = false; ///< hay un cuadro en el lector
    /// Las lecturas dudosas del dorso (sin el verificador compuesto) hasta que dos coinciden.
    std::shared_ptr<documentos::ConfirmadorMrz> dorsoLecturas = std::make_shared<documentos::ConfirmadorMrz>();
    std::string docSugerencia; ///< qué corregir, según lo último que se vio
    /// Después de terminar, la próxima arranca recién cuando esa cara se va (o
    /// aparece otra conocida): si no, la misma persona empezaría de nuevo.
    bool esperaVacio = false;
    std::string ultimaPersona;
    // Para la vista.
    std::string pedidoEnviado;
    /// El `listo` se manda cuando termina de decir la despedida: el celular
    /// suelta el canal al recibirlo, y con él se cortaría la voz a mitad.
    std::string listoPendiente;
    std::string oidoTexto, diceTexto, estado = "esperando";
    // La atención remota: la página de atención que la atiende ("" = la app o el MCP) y si el
    // gateway y la pantalla del tótem ya lo saben.
    std::string atiendeConn;
    bool enAtencion = false;
    // El modelo que entiende. Declarado después de `conv`, que usa: se destruye antes.
    // `piezas`: frases en la síntesis (la voz termina con la última); `textoModelo`: lo que
    // escribió en este turno; `diceAntes`: va adelante de lo próximo que diga el guion en la
    // pantalla (lo del modelo ya se dijo en voz).
    std::unique_ptr<Entendedor> agente;
    int piezas = 0;
    std::string textoModelo;
    std::string diceAntes;
};

ConexionesTotem::ConexionesTotem(Puertos& p) : p_(p) {}
ConexionesTotem::~ConexionesTotem() = default;

i18n::En ConexionesTotem::idioma() const { return i18n::En(idioma_.empty() ? i18n::actual().codigo : idioma_); }

ConexionesTotem::Conexion* ConexionesTotem::buscar(const std::string& conn) {
    const auto it = conns_.find(conn);
    return it == conns_.end() ? nullptr : it->second.get();
}

const ConexionesTotem::Conexion* ConexionesTotem::buscar(const std::string& conn) const {
    const auto it = conns_.find(conn);
    return it == conns_.end() ? nullptr : it->second.get();
}

// --- El evento ------------------------------------------------------------------------------

void ConexionesTotem::ponerEvento(const Totems* totems, const std::string& nombre, const std::string& idioma) {
    totems_ = totems;
    evento_ = nombre;
    idioma_ = idioma;
}

void ConexionesTotem::mandarPantallas() {
    for (auto& [k, c] : conns_) mandarPantalla(*c);
}

void ConexionesTotem::reiniciarTodas(std::int64_t) {
    std::vector<std::string> todas;
    for (const auto& [k, c] : conns_) todas.push_back(k);
    for (const std::string& k : todas) {
        p_.enviar(k, {{"type", "reiniciar"}});
        soltar(k);
    }
    revisarTic();
}

void ConexionesTotem::soltarTodas(std::int64_t) {
    std::vector<std::string> todas;
    for (const auto& [k, c] : conns_) todas.push_back(k);
    for (const std::string& k : todas) soltar(k);
    atienden_.clear(); // el gateway se fue: las páginas de atención vuelven a entrar solas
    p_.vivoCambio();
    revisarTic();
}

void ConexionesTotem::mandarPantalla(Conexion& c) {
    const Totem* t = totems_ != nullptr ? totems_->porId(c.totem) : nullptr;
    if (t == nullptr) return;
    // Una conversación de mentira solo para armar los textos y la lista de
    // campos vacía con la MISMA regla que la de verdad.
    const Conversacion vacia(*t, evento_, idioma_);
    p_.enviar(c.conn, {{"type", "pantalla"}, {"titulo", vacia.titulo()}, {"subtitulo", vacia.subtitulo()}});
    if (c.conv == nullptr || !c.conv->enCurso()) {
        p_.enviar(c.conn, {{"type", "campos"}, {"lista", vacia.camposParaMostrar()}});
        p_.enviar(c.conn, {{"type", "estado"}, {"estado", "esperando"}});
    }
}

ConexionesTotem::Conexion& ConexionesTotem::conexion(const std::string& conn, const std::string& totem, std::int64_t ahora) {
    auto& p = conns_[conn];
    if (!p) {
        p = std::make_unique<Conexion>();
        p->conn = conn;
        p->desdeMs = ahora;
    }
    if (!totem.empty()) p->totem = totem;
    return *p;
}

void ConexionesTotem::soltar(const std::string& conn) {
    auto it = conns_.find(conn);
    if (it == conns_.end()) return;
    p_.cerrarAudio(conn, it->second->canal);
    conns_.erase(it);
    if (conns_.empty()) p_.soltarModelo();
    p_.vivoCambio();
}

// --- El reloj -------------------------------------------------------------------------------

bool ConexionesTotem::activa(const Conexion& c) {
    // Con canal: el oído, la cara y el arranque sin cara. Sin canal todavía puede estar
    // conversando (atendida desde la app), hablando, juntando un turno, buscando la foto o el
    // documento, o con la pantalla de gracias, que vuelve sola a esperar.
    return c.canal > 0 || (c.conv != nullptr && c.conv->enCurso()) || c.ritmo.hablando || !c.turno.empty() ||
           c.ritmo.buscandoFoto || c.ritmo.buscandoDocumento || c.estado == "listo";
}

void ConexionesTotem::revisarTic() {
    const bool hace = std::any_of(conns_.begin(), conns_.end(), [](const auto& kv) { return activa(*kv.second); });
    if (hace == ticPedido_) return;
    ticPedido_ = hace;
    p_.pedirTic(hace);
    // Sin nadie activo, nadie muestra un documento: el modelo con visión ya no hace falta.
    if (!hace) p_.soltarDorso();
}

void ConexionesTotem::tic(std::int64_t ahora) {
    // Nadie mostrando un documento: el modelo local con visión ya no hace falta cargado
    // (si el chat lo usa, sigue; si no, se apaga y libera la GPU).
    if (std::none_of(conns_.begin(), conns_.end(), [](const auto& kv) { return kv.second->ritmo.buscandoDocumento; }))
        p_.soltarDorso();
    // Lo que se hace en una vuelta puede soltar una conexión: se recorre una copia de las claves.
    std::vector<std::string> claves;
    for (const auto& [k, c] : conns_) claves.push_back(k);
    for (const std::string& k : claves) {
        Conexion* cp = buscar(k);
        if (cp == nullptr) continue;
        Conexion& c = *cp;
        if (c.canal > 0) p_.revisarOido(c.conn, c.canal);
        mirarCara(c, ahora);
        mirarDocumento(c, ahora);
        // Lo que el ritmo no sabe (y solo si lo va a mirar: el VAD con turno, la voz hablando).
        RitmoTotem::Mirada m;
        m.atendida = c.conv != nullptr && c.conv->atendida();
        m.conversacion = c.conv != nullptr;
        m.enCurso = c.conv != nullptr && c.conv->enCurso();
        m.hayTurno = !c.turno.empty();
        m.oyendoVoz = m.hayTurno && !c.ritmo.hablando && p_.oyendoVoz(c.conn);
        if (c.ritmo.hablando)
            m.vozSalio = !c.sintetizando && !(c.canal > 0 && p_.vozPendiente(c.canal)) &&
                         !(c.agente && c.agente->ocupado()); // el modelo sigue escribiendo: viene otra frase
        m.buscandoDorso = c.conv != nullptr && c.conv->buscandoDorso();
        m.pantallaListo = c.estado == "listo";
        m.hayCanal = c.canal > 0;
        m.identificando = c.identificando;
        const RitmoTotem::Decision d = c.ritmo.revisar(m, ahora);
        if (d.volverAEsperar) {
            c.estado = "esperando";
            p_.vivoCambio();
        }
        using Paso = RitmoTotem::Paso;
        switch (d.paso) {
            case Paso::Nada: break;
            case Paso::CerrarTurno: cerrarTurno(c, ahora); break;
            case Paso::TerminoDeHablar:
                p_.escuchar(c.conn); // vuelve a escuchar
                if (d.mandarListo) p_.enviar(c.conn, {{"type", "listo"}, {"texto", c.listoPendiente}});
                if (!d.estado.empty()) {
                    c.estado = d.estado;
                    p_.enviar(c.conn, {{"type", "estado"}, {"estado", d.estado}});
                    p_.vivoCambio();
                }
                break;
            case Paso::FotoFallo: aplicar(c, c.conv->fotoFallo(c.fotoSugerencia), ahora); break;
            case Paso::DocumentoFallo: aplicar(c, c.conv->documentoFallo(c.docSugerencia), ahora); break;
            case Paso::Silencio: aplicar(c, c.conv->silencio(), ahora); break;
            case Paso::Arrancar: {
                const bool conocida = !c.ultimaCara.empty() && c.ultimaCara != "?";
                empezar(c, conocida ? c.ultimaCara : std::string(), conocida ? c.ultimoNombre : std::string(), ahora);
                break;
            }
        }
    }
    revisarTic();
}

// --- Lo que llega del gateway -----------------------------------------------------------------

void ConexionesTotem::alEvento(const json& ev, std::int64_t ahora) {
    if (alEventoAtencion(ev, ahora)) { // la atención remota: al final del archivo
        revisarTic();
        return;
    }
    const std::string tipo = texto(ev, "tipo");
    const std::string conn = texto(ev, "conn");
    const std::string totem = texto(ev, "id");
    if (tipo == "intento") {
        anotar(diag::Nivel::Aviso,
               i18n::llenar(i18n::traducir("registro", "tótem «%1»: intento fallido desde %2"), {texto(ev, "id"), texto(ev, "ip")}));
        return;
    }
    if (conn.empty()) return;
    if (tipo == "conexion") {
        if (!ev.value("conectado", false)) {
            soltar(conn);
            p_.conectadosCambio();
            revisarTic();
            return;
        }
        Conexion& c = conexion(conn, totem, ahora);
        c.ip = texto(ev, "ip");
        c.lugar = texto(ev, "lugar");
        p_.prepararModelo(); // que el modelo que entiende ya esté cargado cuando alguien hable
        mandarPantalla(c);
        p_.vivoCambio();
        p_.conectadosCambio(); // `conectados` de cada tótem
        revisarTic();
        return;
    }
    Conexion* cp = buscar(conn);
    Conexion& c = cp != nullptr ? *cp : conexion(conn, totem, ahora);
    if (tipo == "canal") {
        const int k = ev.value("canal", 0);
        if (k == c.canal) return;
        p_.cerrarAudio(c.conn, c.canal);
        c.ritmo.hablando = false;
        // Sin canal no hay voz: lo que se conversaba se pierde (no se guarda a medias).
        if (k == 0 && c.conv != nullptr && c.conv->enCurso()) {
            c.agente.reset(); // usa la conversación
            c.conv.reset();
        }
        c.canal = k;
        if (k > 0) {
            c.ritmo.canalDesdeMs = ahora;
            c.ritmo.primeraDelCanal = true;
            c.esperaVacio = false;
            p_.abrirAudio(c.conn, k);
        }
    } else if (tipo == "audio") {
        c.audio = ev.value("listo", false);
    } else if (tipo == "toque") {
        alToque(c, ev, ahora);
    } else if (tipo == "codigo") {
        alCodigoDePagina(c, ev);
        revisarTic();
        return;
    } else if (tipo == "cuadro") {
        if (c.ritmo.buscandoDocumento && !c.leyendoDocumento && c.conv != nullptr) {
            // El respaldo de los navegadores sin BarcodeDetector (Safari): una foto entera de la
            // cámara, sin la compresión del video, que se lee acá con zxing.
            c.leyendoDocumento = true;
            p_.pedirCuadroDePagina(c.conn);
        }
        revisarTic();
        return;
    }
    p_.vivoCambio();
    revisarTic();
}

void ConexionesTotem::reconocer(const json& r, std::int64_t ahora) {
    for (const json& x : r.value("conexiones", json::array())) {
        const std::string conn = texto(x, "conn");
        if (conn.empty()) continue;
        Conexion& k = conexion(conn, texto(x, "id"), ahora);
        k.ip = texto(x, "ip");
        k.lugar = texto(x, "lugar");
        if (x.contains("desdeMs") && x["desdeMs"].is_number()) k.desdeMs = x["desdeMs"].get<std::int64_t>();
        k.audio = x.value("audio", false);
        const int canal = x.value("canal", 0);
        if (canal > 0 && k.canal != canal) {
            k.canal = canal;
            k.ritmo.canalDesdeMs = ahora;
            k.ritmo.primeraDelCanal = true;
            p_.abrirAudio(k.conn, canal);
        }
    }
    // Quienes tenían abierta la página de atención (el gateway las conserva si solo se
    // reinició la app).
    for (const json& x : r.value("atienden", json::array())) {
        const std::string aconn = texto(x, "aconn");
        if (aconn.empty()) continue;
        atienden_[aconn] = {texto(x, "nombre"), texto(x, "ip"), texto(x, "lugar")};
    }
    if (!atienden_.empty()) p_.alGateway("enviar", paraAtencion());
    p_.vivoCambio();
    revisarTic();
}

// --- La cara ------------------------------------------------------------------------------

void ConexionesTotem::mirarCara(Conexion& c, std::int64_t ahora) {
    if (c.identificando || c.canal == 0) return;
    if (ahora - c.ultimaFotoMs < kEntreFotosMs) return;
    // Conversando, solo hace falta si todavía no hay una foto que sirva (para
    // el alta) o no se sabe quién es; en el paso de la foto, siempre (y más seguido).
    if (c.ritmo.buscandoFoto) {
        if (c.ritmo.fotoDesdeMs == 0 || ahora - c.ultimaFotoMs < kEntreFotosFrenteMs) return;
    } else if (c.conv != nullptr && c.conv->enCurso() && !c.mejorFoto.empty()) {
        return;
    }
    // La cámara del tótem llega por WebRTC al canal BA.Registro.<k>, como la del
    // lobby: el mismo cuadro que ve el operador (o un monitor) es el que se reconoce.
    // Marcado antes de pedir: la respuesta puede volver en el acto (si la foto no se puede leer).
    const std::int64_t antes = c.ultimaFotoMs;
    c.ultimaFotoMs = ahora;
    c.identificando = true;
    if (!p_.pedirCara(c.conn, c.canal)) { // no había un cuadro nuevo
        c.ultimaFotoMs = antes;
        c.identificando = false;
    }
}

void ConexionesTotem::sinCara(const std::string& conn) {
    if (Conexion* c = buscar(conn)) c->identificando = false;
    revisarTic();
}

void ConexionesTotem::alCara(const std::string& conn, const Cara& q, std::int64_t ahora) {
    Conexion* cp = buscar(conn);
    if (cp == nullptr) return;
    Conexion& c = *cp;
    c.identificando = false;
    // El paso de la foto: la primera toma de frente que sirve es la de su ficha.
    if (c.ritmo.buscandoFoto && c.ritmo.fotoDesdeMs > 0 && c.conv != nullptr) {
        if (q.hayCara && q.sirve) {
            c.mejorFoto = q.foto;
            if (!q.personaId.empty()) c.conv->conocerCara({q.personaId, q.nombre, false});
            aplicar(c, c.conv->fotoLista(), ahora);
        } else {
            c.fotoSugerencia = q.hayCara ? q.sugerencia : idioma().traducir("totem", "No te veo la cara.");
        }
        revisarTic();
        return;
    }
    if (c.conv != nullptr && c.conv->enCurso()) {
        if (q.hayCara && q.sirve && c.mejorFoto.empty()) c.mejorFoto = q.foto;
        if (!q.personaId.empty()) c.conv->conocerCara({q.personaId, q.nombre, false});
        return;
    }
    const std::string clave = !q.hayCara ? std::string() : q.personaId.empty() ? std::string("?") : q.personaId;
    if (c.esperaVacio) {
        // Recién terminó: la próxima arranca cuando esa cara se va o aparece otra persona conocida.
        const bool otra = !clave.empty() && clave != "?" && clave != c.ultimaPersona;
        if (clave.empty() || otra) {
            c.esperaVacio = false;
            c.mejorFoto.clear();
            c.vecesCara = 0;
        }
        if (clave.empty() || !otra) return;
    }
    if (clave.empty()) {
        c.vecesCara = 0;
        c.ultimaCara.clear();
        return;
    }
    if (clave == c.ultimaCara) {
        ++c.vecesCara;
    } else {
        c.ultimaCara = clave;
        c.ultimoNombre = q.nombre;
        c.vecesCara = 1;
        c.mejorFoto.clear();
    }
    if (q.sirve) c.mejorFoto = q.foto;
    // Dos fotos seguidas de la misma cara: hay alguien parado ahí, no pasando.
    if (c.vecesCara >= 2) empezar(c, q.personaId, q.nombre, ahora);
    revisarTic();
}

void ConexionesTotem::conocida(const std::string& conn, const std::string& personaId) {
    // Desde ahora esa cara es conocida: la que acaba de terminar, no «otra persona»
    // frente al tótem (si no, el kiosco la saludaría de nuevo enseguida).
    if (Conexion* c = buscar(conn); c != nullptr && !personaId.empty()) c->ultimaPersona = personaId;
}

// --- La conversación --------------------------------------------------------------------------

void ConexionesTotem::empezar(Conexion& c, const std::string& personaId, const std::string& nombre, std::int64_t ahora) {
    const Totem* t = totems_ != nullptr ? totems_->porId(c.totem) : nullptr;
    if (t == nullptr || !t->activo) return;
    if (c.conv != nullptr && c.conv->enCurso()) return;
    Conversacion::Cara cara;
    cara.personaId = personaId;
    cara.nombre = nombre;
    cara.yaRegistrado = !personaId.empty() && p_.yaRegistrado(personaId);
    c.agente.reset(); // el de la conversación anterior
    c.conv = std::make_unique<Conversacion>(*t, evento_, idioma_);
    // El dorso se ofrece solo si hay con qué leerlo (el modelo propio del operador).
    c.conv->ofrecerDorso(p_.dorsoDisponible());
    crearAgente(c, true); // el modelo que entiende, si hay (si no, el guion solo)
    c.ritmo.primeraDelCanal = false;
    c.esperaVacio = false;
    c.pedidoEnviado.clear();
    c.oidoTexto.clear();
    c.ritmo.ultimaVozMs = ahora;
    const Conversacion::Respuesta r = c.conv->empezar(cara);
    anotar(diag::Nivel::Info,
           cara.personaId.empty()
               ? i18n::llenar(i18n::traducir("registro", "tótem «%1» (%2): empieza"), {t->id, c.conn})
               : i18n::llenar(i18n::traducir("registro", "tótem «%1» (%2): empieza con %3"), {t->id, c.conn, cara.personaId}));
    aplicar(c, r, ahora);
}

void ConexionesTotem::alOido(const std::string& conn, const std::string& t, std::int64_t ahora) {
    Conexion* cp = buscar(conn);
    if (cp == nullptr || t.empty()) return;
    Conexion& c = *cp;
    // Si el oído sigue mientras habla el asistente (la página cancela el eco), una frase que
    // termina con la voz sonando es la persona que siguió hablando: la frase anterior se
    // cerró en una pausa, pero la voz nunca paró y el VAD no avisó que «empieza» otra.
    // Interrumpe como si hubiera empezado ahora.
    if (c.ritmo.hablando && p_.oyeMientrasHabla(conn)) interrumpido(conn, ahora);
    // Sin eso, lo que el oído cerró justo cuando el asistente empezaba a hablar (o el eco
    // de su propia voz) no cuenta.
    if (c.ritmo.hablando) return;
    c.oidoTexto = t;
    c.ritmo.ultimaVozMs = ahora;
    anotar(diag::Nivel::Depuracion, i18n::llenar(i18n::traducir("registro", "tótem (%1): oyó «%2»"), {conn, c.oidoTexto}));
    p_.enviar(conn, {{"type", "oido"}, {"texto", c.oidoTexto}});
    // Atendida: lo oído se muestra (a quien atiende también), pero le contesta una persona.
    if (c.conv != nullptr && c.conv->atendida()) {
        p_.vivoCambio();
        return;
    }
    if (c.conv != nullptr && c.conv->enCurso()) {
        // Se junta con lo anterior del turno; sale cuando la persona termina (tic). Un «sí» o un
        // «no» corto a una confirmación, al instante: ahí no viene nada más.
        c.turno = c.turno.empty() ? c.oidoTexto : c.turno + " " + c.oidoTexto;
        c.ritmo.turnoMs = c.ritmo.ultimaVozMs;
        if (c.turno != c.oidoTexto) p_.enviar(conn, {{"type", "oido"}, {"texto", c.turno}});
        if (c.conv->pedido().value("confirmando", false) && entenderSiNo(c.turno, c.conv->idioma()) != -1 &&
            palabras(c.turno) <= 3)
            cerrarTurno(c, ahora);
    }
    p_.vivoCambio();
    revisarTic();
}

void ConexionesTotem::cerrarTurno(Conexion& c, std::int64_t ahora) {
    const std::string dicho = c.turno;
    c.turno.clear();
    c.ritmo.turnoMs = 0;
    if (dicho.empty() || c.conv == nullptr || !c.conv->enCurso() || c.conv->atendida()) return;
    anotar(diag::Nivel::Depuracion, i18n::llenar(i18n::traducir("registro", "tótem (%1): turno «%2»"), {c.conn, dicho}));
    if (!c.ritmo.buscandoFoto) p_.enviar(c.conn, {{"type", "estado"}, {"estado", "pensando"}});
    if (alOidoAgente(c, dicho)) { // lo entiende el modelo (y el guion retoma)
        p_.vivoCambio();
        return;
    }
    aplicar(c, c.conv->oido(dicho), ahora);
    p_.vivoCambio();
}

void ConexionesTotem::alToque(Conexion& c, const json& ev, std::int64_t ahora) {
    const std::string accion = texto(ev, "accion");
    if (accion == "empezar") {
        c.esperaVacio = false;
        const bool conocida = !c.ultimaCara.empty() && c.ultimaCara != "?";
        empezar(c, conocida ? c.ultimaCara : std::string(), conocida ? c.ultimoNombre : std::string(), ahora);
    } else if (c.conv != nullptr && accion == "cancelar") {
        if (c.agente) c.agente->cancelar();
        aplicar(c, c.conv->cancelar(), ahora);
    } else if (c.conv != nullptr && accion == "escrito") {
        c.ritmo.ultimaVozMs = ahora;
        if (c.agente) c.agente->cancelar(); // lo tocado manda sobre lo que el modelo estuviera pensando
        aplicar(c, c.conv->escrito(texto(ev, "clave"), texto(ev, "valor")), ahora);
    }
}

void ConexionesTotem::aplicar(Conexion& c, const Conversacion::Respuesta& r, std::int64_t ahora) {
    if (c.conv == nullptr) return;
    // El documento se busca mientras la conversación esté en ese paso (lo dice ella,
    // no una bandera aparte). Cada vez que lo pide de nuevo, el tiempo cuenta de
    // nuevo desde que termina de decirlo (tic); la charla oída mientras tanto, no.
    if (c.conv->estado() == Conversacion::Estado::Documento) {
        if (!c.ritmo.buscandoDocumento || !r.decir.empty()) {
            c.ritmo.buscandoDocumento = true;
            c.ritmo.docDesdeMs = 0;
            c.docSugerencia.clear();
            c.dorsoLecturas->reiniciar(); // lo pidió de nuevo (o «no»): las fotos de antes no cuentan
            // Si después hace falta el dorso, que el modelo local con visión ya esté cargado.
            if (c.conv->dorsoDisponible()) p_.prepararDorso();
        }
    } else {
        c.ritmo.buscandoDocumento = false;
        c.dorsoLecturas->reiniciar();
    }
    // La foto de frente: se busca recién cuando terminó de pedirla (tic), no mientras habla.
    // Una respuesta vacía (algo que se oyó mientras mira a la cámara) no la interrumpe.
    if (r.foto) {
        c.ritmo.buscandoFoto = true;
        c.ritmo.fotoDesdeMs = 0;
        c.fotoSugerencia.clear();
    } else if (r.terminar || !r.decir.empty()) {
        c.ritmo.buscandoFoto = false;
    }
    if (r.campos) p_.enviar(c.conn, {{"type", "campos"}, {"lista", c.conv->camposParaMostrar()}});
    // Lo que se pide cambia también al pasar a confirmar (aparecen los botones Sí / No).
    const std::string clavePedido =
        r.pedir.empty() ? std::string() : r.pedir + (c.conv->estado() == Conversacion::Estado::Confirmando ? "?" : "");
    if (!clavePedido.empty() && clavePedido != c.pedidoEnviado) {
        json p = c.conv->pedido();
        if (p.is_object()) {
            p["type"] = "pedir";
            p_.enviar(c.conn, p);
        }
        c.pedidoEnviado = clavePedido;
    }
    if (!r.decir.empty()) decir(c, r.decir);
    if (r.guardar)
        p_.guardarRegistro(c.conn, c.totem, c.conv->nombre(), c.conv->cara().personaId, c.conv->datos(), c.mejorFoto);
    if (r.terminar && c.enAtencion) liberarAtencion(c); // cancelada desde el tótem, por ejemplo
    if (r.terminar && c.agente) c.agente->cancelar();
    if (r.terminar) {
        if (c.ritmo.hablando) {
            c.listoPendiente = r.decir;
            c.ritmo.hayListo = true;
        } else {
            p_.enviar(c.conn, {{"type", "listo"}, {"texto", r.decir}});
        }
        c.estado = "listo";
        c.esperaVacio = true;
        c.ultimaPersona = c.conv->cara().personaId;
        if (c.ultimaPersona.empty()) c.ultimaPersona = c.ultimaCara;
        c.ritmo.terminoMs = ahora;
        c.pedidoEnviado.clear();
    } else if (r.decir.empty() && !c.ritmo.buscandoFoto && !c.ritmo.buscandoDocumento && !c.conv->atendida()) {
        c.estado = "escuchando";
        p_.enviar(c.conn, {{"type", "estado"}, {"estado", "escuchando"}});
    }
    p_.vivoCambio();
}

void ConexionesTotem::decir(Conexion& c, const std::string& t) {
    // Lo que dijo el modelo en este turno (ya en voz) queda en la pantalla adelante.
    const std::string pantalla = c.diceAntes.empty() ? t : c.diceAntes + " " + t;
    c.diceAntes.clear();
    c.diceTexto = pantalla;
    p_.enviar(c.conn, {{"type", "dice"}, {"texto", pantalla}});
    hablar(c, t);
}

void ConexionesTotem::hablar(Conexion& c, const std::string& t) {
    if (p_.sintetizar(c.conn, c.canal, t)) {
        c.ritmo.hablando = true;
        c.sintetizando = true;
        ++c.piezas; // varias frases en cola (el modelo, frase por frase): la voz termina con la última
        c.ritmo.finHablaMs = 0;
        if (c.estado != "listo") {
            c.estado = "hablando";
            p_.enviar(c.conn, {{"type", "estado"}, {"estado", "hablando"}});
        }
        return;
    }
    // Sin voz: la página lo muestra escrito y se sigue escuchando.
    if (c.estado != "listo") {
        c.estado = "escuchando";
        p_.enviar(c.conn, {{"type", "estado"}, {"estado", "escuchando"}});
    }
}

void ConexionesTotem::alTerminarFrase(const std::string& conn) {
    if (Conexion* c = buscar(conn); c != nullptr && --c->piezas <= 0) {
        c->piezas = 0;
        c->sintetizando = false;
    }
}

// --- El documento ------------------------------------------------------------------------------

void ConexionesTotem::mirarDocumento(Conexion& c, std::int64_t ahora) {
    if (!c.ritmo.buscandoDocumento || c.ritmo.docDesdeMs == 0 || c.leyendoDocumento || c.canal == 0 || c.conv == nullptr)
        return;
    if (ahora - c.ultimoDocMs < kEntreDocumentosMs) return;
    // El mismo cuadro de la cámara del tótem que se reconoce (y que ve el operador). A lo sumo
    // uno por conexión en el lector (leyendoDocumento): uno descartado no volvería y dejaría la
    // conexión esperando para siempre.
    const std::int64_t antes = c.ultimoDocMs;
    c.ultimoDocMs = ahora;
    c.leyendoDocumento = true; // antes de pedir: la respuesta puede volver en el acto
    if (!p_.pedirDocumento(c.conn, c.canal, c.conv->documentosAceptados())) { // no había un cuadro nuevo
        c.ultimoDocMs = antes;
        c.leyendoDocumento = false;
    }
}

void ConexionesTotem::alCodigoDePagina(Conexion& c, const json& ev) {
    // La página leyó el código ella (BarcodeDetector del navegador, sobre la imagen ORIGINAL
    // de la cámara: el PDF417 de un DNI es demasiado denso para el video comprimido) y manda
    // solo el texto. Se traduce con los mismos tipos de documento (los de fábrica y los de
    // los plugins).
    if (!c.ritmo.buscandoDocumento || c.leyendoDocumento || c.conv == nullptr) return;
    const std::string t = texto(ev, "texto");
    if (t.empty()) return;
    c.leyendoDocumento = true;
    p_.traducirCodigo(c.conn, texto(ev, "formato"), t, c.conv->documentosAceptados());
}

ConexionesTotem::QueLeer ConexionesTotem::alCuadroDePagina(const std::string& conn, bool hay) {
    QueLeer q;
    Conexion* k = buscar(conn);
    if (k == nullptr) return q;
    if (!hay || k->conv == nullptr) {
        k->leyendoDocumento = false;
        return q;
    }
    q.aceptados = k->conv->documentosAceptados();
    q.frente = true;
    // Pidió la parte de atrás: la misma foto, al modelo propio (el OCR de la zona de
    // lectura mecánica). Una por vez por conexión: mientras una está en el modelo, las
    // demás fotos solo buscan el código del frente.
    if (k->conv->buscandoDorso() && !k->ritmo.leyendoDorso) {
        k->ritmo.leyendoDorso = true;
        q.dorso = true;
        q.lecturas = k->dorsoLecturas;
    }
    return q;
}

void ConexionesTotem::alLeerDocumento(const std::string& conn, const Lectura& r, std::int64_t ahora) {
    Conexion* kp = buscar(conn);
    if (kp == nullptr) return;
    Conexion& k = *kp;
    k.leyendoDocumento = false;
    if (!k.ritmo.buscandoDocumento || k.conv == nullptr) return;
    if (r.leido) {
        // En el registro, a lo sumo el tipo y los últimos tres dígitos.
        anotar(diag::Nivel::Info, i18n::llenar(i18n::traducir("registro", "tótem «%1» (%2): documento leído (%3)"),
                                               {k.totem, conn, documentos::paraLog(documentos::Datos::deJson(r.datos))}));
        aplicar(k, k.conv->documentoLeido(r.datos), ahora);
        revisarTic();
        return;
    }
    // Qué corregir, si pasa el tiempo: se vio un código pero no es de un documento aceptado.
    if (r.hayCodigos)
        k.docSugerencia =
            idioma().traducir("totem", "Ese código no es de un documento que pueda leer. Mostrame el lado del código de barras.");
}

void ConexionesTotem::alLeerDorso(const std::string& conn, const Lectura& r, std::int64_t ahora) {
    Conexion* kp = buscar(conn);
    if (kp == nullptr) return;
    Conexion& k = *kp;
    k.ritmo.leyendoDorso = false;
    if (!k.ritmo.buscandoDocumento || k.conv == nullptr) return;
    const std::string quien = i18n::llenar(i18n::traducir("registro", "tótem «%1» (%2)"), {k.totem, conn});
    if (r.leido) {
        // Como el frente: a lo sumo el tipo y los últimos tres dígitos.
        anotar(diag::Nivel::Info, i18n::llenar(i18n::traducir("registro", "%1: documento leído por el dorso, con el %2 (%3)"),
                                               {quien, r.con, documentos::paraLog(documentos::Datos::deJson(r.datos))}));
        // De dónde salió el dato, en el padrón: el dorso (si no, documentoLeido pone «codigo»).
        json datos = r.datos;
        if (datos.is_object()) datos["origen"] = "dorso";
        aplicar(k, k.conv->documentoLeido(datos), ahora);
        revisarTic();
        return;
    }
    // Por qué no (sin datos): el modelo no contestó, no se ven los renglones o no cierran.
    if (!r.error.empty())
        anotar(diag::Nivel::Aviso, i18n::llenar(i18n::traducir("registro", "%1: dorso: %2"), {quien, r.error}));
    if (r.dudosa) return; // cerró casi todo: la próxima foto que coincida la confirma
    if (r.noSeVe) k.docSugerencia = idioma().traducir("totem", "No veo los renglones de abajo.");
    else if (r.hayDetalle) k.docSugerencia = idioma().traducir("totem", "No se leen bien los renglones de abajo.");
}

// --- Lo que se lee ---------------------------------------------------------------------------

json ConexionesTotem::fila(const Conexion& c) const {
    const Totem* t = totems_ != nullptr ? totems_->porId(c.totem) : nullptr;
    const bool atendida = c.conv != nullptr && c.conv->atendida();
    return {{"conn", c.conn},
            {"id", c.totem},
            {"totem", t != nullptr ? t->nombre : c.totem},
            {"canal", c.canal},
            {"audio", c.audio},
            {"ip", c.ip},
            {"lugar", c.lugar},
            {"desdeMs", c.desdeMs},
            {"estado", c.estado},
            {"paso", c.conv != nullptr ? c.conv->pasoParaOperador() : i18n::traducir("TotemsViewModel", "esperando")},
            {"quien", c.conv != nullptr ? c.conv->nombre() : std::string()},
            {"oido", c.oidoTexto},
            {"dice", c.diceTexto},
            {"atendida", atendida},
            {"atiende", atendida ? (c.conv->atendidaPor().empty() ? i18n::traducir("TotemsViewModel", "alguien del equipo") : c.conv->atendidaPor())
                                 : std::string()},
            {"campos", c.conv != nullptr ? c.conv->camposParaMostrar() : json::array()},
            // Con qué entiende esta conversación: el modelo del rol «registro» o el guion solo.
            {"modo", c.agente && !c.agente->caido() ? "agente" : "guion"},
            {"modelo", c.agente ? c.agente->nombre() : std::string()},
            {"modoPorQue", c.agente && c.agente->caido() ? c.agente->motivoCaida() : std::string()}};
}

json ConexionesTotem::filas() const {
    json out = json::array();
    for (const auto& [k, c] : conns_) out.push_back(fila(*c));
    return out;
}

json ConexionesTotem::filasParaAfuera() const {
    json cs = json::array();
    for (const auto& [k, c] : conns_) {
        json f = fila(*c);
        // Por MCP, sin los datos del documento: lo dicho y oído con los números largos
        // tapados, y el campo `documento` en su resumen.
        f["dice"] = documentos::taparNumeros(f.value("dice", std::string()));
        f["oido"] = documentos::taparNumeros(f.value("oido", std::string()));
        if (c->conv != nullptr) {
            const json d = c->conv->datos();
            for (json& campo : f["campos"]) {
                const std::string clave = campo.value("clave", std::string());
                if (!d.contains(clave) || !d[clave].is_object()) continue;
                const json r = documentos::resumenParaAfuera(d[clave]);
                const std::string nombre = r.value("nombre", std::string()), numero = r.value("numero", std::string());
                campo["valor"] = nombre.empty() ? numero : numero.empty() ? nombre : nombre + " · " + numero;
            }
        }
        cs.push_back(std::move(f));
    }
    return cs;
}

int ConexionesTotem::conectados(const std::string& totem) const {
    return static_cast<int>(std::count_if(conns_.begin(), conns_.end(), [&](const auto& kv) { return kv.second->totem == totem; }));
}

int ConexionesTotem::conversando() const {
    return static_cast<int>(
        std::count_if(conns_.begin(), conns_.end(), [](const auto& kv) { return kv.second->conv && kv.second->conv->enCurso(); }));
}

// ============================================================================
// La atención remota (docs/funciones/REGISTRO-TOTEM.md, «Atención remota»)
//
// Una persona del equipo, desde cualquier lado (la página /registro-atiende/ del
// gateway, con el link de este evento), ve todas las conversaciones, mira y escucha
// una por WebRTC (eso lo resuelve el gateway: ruta RegistroAtiende), la ATIENDE
// ella —el guion se pausa (registro/Conversacion::atender) y el asistente se calla—
// y completa o corrige los datos. Acá se decide: quién atiende qué (una a la vez),
// qué se hace con lo que pide y qué ven las páginas (el mismo estado que la pestaña
// Registro y totem_estado, más lo que hace falta para editar).
// ============================================================================

json ConexionesTotem::atiendenJson() const {
    json lista = json::array();
    for (const auto& [aconn, a] : atienden_) {
        std::string atiende;
        for (const auto& [k, c] : conns_)
            if (c->enAtencion && c->atiendeConn == aconn && c->conv != nullptr && c->conv->atendida()) atiende = k;
        lista.push_back({{"aconn", aconn}, {"nombre", a.nombre}, {"lugar", a.lugar}, {"ip", a.ip}, {"atiende", atiende}});
    }
    return lista;
}

json ConexionesTotem::conversacionesJson() const {
    json lista = json::array();
    for (const auto& [k, cp] : conns_) {
        const Conexion& c = *cp;
        json f = fila(c);
        f["atiendeConn"] = c.conv != nullptr && c.conv->atendida() ? c.atiendeConn : std::string();
        f["enCurso"] = c.conv != nullptr && c.conv->enCurso();
        f["pedido"] = c.conv != nullptr ? c.conv->pedido() : json();
        // Los campos con lo que hace falta para editarlos (el tipo, las opciones), en el orden
        // de la conversación (o del tótem, si todavía no empezó).
        const Totem* t = totems_ != nullptr ? totems_->porId(c.totem) : nullptr;
        if (c.conv == nullptr && t == nullptr) {
            f["editables"] = json::array();
        } else {
            const Totem& tt = c.conv != nullptr ? c.conv->totem() : *t;
            json cs = c.conv != nullptr ? c.conv->camposParaMostrar() : Conversacion(tt, evento_, idioma_).camposParaMostrar();
            for (std::size_t i = 0; i < cs.size() && i < tt.campos.size(); ++i) {
                cs[i]["tipo"] = nombreDeTipo(tt.campos[i].tipo);
                cs[i]["obligatorio"] = tt.campos[i].obligatorio;
                cs[i]["opciones"] = tt.campos[i].opciones;
            }
            f["editables"] = cs;
        }
        lista.push_back(f);
    }
    return lista;
}

json ConexionesTotem::paraAtencion(const std::string& aconn) const {
    json cuerpo{{"msg", {{"type", "conversaciones"}, {"evento", evento_}, {"lista", conversacionesJson()}}}};
    if (aconn.empty()) cuerpo["atiende"] = "*";
    else cuerpo["aconn"] = aconn;
    return cuerpo;
}

void ConexionesTotem::callarAsistente(Conexion& c) {
    p_.callar(c.conn, c.canal, false); // el oído sigue: quien atiende lee lo que se le entiende
    c.ritmo.hablando = false;
    c.sintetizando = false;
    c.ritmo.finHablaMs = 0;
    c.ritmo.hayListo = false;
    c.ritmo.buscandoFoto = false;
}

void ConexionesTotem::interrumpido(const std::string& conn, std::int64_t ahora) {
    Conexion* cp = buscar(conn);
    if (cp == nullptr) return;
    Conexion& c = *cp;
    // Solo con el asistente hablando en una conversación en curso, y no en la despedida
    // (el `listo` se manda cuando termina de decirla). Atendida, habla una persona.
    if (!c.ritmo.hablando || c.ritmo.hayListo || c.conv == nullptr || !c.conv->enCurso() || c.conv->atendida()) return;
    // La síntesis es una para todos los tótems: se corta solo si no habla en otro.
    const bool soloEsta = std::none_of(conns_.begin(), conns_.end(), [&c](const auto& kv) {
        return kv.second.get() != &c && kv.second->ritmo.hablando;
    });
    p_.callar(c.conn, c.canal, soloEsta); // lo que la síntesis entregue todavía no sale
    c.ritmo.hablando = false;
    c.sintetizando = false;
    c.ritmo.finHablaMs = 0;
    c.ritmo.ultimaVozMs = ahora;
    // Lo mismo que cuando termina de hablar (tic): la foto o el documento se buscan desde ahora.
    if (c.ritmo.buscandoFoto) c.ritmo.fotoDesdeMs = ahora;
    if (c.ritmo.buscandoDocumento) c.ritmo.docDesdeMs = ahora;
    c.estado = c.ritmo.buscandoFoto ? "foto" : !c.ritmo.buscandoDocumento ? "escuchando" : c.conv->buscandoDorso() ? "dorso" : "documento";
    p_.enviar(c.conn, {{"type", "estado"}, {"estado", c.estado}});
    anotar(diag::Nivel::Info, i18n::llenar(i18n::traducir("registro", "tótem (%1): la persona interrumpió al asistente"), {c.conn}));
    p_.vivoCambio();
    revisarTic();
}

void ConexionesTotem::liberarAtencion(Conexion& c) {
    if (!c.enAtencion) return;
    c.enAtencion = false;
    c.atiendeConn.clear();
    p_.alGateway("atendida", {{"conn", c.conn}, {"aconn", ""}});
    p_.enviar(c.conn, {{"type", "atiende"}, {"nombre", ""}, {"activo", false}});
    if (c.estado == "atendida") c.estado = "escuchando";
}

std::string ConexionesTotem::atender(const std::string& conn, const std::string& quien, const std::string& aconn,
                                     std::int64_t ahora) {
    Conexion* cp = buscar(conn);
    if (cp == nullptr) return i18n::traducir("TotemsViewModel", "No hay esa conexión (ya se fue).");
    Conexion& c = *cp;
    if (c.conv != nullptr && c.conv->atendida() && c.atiendeConn != aconn)
        return i18n::llenar(i18n::traducir("TotemsViewModel", "Ya la atiende %1."), {c.conv->atendidaPor().empty() ? i18n::traducir("TotemsViewModel", "alguien del equipo") : c.conv->atendidaPor()});
    if (c.conv == nullptr || !c.conv->enCurso()) {
        // Todavía no conversa (o ya terminó y la persona sigue ahí): empieza una, atendida.
        const Totem* t = totems_ != nullptr ? totems_->porId(c.totem) : nullptr;
        if (t == nullptr) return i18n::traducir("TotemsViewModel", "Ese tótem ya no existe.");
        c.agente.reset(); // usa la conversación anterior
        c.conv = std::make_unique<Conversacion>(*t, evento_, idioma_);
        const bool conocida = !c.ultimaCara.empty() && c.ultimaCara != "?";
        if (conocida) c.conv->conocerCara({c.ultimaCara, c.ultimoNombre, false});
        c.ritmo.primeraDelCanal = false;
        c.esperaVacio = false;
        c.pedidoEnviado.clear();
    }
    if (!c.conv->atender(quien)) return i18n::traducir("TotemsViewModel", "Esa conversación ya terminó.");
    if (c.agente) c.agente->cancelar(); // lo que el modelo estuviera pensando ya no se dice
    callarAsistente(c);
    c.atiendeConn = aconn;
    c.enAtencion = true;
    c.estado = "atendida";
    // El gateway: su voz baja a ese tótem (si es una página de atención; desde la app, a nadie).
    p_.alGateway("atendida", {{"conn", conn}, {"aconn", aconn}});
    // La pantalla del tótem: quién la atiende, y los datos tal como están.
    p_.enviar(conn, {{"type", "atiende"}, {"nombre", quien}, {"activo", true}});
    p_.enviar(conn, {{"type", "campos"}, {"lista", c.conv->camposParaMostrar()}});
    anotar(diag::Nivel::Info, i18n::llenar(i18n::traducir("registro", "tótem «%1» (%2): la atiende %3"),
                                           {c.totem, conn, quien.empty() ? i18n::traducir("registro", "alguien del equipo") : quien}));
    (void)ahora;
    p_.vivoCambio();
    revisarTic();
    return {};
}

std::string ConexionesTotem::terminar(const std::string& conn, const std::string& como, const std::string& aconn,
                                      std::int64_t ahora) {
    Conexion* cp = buscar(conn);
    if (cp == nullptr) return i18n::traducir("TotemsViewModel", "No hay esa conexión (ya se fue).");
    Conexion& c = *cp;
    if (c.conv == nullptr || !c.conv->enCurso()) return i18n::traducir("TotemsViewModel", "Esa conversación no está en curso.");
    // Una página de atención termina solo lo que atiende ella (o lo que no atiende nadie); la
    // app y el MCP pueden siempre.
    if (!aconn.empty() && c.conv->atendida() && c.atiendeConn != aconn) return i18n::traducir("TotemsViewModel", "La atiende otra persona.");
    Conversacion::Respuesta r;
    if (como == "asistente") {
        if (!c.conv->atendida()) return i18n::traducir("TotemsViewModel", "No la está atendiendo nadie: ya la lleva el asistente.");
        r = c.conv->devolver();
    } else if (como == "guardar") {
        std::string error;
        r = c.conv->guardarYa(error);
        if (!error.empty()) return error;
    } else if (como == "cancelar") {
        r = c.conv->cancelar();
    } else {
        return i18n::traducir("TotemsViewModel", "«como» es asistente, guardar o cancelar.");
    }
    liberarAtencion(c);
    c.ritmo.ultimaVozMs = ahora;
    c.pedidoEnviado.clear(); // que la pantalla vuelva a mostrar qué se pide
    anotar(diag::Nivel::Info,
           i18n::llenar(i18n::traducir("registro", "tótem «%1» (%2): atención terminada (%3)"), {c.totem, conn, como}));
    aplicar(c, r, ahora);
    p_.vivoCambio();
    revisarTic();
    return {};
}

std::string ConexionesTotem::ponerDato(const std::string& conn, const std::string& clave, const std::string& valor,
                                       const std::string& aconn, std::int64_t ahora) {
    Conexion* cp = buscar(conn);
    if (cp == nullptr) return i18n::traducir("TotemsViewModel", "No hay esa conexión (ya se fue).");
    Conexion& c = *cp;
    if (c.conv == nullptr || !c.conv->enCurso()) return i18n::traducir("TotemsViewModel", "Todavía no hay una conversación en curso ahí: atendela para empezarla.");
    if (!aconn.empty() && c.conv->atendida() && c.atiendeConn != aconn) return i18n::traducir("TotemsViewModel", "La atiende otra persona.");
    std::string error;
    const Conversacion::Respuesta r = c.conv->poner(clave, valor, error);
    if (!error.empty()) return error;
    c.ritmo.ultimaVozMs = ahora;
    aplicar(c, r, ahora);
    revisarTic();
    return {};
}

bool ConexionesTotem::alEventoAtencion(const json& ev, std::int64_t ahora) {
    const std::string tipo = texto(ev, "tipo");
    if (tipo == "intento" && ev.value("atiende", false)) {
        anotar(diag::Nivel::Aviso,
               i18n::llenar(i18n::traducir("registro", "atención remota: intento fallido desde %1"), {texto(ev, "ip")}));
        return true;
    }
    const std::string aconn = texto(ev, "aconn");
    if (tipo == "atiende-conexion") {
        if (aconn.empty()) return true;
        if (ev.value("conectado", false)) {
            atienden_[aconn] = {texto(ev, "nombre"), texto(ev, "ip"), texto(ev, "lugar")};
            anotar(diag::Nivel::Info, i18n::llenar(i18n::traducir("registro", "atención remota: entró «%1» desde %2"),
                                                   {texto(ev, "nombre"), texto(ev, "ip")}));
            p_.alGateway("enviar", paraAtencion(aconn)); // lo que hay ahora, sin esperar al próximo cambio
        } else {
            atienden_.erase(aconn);
            // Se fue (cerró la página, se cortó): lo que atendía vuelve al asistente, que no
            // quede una persona frente al tótem esperando a nadie.
            std::vector<std::string> suyas;
            for (const auto& [k, c] : conns_)
                if (c->enAtencion && c->atiendeConn == aconn && c->conv != nullptr && c->conv->atendida()) suyas.push_back(k);
            for (const std::string& k : suyas) terminar(k, "asistente", {}, ahora);
        }
        p_.vivoCambio();
        return true;
    }
    if (tipo != "atiende") return false;
    const std::string conn = texto(ev, "conn");
    const std::string nombre = texto(ev, "nombre");
    const std::string accion = texto(ev, "accion");
    std::string error;
    if (accion == "atender") error = atender(conn, nombre, aconn, ahora);
    else if (accion == "devolver") error = terminar(conn, "asistente", aconn, ahora);
    else if (accion == "guardar") error = terminar(conn, "guardar", aconn, ahora);
    else if (accion == "cancelar") error = terminar(conn, "cancelar", aconn, ahora);
    else if (accion == "dato") error = ponerDato(conn, texto(ev, "clave"), texto(ev, "valor"), aconn, ahora);
    // Lo que no se pudo, a quien lo pidió (lo que sí, lo ve en la lista que se le manda).
    if (!error.empty() && !aconn.empty())
        p_.alGateway("enviar", {{"aconn", aconn}, {"msg", {{"type", "aviso"}, {"texto", error}, {"conn", conn}}}});
    return true;
}

// ============================================================================
// El modelo que entiende (el rol «registro»; registro/AgenteRegistro, registro/AgenteTotem)
//
// Iniciativa mixta: el guion pregunta y confirma; el modelo entiende lo que la persona dice
// (varios datos de una, correcciones, preguntas sobre el evento con la ficha del tótem) y anota
// por herramientas que validan. Uno por conversación (lo crea quien tiene el servidor: los
// Puertos). Sin modelo, o si no contesta a tiempo, el guion solo.
// ============================================================================

void ConexionesTotem::crearAgente(Conexion& c, bool avisar) {
    c.agente.reset();
    c.textoModelo.clear();
    c.diceAntes.clear();
    if (c.conv == nullptr || !c.conv->totem().conModelo) return;
    c.agente = p_.crearEntendedor(c.conn, c.totem, *c.conv, avisar);
}

bool ConexionesTotem::alOidoAgente(Conexion& c, const std::string& dicho) {
    // El modelo puede no estar listo cuando la conversación empieza (el servidor local
    // carga en unos segundos): se vuelve a intentar en cada turno, y apenas está, entiende
    // él. Antes se decidía una vez y la conversación entera quedaba con el guion solo.
    // (Un agente que ya se cayó por no contestar sigue existiendo: no se rehace acá.)
    if (c.agente == nullptr && c.conv != nullptr && c.conv->totem().conModelo) crearAgente(c, false);
    return c.agente != nullptr && c.agente->oido(dicho);
}

void ConexionesTotem::alTextoModelo(const std::string& conn, const std::string& acumulado, std::int64_t ahora) {
    // Lo que va escribiendo: a la pantalla del tótem en el acto (y a quien mira la conversación).
    Conexion* k = buscar(conn);
    if (k == nullptr) return;
    k->textoModelo = acumulado;
    k->diceTexto = acumulado;
    k->ritmo.ultimaVozMs = ahora; // pensando no es un silencio
    p_.enviar(k->conn, {{"type", "dice"}, {"texto", acumulado}});
    p_.vivoCambio();
}

void ConexionesTotem::alFraseModelo(const std::string& conn, const std::string& frase, std::int64_t) {
    // Frase por frase a la voz, mientras sigue escribiendo.
    if (Conexion* k = buscar(conn)) hablar(*k, frase);
    revisarTic();
}

void ConexionesTotem::alListoModelo(const std::string& conn, const Conversacion::Respuesta& r, std::int64_t ahora) {
    // Terminó el turno: lo que hace el guion (confirmar lo anotado, retomar la pregunta…).
    Conexion* k = buscar(conn);
    if (k == nullptr || k->conv == nullptr) return;
    k->diceAntes = std::move(k->textoModelo);
    k->textoModelo.clear();
    k->ritmo.ultimaVozMs = ahora;
    aplicar(*k, r, ahora);
    k->diceAntes.clear(); // si el guion no dijo nada, lo del modelo ya está en la pantalla
    revisarTic();
}

void ConexionesTotem::alCaerModelo(const std::string& conn, const std::string& motivo) {
    const Conexion* k = buscar(conn);
    if (k == nullptr) return;
    anotar(diag::Nivel::Aviso,
           i18n::llenar(i18n::traducir("registro", "tótem «%1» (%2): el modelo no contesta, sigue el guion solo (%3)"),
                        {k->totem, k->conn, motivo}));
    p_.vivoCambio();
}

} // namespace ba::registro
