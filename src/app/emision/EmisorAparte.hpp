// ============================================================================
// src/app/emision/EmisorAparte.hpp — las salidas que codifica un emisor aparte (F3-01)
//
// Pieza de BroadcastEngine (ver EL EMISOR APARTE en BroadcastEngine.hpp y
// docs/arquitectura/04-plan-unificado.md). Para una salida con emisor, lo que el hilo de
// salidas armaría acá (las mismas configuraciones de motor::Transmision, motor::Grabacion y
// motor::Repeticion) se escribe en su pedido (Runtime/emisor/<id>.json); el `ba-emisor` lo
// aplica, y lo que hace se lee de su estado para la foto de las salidas.
//
// Su video le llega por canales propios (CanalEmisor): el programa de la salida en NV12 y
// del tamaño de la salida, y la señal limpia mientras se la pide. Las pistas 2-6 de su
// grabación, por un anillo cada una (Motor::pistasAlEmisor).
//
// Hilos: lo del pedido y el estado (SalidaEmision) es del HILO DE SALIDAS, como lo que
// reemplaza; lo que adopta de un pedido anterior se lo cuenta al dueño por el buzón. El canal
// del programa lo publica el dueño (al arrancar y al crear o borrar una salida); el de la
// señal limpia, el hilo de salidas. Los cuadros los manda el hilo del motor (candado del canal).
// ============================================================================
#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "emision/BroadcastEngine.hpp"

namespace ba::app {

struct CanalEmisor;
struct SalidaEmision;

class EmisorAparte {
   public:
    explicit EmisorAparte(BroadcastEngine& m) : m_(m) {}

    /// Las salidas que codifica un emisor aparte (BA_EMISOR, lo pone el launcher).
    static const std::set<std::string>& salidasConEmisor();

    /// Publica el canal de video para el emisor (el programa o la señal limpia) en NV12 del
    /// tamaño de la salida y pide su vista. false + lastError.
    bool publicarCanal(std::shared_ptr<CanalEmisor>& c, const std::string& id, bool porOmision, bool limpio);
    /// Suelta la vista del canal (el motor se apaga: el emisor pasa a la placa); con
    /// `cerrar`, también el canal.
    void soltarCanal(std::shared_ptr<CanalEmisor>& c, bool cerrar);
    /// Lo de cada salida con emisor, al apagar el motor (sus vistas sueltas, sin enganchar).
    /// En el dueño, con el hilo de salidas parado.
    void desenganchar();

    // --- Del hilo de salidas --------------------------------------------------------------
    /// Relee su estado (cada medio segundo) y carga el pedido si todavía no está.
    void leer(SalidaEmision& s);
    /// Lo que hace el emisor ahora con la transmisión ("transmision") o la grabación
    /// ("grabacion"): si su estado dice que la tiene andando, cayéndose o reconectando.
    bool laTiene(const SalidaEmision& s, const char* que) const;
    /// El pedido de la salida (lo carga del disco la primera vez), cambiado por `cambio`,
    /// con el cuadro de salida y la placa de ahora y el `seq` siguiente, escrito. "" o el error.
    std::string pedir(SalidaEmision& s, const std::function<void(emisor::Pedido&)>& cambio);
    /// El destino extra `id` como está en `desde` (nullptr: sacarlo) en el pedido. "" o el error.
    std::string extraAl(SalidaEmision& s, const std::string& id, const emisor::Pedido* desde);
    /// La repetición en el pedido (segundos 0: apagada) y guardar un clip (otro `guardarSeq`).
    std::string replayAl(SalidaEmision& s, int segundos, const std::string& dir, int kbps);
    std::string guardarReplayEn(SalidaEmision& s, const std::string& destino);
    /// La señal limpia en el pedido (nullptr: apagarla), con su canal publicado.
    std::string limpioAl(SalidaEmision& s, const emisor::PedidoLimpio* l);
    /// Pone en la foto de la salida lo que hace el emisor: `principal` y `grabacion`.
    void fotoDel(const SalidaEmision& s, nlohmann::json& f) const;
    /// Algo de esa salida transmite o graba en el emisor.
    bool aireEn(const SalidaEmision& s) const;

   private:
    BroadcastEngine& m_;
};

} // namespace ba::app
