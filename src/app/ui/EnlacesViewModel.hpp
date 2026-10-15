// ============================================================================
// src/app/ui/EnlacesViewModel.hpp — todos los enlaces del aparato, en un lugar
//
// Cada servicio web que el aparato le ofrece a alguien (los invitados, el
// público, el teleprompter, los rótulos desde el celular, la botonera, la
// sesión remota, el túnel...) tenía su enlace en SU página, con su propio
// diálogo de QR. Para pasar un enlace había que acordarse dónde estaba. Acá
// están todos: con su estado (el LED), el botón para activarlo si se puede, el
// enlace de la red local y el de afuera (el túnel), para copiar, abrir o
// mostrar en QR.
//
// El catálogo se declara UNA vez (en el .cpp) y lo leen la página Enlaces y el
// MCP (enlaces_listar / enlace_activar). Los enlaces los da la sala
// (EnlacesSala) y son los mismos que dan las otras páginas: acá no se arma
// ninguno a mano. Los que son por persona (cada integrante, cada televisor,
// cada invitación) no se listan uno por uno: la fila lleva a la página donde
// se dan.
//
// No conoce a las clases de cada servicio: StudioViewModel le pasa, en
// `Fuentes`, cómo preguntar y cómo prender cada cosa.
// ============================================================================
#pragma once

#include <functional>
#include <map>
#include <string>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace ba::app {

namespace sala {
class EnlacesSala;
}
struct ServicioEnlace; ///< una fila del catálogo (EnlacesViewModel.cpp)

class EnlacesViewModel : public QObject {
    Q_OBJECT
    /// [{id, grupo, nombre, para, led ("ok"|"aviso"|"apagado"|"error"), estado,
    ///   activable, activo, local, remoto, nota, pagina, porPersona, accion}].
    Q_PROPERTY(QVariantList servicios READ servicios NOTIFY cambio)
    /// Hay túnel andando: los enlaces de afuera valen.
    Q_PROPERTY(bool hayTunel READ hayTunel NOTIFY cambio)

   public:
    /// Lo que el modelo necesita saber y poder hacer, sin conocer a cada clase.
    struct Fuentes {
        std::function<sala::EnlacesSala*()> enlaces;
        std::function<bool()> salaEnLinea;
        std::function<bool()> salaCerrada;
        std::function<void(bool)> cerrarSala;
        /// {activo, via, estado ("listo"|"conectando"|"error"|"apagado"), url, texto}
        std::function<QVariantMap()> tunel;
        /// "" si arrancó; si no, el motivo.
        std::function<QString(bool)> activarTunel;
        /// {activo, url, aviso}
        std::function<QVariantMap()> apiRemota;
        std::function<void(bool)> activarApiRemota;
        /// {activo, puerto, error}
        std::function<QVariantMap()> satellite;
        std::function<QString(bool)> activarSatellite;
        /// La dirección de la sala en la red local (http://ip:puerto), "" si no hay.
        std::function<QString()> origenLocal;
        /// {hay (la web generada), evento (el abierto)}
        std::function<QVariantMap()> webEvento;
        std::function<void()> abrirConsolaWeb;
    };

    explicit EnlacesViewModel(Fuentes f, QObject* parent = nullptr);

    QVariantList servicios() const;
    bool hayTunel() const;

    /// Vuelve a pedirle a la sala los enlaces (los de afuera, si hay túnel).
    Q_INVOKABLE void refrescar();
    /// Prende o apaga el servicio `id` (los que tienen `activable`). "" o el motivo.
    Q_INVOKABLE QString activar(const QString& id, bool activo);
    /// La acción propia de una fila (`accion`: p. ej. abrir la consola web).
    Q_INVOKABLE void accion(const QString& id);

    /// Algo cambió en un servicio (lo llama quien conoce las señales).
    void avisarCambio();

   signals:
    void cambio();

   private:
    QVariantMap fila(const ServicioEnlace& s) const;
    void pedir(const std::string& id, bool remoto);

    Fuentes f_;
    /// Lo que contestó la sala: id → {local, remoto, nota}.
    std::map<std::string, QVariantMap> urls_;
    bool tuvoTunel_ = false;
    bool tuvoSala_ = false;
};

} // namespace ba::app
