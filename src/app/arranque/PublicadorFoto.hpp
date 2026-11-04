// ============================================================================
// src/app/arranque/PublicadorFoto.hpp — la foto que lee la interfaz y los avisos del runtime
//
// Pieza de AppRuntime (ver AppRuntime.hpp; el patrón en docs/arquitectura/05-motor-del-plano.md).
// Arma la foto del motor (emision/FotoMotor.hpp) con lo que tienen las demás piezas y el
// motor, la compara por partes y se la publica a la ventanilla; suma lo que aportan los
// modelos del dueño (los conmutadores, las capas DSK) y avisa a los que observan los cambios
// del runtime (AppRuntime::Cambio).
//
// Hilos:
//   - EL DUEÑO (el plano de control) arma y publica (publicarFoto, con el turno): foto_,
//     trabajo_ y lo que la foto leyó de archivos (fotoListaSalidas_, fotoDestinos_) son
//     suyos. Los avisos a los observadores corren en él (avisar se lo encarga).
//   - DESDE CUALQUIER HILO: fotoSucia (un atómico y, una vez, la publicación encolada al
//     dueño), ultimaFoto (su candado), observar/dejarDeObservar y aportar/dejarDeAportar
//     (sus candados), anotarCambioAudio (lo llama el motor en el hilo que hizo el cambio).
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "arranque/RuntimeConfig.hpp"
#include "emision/FotoMotor.hpp"

namespace ba::app {

class PublicadorFoto {
   public:
    explicit PublicadorFoto(AppRuntime& rt) : rt_(rt) {}

    // --- La foto (ver AppRuntime: «La foto que lee la interfaz») -----------------------------
    void setFoto(std::function<void(std::shared_ptr<const FotoMotor>, unsigned partes)> publicar);
    int aportarALaFoto(std::function<void(FotoMotor&, unsigned partes)> aporte);
    void dejarDeAportar(int id);
    /// Esas partes cambiaron: se rearman en la próxima publicación, que se le encarga al dueño.
    void fotoSucia(unsigned partes) const;
    /// Solo las marca (sin encargar la publicación): la vuelta del motor publica enseguida.
    void marcar(unsigned partes) const { fotoSucia_.fetch_or(partes); }
    void publicarFoto();
    std::shared_ptr<const FotoMotor> ultimaFoto() const;

    // --- Los avisos ---------------------------------------------------------------------------
    int observar(std::function<void(AppRuntime::Cambio)> fn);
    void dejarDeObservar(int id);
    /// Ensucia las partes del cambio y avisa a los que observan, en el dueño.
    void avisar(AppRuntime::Cambio c) const;
    /// Qué partes de la foto toca cada aviso.
    static unsigned partesDe(AppRuntime::Cambio c);

    /// El motor avisa que cambió el audio de una fuente (parte Mezcla; FotoMezcla.cpp).
    void anotarCambioAudio(const std::string& fuente, const char* que);
    /// En cada vuelta del motor (en el dueño, con el turno): si las fuentes con audio ya no
    /// son los canales de la foto, la Mezcla se rearma (FotoMezcla.cpp).
    void seguirCanalesDelMotor();

   private:
    /// Además de las partes: lo que la foto lee de archivos se vuelve a leer.
    static constexpr unsigned kFotoRelerArchivos = FotoMotor::RelerArchivos;
    void avisarObservadores(AppRuntime::Cambio c) const;
    /// Rearma en `f` las partes `partes` (en el dueño, con el turno): ver FotoMotor.
    void armarFoto(FotoMotor& f, unsigned partes);
    /// La parte Mezcla (FotoMezcla.cpp).
    void armarMezcla(FotoMotor::MezclaAudio& m);

    AppRuntime& rt_;
    mutable std::mutex altasMu_;
    std::map<std::string, std::uint64_t> altasAudio_; ///< las altas de cada fuente (CanalAudio::altas)
    mutable std::mutex observadoresMu_; ///< se suman desde la interfaz; se avisa en el dueño
    std::vector<std::pair<int, std::function<void(AppRuntime::Cambio)>>> observadores_;
    int proximoObservador_ = 0;
    // La publicación, lo que suman otros, lo sucio y la última publicada.
    std::function<void(std::shared_ptr<const FotoMotor>, unsigned)> publicarFoto_;
    mutable std::mutex aportesMu_;
    std::vector<std::pair<int, std::function<void(FotoMotor&, unsigned)>>> aportes_;
    int proximoAporte_ = 0;
    mutable std::atomic<unsigned> fotoSucia_{FotoMotor::Todas | kFotoRelerArchivos};
    mutable std::atomic<bool> fotoPedida_{false};
    std::shared_ptr<const FotoMotor> foto_; ///< la última publicada (solo el dueño)
    /// Donde se rearma lo sucio (solo el dueño): se copia a una foto nueva solo si algo cambió.
    FotoMotor trabajo_;
    mutable std::mutex ultimaFotoMu_;
    std::shared_ptr<const FotoMotor> ultimaFoto_; ///< la misma, para otros hilos (ultimaFoto)
    /// Lo que la foto lee de archivos (Config/salidas.json, destinos.json): solo cuando
    /// cambian las salidas, no en cada vuelta.
    nlohmann::json fotoListaSalidas_, fotoDestinos_;
};

} // namespace ba::app
