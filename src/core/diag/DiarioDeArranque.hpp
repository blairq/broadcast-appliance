// ============================================================================
// src/core/diag/DiarioDeArranque.hpp — quién se llevó puesto el arranque
//
// EL PROBLEMA. El programa levanta cosas del disco al arrancar: la disposición
// del dashboard, los vínculos de rótulos, el último evento, y mañana las
// extensiones. Cualquiera de esas puede estar rota —la escribió un asistente,
// o quedó a medias por un corte— y llevarse puesto el arranque. Y si el
// arranque muere ahí, el programa no llega a contar qué pasó: la próxima vez
// vuelve a intentar lo mismo y vuelve a morir. Eso es un equipo que no
// enciende, media hora antes de salir al aire.
//
// LA IDEA, que es la de los navegadores: antes de cargar cada cosa se anota en
// disco QUÉ se está por cargar, y recién cuando la interfaz dibujó se marca el
// arranque como terminado. Si al arrancar el diario quedó con algo a medias,
// ESO fue lo que rompió: se saltea, se dice por qué, y el programa levanta.
//
// Se mide QUE EL ARRANQUE HAYA TERMINADO, no que el proceso siga vivo: por eso
// agarra igual una caída que un cuelgue, que es el caso que ningún try/catch
// alcanza.
//
// No reemplaza al circuit breaker del launcher (cinco caídas en dos minutos →
// modo seguro): lo complementa. Aquél apaga todo; éste apaga al culpable y
// deja andar el resto.
// ============================================================================
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ba::diag {

class DiarioDeArranque {
  public:
    /// Cuántos arranques seguidos puede romper algo antes de quedar apagado
    /// para siempre (hasta que alguien lo prenda a mano). Dos: el primero
    /// puede ser mala suerte, el segundo ya es un patrón.
    static constexpr int kRoturasParaApagar = 2;

    /// Lee el diario anterior y abre uno nuevo. Si el anterior quedó a medias,
    /// `culpable()` lo dice desde acá.
    explicit DiarioDeArranque(std::filesystem::path archivo);
    ~DiarioDeArranque();

    /// Qué quedó a medias en el arranque anterior. Vacío = el anterior terminó
    /// bien (o es el primero).
    const std::string& culpable() const;

    /// true si eso NO hay que cargar: rompió demasiadas veces, o lo apagaron.
    bool apagado(const std::string& id) const;
    /// Cuántos arranques rompió.
    int roturas(const std::string& id) const;

    /// Se empieza a cargar algo. Se escribe y se sincroniza ANTES de cargar:
    /// si lo que sigue cuelga la máquina, el dato ya está en el disco.
    void empieza(const std::string& id);
    /// Eso cargó bien.
    void termina(const std::string& id);

    /// El arranque llegó hasta el final (la interfaz dibujó su primer cuadro).
    /// A partir de acá, lo que pase ya no es culpa del arranque.
    void arranqueOk();

    /// Modo seguro: NO se carga nada del disco, en esta corrida nada mas.
    ///
    /// No se anota en el diario a proposito. Apagar algo deja un antecedente
    /// que hay que prender a mano despues; el modo seguro es una medida de
    /// esta vez, y si dejara todo apagado para siempre, salir del modo seguro
    /// no alcanzaria para volver a la normalidad.
    void setModoSeguro(bool si);
    bool modoSeguro() const;

    /// Lo prende o lo apaga a mano (desde la interfaz o el asistente).
    void setApagado(const std::string& id, bool apagado);
    /// Todo lo que está apagado.
    std::vector<std::string> apagados() const;
    /// Nº de arranque, para poder cruzarlo con el registro.
    int arranque() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

/// El diario del proceso. Hay que abrirlo una vez, temprano.
DiarioDeArranque& diario();
/// Lo abre (o lo reabre) en esa ruta. Idempotente.
void abrirDiario(const std::filesystem::path& archivo);

/// Envoltorio para no olvidarse el `termina`: se anota al entrar y se cierra al
/// salir del alcance, pase lo que pase.
///
///     { ba::diag::Cargando c("vinculos"); leerVinculos(); }
class Cargando {
  public:
    explicit Cargando(std::string id);
    ~Cargando();
    /// true si NO hay que cargarlo (está apagado por haber roto el arranque).
    bool saltear() const { return saltear_; }

  private:
    std::string id_;
    bool saltear_ = false;
};

} // namespace ba::diag
