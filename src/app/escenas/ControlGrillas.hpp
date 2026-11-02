// ============================================================================
// src/app/escenas/ControlGrillas.hpp — la grilla de personas, andando (modelo)
//
// Junta lo que hace falta para decidir quién va en cada «Grilla de personas»
// (fuentes ba_grilla) y se lo manda al gateway, que pinta los mosaicos:
//   - la gente: los admitidos de la Sala (snapshot del gateway) y los
//     integrantes habilitados de la Asamblea (asamblea::SesionRemota), con la
//     prioridad del integrante (Cuerpo, campo «prioridad»);
//   - quién habla: el evento «hablan» del gateway (mide el audio de cada uno) y,
//     en la asamblea, quien tiene la palabra;
//   - quién ya está al aire en otra fuente: lo que se ve en el programa —la
//     escena al aire y las capas DSK que están arriba, entrando en las escenas
//     anidadas (grilla::fuentesQueSeVen)—: el lugar de un invitado, el canal del
//     remoto con la palabra, la banca con la palabra y las grillas que se ven
//     (quien está en una de esas no se repite en las demás, ver el Repartidor);
//   - las grillas: las fuentes ba_grilla con sus ajustes. A cada una le asigna su
//     canal BA.Grilla.<k> (el primero libre) y lo guarda en sus ajustes;
//   - lo que el operador decidió a mano (grilla::Manual): fijar a alguien en una
//     grilla, sacarlo, moverlo de una a otra, devolverlo a lo automático, elegir el
//     orador («cuando alguien habla» con disparo a mano). Vive en memoria: las claves
//     son de la conexión de cada uno;
//   - para el orador: quién tiene la palabra y quién es el siguiente (asamblea), el
//     orador al aire y el siguiente de la fila (conferencia, modo oradores) y desde
//     cuándo habla cada uno sin parar (voz sostenida);
//   - la ESCENA DEL ORADOR (al_hablar = escena): la arma a pedido (armarEscenaOrador) y,
//     según «pasar_a_escena», la pone al aire sola en su salida (automático, con
//     Partes::ponerAlAire) o se lo pide al asistente de automatización (paraElAsistente)
//     y le avisa SIEMPRE al operador (notify): «¿paso a …?» y, cuando el corte se ve en la
//     foto, «pasé a …». La decisión es grilla::PaseEscenaOrador;
//   - los NOMBRES: el gateway no tiene fuentes, así que cada rótulo (nombre y cargo o
//     banca) se dibuja acá con Qt como una máscara A8 y se le manda una vez
//     (POST /api/v1/grillas/rotulos, de a tandas de menos de 1 MB).
// La lógica (reparto, lugares estables, disposición) es grilla::Repartidor; acá
// solo se juntan los datos y se manda el resultado cuando cambia. Lo del motor (la
// escena al aire, las capas DSK, las escenas y los ajustes de cada fuente) sale de la
// foto (F3-03, emision/FotoMotor.hpp): no entra al motor.
//
// Lo usan la ventana Propiedades (los ajustes de la fuente y quién está en ella:
// VistaGrillas), la Sala y la Asamblea (el resumen) y el MCP
// (grilla_personas_estado / _ajustar / _fijar / _sacar / _mover / _devolver), por el
// mismo camino.
// ============================================================================
#pragma once

#include "escenas/GrillaPersonas.hpp"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include "ui/CapturaError.hpp"

namespace ba::app {
class AppRuntime;
class ConferenceClient;
class VentanillaMotor;
}
namespace ba::app::asamblea {
class Cuerpo;
class SesionRemota;
}

namespace ba::app::escenas {

class ControlGrillas : public QObject {
    Q_OBJECT
    /// El error de una acción de la interfaz (las Q_INVOKABLE void): a quien mira.
    void avisarError(const QString& e) {
        if (!e.isEmpty() && !::ba::app::CapturaError::tomar(e)) emit notify(e, "error");
    }
    void avisarError(const std::string& e) { avisarError(QString::fromStdString(e)); }
    /// [{fuente, canal, grupo, al_aire, personas: [{clave, nombre, fijo, hablando}], fuera: [{clave, nombre}],
    /// mas, aviso?}]: lo que muestra cada grilla (y a quién se sacó a mano de cada una).
    Q_PROPERTY(QVariantList grillas READ grillasUi NOTIFY cambio)
    /// [{clave, nombre, origen, con_camara, al_aire, hablando, prioridad, en: [fuentes], fijo_en}]: la gente que puede
    /// aparecer en una grilla.
    Q_PROPERTY(QVariantList gente READ genteUi NOTIFY cambio)

