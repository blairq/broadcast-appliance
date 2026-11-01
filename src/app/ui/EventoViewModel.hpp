// ============================================================================
// src/app/ui/EventoViewModel.hpp — el evento, para la interfaz (Studio.evento)
//
// La página Evento es varias cosas, y cada una es su view model (con sus propiedades y
// sus avisos):
//
//   lista     EventosListaViewModel   los eventos, el en curso, las carpetas, el automático
//   guion     GuionViewModel          editar el guion, guardar, la revisión, el chequeo
//   padron    PadronViewModel         la gente, las acreditaciones, las invitaciones
//   corrida   CorridaViewModel        la línea viva: EjecutorLinea, HOLD, la retoma
//   web       WebEventoViewModel      la página web del evento
//   informe   InformeEventoViewModel  informe, capítulos, acta, escaleta
//
// Todos leen el mismo EventoAbierto (el guion, la línea y su lectura como cues): una sola
// copia del evento abierto.
//
// LA LÍNEA MANDA. El plan del evento es su línea de tiempo (evento/Linea.hpp); el guion
// de cues que ven la página Guion y `guion_*` es su lectura (proyectar), y editarlo edita
// la línea (reconciliar). Un evento escrito antes de la línea se migra al abrirlo (con una
// copia del v1 en Respaldos/).
//
// ESTO ES LA COSTURA: abrir, cerrar, crear y borrar un evento tocan a todos (en orden).
// Nada más: lo de cada pieza se usa en la pieza (`Studio.evento.guionVm.guion`,
// `ev.corridaVm().lineaGo(…)`), sin una segunda API que la reexponga.
// ============================================================================
#pragma once

#include <filesystem>
#include <functional>
#include <optional>

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include "evento/Corrida.hpp"
#include "evento/Evento.hpp"
#include "evento/EventoStore.hpp"
#include "evento/LineaEdicion.hpp"
#include "evento/Padron.hpp"
#include "sala/ListaPrompters.hpp"
#include "ui/CorridaViewModel.hpp"
#include "ui/EventoAbierto.hpp"
#include "ui/EventosListaViewModel.hpp"
#include "ui/GuionViewModel.hpp"
#include "ui/InformeEventoViewModel.hpp"
#include "ui/PadronViewModel.hpp"
#include "ui/WebEventoViewModel.hpp"

namespace ba::app {
class CarruselViewModel;
class AppRuntime;
class ConferenceViewModel;
class VentanillaMotor;
namespace graficos {
class Emisor;
class Vinculos;
} // namespace graficos

class EventoViewModel : public QObject {
    Q_OBJECT
    // --- Las piezas: la QML y el C++ las usan directo (`Studio.evento.guionVm.guion`) ----
    Q_PROPERTY(QObject* listaVm READ listaObj CONSTANT)
    Q_PROPERTY(QObject* guionVm READ guionObj CONSTANT)
    Q_PROPERTY(QObject* padronVm READ padronObj CONSTANT)
    Q_PROPERTY(QObject* corridaVm READ corridaObj CONSTANT)
    Q_PROPERTY(QObject* webVm READ webObj CONSTANT)
    Q_PROPERTY(QObject* informeVm READ informeObj CONSTANT)

   public:
    /// :param sinAbrirUltimo: no abre solo el último evento que se tocó (el arranque
    ///        anterior se lo llevó puesto; ver diag/DiarioDeArranque.hpp).
    /// `ventanilla`: el motor (del plano de control) se le pide y se lee su foto (F3-03).
    EventoViewModel(AppRuntime& rt, VentanillaMotor& ventanilla, ConferenceViewModel* sala, graficos::Emisor* graficos,
                    bool sinAbrirUltimo = false, QObject* parent = nullptr);
    ~EventoViewModel() override;

