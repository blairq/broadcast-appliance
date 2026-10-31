// ============================================================================
// src/app/ui/EventoAbierto.hpp — el evento abierto: lo que comparten las vistas del evento
//
// El guion (`evento::Evento`), su línea de tiempo (lo que manda) y su lectura como guion
// de cues (`Proyeccion`), en qué carpeta está y si tiene cambios sin guardar. Lo leen
// todas las piezas de la página Evento (guion, corrida, padrón, web, informe: ver
// EventoViewModel.hpp) y lo cambian dos: el guion (editar) y la corrida (la línea en
// vivo, linea_editar). Una sola copia: nadie guarda la suya.
//
// AVISA lo que cambia, con señales separadas (el que dibuja la lista de cues no tiene
// por qué enterarse de cada segundo de la corrida):
//   - abiertoCambio: se abrió otro evento (o se cerró).
//   - guionCambio:   cambió el guion (lo editado, la línea, el nombre…).
//   - sucioCambio:   cambió `sucio`.
//   - lineaCambio:   la línea es otra; `desdeLinea`: cambió por su cuenta (linea_editar,
//                    deshacer, el orden del día), no por el guion de cues.
// ============================================================================
#pragma once

#include <filesystem>
#include <string>

#include <QObject>
#include <QString>

#include "evento/Evento.hpp"
#include "evento/EventoStore.hpp"
#include "evento/LineaEdicion.hpp"

namespace ba::app {

class EventoAbierto : public QObject {
    Q_OBJECT
   public:
    explicit EventoAbierto(std::filesystem::path raizEventos, QObject* parent = nullptr);

    const evento::EventoStore& store() const { return store_; }
    /// La carpeta del abierto (relativa a Eventos/), vacía si no hay ninguno.
    const QString& carpeta() const { return abierto_; }
    bool hay() const { return !abierto_.isEmpty(); }
    /// La carpeta en el disco (vacía si no hay ninguno).
    std::filesystem::path dir() const;
    /// Una subcarpeta del abierto («Discursos», «Medios»…), vacía si no hay ninguno.
    std::filesystem::path sub(const char* nombre) const;
    /// `rel` dentro de `sub` de la carpeta del evento, o vacía si se sale de ahí (un «..»
    /// en un guion que vino de otra máquina no le saca archivos al que lo abre).
    QString dentro(const QString& sub, const QString& rel) const;

    const evento::Evento& evento() const { return e_; }
    const evento::linea::Linea& linea() const { return linea_; }
    const evento::linea::Proyeccion& proyeccion() const { return proyeccion_; }
    bool sucio() const { return sucio_; }
    /// El arranque como instante absoluto, ISO-8601 con desfasaje ("2026-09-24T19:00:00-03:00").
    /// Vacío si falta la fecha o la hora. Es lo único que le sirve al que está en otro huso.
    QString cuando() const;
    /// El nombre para mostrar (el del guion o, sin, la carpeta).
    QString nombre() const { return e_.nombre.empty() ? abierto_ : QString::fromStdString(e_.nombre); }

    /// Lee el guion de `carpeta` del disco (no lo abre). `ok` dice si se pudo.
    evento::Evento leer(const std::string& carpeta, bool* ok, std::string* err) const { return store_.cargar(carpeta, ok, err); }
    /// Lo deja abierto: la línea del archivo o, en un evento de antes (v1), la migración
    /// del guion de cues (con una copia del v1 en Respaldos/ y el archivo pasado a v2).
    /// Devuelve el aviso de la migración para la bitácora (vacío si no hubo).
    QString poner(const QString& carpeta, evento::Evento e);
    void cerrar();
    /// Escribe el guion (la línea manda; `bloques` va como su lectura). "" o el error.
    std::string guardar();
    /// La carpeta del abierto se movió (mover a otra carpeta del histórico).
    void renombrar(const QString& carpeta);

    // --- Cambiar --------------------------------------------------------------------
    /// El guion de cues para editar. Después de tocarlo: `editado()`.
    evento::Evento& editar() { return e_; }
    /// Lo editado en el guion de cues pasa a la línea (lo que no se tocó queda exacto) y
    /// queda sin guardar.
    void editado();
    /// La línea cambió por su cuenta: el guion de cues se vuelve a leer y queda sin guardar.
    void ponerLinea(evento::linea::Linea l);
    /// Sin guardar (sin tocar la línea): p. ej. el idioma.
    void marcarSucio();

    evento::Cue* cue(int bloque, int cue);
    const evento::Cue* cue(int bloque, int cue) const;

   signals:
    void abiertoCambio();
    void guionCambio();
    void sucioCambio();
    void lineaCambio(bool desdeLinea);

   private:
    void ponerSucio(bool si);

    evento::EventoStore store_;
    QString abierto_;
    evento::Evento e_;
    /// La línea del evento abierto (lo que manda) y su lectura como guion de cues
    /// (`e_.bloques` es `proyeccion_.bloques`).
    evento::linea::Linea linea_;
    evento::linea::Proyeccion proyeccion_;
    bool sucio_ = false;
};

} // namespace ba::app