   public:
    using json = nlohmann::json;
    /// Lo que usa (todo vive más que este modelo). `sesion` y `cuerpo` pueden faltar.
    struct Partes {
        ConferenceClient& sala;
        VentanillaMotor& motor; ///< la foto del motor (lo que se ve, las fuentes)
        asamblea::SesionRemota* sesion = nullptr;
        const asamblea::Cuerpo* cuerpo = nullptr;
        AppRuntime* rt = nullptr; ///< para armar la escena del orador (en el hilo del dueño del motor)
        /// Pone `escena` al aire en `salida` ("" = la principal), en el dueño del motor (con su
        /// transición). Lo da StudioViewModel; sin esto, el pase automático solo avisa.
        std::function<void(const std::string& salida, const std::string& escena)> ponerAlAire;
    };
    explicit ControlGrillas(Partes p, QObject* parent = nullptr);
    ~ControlGrillas() override;

    /// {grillas: [{fuente, canal, grupo, origen, orden, tope, al_aire, personas, fuera, mas}], gente: [...]}.
    json estado() const { return estado_; }
    QVariantList grillasUi() const;
    QVariantList genteUi() const;
    /// Cambia ajustes de una grilla (validados): "" o el error.
    std::string ajustar(const std::string& fuente, const json& cambios);
    /// Las fuentes ba_grilla que hay (nombres).
    std::vector<std::string> fuentes() const;

    // --- A mano (la vista y el MCP): "" o el error ------------------------------------
    /// Fija a `clave` en `fuente`: va siempre en esa (primero), aunque los filtros digan otra cosa.
    std::string fijar(const std::string& fuente, const std::string& clave);
    /// Saca a `clave` de `fuente` (y de las grillas de su grupo).
    std::string sacar(const std::string& fuente, const std::string& clave);
    /// La pasa de `desde` a `hacia`: la fija en `hacia` y, si son de grupos distintos, la saca de `desde`.
    std::string mover(const std::string& clave, const std::string& desde, const std::string& hacia);
    /// Vuelve a lo automático en `fuente` (ni fijada ni sacada).
    std::string devolver(const std::string& fuente, const std::string& clave);
    Q_INVOKABLE void fijarQml(const QString& fuente, const QString& clave) { avisarError(QString::fromStdString(fijar(fuente.toStdString(), clave.toStdString()))); }
    Q_INVOKABLE void sacarQml(const QString& fuente, const QString& clave) { avisarError(QString::fromStdString(sacar(fuente.toStdString(), clave.toStdString()))); }
    Q_INVOKABLE void moverQml(const QString& clave, const QString& desde, const QString& hacia) {
        avisarError(QString::fromStdString(mover(clave.toStdString(), desde.toStdString(), hacia.toStdString())));
    }
    Q_INVOKABLE void devolverQml(const QString& fuente, const QString& clave) { avisarError(QString::fromStdString(devolver(fuente.toStdString(), clave.toStdString()))); }
    /// El orador de `fuente` a mano (gana a cualquier disparo, en el acto); clave "" = que
    /// vuelva a decidirlo el disparo.
    std::string destacar(const std::string& fuente, const std::string& clave);
    Q_INVOKABLE void destacarQml(const QString& fuente, const QString& clave) { avisarError(QString::fromStdString(destacar(fuente.toStdString(), clave.toStdString()))); }
    /// Arma la escena del orador de `fuente`: la escena «Orador · <fuente>» con una grilla
    /// que muestra solo a su orador (muestra = orador, sigue = fuente), grande, con nombre y
    /// cargo. Lo que ya exista no se toca. No la pone al aire. "" o el error (lo que falle al
    /// crearla, después, va a notify). `escena`/`grilla`: los nombres que usa.
    std::string armarEscenaOrador(const std::string& fuente, std::string* escena = nullptr, std::string* grilla = nullptr);

