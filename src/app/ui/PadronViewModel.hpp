// ============================================================================
// src/app/ui/PadronViewModel.hpp — el padrón: la gente del evento abierto
//
// Vive en la misma carpeta que el guion pero en su propio archivo, y con otro ritmo: el
// guion se escribe antes y se guarda cuando uno quiere; el padrón cambia durante el
// evento —cada persona que entra es un cambio— y se guarda SOLO, en el momento. Por eso
// no tiene «sin guardar»: en la puerta, con cola, nadie va a acordarse de apretar guardar.
//
// LAS REGLAS VIVEN ACÁ, para la página y para el MCP (`padron_*`): dejar pasar solo a
// alguien exige su ficha de rostro, y que exista (`motivoEntrarSolo`).
//
// LAS ACREDITACIONES por QR: mientras hay un evento abierto se le pide al gateway cada
// 2 s quiénes escanearon su invitación. (Cuando el gateway avise por su canal de eventos,
// esto pasa a escucharlo: ver `recogerAcreditaciones`.)
// ============================================================================
#pragma once

#include <functional>
#include <optional>

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include "evento/Evento.hpp"
#include "evento/Padron.hpp"

namespace ba::app {

class ConferenceViewModel;
class EventoAbierto;

class PadronViewModel : public QObject {
    Q_OBJECT
    /// [{id, personaId, nombre, papel, canal, estado, contacto, nota, dejarPasarSolo, vota,
    ///   codigo, llegoMs, porDonde, registro, datos}]
    Q_PROPERTY(QVariantList padron READ padron NOTIFY padronChanged)
    /// {total, presentes, presenciales, enLinea, confirmados, faltan, conVoto, votoPresente, sinDato}
    Q_PROPERTY(QVariantMap resumenPadron READ resumenPadron NOTIFY padronChanged)

   public:
    /// Si una ficha de rostro existe y su nombre.
    using FichaDe = std::function<std::pair<bool, std::string>(const std::string& personaId)>;

    PadronViewModel(EventoAbierto& abierto, ConferenceViewModel* sala, QObject* parent = nullptr);

    void setFichaDe(FichaDe f) { fichaDe_ = std::move(f); }

    QVariantList padron() const;
    QVariantMap resumenPadron() const;
    const evento::Padron& modelo() const { return padron_; }
    std::optional<evento::Padron::Votantes> votantes() const;

    /// Al abrir otro evento (o cerrar): el padrón de su carpeta.
    void cargar();
    /// Escribe el padrón al disco (cada cambio ya lo hace solo).
    bool guardarAhora();

    /// "" o por qué esa persona no puede quedar con «entra sola»: sin ficha de rostro no hay
    /// con qué reconocerla, y una ficha que no existe no deja pasar a nadie. `datos` como
    /// los de `anotar`/`cambiarAsistente`; `id`: el anotado que se cambia (su ficha, si
    /// `datos` no trae otra).
    std::string motivoEntrarSolo(const QVariantMap& datos, const QString& id = {}) const;

    /// Devuelve el id del anotado, o vacío (sin evento abierto, sin nombre o sin permiso).
    Q_INVOKABLE QString anotar(const QVariantMap& datos);
    /// Cambia solo las claves que vengan en el mapa.
    Q_INVOKABLE bool cambiarAsistente(const QString& id, const QVariantMap& datos);
    Q_INVOKABLE bool quitarAsistente(const QString& id);
    /// Marca que llegó. `porDonde`: "rostro", "qr", "mostrador", "lobby".
    Q_INVOKABLE bool acreditar(const QString& id, const QString& porDonde);
    Q_INVOKABLE bool despedir(const QString& id);
    /// Busca a alguien por su ficha de rostro o por el nombre tipeado. El id, o vacío.
    QString buscarEnPadron(const QString& personaId, const QString& nombre) const;
    bool puedeEntrarSolo(const QString& personaId) const;
    bool acreditarPorPersona(const QString& personaId, const QString& porDonde);
    /// Lo que deja un tótem de registro: busca a la persona por su ficha (o por el nombre,
    /// si ya estaba en la lista), le suma `datos` y el tótem, o la anota; y la acredita.
    QString registrarDesdeTotem(const QString& personaId, const QString& nombre, const QString& totem, const nlohmann::json& datos);
    bool registradoPorTotem(const QString& personaId) const;
    /// Vuelca la lista blanca vieja del lobby (Evento::autoAceptar) al padrón, una vez.
    /// true si había algo (el guion se tiene que volver a guardar sin ella).
    bool migrarAutoAceptar(const std::vector<evento::Invitado>& lista);
    /// La invitación de una persona del padrón: el texto, el enlace con su código adentro y
    /// los módulos del QR. Va al gateway a pedir el enlace, así que contesta por `luego`.
    void pedirInvitacion(const QVariantMap& quien, bool remoto, std::function<void(const QVariantMap&)> luego);

   signals:
    void padronChanged();
    void notify(const QString& texto, const QString& tipo);

   private:
    /// Va a buscar al gateway los que escanearon su invitación y los acredita.
    void recogerAcreditaciones();

    EventoAbierto& ab_;
    ConferenceViewModel* sala_ = nullptr;
    evento::Padron padron_;
    FichaDe fichaDe_;
    QTimer buzon_;
};

} // namespace ba::app
