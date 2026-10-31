// ============================================================================
// src/app/ui/EventosListaViewModel.hpp — la lista de eventos, el en curso y las carpetas
//
// Lo que la página Evento y el Histórico muestran de Eventos/: los eventos (con su ficha
// del histórico), las carpetas que los agrupan, cuál está en curso y si se crea solo el
// del día (Config/historico.json, `evento_automatico`).
//
// UNA CACHÉ, LEÍDA EN OTRO HILO. Recorrer Eventos/ es leer un evento.json y un
// ficha.json por evento (con un año de jornadas automáticas, cientos): eso no va en el
// hilo de la interfaz ni en cada lectura de una propiedad. La lista se lee una vez al
// arrancar, en un hilo de trabajo, y se vuelve a leer:
//   - después de cada cambio hecho por acá (crear, mover, borrar, iniciar…);
//   - cuando cambia algo en Eventos/ o en una carpeta que agrupa (QFileSystemWatcher:
//     un evento copiado a mano en la carpeta aparece solo).
// Las propiedades leen la caché y avisan cuando llega la lectura nueva (`listaChanged`,
// `enCursoChanged`); nadie tiene que pedir «refrescar».
//
// Lo que cambia de un evento abierto y guardado (nombre, fecha, cues) se pone en la
// caché al guardar (`alGuardar`), sin esperar a releer.
// ============================================================================
#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

#include <QFileSystemWatcher>
#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include "evento/EventoStore.hpp"

namespace ba::app {

class EventosListaViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList eventos READ eventos NOTIFY listaChanged)
    /// Las carpetas que agrupan eventos ("2026/Octubre"...).
    Q_PROPERTY(QStringList carpetas READ carpetas NOTIFY listaChanged)
    /// {carpeta, nombre, grupo, desdeMs, automatico}; vacío si no hay ninguno.
    Q_PROPERTY(QVariantMap enCurso READ enCurso NOTIFY enCursoChanged)
    Q_PROPERTY(bool eventoAutomatico READ eventoAutomatico WRITE setEventoAutomatico NOTIFY automaticoChanged)
    /// false mientras no llegó la primera lectura.
    Q_PROPERTY(bool lista READ leida NOTIFY listaChanged)
   public:
    /// `raiz`: Eventos/; `ajustes`: Config/historico.json.
    EventosListaViewModel(std::filesystem::path raiz, std::filesystem::path ajustes, QObject* parent = nullptr);
    ~EventosListaViewModel() override;

    // --- Lo que hay (de la caché) --------------------------------------------------
    QVariantList eventos() const { return eventosUi_; }
    QStringList carpetas() const { return carpetasUi_; }
    QVariantMap enCurso() const;
    bool leida() const { return leida_; }
    const std::vector<evento::Ficha>& fichas() const { return fichas_; }
    /// La carpeta del en curso (vacía si no hay ninguno).
    const std::string& carpetaEnCurso() const { return enCurso_; }
    /// La ficha del histórico de un evento (de la caché; uno que todavía no está, del disco).
    evento::DatosDelEvento datos(const std::string& carpeta) const;
    /// Cuántas veces se leyó Eventos/ entero (para las pruebas: abrir la página no relee).
    int lecturas() const { return lecturas_.load(); }
    /// Espera a que no quede ninguna lectura en curso (para las pruebas y al cerrar).
    void esperarLecturas();

    // --- Config/historico.json -----------------------------------------------------
    bool eventoAutomatico() const { return automatico_; }
    void setEventoAutomatico(bool si);
    /// Lo que dice Config/historico.json (leído una vez; lo de acá se guarda por persist).
    const nlohmann::json& ajustesHistorico() const { return ajustes_; }
    void guardarAjustesHistorico(nlohmann::json j);

    // --- Cambios (en el hilo de la interfaz: son acciones del operador) --------------
    /// Que haya uno en curso (el del día si hace falta). Corre cada minuto: solo va al
    /// disco cuando hace falta (no hay ninguno, o el automático es de otro día).
    void asegurarEnCurso();
    bool iniciar(const std::string& carpeta, std::string* err);
    bool terminar(std::string* err);
    std::string mover(const std::string& carpeta, const std::string& aCarpeta, std::string* err);
    std::string crearCarpeta(const std::string& ruta, std::string* err);
    std::string crear(const std::string& nombre, const std::string& enCarpeta, std::string* err);
    bool borrar(const std::string& carpeta, std::string* err);
    bool guardarDatos(const std::string& carpeta, const evento::DatosDelEvento& d, std::string* err);
    /// El abierto se guardó: lo suyo pasa a la caché ya.
    void alGuardar(const std::string& carpeta, const evento::Evento& e);
    /// Vuelve a leer Eventos/ en el fondo (avisa al terminar).
    void releer();

   signals:
    void listaChanged();
    void enCursoChanged();
    void automaticoChanged();
    void notify(const QString& texto, const QString& tipo);

   private:
    struct Leido {
        std::vector<evento::Ficha> fichas;
        std::vector<std::string> carpetas;
        std::string enCurso;
    };
    void llego(Leido l);
    void armarUi();
    void vigilar();

    evento::EventoStore store_;
    std::filesystem::path ajustesArchivo_;
    nlohmann::json ajustes_ = nlohmann::json::object();
    bool automatico_ = true;

    std::vector<evento::Ficha> fichas_;
    std::vector<std::string> carpetas_;
    std::string enCurso_;
    QVariantList eventosUi_;
    QStringList carpetasUi_;
    bool leida_ = false;

    QThreadPool fondo_; ///< un hilo: las lecturas, de a una
    bool leyendo_ = false, otraVez_ = false;
    std::atomic<int> lecturas_{0};
    QFileSystemWatcher vigia_;
};

} // namespace ba::app
