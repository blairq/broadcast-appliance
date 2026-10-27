// ============================================================================
// src/app/registro/AgenteTotem.cpp — ver AgenteTotem.hpp
// ============================================================================
#include "registro/AgenteTotem.hpp"

#include "i18n/I18n.hpp"

#include <QPointer>

#include "ajustes/Ajustes.hpp"
#include "diag/Registro.hpp"

namespace ba::registro {

AgenteTotem::Tiempos AgenteTotem::tiemposDeAjustes() {
    Tiempos t;
    t.primeraMs = ba::ajustes::sAMs("registro.agentePrimeraS", 5);
    t.turnoMs = ba::ajustes::sAMs("registro.agenteTurnoS", 15);
    t.fallasMax = ba::ajustes::entero("registro.agenteFallas", 2);
    return t;
}

AgenteTotem::AgenteTotem(ba::app::agent::FuenteMirada fuente, std::string prompt, Conversacion& conv, Tiempos tiempos,
                         QObject* parent)
    : QObject(parent), fuente_(std::move(fuente)), conv_(conv), agente_(std::move(prompt), fuente_.modelo),
      tiempos_(tiempos) {
    primera_.setSingleShot(true);
    tope_.setSingleShot(true);
    connect(&primera_, &QTimer::timeout, this, [this]() {
        falla(ba::i18n::llenar(ba::i18n::traducir("registro", "no empezó a contestar en %1 s"),
                               {std::to_string(tiempos_.primeraMs / 1000)}));
    });
    connect(&tope_, &QTimer::timeout, this,
            [this]() {
                falla(ba::i18n::llenar(ba::i18n::traducir("registro", "tardó más de %1 s en contestar"),
                                       {std::to_string(tiempos_.turnoMs / 1000)}));
            });
    conv_.usarModelo(true);
}

AgenteTotem::~AgenteTotem() {
    if (pedido_) pedido_->cancelar();
    if (calentando_) calentando_->cancelar();
}

void AgenteTotem::precalentar() {
    if (caido_ || calentando_) return;
    QPointer<AgenteTotem> yo(this);
    calentando_ = ba::app::agent::ChatEnVivo::el().pedir(fuente_, agente_.precalentar(conv_), this, nullptr,
                                                        [yo](TurnoModelo, std::string) {
                                                            if (yo) yo->calentando_.reset();
                                                        });
}

bool AgenteTotem::oido(const std::string& dicho) {
    if (caido_ || !conv_.enCurso() || conv_.atendida()) return false;
    if (ocupado_) {
        // Siguió hablando mientras el modelo pensaba: va todo junto en el turno siguiente.
        pendiente_ += (pendiente_.empty() ? "" : " ") + dicho;
        return true;
    }
    if (conv_.paraElGuion(dicho)) return false;
    empezar(dicho);
    return true;
}

void AgenteTotem::cancelar() {
    if (pedido_) pedido_->cancelar();
    pedido_.reset();
    primera_.stop();
    tope_.stop();
    ocupado_ = false;
    pendiente_.clear();
    frases_.terminar();
}

void AgenteTotem::empezar(const std::string& dicho) {
    ocupado_ = true;
    dicho_ = dicho;
    texto_.clear();
    frases_.terminar();
    dijoAlgo_ = rechazos_ = false;
    mandar(agente_.empezarTurno(conv_, dicho));
    tope_.start(tiempos_.turnoMs);
}

void AgenteTotem::mandar(const nlohmann::json& cuerpo) {
    empezo_ = false;
    primera_.start(tiempos_.primeraMs);
    QPointer<AgenteTotem> yo(this);
    pedido_ = ba::app::agent::ChatEnVivo::el().pedir(
        fuente_, cuerpo, this,
        [yo](const std::string& nuevo) {
            if (yo) yo->alNuevo(nuevo);
        },
        [yo](TurnoModelo t, std::string error) {
            if (yo) yo->alTerminarVuelta(t, error);
        });
}

void AgenteTotem::alNuevo(const std::string& nuevo) {
    if (!ocupado_) return;
    empezo_ = true;
    primera_.stop();
    if (nuevo.empty()) return;
    texto_ += nuevo;
    if (alTexto) alTexto(paraHablar(texto_));
    // La primera frase sale apenas está entera: la voz arranca mientras el modelo sigue.
    for (const std::string& f : frases_.agregar(nuevo)) {
        const std::string h = paraHablar(f);
        if (!h.empty() && alFrase) alFrase(h);
    }
}

void AgenteTotem::alTerminarVuelta(const TurnoModelo& t, const std::string& error) {
    if (!ocupado_) return;
    pedido_.reset();
    primera_.stop();
    if (!error.empty()) {
        falla(error);
        return;
    }
    if (const std::string resto = paraHablar(frases_.terminar()); !resto.empty() && alFrase) alFrase(resto);
    // En depuración (son datos de la persona): qué anotó el modelo en cada vuelta.
    for (const LlamadaHerramienta& ll : t.llamadas)
        ba::diag::registro().linea(ba::diag::Nivel::Depuracion, "registro", 0,
                                   ba::i18n::llenar(ba::i18n::traducir("registro", "modelo: %1 %2"), {ll.nombre, ll.argumentos}));
    if (!t.texto.empty())
        ba::diag::registro().linea(ba::diag::Nivel::Depuracion, "registro", 0,
                                   ba::i18n::llenar(ba::i18n::traducir("registro", "modelo (texto): «%1»"), {t.texto}));
    const AgenteRegistro::Vuelta v = agente_.alTerminarVuelta(conv_, t);
    dijoAlgo_ = v.dijoAlgo;
    rechazos_ = v.rechazos;
    // Lo que pidió decir con registro_decir (si además lo escribió como texto, ya salió).
    if (!v.decir.empty() && texto_.find_first_not_of(" \n") == std::string::npos) {
        texto_ = v.decir;
        if (alTexto) alTexto(paraHablar(texto_));
        if (const std::string h = paraHablar(v.decir); !h.empty() && alFrase) alFrase(h);
    }
    if (v.otraVuelta) {
        // Algo no validó: una vuelta más para que lo repregunte (dentro del mismo tope).
        mandar(v.cuerpo);
        return;
    }
    tope_.stop();
    fallas_ = 0;
    bool usarGuion = false;
    Conversacion::Respuesta r = conv_.despuesDelModelo(v.dijoAlgo, v.rechazos, usarGuion);
    if (usarGuion) r = conv_.oido(dicho_); // no anotó ni dijo nada: lo entiende el guion
    cerrar(r, usarGuion);
}

void AgenteTotem::falla(const std::string& motivo) {
    if (!ocupado_) return;
    ba::diag::registro().linea(ba::diag::Nivel::Info, "registro", 0,
                               ba::i18n::llenar(ba::i18n::traducir("registro", "el modelo no contestó a tiempo, sigue el "
                                                                               "guion en este turno: %1"),
                                                {motivo}));
    if (pedido_) pedido_->cancelar();
    pedido_.reset();
    primera_.stop();
    tope_.stop();
    frases_.terminar();
    if (++fallas_ >= tiempos_.fallasMax && !caido_) {
        caido_ = true;
        motivo_ = motivo;
        conv_.usarModelo(false);
        if (alCaer) alCaer(motivo);
    }
    // Lo que el modelo alcanzó a anotar (una primera vuelta) queda; si no anotó nada, el guion
    // entiende lo oído con sus normalizadores, como sin modelo.
    bool usarGuion = false;
    Conversacion::Respuesta r = conv_.despuesDelModelo(dijoAlgo_, rechazos_, usarGuion);
    if (usarGuion) r = conv_.oido(dicho_);
    cerrar(r, true);
}

void AgenteTotem::cerrar(const Conversacion::Respuesta& r, bool porGuion) {
    ocupado_ = false;
    agente_.anotarIntercambio(dicho_, paraHablar(texto_ + (texto_.empty() || r.decir.empty() ? "" : " ") + r.decir));
    // `alListo` puede terminar la conversación (o borrar esto): lo juntado se toma antes.
    const std::string juntado = std::move(pendiente_);
    pendiente_.clear();
    QPointer<AgenteTotem> yo(this);
    if (alListo) alListo(r, porGuion);
    if (!yo || juntado.empty() || !conv_.enCurso()) return;
    if (!oido(juntado)) {
        const Conversacion::Respuesta r2 = conv_.oido(juntado);
        if (alListo) alListo(r2, true);
    }
}

} // namespace ba::registro
