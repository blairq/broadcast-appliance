// SPDX-License-Identifier: GPL-2.0-or-later
//
// Dsk: las cuatro capas globales encima del programa (BroadcastEngine::dsk*):
// un logo o mosca, el recuadro del intérprete de LSA, un zócalo fijo, un
// reloj. Cambiar de escena no las toca. Acá vive lo que el motor no sabe:
// el nombre de cada capa, qué tiene, si es FIJA (la de LSA: bajarla pide
// confirmación y no se baja sola), el fundido y si estaba arriba (se restaura
// al volver a abrir el programa). Config/dsk.json.
//
// Las capas se DEFINEN una vez y valen para todas las salidas (PLAN-SALIDAS);
// cada salida las sube y las baja por su cuenta (la línea de tiempo sube el logo
// en la principal y no en la sala). Lo de la por omisión va en `visible`; lo de
// las demás, en `visible_en: {<salida>: true}`, y vuelve igual tras un corte.
//
// DOS PIEZAS (F3-03, docs/arquitectura/05-motor-del-plano.md: el motor es del plano de
// control):
//   CapasDsk  el modelo, del DUEÑO del motor (el hilo de control): las definiciones, el
//             archivo y lo que se le pide al motor. Lo usan las órdenes de la ventanilla y
//             las herramientas dsk_* (Hilo::Control); lo que muestra va a la foto
//             (FotoMotor::dsk, la parte Superficies) con paraLaFoto.
//   Dsk       lo que ve la interfaz (QML, la línea de tiempo, el rótulo de traducción, el
//             bus): LEE la foto y PIDE por la ventanilla. Maneja las de su salida activa
//             (setSalida, como el Mezclador): `capas` trae `visible` de ESA salida y
//             mostrarUi sube y baja en ella. Lo que no se puede hacer se sabe en el acto
//             (mirando la foto, con las mismas reglas: CapasDsk::revisar*); lo que dice el
//             motor después, llega como `fallo`.
#pragma once

#include <QObject>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ba::app {
class VentanillaMotor;
}

namespace ba::app::control {

class CapasDsk {
   public:
    using json = nlohmann::json;
    /// Lo del motor que usa (BroadcastEngine::dskArmar, dskMostrar, dskEstado, lastError):
    /// lo pone la app; un test, uno sintético. `salida` vacía: la por omisión.
    struct Motor {
        std::function<bool(int capa, const std::vector<std::string>& contenido, const json& recuadro,
                           const std::string& salida)>
            armar;
        std::function<bool(int capa, bool visible, int ms, const std::string& salida)> mostrar;
        std::function<json(const std::string& salida)> estado; ///< [{capa, opacidad}]
        std::function<std::string()> error;
    };
    CapasDsk(const std::filesystem::path& datos, Motor motor);

    /// [{capa, nombre, contenido[], recuadro, fijo, transicion_ms, visible, opacidad}]:
    /// `visible` y `opacidad`, los de `salida` (vacía: la por omisión).
    json capas(const std::string& salida = {}) const;
    /// ¿Está arriba en `salida`? (vacía: la por omisión).
    bool arriba(int capa, const std::string& salida = {}) const;
    /// Define la capa (1..4). `def`: {nombre?, contenido?[] (escenas o fuentes), recuadro?, fijo?, transicion_ms?}.
    std::string definir(int capa, const json& def);
    /// Sube o baja en `salida` (vacía: la por omisión). Bajar una fija necesita
    /// `confirmada` (la confirmación de una persona).
    std::string mostrar(int capa, bool visible, bool confirmada, const std::string& salida = {});
    /// La plantilla «Intérprete LSA»: la `entrada` en un recuadro abajo a la derecha, fija.
    std::string plantillaLsa(int capa, const std::string& entrada, const std::string& posicion, double tamano);
    /// Al arrancar: vuelve a armar las capas y sube las que estaban arriba (sin fundido).
    void restaurar();
    /// Lo que va en la foto: {capas: las definiciones, salidas: {<id>: capas(id)}}.
    json paraLaFoto(const std::vector<std::string>& salidas) const;
    /// Cambió algo (la app ensucia la foto: FotoMotor::Superficies).
    void setAlCambiar(std::function<void()> fn) { alCambiar_ = std::move(fn); }

    // Las reglas que se miran antes de pedirle nada al motor: "" o el error. Las usa
    // también la interfaz (Dsk), con la capa que le llegó en la foto.
    static std::string revisarCapa(int capa);
    static std::string revisarDefinicion(int capa, const json& def);
    static std::string revisarMostrar(const json& capa, bool visible, bool confirmada);
    static std::string revisarLsa(const std::string& entrada);
    /// La definición de la plantilla «Intérprete LSA».
    static json definicionLsa(const std::string& entrada, const std::string& posicion, double tamano);
    /// ¿Es la salida por omisión? ("" o su id).
    static bool esPorOmision(const std::string& salida);

   private:
    void cargar();
    void guardar() const;
    json* buscar(int capa);
    void avisar();
    std::filesystem::path archivo_;
    Motor motor_;
    json capas_ = json::array();
    std::function<void()> alCambiar_;
};

class Dsk : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList capas READ capasUi NOTIFY cambio)
   public:
    using json = nlohmann::json;
    /// `capas`: el modelo del dueño (solo se le pide por la ventanilla).
    Dsk(CapasDsk& capas, VentanillaMotor& ventanilla, QObject* parent = nullptr);

    /// Como CapasDsk::capas, de la foto.
    json capas(const std::string& salida = {}) const;
    bool arriba(int capa, const std::string& salida = {}) const;
    /// La salida que maneja la interfaz ("" = la por omisión): la de `capas` y mostrarUi.
    void setSalida(const std::string& salida);
    // Lo piden al dueño: "" (pedido; si el motor no puede, llega `fallo`) o el error que
    // se ve en el acto (la capa no existe, es fija y no hay confirmación, está vacía…).
    std::string definir(int capa, const json& def);
    std::string mostrar(int capa, bool visible, bool confirmada, const std::string& salida = {});
    std::string plantillaLsa(int capa, const std::string& entrada, const std::string& posicion, double tamano);
    void restaurar();
    /// Cambiaron las capas (alguna definición o lo que está arriba en alguna salida): el bus.
    void setAlCambiar(std::function<void(const json&)> fn) { alCambiar_ = std::move(fn); }

    QVariantList capasUi() const;
    Q_INVOKABLE QString definirUi(int capa, const QVariantMap& def);
    Q_INVOKABLE QString mostrarUi(int capa, bool visible, bool confirmada);
    Q_INVOKABLE QString plantillaLsaUi(int capa, const QString& entrada);
    Q_INVOKABLE bool fija(int capa) const;

   signals:
    void cambio();
    /// Lo pedido no se pudo (lo dijo el dueño, ya traducido).
    void fallo(const QString& texto);

   private:
    void alCambiarFoto(unsigned partes);
    /// La capa `capa` como está en la foto (definición; null si no está).
    json definicion(int capa) const;
    void pedir(const char* que, std::function<std::string(CapasDsk&)> orden);
    CapasDsk& capas_;
    VentanillaMotor& v_;
    std::function<void(const json&)> alCambiar_;
    std::string salidaUi_; ///< ver setSalida
    json ultimaUi_;        ///< lo último que vio QML (para avisar solo si cambió)
    json ultimaFoto_;      ///< FotoMotor::dsk de la última foto (para el bus)
};

} // namespace ba::app::control
