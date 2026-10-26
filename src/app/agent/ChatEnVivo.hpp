// ============================================================================
// src/app/agent/ChatEnVivo.hpp — charlar con un modelo EN LA APP, en streaming y con herramientas
//
// Un cliente de /v1/chat/completions (la API de OpenAI) dentro del proceso, para lo que tiene
// que contestar en el acto: el tótem de registro (registro/AgenteTotem), donde una persona está
// esperando frente a una pantalla. Un agente de consola (claude, opencode, pi) tarda minutos en
// arrancar; esto es un pedido HTTP por turno, con la respuesta llegando de a pedazos
// (`stream: true`) para decir la primera frase mientras el modelo escribe la segunda.
//
// Es la misma idea que agent/Mirada (un pedido suelto a un servidor con la API de OpenAI: el
// llama-server del programa en 127.0.0.1, el modelo propio, un proveedor de la nube), pero de
// texto, en streaming y con herramientas. Los servidores se describen con la misma
// `FuenteMirada` (url con /v1, clave, modelo).
//
// VARIAS CONVERSACIONES A LA VEZ (hasta ocho tótems): contra un mismo servidor van a lo sumo
// `paralelo` pedidos juntos (el ajuste `registro.agenteParalelo`, 2 de fábrica: el llama-server
// local reparte la GPU entre los que atiende, y dos a medias son peor que uno entero); los demás
// esperan en una cola por servidor, en orden. Quien espera demasiado (la cola cuenta en su
// tiempo) cancela y sigue con el guion: ver AgenteTotem.
//
// El transporte es net/HttpsClient (mbedtls del bundle, nada del sistema) en un hilo por pedido;
// las respuestas se entregan en el hilo de la interfaz. Lo que llega se lee con
// registro::LectorSse.
// ============================================================================
#pragma once

#include <atomic>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "agent/Mirada.hpp"
#include "registro/AgenteRegistro.hpp"

class QObject;

namespace ba::app::agent {

class ChatEnVivo {
   public:
    /// El del proceso.
    static ChatEnVivo& el();

    /// Texto nuevo que escribió el modelo (en el hilo de la interfaz). La primera vez que llega
    /// algo se llama aunque sea "" (empezó a contestar, quizá solo con herramientas).
    using AlTexto = std::function<void(const std::string& nuevo)>;
    /// Terminó: la vuelta entera, o el motivo del error ("cancelado" no se entrega).
    using AlTerminar = std::function<void(ba::registro::TurnoModelo turno, std::string error)>;

    /// Un pedido en cola o en curso.
    class Pedido {
       public:
        /// No contestar más (ni texto ni fin) y soltar su lugar: lo que estaba en cola no
        /// sale, y lo que estaba leyendo se corta.
        void cancelar();
        bool cancelado() const { return cancel_->load(); }

       private:
        friend class ChatEnVivo;
        std::shared_ptr<std::atomic<bool>> cancel_ = std::make_shared<std::atomic<bool>>(false);
        std::function<void()> soltar_;
    };

    /// Manda `cuerpo` (con "stream": true) a `f`. Las respuestas no llegan si `contexto` ya no
    /// existe o si se canceló.
    std::shared_ptr<Pedido> pedir(const FuenteMirada& f, const nlohmann::json& cuerpo, QObject* contexto, AlTexto alTexto,
                                  AlTerminar alTerminar);

    /// Cuántos pedidos van juntos contra un mismo servidor (de fábrica, el ajuste
    /// registro.agenteParalelo).
    void setParalelo(int n);
    int paralelo() const;
    /// El servidor de un proveedor de la pestaña Agentes que habla chat-completions sin un
    /// agente de consola: "propio" (Config/modelo-propio.json y su clave), "openrouter" y
    /// "gemini-api" (con su clave). El modelo local no está acá: su puerto lo da
    /// ServidorModeloLocal. false y `falta` si no se puede (sin clave, o un proveedor de cuenta
    /// como Claude o OpenCode Go, que solo se usan por su agente).
    static bool fuenteDeProveedor(const std::string& proveedor, const std::string& modelo,
                                  const std::filesystem::path& dataRoot, FuenteMirada& out, std::string* falta = nullptr);

    /// Para el estado: cuántos hay andando y esperando contra `url`.
    int enCurso(const std::string& url) const;
    int enCola(const std::string& url) const;

   private:
    ChatEnVivo();
    struct Trabajo;
    struct Servidor {
        int andando = 0;
        std::deque<std::shared_ptr<Trabajo>> cola;
    };
    void arrancar(const std::shared_ptr<Trabajo>& t);
    /// Un lugar menos andando contra ese servidor: arranca el próximo de la cola.
    void liberar(const std::string& url);

    mutable std::mutex m_;
    std::map<std::string, Servidor> servidores_;
    int paralelo_ = 2;
};

} // namespace ba::app::agent
