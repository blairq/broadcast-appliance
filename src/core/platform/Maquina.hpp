// ============================================================================
// src/core/platform/Maquina.hpp — qué máquina es esta (sin datos personales)
//
// El appliance es portable: el mismo disco va de una PC a otra y la carpeta de
// datos viaja con él. Lo que es de la MÁQUINA y no del disco (qué drivers de
// audio y video anduvieron, cuáles se colgaron) se guarda bajo una huella de la
// máquina, para no aplicarle a la notebook del estudio lo que se aprendió en la
// PC de la sala.
//
// La huella es un hash del id que el sistema le da a la máquina (IOPlatformUUID
// en macOS, MachineGuid en Windows, /etc/machine-id en Linux) y del modelo: no
// se puede volver atrás al id, no tiene el nombre de la máquina ni del usuario,
// y no cambia al reiniciar, al mover el disco ni al cambiar de letra.
//
// BA_HUELLA_MAQUINA la fija (los tests simulan «otra máquina» con eso).
// ============================================================================
#pragma once

#include <string>

namespace ba::platform {

/// 16 caracteres hexadecimales, estables para esta máquina.
std::string huellaDeMaquina();

/// El modelo (p. ej. «MacBookPro18,3», «HP ProBook 450 G8»), para mostrar al lado
/// de la huella. Vacío si el sistema no lo dice.
std::string modeloDeMaquina();

/// La huella de un id crudo (FNV-1a de 64 bits, con sal): estable entre
/// compiladores y sistemas (std::hash no lo es). Pública para los tests.
std::string huellaDe(const std::string& crudo);

/// El id que el sistema le da a la máquina, crudo (por sistema: Maquina_*.cpp).
/// Nunca sale de este proceso: solo se usa para calcular la huella.
std::string idCrudoDeMaquina();

} // namespace ba::platform