    // --- Las piezas, para el C++ ------------------------------------------------------------
    EventosListaViewModel& listaVm() { return *lista_; }
    const EventosListaViewModel& listaVm() const { return *lista_; }
    GuionViewModel& guionVm() { return *guion_; }
    const GuionViewModel& guionVm() const { return *guion_; }
    PadronViewModel& padronVm() { return *padron_; }
    const PadronViewModel& padronVm() const { return *padron_; }
    CorridaViewModel& corridaVm() { return *corrida_; }
    const CorridaViewModel& corridaVm() const { return *corrida_; }
    WebEventoViewModel& webVm() { return *web_; }
    const WebEventoViewModel& webVm() const { return *web_; }
    InformeEventoViewModel& informeVm() { return *informe_; }
    const InformeEventoViewModel& informeVm() const { return *informe_; }
    /// El evento abierto (el guion, la línea, su carpeta): la copia que leen todas las piezas.
    const EventoAbierto& eventoAbierto() const { return *ab_; }
    graficos::Emisor* emisorGraficos() const { return graficos_; }

    /// Los carruseles: los usan el guion (las opciones) y la corrida (los cues).
    void setCarruseles(CarruselViewModel* c);

    // --- Lo que toca a varias piezas a la vez: abrir, cerrar, crear, borrar… --------------
    Q_INVOKABLE QString crear(const QString& nombre, const QString& enCarpeta = {});
    Q_INVOKABLE bool abrir(const QString& carpeta);
    Q_INVOKABLE void cerrar();
    /// Lo borra del disco. Si es el abierto, primero lo cierra SIN guardar.
    Q_INVOKABLE bool borrar(const QString& carpeta);
    /// Pone en curso un evento (termina el que estaba); si su página web tiene dirección
    /// propia, la dirección pública pasa a ser esa.
    Q_INVOKABLE bool iniciarEvento(const QString& carpeta);
    /// Lo lleva a otra carpeta (se crea si no está), guardando antes si es el abierto.
    /// Devuelve la ruta nueva.
    Q_INVOKABLE QString moverEvento(const QString& carpeta, const QString& aCarpeta);

    // --- Dónde están las cosas ----------------------------------------------------------------
    /// La carpeta de un evento (vacía = el en curso; vacío si no hay).
    std::filesystem::path carpetaEvento(const std::string& carpeta = {}) const;
    /// El bitacora.jsonl del evento en curso (vacío si no hay ninguno).
    std::filesystem::path archivoBitacora() const { return archivoBitacoraDe({}); }
    std::filesystem::path archivoBitacoraDe(const std::string& carpeta) const;
    /// Eventos/ (donde están todos) y Config/ (lo que se fotografía).
    std::filesystem::path raizEventos() const { return ab_->store().raiz(); }
    std::filesystem::path carpetaConfig() const;

   signals:
    /// Los avisos de todas las piezas, juntos (a Studio.notify).
    void notify(const QString& texto, const QString& tipo);
    /// Se puso en curso un evento con sitio y «dirección al iniciar»: la dirección pública
    /// tiene que pasar a ser la de su subdominio (lo hace StudioViewModel).
    void pedirDireccion(const QString& subdominio);

   private:
    QObject* listaObj() const { return lista_; }
    QObject* guionObj() const { return guion_; }
    QObject* padronObj() const { return padron_; }
    QObject* corridaObj() const { return corrida_; }
    QObject* webObj() const { return web_; }
    QObject* informeObj() const { return informe_; }

    AppRuntime& rt_;
    graficos::Emisor* graficos_ = nullptr;
    // Hijos de este QObject: se van con él.
    EventoAbierto* ab_;
    EventosListaViewModel* lista_;
    GuionViewModel* guion_;
    PadronViewModel* padron_;
    CorridaViewModel* corrida_;
    WebEventoViewModel* web_;
    InformeEventoViewModel* informe_;
    QTimer vigiaEnCurso_; ///< cada minuto: el cambio de día y el evento automático
};

} // namespace ba::app
