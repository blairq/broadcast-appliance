// SPDX-License-Identifier: GPL-2.0-or-later
//
// AvisoFueraDeUi: las alarmas que tienen que salir AUNQUE la interfaz no ande
// (F1-13, docs/arquitectura/04-plan-unificado.md). La primera: «interfaz trabada
// con aire», que detecta LatidoAire en su hilo.
//
// Los avisos de siempre (disparadores → Notificador, bus → control web) viven en
// el hilo de la interfaz: con la interfaz trabada no sale nada, que es justo
// cuando más falta hace. Este lleva su propio hilo con su bucle de eventos de Qt
// y desde ahí:
//
//   - manda el aviso por los notificadores (Telegram, mail, webhook) con un
//     Notificador propio, leído de Config/notificadores.json en el momento (el de
//     la interfaz no se toca: es de otro hilo);
//   - se lo pasa al gateway (B), que lo empuja a TODOS los aparatos del control
//     web conectados (POST /api/v1/control/enviar {todas, msg: {type: "alarma"}}).
//
// avisar() se puede llamar desde cualquier hilo y vuelve en el acto (encola). Lo usan
// también los disparadores (AlertEngine, que corre en el plano de control) para sus
// notificadores: así una alarma sale aunque la interfaz no ande (F3-03).
#pragma once

#include "emision/LatidoAire.hpp"

#include <nlohmann/json.hpp>

#include <QObject>
#include <QThread>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class QNetworkAccessManager;

namespace ba::app {

class AvisoFueraDeUi {
   public:
    struct Alarma {
        std::string id;     ///< «ui_trabada»: la misma alarma al empezar y al terminar
        bool activa = true; ///< false = terminó (se avisa también, para que no quede colgada)
        std::string titulo;
        std::string texto;
        /// A qué notificadores: todos o los de `notificar`.
        bool notificarTodos = true;
        std::vector<std::string> notificar;
        /// También a los aparatos del control web (por el gateway). Los disparadores no: a
        /// esos les llega por el bus de eventos.
        bool controlWeb = true;
        /// Cómo le fue a cada notificador (en el hilo de los avisos: encolar y volver).
        std::function<void(const std::string& id, bool ok, const std::string& detalle)> resultado;
    };

    /// `datos`: donde está Config/notificadores.json.
    explicit AvisoFueraDeUi(std::filesystem::path datos);
    ~AvisoFueraDeUi();
    AvisoFueraDeUi(const AvisoFueraDeUi&) = delete;
    AvisoFueraDeUi& operator=(const AvisoFueraDeUi&) = delete;

    /// Desde cualquier hilo; no espera.
    void avisar(Alarma a);
    /// Lleva la alarma «interfaz trabada con aire» de `latido` (hasta que este se
    /// destruya: ahí se desengancha, esperando al aviso en curso).
    void escuchar(LatidoAire& latido);
    /// La alarma «interfaz trabada con aire» (al empezar o, con !trabada, al terminar).
    static Alarma uiTrabada(bool trabada, std::uint64_t ms, bool motorVivo, const LatidoAire::Politica& p);

    /// El mensaje que reciben los aparatos del control web.
    static nlohmann::json mensajeControlWeb(const Alarma& a);

   private:
    void enHilo(const Alarma& a); ///< en hilo_
    std::filesystem::path datos_;
    LatidoAire* latido_ = nullptr;
    QThread hilo_;
    QObject* contexto_ = nullptr;      ///< vive en hilo_: lo encolado corre ahí
    QNetworkAccessManager* red_ = nullptr; ///< de hilo_ (se crea ahí la primera vez)
};

} // namespace ba::app