    /// CUÁNTOS CUADROS NUEVOS MUESTRA CADA CARA (motor/AnalisisCeldas.hpp): mide `segundos`
    /// cada grilla que el gateway está pintando, celda por celda, en dos lugares:
    ///   - `entrada`: el mosaico apenas lo lee la app (lo que mandó el gateway y llegó: los
    ///     cuadros nuevos de cada persona, sus intervalos y los que el gateway publicó y no
    ///     se leyeron);
    ///   - `programa`: la misma celda en el programa (lo que se ve), si la grilla está al aire;
    ///     `programa_rect` [x, y, w, h] (fracciones del lienzo) dice dónde está la grilla en el
    ///     programa (de fábrica, el lienzo entero).
    /// La referencia de cada celda es lo que manda su capa (baja: 15 cps; alta: 30). `listo`
    /// se llama en el hilo de la interfaz con {grillas: [{fuente, canal, al_aire, entrada,
    /// programa}]}; queda en ultimaMedicion(). "" o el error (una por vez).
    std::string medir(int segundos, const json& opciones, std::function<void(const json&)> listo);
    json ultimaMedicion() const { return ultimaMedicion_; }

   signals:
    /// Para el operador (ya traducido): los errores de las acciones de la interfaz y los
    /// avisos del pase a la escena del orador.
    void notify(const QString& texto, const QString& tipo);
    /// Un pedido para el asistente de automatización (pasar a la escena del orador): lo
    /// conecta StudioViewModel (solo si el asistente está andando).
    void paraElAsistente(const QString& texto);
    void cambio();

   public slots:
    /// Junta todo, reparte y, si cambió, se lo manda al gateway.
    void recalcular();

   private:
    /// Lo que se ve en el programa: la escena al aire y las capas DSK de arriba, con las anidadas.
    std::vector<std::string> queSeVe() const;
    std::vector<grilla::Persona> gente(std::int64_t ahora, const std::vector<std::string>& seVe);
    std::vector<grilla::Config> configs(const std::vector<std::string>& seVe);
    /// Valida que haya una grilla `fuente` (y, si `clave` no está vacía, que esa persona esté conectada).
    std::string validarMano(const std::string& fuente, const std::string& clave) const;
    /// El pase a la escena del orador de cada grilla con al_hablar = escena (una vuelta).
    void pasarAEscenas(const std::vector<grilla::Config>& cfgs, const std::vector<grilla::Resultado>& res,
                       const std::vector<grilla::Persona>& g);
    /// Dibuja y manda al gateway los rótulos que todavía no tiene.
    void mandarRotulos(const std::map<std::string, std::pair<std::string, std::string>>& rotulos);

    Partes p_;
    grilla::Repartidor rep_;
    grilla::Manual manual_;
    std::set<std::string> hablan_;            ///< lo que dijo el gateway
    std::map<std::string, std::int64_t> voz_; ///< clave → la última vez que habló
    std::map<std::string, std::int64_t> hablaDesde_; ///< clave → desde cuándo habla sin parar
    std::set<std::string> rotulosEnviados_;        ///< los que el gateway ya tiene (o se le están mandando)
    std::map<std::string, grilla::PaseEscenaOrador> pases_; ///< fuente → su pase a la escena del orador
    struct Propuesto {
        std::string salida, escena, quien;
    };
    std::map<std::string, Propuesto> propuestos_; ///< fuente → lo que se le pidió al asistente (para avisar cuando pase)
    json ultimoPedido_;                       ///< lo último que se le mandó al gateway
    json estado_ = json::object();
    QVariantList grillasQt_, genteQt_; ///< estado_ para la vista (grillasUi, genteUi), armado al cambiar
    json ultimaMedicion_;   ///< lo último que midió medir()
    std::thread hiloMedir_; ///< la medición en curso (no en el hilo de la interfaz: toma candados de las entradas)
    std::mutex medirMu_;
    std::condition_variable medirCv_;
    bool midiendo_ = false;  ///< bajo medirMu_
    bool cortarMedir_ = false; ///< bajo medirMu_: se va (el destructor)
    QTimer reloj_;  ///< el programa y las fuentes cambian por muchos caminos: se mira seguido
    QTimer pronto_; ///< junta varios avisos seguidos en una sola vuelta
};

} // namespace ba::app::escenas
