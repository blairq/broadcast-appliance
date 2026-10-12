// src/app/audio/EquipoAudio.hpp — las entradas y salidas físicas del equipo, para la Matriz
//
// Todo lo que está enchufado aparece en la Matriz, y se sigue lo que se
// enchufa y desenchufa (QMediaDevices):
//
//   * SALIDAS: cada dispositivo (las bocinas, un monitor con audio, una placa
//     USB de 4 salidas) da una columna por par de canales (1-2, 3-4; si el
//     último canal queda solo, mono). El dispositivo se abre UNA vez con
//     todos sus canales y cada par escribe en los suyos (AlDispositivoMulti);
//     lo alimenta el mismo hilo de mezcla que a los buses virtuales. Se abre
//     solo mientras algo le manda audio.
//   * ENTRADAS: cada canal de cada dispositivo es una fuente de audio
//     («Behringer UMC · Entrada 3», tipo lógico ba_entrada_fisica: una entrada
//     del motor que lee lo capturado por la cola de `bavoz`): está en la
//     Matriz como cualquier fila y en Audio con su rack. De fábrica sin
//     pistas (no sale al aire sola). La captura del dispositivo se abre solo
//     si alguno de sus canales se usa (una pista, la escucha, un envío): sin
//     eso no se prende la luz del micrófono ni se gasta nada. No se repite un
//     dispositivo que ya es una entrada de la app (el micrófono de la Mac que
//     ya está en Audio).
//
// Se puede ocultar todo el equipo o un dispositivo (y lo oculto no se abre).
// Se guarda con la Matriz (Config/audio-matriz.json, «equipo»).

#pragma once

#include <functional>
#include <QMediaDevices>
#include <QObject>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>


namespace ba::app::audio {

namespace anillo {
struct Ring;
}

/// Los dispositivos de entrada que ya son (o van a ser) una entrada de audio
/// de la app aunque su fuente todavía no exista: con el audio del sistema
/// trabado, la entrada del micrófono tarda en abrirse, y mientras tanto la
/// placa tomaba el MISMO nombre («Micrófono de MacBook Pro») y el motor
/// renombraba la de la app a «… 2». Lo da el motor (sus entradas pendientes).
void setDispositivosReservados(std::function<std::set<std::string>()> f);

class EquipoAudio : public QObject {
    Q_OBJECT
   public:
    explicit EquipoAudio(QObject* parent = nullptr);
    ~EquipoAudio() override;

    /// Una columna de la Matriz: un par de canales de una salida.
    struct Columna {
        std::string id;          ///< "fis:<dispositivo>:<par>" (estable entre enchufadas)
        std::string nombre;      ///< «Bocinas de MacBook Pro · 1-2»
        std::string dispositivo; ///< nombre del dispositivo
    };
    std::vector<Columna> columnas() const;
    /// El anillo de una columna (nulo si no existe).
    std::shared_ptr<anillo::Ring> anilloDe(const std::string& columna) const;
    /// Las columnas que reciben algo: abre los dispositivos que hacen falta y
    /// cierra los que no.
    void usarColumnas(const std::set<std::string>& usadas);
    /// Las fuentes de entrada que se usan: abre o cierra las capturas.
    void usarEntradas(const std::set<std::string>& usadas);
    /// Las fuentes que crea (las filas de entradas físicas).
    bool esEntradaFisica(const std::string& fuente) const;

    bool mostrar() const { return mostrar_; }
    void setMostrar(bool on);
    void setVisible(const std::string& dispositivoId, bool visible);
    /// [{id, nombre, tipo: salida|entrada, canales, visible, abierto}]
    QVariantList dispositivos() const;
    /// Para diagnosticar (audio_matriz): cada dispositivo, si se pidió, si se abrió
    /// (y si no, por qué) y cuánto audio pasó.
    nlohmann::json estadoDispositivos() const;

    nlohmann::json guardado() const;
    /// CoreAudio contestó (en otro hilo) cuántos canales tiene un dispositivo.
    void canalesSabidos();
    void cargar(const nlohmann::json& j);

   signals:
    /// Cambiaron los dispositivos (se enchufó o desenchufó algo, o se ocultó).
    void cambio();

   private:
    struct Salida;
    struct Entrada;
    void refrescar();
    bool visible(const std::string& id) const;

    QMediaDevices medios_;
    bool mostrar_ = true;
    std::map<std::string, bool> visibles_; ///< id -> visible (lo que no está: visible)
    std::map<std::string, std::unique_ptr<Salida>> salidas_;
    std::map<std::string, std::unique_ptr<Entrada>> entradas_;
};

} // namespace ba::app::audio
