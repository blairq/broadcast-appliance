// ============================================================================
// src/app/evento/Condicion.hpp — las condiciones de la línea, evaluadas (L7)
//
// El plan: docs/planes/PLAN-LINEA-DE-TIEMPO.md (3.2 Condicion y 3.6). Una
// `Condicion` es un texto chico sobre lo que ya existe; acá se entiende y se
// evalúa. No es un lenguaje general: alcanzan estas formas y `y`/`o`/`no`
// (también `&&`, `||`, `!`) con paréntesis.
//
//   bus:<tema>                      el último valor del tema es «verdadero»
//                                   (true, un número distinto de 0, un texto o un
//                                   objeto no vacío)
//   bus:<tema>.campo.campo OP valor OP: == != >= <= > <; valor: 'texto', 12, true
//   medio:<pieza>.termino           el medio de esa pieza llegó al final
//   hora>=19:30 (y los otros OP)    la hora del día, en el huso del evento
//   pieza:<id>.fin [+ 2s | + 500ms] la pieza se terminó (hace eso)
//   tecla:F5  botonera:<id>  web:<botón>
//                                   un PULSO: vale solo en el instante en que pasa
//
// Ejemplos: "bus:votacion/abierta", "no bus:votacion/abierta",
// "bus:oradores/al_aire.banca == 'banca-12'", "bus:sesion/punto.estado == 'votado'
// o tecla:F5", "pieza:z3.fin + 2s".
//
// QUIÉN SABE QUÉ. El `Mundo` es lo que se sabe afuera (el bus, los pulsos, los
// medios terminados, las piezas que se sacaron, la hora); lo arma quien corre la
// línea (la Corrida). El `Seguimiento` recuerda cada condición en el tiempo: si se
// cumple y DESDE CUÁNDO, que es lo que el resolvedor necesita (Entrada::condiciones)
// para poner la hora a un paso «cuando» o el fin a uno «mientras».
//
// Puro, sin Qt y sin reloj (el `ahora` está en el Mundo): se prueba en test_linea.
// ============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "evento/Linea.hpp"

namespace ba::evento::linea {

/// Lo que se sabe afuera, para evaluar.
struct Mundo {
    int64_t ahoraMs = 0;
    /// Para «hora>=HH:MM», con la misma regla que el resolvedor (una hora anterior al
    /// arranque previsto es del día siguiente).
    int64_t medianocheMs = 0;
    int64_t inicioPlaneadoMs = 0;
    std::map<std::string, nlohmann::json> bus; ///< tema → último valor
    std::set<std::string> pulsos;             ///< "tecla:F5", "botonera:x", "web:y" que acaban de pasar
    std::map<std::string, int64_t> mediosTerminados; ///< pieza → cuándo terminó su medio
    std::map<std::string, int64_t> finesDePieza;     ///< pieza → cuándo se sacó
};

struct Evaluacion {
    bool cumple = false;
    /// Desde cuándo vale lo que vale, si se sabe exacto (la hora, el fin de una pieza,
    /// el medio que terminó); -1 si no (el bus: lo anota el Seguimiento al verlo cambiar).
    int64_t desdeMs = -1;
    /// El próximo instante en que puede cambiar solo, por la hora; kNunca si no.
    int64_t cambiaMs = kNunca;
};

/// Evalúa. Lo que no se entiende no se cumple (lo marca errorDeCondicion).
Evaluacion evaluar(const Condicion& c, const Mundo& m);
/// "" si se entiende; si no, por qué no (para la revisión previa).
std::string errorDeCondicion(const Condicion& c);
/// Los temas del bus que mira (para suscribirse solo a esos).
std::vector<std::string> temasDe(const Condicion& c);

/// Todas las condiciones que usa la línea: los pasos «cuando» y «mientras» (también
/// el inicio de los bloques), las piezas «cuando», los disparadores y el
/// "medio:<pieza>.termino" de lo que termina con su medio (el resolvedor lo busca así).
std::set<Condicion> condicionesDe(const Linea& l);
/// Los pulsos que espera la línea ("tecla:F5", "botonera:<id>", "web:<botón>"), en
/// orden de aparición: las teclas que hay que atender y los botones que la web muestra.
std::vector<std::string> pulsosDe(const Linea& l);
/// Con qué se compara un pulso: una tecla, sin mayúsculas y con los modificadores como
/// conjunto («tecla:Ctrl+Shift+F5» = «tecla:shift+ctrl+f5»: el atajo publica la forma de
/// Qt y la condición se escribe como venga); los demás, tal cual.
std::string clavePulso(const std::string& pulso);

/// Cada condición en el tiempo: si se cumple y desde cuándo.
class Seguimiento {
   public:
    /// Vuelve a evaluar `conds` en el mundo. true si alguna cambió.
    bool actualizar(const std::set<Condicion>& conds, const Mundo& m);
    const std::map<Condicion, EstadoCondicion>& estados() const { return estados_; }
    bool cumple(const Condicion& c) const;
    /// El próximo instante en que alguna puede cambiar sola (kNunca si ninguna).
    int64_t proximoCambioMs() const { return proximoMs_; }
    void olvidar() {
        estados_.clear();
        proximoMs_ = kNunca;
    }

   private:
    std::map<Condicion, EstadoCondicion> estados_;
    int64_t proximoMs_ = kNunca;
};

} // namespace ba::evento::linea
