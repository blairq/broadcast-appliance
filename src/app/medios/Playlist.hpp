// ============================================================================
// src/app/medios/Playlist.hpp — la playlist de medios (T46)
//
// Una lista de archivos que se pasan uno detrás del otro en la escena
// «Playlist» (una sola fuente de medio, «Playlist (medio)»): cada ítem con su
// «in» y su «out» (segundos; 0 = desde el principio / hasta el final), pasar
// solo al siguiente (auto-next), volver a empezar al terminar (loop) y armarla
// en PVW para el take. Se guarda en Config/playlist.json.
//
// Mira el medio cada 250 ms: cuando llega al «out» o termina, pasa al siguiente
// (si auto-next). No crea una fuente por ítem: el cambio es el archivo de la
// misma fuente, así la escena no cambia de forma entre un clip y otro.
//
// libobs solo hace correr un medio que se está mostrando: si la escena no está
// al aire, «play» la arma en PVW (el programa no cambia) y el operador la pasa
// con el take.
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace ba::app {
class AppRuntime;
}

namespace ba::app::medios {

class Playlist : public QObject {
    Q_OBJECT
    /// [{archivo, nombre, in_s, out_s}]
    Q_PROPERTY(QVariantList items READ itemsUi NOTIFY cambio)
    /// {indice, reproduciendo, auto_next, loop, restante_s, duracion_s}
    Q_PROPERTY(QVariantMap estado READ estadoUi NOTIFY estadoCambio)

   public:
    static constexpr const char* kEscena = "Playlist";
    static constexpr const char* kFuente = "Playlist (medio)";

    explicit Playlist(AppRuntime& rt, QObject* parent = nullptr);

    /// Reemplaza la lista. Cada ítem: {archivo (ruta absoluta), nombre?, in_s?, out_s?}. "" o el error.
    std::string setItems(const nlohmann::json& items);
    nlohmann::json items() const { return items_; }
    nlohmann::json estado() const;
    /// play (desde `indice` o el actual), next, prev, stop. "" o el error.
    std::string control(const std::string& accion, int indice = -1);
    void setAutoNext(bool on);
    void setLoop(bool on);
    /// Crea la escena si falta y la deja armada en PVW (con `armar`, el VM).
    void setArmar(std::function<void(const QString&)> f) { armar_ = std::move(f); }

    QVariantList itemsUi() const;
    QVariantMap estadoUi() const;
    Q_INVOKABLE QString agregarUi(const QString& archivo);
    Q_INVOKABLE QString quitarUi(int indice);
    Q_INVOKABLE QString moverUi(int indice, int destino);
    Q_INVOKABLE QString controlUi(const QString& accion, int indice = -1);
    Q_INVOKABLE void autoNextUi(bool on) { setAutoNext(on); }
    Q_INVOKABLE void loopUi(bool on) { setLoop(on); }
    Q_INVOKABLE void armarUi();

   signals:
    void cambio();
    void estadoCambio();
    void notify(const QString& texto, const QString& tipo);

   private:
    std::string asegurarEscena();
    std::string cargarItem(int i);
    void vigilar();
    void guardar() const;

    AppRuntime& rt_;
    std::filesystem::path archivo_;
    nlohmann::json items_ = nlohmann::json::array();
    int indice_ = -1;
    bool reproduciendo_ = false, autoNext_ = true, loop_ = false;
    bool buscarIn_ = false; ///< falta llevarlo al «in» cuando arranque
    int intentosIn_ = 0;
    QTimer reloj_;
    std::function<void(const QString&)> armar_;
};

} // namespace ba::app::medios
