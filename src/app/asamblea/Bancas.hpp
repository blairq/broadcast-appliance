// SPDX-License-Identifier: GPL-2.0-or-later
//
// Bancas: la fila de oradores del recinto. Cada banca sale del Cuerpo
// (asamblea/Cuerpo): su preset PTZ, su micrófono, su nombre y cargo validados.
// Es la FILA ÚNICA: también entran los que hablan desde el navegador (la sesión
// remota, en la sala o afuera; docs/funciones/SESION-REMOTA.md). A esos no se les mueve
// ninguna cámara: se les da la palabra por la sesión remota (`palabraRemota`).
//
// Poner una banca al aire es UNA acción:
//   1. la PTZ va al preset de la banca, SOLO si esa cámara no está al aire
//      (interlock: si está, no se mueve y se avisa);
//   2. la escena que muestra esa cámara queda ARMADA en el preview (modo
//      «armar», de fábrica; sale con TAKE) o se corta («directo»);
//   3. el zócalo toma nombre, cargo y bloque de la persona ({orador});
//   4. arranca el reloj de su tiempo;
//   5. se abre el micrófono de su banca y se cierra el de la anterior.
// Todo lo hace una persona (el operador o lo que él pidió): lo automático pasa
// por las guardas de aire del MCP.
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace ba::app::asamblea {

class Cuerpo;

class Bancas : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap estado READ estado NOTIFY cambio)
   public:
    /// Lo que Bancas necesita del resto de la app.
    struct Ganchos {
        std::function<bool(const std::string& camara)> camaraAlAire;
        std::function<std::string(const std::string& camara, const std::string& posicion)> irAPosicion; ///< "" o error
        std::function<std::string(const std::string& camara)> escenaDe; ///< la escena que muestra esa cámara ("" si ninguna)
        std::function<void(const std::string& escena)> armar;
        std::function<void(const std::string& escena)> cortar;
        std::function<void(const std::string& fuente, bool abierto)> microfono;
        /// El micrófono de la BANCA en la central de conferencia (Bosch DCN), si
        /// está conectada y la banca tiene unidad. Devuelve si lo tomó. `terminar`:
        /// además la saca de la lista de oradores de la central.
        std::function<bool(const std::string& banca, bool abierto, bool terminar)> microfonoBanca;
        /// El integrante habla desde el navegador (conectado y habilitado en la sesión remota).
        std::function<bool(const std::string& id)> esRemoto;
        /// Darle o quitarle la palabra por la sesión remota (abre su audio, le avisa al celular).
        std::function<void(const std::string& id, bool palabra, int segundos)> palabraRemota;
        /// La fila cambió (para avisarle a cada remoto su turno).
        std::function<void(const std::vector<std::string>& cola, const std::string& alAire)> filaCambio;
        /// La fuente «Banca con la palabra» ya encuadra esta toma recortando la toma
        /// entera (posición digital, hay escenas con la fuente): no hay que mover nada.
        std::function<bool(const std::string& camara, const std::string& posicion)> laEncuadraLaFuente;
        /// La escena que muestra la ranura `n` de la fuente de banca ("" si ninguna).
        std::function<std::string(int ranura)> escenaDeRanura;
        /// Las posiciones con ese nombre en todas las cámaras: [{camara, posicion}].
        std::function<std::vector<std::pair<std::string, std::string>>(const std::string& nombre)> posicionesConNombre;
    };
    Bancas(Cuerpo& cuerpo, Ganchos g, QObject* parent = nullptr);

    /// {cola: [integrantes], al_aire: integrante|null, desde_ms, usado_s, asignado_s, bancas: [todas]}.
    QVariantMap estado() const;
    nlohmann::json estadoJson() const;

    /// Suma a la fila (por banca, id o nombre). "" o el error.
    Q_INVOKABLE QString agregar(const QString& clave);
    Q_INVOKABLE void quitar(const QString& clave);
    /// La pone al aire: `modo` "armar" (de fábrica) o "directo". Devuelve "" o el
    /// error; los avisos (cámara al aire, sin preset) van en `aviso`.
    Q_INVOKABLE QString alAire(const QString& clave, const QString& modo = "armar", int segundos = -1);
    /// Saca a la del aire (cierra su micrófono y para el reloj).
    Q_INVOKABLE void terminar();
    /// Solo la cámara: lleva la PTZ al preset de la banca (para tenerla lista),
    /// sin armar ni abrir micrófonos. No mueve la cámara que está al aire.
    /// "" o el error.
    Q_INVOKABLE QString apuntar(const QString& clave);
    /// Para el debate: deja lista la toma de la banca. Si la fuente de banca la
    /// encuadra sola (digital), no mueve nada; si es motorizada, la mueve (si no
    /// está al aire). "" o el error.
    Q_INVOKABLE QString prepararToma(const QString& clave);
    /// ENFOCAR: lleva la cámara a lo que se pida, de palabra: «banca 10», «10»,
    /// «Pepito López» (o solo «López», sin tildes) o el nombre de una posición de
    /// cualquier cámara («Presidencia»). Solo apunta (no da la palabra). Con la
    /// cámara al aire no se mueve salvo `forzar`. "" o el error.
    Q_INVOKABLE QString enfocar(const QString& que, bool forzar = false);
    /// Si el último `enfocar` no movió la cámara porque estaba al aire (para no
    /// reconocerlo por el texto del error, que va traducido).
    bool enfocarFrenoPorAire() const { return frenoPorAire_; }
    /// Lo que entendería `enfocar`, sin mover nada: {tipo: banca|persona|posicion,
    /// banca?, integrante?: {id, nombre}, camara, posicion} o {error, candidatos?}.
    nlohmann::json resolver(const std::string& que) const;

    /// La primera de la fila al aire.
    Q_INVOKABLE QString siguiente(const QString& modo = "armar");
    /// El tiempo de cada uno al darle la palabra (de fábrica, 3 minutos). Se guarda.
    Q_INVOKABLE void setAsignado(int segundos);
    /// Le suma (o resta) tiempo al que habla.
    Q_INVOKABLE void sumarTiempo(int segundos);
    int asignado() const { return asignadoTurnoS_; }
    long long desdeMs() const { return desdeMs_; }
    std::string alAireId() const { return alAire_; }
    std::vector<std::string> cola() const { return cola_; }
    Q_INVOKABLE QString ultimoAviso() const { return QString::fromStdString(aviso_); }

    /// El orador presencial al aire para los zócalos ({nombre, cargo, bloque, validado}); {} si no hay.
    nlohmann::json oradorParaZocalo() const;

   signals:
    void cambio();

   private:
    bool frenoPorAire_ = false; ///< enfocarFrenoPorAire
    Cuerpo& cuerpo_;
    Ganchos g_;
    std::vector<std::string> cola_; ///< ids de integrantes
    std::string alAire_;           ///< id del que habla
    long long desdeMs_ = 0;
    int asignadoS_ = 180;     ///< el de fábrica de cada turno
    int asignadoTurnoS_ = 180; ///< el de este turno (con lo que se le sumó)
    void avisarFila();
    std::string aviso_;
};

} // namespace ba::app::asamblea
