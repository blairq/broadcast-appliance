#pragma once
// El recinto como modelo: una fila por banca con TODO lo que muestra su celda, ya
// cruzado. Lo que se ve de una banca sale de cinco lados (el lugar y quién se sienta:
// Cuerpo; la fila de oradores: Bancas; la central de conferencia: ConferenciaDcn; la
// sesión remota: SesionRemota; la foto de su posición PTZ: ControlCamaras) y antes cada
// celda de RecintoGrilla.qml lo cruzaba con búsquedas lineales en cada redibujo. Acá se
// cruza una vez por cambio, y la vista se enlaza a roles.
//
// Cada fila: lo del lugar (banca, fila, columna, x?, y?, toma, integrante) y además
//   turno         su lugar en la fila de oradores (1, 2…; 0 si no está)
//   alAire        tiene la palabra
//   unidad        su unidad en la central ({tipo, conectada…} o nada)
//   micAbierto, turnoCentral (pidió la palabra en la central: su turno), leToca,
//   presidente, sinUnidad         (todo falso/0 sin la central conectada)
//   remoto        su participante de la sesión remota (o nada); conectado, esperando,
//                 habilitado
//   fotoCara      en vivo si está conectado por el navegador; si no, su foto del Cuerpo;
//                 si no tiene, la de la posición PTZ de su banca
//   fotoPtz       la foto de la posición PTZ de su banca ("" si no hay)
// En QML: Studio.asamblea.recinto (bancas: el modelo, por banca; porLugar: "fila,columna"
// -> fila; presidencia: la fila de la banca de la presidencia).

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

#include "ui/ListaObjetos.hpp"

namespace ba::app::asamblea {

class RecintoModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* bancas READ bancas CONSTANT)
    Q_PROPERTY(QVariantMap porLugar READ porLugar NOTIFY cambio)
    Q_PROPERTY(QVariantMap presidencia READ presidencia NOTIFY cambio)
    /// Alguien de la sesión remota espera que lo habiliten (para el titileo).
    Q_PROPERTY(bool hayEsperando READ hayEsperando NOTIFY cambio)

   public:
    /// De dónde lee (cada uno, el getter de su dueño para la vista).
    struct Fuentes {
        std::function<QVariantMap()> recinto;       ///< Cuerpo: {bancas: [lugar], presidencia}
        std::function<QVariantMap()> oradores;      ///< Bancas: {cola: [{id}], al_aire: {id}}
        std::function<QVariantMap()> central;       ///< ConferenciaDcn: {unidades, pedidos, abiertos, siguiente}
        std::function<bool()> centralConectada;
        std::function<QVariantList()> participantes; ///< SesionRemota: [{id, conectado, estado, lugar}]
        std::function<QString(const QString&)> fotoRemota; ///< la foto en vivo de un conectado ("" si no hay)
        std::function<QVariantList()> camaras;      ///< ControlCamaras: [{entrada, posiciones: [{id, nombre, miniatura_url}]}]
    };
    explicit RecintoModel(Fuentes f, QObject* parent = nullptr);

    QAbstractItemModel* bancas() { return &bancas_; }
    QVariantMap porLugar() const { return porLugar_; }
    QVariantMap presidencia() const { return presidencia_; }
    bool hayEsperando() const { return hayEsperando_; }

    /// Vuelve a cruzar todo, ya.
    void recalcular();
    /// Lo mismo al terminar la vuelta (lo conecta el dueño a cada aviso de las fuentes:
    /// varios avisos juntos, un solo cruce).
    void pedirRecalculo() { tarde_.start(); }

   signals:
    void cambio();

   private:
    QVariantMap fila(const QVariantMap& lugar, const QVariantMap& ctx) const;

    Fuentes f_;
    QTimer tarde_;
    ListaObjetos bancas_{QStringLiteral("banca"), {}};
    QVariantMap porLugar_, presidencia_;
    bool hayEsperando_ = false;
};

} // namespace ba::app::asamblea
