// src/app/agent/Subtitulos.hpp — los cuadros «Subtítulos» (ba_subtitulos) de las escenas
//
// Un cuadro de la escena que muestra, como subtítulos de televisión, lo que
// llega por una SALIDA DE TEXTO de la app (la transcripción de una «Voz a
// texto» o una traducción: agent/FlujosTexto, agent/Traductor). Cada frase:
//   - se parte en líneas de `caracteres` como máximo, sin cortar palabras, y
//     en bloques de `lineas` líneas (lo que entra en pantalla a la vez);
//   - cada bloque se muestra el tiempo que lleva leerlo (`cps` caracteres por
//     segundo), nunca menos de `minimo` segundos; si se acumulan bloques
//     (habla rápido), los que esperan se aceleran para no quedar atrás;
//   - el cuadro se borra `borrar` segundos después del último.
// Cada línea va centrada, con su propio fondo (como en la tele), dentro de un
// cuadro de tamaño FIJO (`ancho` x lo que ocupan `lineas` líneas): así se ubica
// en la escena una vez y no salta cuando cambia el texto. Las líneas se apoyan
// abajo. Lo dibuja Qt y va al motor como entrada «app» (FuentesApp).
//
// El reloj de los bloques y el dibujo corren en su hilo («subtitulos», ver
// emision/PintorAire.hpp): con la interfaz trabada, lo que ya llegó se sigue
// mostrando a su ritmo y se borra a tiempo. Las frases entran bajo `mu_`.
#pragma once

#include <QObject>
#include <QString>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ba::app::fuentes {
class PintorAire;
}

namespace ba::app::agent {

class FlujosTexto;

class Subtitulos : public QObject {
    Q_OBJECT
   public:
    explicit Subtitulos(FlujosTexto& flujos, QObject* parent = nullptr);
    ~Subtitulos() override;

    /// Cuántos cuadros «Subtítulos» muestran `flujo` (para avisar si ninguno).
    int cuadrosDe(const QString& flujo) const;
    /// Vacía los cuadros: el de nombre `nombre`, o los que muestran `flujo`, o
    /// todos (los dos vacíos). Cuántos vació.
    int limpiar(const std::string& nombre, const std::string& flujo);
    /// El que está vivo (para el MCP), o nullptr.
    static Subtitulos* activo();

   private:
    struct Estado {
        std::uint64_t version = 0;
        std::deque<std::vector<std::string>> cola;
        std::vector<std::string> actual;
        double restante = 0; ///< lo que le queda al bloque actual (s)
        double vacio = 0;    ///< desde que terminó el último (s)
        bool cambio = true;  ///< hay que volver a dibujar
    };
    void alFrase(const QString& flujo, const QString& texto);
    /// Una vuelta del reloj de los bloques (en el hilo de los subtítulos).
    void tic();

    FlujosTexto& flujos_;
    mutable std::mutex mu_;
    std::map<std::uint64_t, Estado> estados_; // por id de FuentesApp (bajo mu_)
    std::unique_ptr<fuentes::PintorAire> pintor_; ///< último: se apaga primero
};

} // namespace ba::app::agent
