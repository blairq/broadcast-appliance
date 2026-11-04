// ============================================================================
// src/app/tests/test_foto_partes.cpp — qué avisa la foto del motor (emision/FotoMotor.hpp)
//
// El caso del hallazgo: la publicación comparaba SOLO lo sucio, y había campos de una
// parte que se rearmaban con otra (el tema web con Ajustes, siendo de Superficies; el
// legalizador con Ajustes, siendo de Video; los ajustes de una cámara con Escenas; las
// capas DSK con Salidas). Cambiaba el campo, se publicaba sin su parte, y como el reloj
// del motor publica enseguida, la comparación siguiente ya daba igual: la vista no se
// enteraba nunca.
//
//   1. Cada campo de la foto es de UNA parte (partesDistintas lo ve, y en la suya).
//   2. Con TODAS las combinaciones de partes sucias, quien arma cada campo con la tabla
//      (FotoMotor::rearmar) y la publicación (queCambio) coinciden: después de rearmar se
//      comparan TODAS las partes y nada cambió sin estar comparado.
//   3. Los casos del hallazgo, uno por uno, avisan su parte.
//   4. LA FOTO POR PARTES COMPARTIDAS (emision/Compartido.hpp; lo medido con 120
//      invitados: el hilo de control copiaba la foto entera, con las escenas y la mezcla,
//      cada ~100 ms): copiar la foto no copia lo que crece con los invitados; rearmar una
//      parte no toca las otras; rearmarla igual se queda con el valor de antes; editar una
//      compartida no cambia la publicada; y quien lee ve lo mismo que con valores.
// ============================================================================
#include "emision/FotoMotor.hpp"

#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using ba::app::FotoMotor;

namespace {
int fallas = 0;
#define CHECK(c, msg)                                                                     \
    do {                                                                                  \
        if (!(c)) {                                                                       \
            std::printf("FALLA: %s (%s, linea %d)\n", msg, #c, __LINE__);               \
            ++fallas;                                                                     \
        } else {                                                                          \
            std::printf("ok: %s\n", msg);                                                 \
        }                                                                                 \
    } while (0)

/// Un campo y cómo cambiarlo (cada vez distinto: `n`).
struct Campo {
    const char* nombre;
    unsigned parte;
    std::function<void(FotoMotor&, int n)> cambiar;
};

std::vector<Campo> campos() {
    using F = FotoMotor;
    return {
        {"escenas", F::Escenas, [](F& f, int n) { f.escenas = {F::Escena{"e" + std::to_string(n), {}, false, {}}}; }},
        {"fuentes", F::Escenas, [](F& f, int n) { f.fuentes = {{"f" + std::to_string(n), "t"}}; }},
        {"tipos", F::Escenas, [](F& f, int n) { f.tipos = {F::Tipo{"t" + std::to_string(n), "", false, false}}; }},
        {"transicionesEntrada", F::Escenas, [](F& f, int n) { f.transicionesEntrada["x"] = n; }},
        {"salidas", F::Salidas, [](F& f, int n) { f.salidas = {F::Salida{.id = "s" + std::to_string(n)}}; }},
        {"tope", F::Salidas, [](F& f, int n) { f.tope = 100 + n; }},
        {"estado", F::Estado, [](F& f, int n) { f.estado.cuadros = n + 1; }},
        {"destinos", F::Emision, [](F& f, int n) { f.destinos = {n}; }},
        {"grabacion", F::Emision, [](F& f, int n) { f.grabacion["n"] = n; }},
        {"limpia", F::Emision, [](F& f, int n) { f.limpia["n"] = n; }},
        {"isos", F::Emision, [](F& f, int n) { f.isos = {n}; }},
        {"apurosDisco", F::Emision, [](F& f, int n) { f.apurosDisco["n"] = n; }},
        {"remux", F::Emision, [](F& f, int n) { f.remux["n"] = n; }},
        {"retornos", F::Emision, [](F& f, int n) { f.retornos["n"] = n; }},
        {"ajustes", F::Ajustes, [](F& f, int n) { f.ajustes.invitados = n + 1; }},
        {"medios", F::Medidas, [](F& f, int n) { f.medios = {n}; }},
        {"salud", F::Medidas, [](F& f, int n) { f.salud["n"] = n; }},
        {"video", F::Video, [](F& f, int n) { f.video["n"] = n; }},
        {"fuentesVideo", F::Video, [](F& f, int n) { f.fuentesVideo["n"] = n; }},
        {"alAire", F::Video, [](F& f, int n) { f.alAire = {std::to_string(n)}; }},
        {"configImagen", F::Video, [](F& f, int n) { f.configImagen["legalizar"] = n; }},
        {"configuracion", F::Configuracion,
         [](F& f, int) { f.configuracion.app = std::shared_ptr<const ba::app::AppConfig>(reinterpret_cast<const ba::app::AppConfig*>(&f), [](auto*) {}); }},
        {"equipo", F::Equipo, [](F& f, int n) { f.equipo.relojTipo = std::to_string(n); }},
        {"sala", F::Sala, [](F& f, int n) { f.sala.puerto = 9000 + n; }},
        {"dsk", F::Superficies, [](F& f, int n) { f.dsk["n"] = n; }},
        {"temaWeb", F::Superficies, [](F& f, int n) { f.temaWeb = "tema" + std::to_string(n); }},
        {"mezcla", F::Mezcla, [](F& f, int n) { f.mezcla.pistasGrabacion = n + 1; }},
    };
}

/// Lo que hace AppRuntime::publicarFoto (con sus aportes), en chico: rearma lo sucio de
/// cada campo sobre la de trabajo. Cada campo se rearma con su parte o, si está en la tabla
/// de arrastres, con lo que lo mueve (FotoMotor::rearmar), como lo hacen armarFoto,
/// VideoGrafos::aportar y el aporte de las capas DSK.
void rearmar(FotoMotor& f, unsigned sucias, int n) {
    const auto fila = [](const char* nombre) -> const FotoMotor::Arrastre* {
        const std::string s = nombre;
        if (s == "fuentesVideo") return &FotoMotor::kFuentesVideo;
        if (s == "alAire") return &FotoMotor::kAlAire;
        if (s == "configImagen") return &FotoMotor::kConfigImagen;
        if (s == "dsk") return &FotoMotor::kDsk;
        if (s == "temaWeb") return &FotoMotor::kTemaWeb;
        if (s == "fuentes") return &FotoMotor::kFuentes;
        return nullptr;
    };
    for (const Campo& c : campos()) {
        const FotoMotor::Arrastre* a = fila(c.nombre);
        if (a != nullptr ? FotoMotor::rearmar(*a, sucias) : (sucias & c.parte) != 0) c.cambiar(f, n);
    }
}
} // namespace

int main() {
    // 1. Cada campo es de una parte, y partesDistintas lo ve en esa.
    {
        int mal = 0;
        for (const Campo& c : campos()) {
            FotoMotor a, b;
            c.cambiar(b, 1);
            const unsigned d = FotoMotor::partesDistintas(a, b, FotoMotor::Todas);
            if (d != c.parte) {
                std::printf("  el campo «%s» cambia la parte %u y es de la %u\n", c.nombre, d, c.parte);
                ++mal;
            }
        }
        CHECK(mal == 0, "1: cada campo de la foto está en la tabla de su parte (partesDistintas)");
    }
    // Las filas de la tabla nombran partes de verdad, distintas de la suya.
    {
        bool bien = true;
        for (const FotoMotor::Arrastre& a : FotoMotor::kArrastres)
            bien = bien && (a.por & ~unsigned(FotoMotor::Todas)) == 0 && (a.por & a.de) == 0 && a.por != 0;
        CHECK(bien, "1: cada arrastre es de otras partes (no de la suya ni de RelerArchivos)");
    }

    // 2. Todas las combinaciones de partes sucias: lo que se rearma, se compara.
    {
        std::vector<unsigned> bits;
        for (unsigned b = 1; b != 0 && b <= FotoMotor::Todas; b <<= 1)
            if ((FotoMotor::Todas & b) != 0) bits.push_back(b);
        int sinMarcar = 0, sinAviso = 0, n = 0;
        for (unsigned combo = 1; combo < (1u << bits.size()); ++combo) {
            unsigned sucias = 0;
            for (std::size_t i = 0; i < bits.size(); ++i)
                if ((combo & (1u << i)) != 0) sucias |= bits[i];
            FotoMotor publicada;
            rearmar(publicada, FotoMotor::Todas, ++n);
            FotoMotor trabajo = publicada;
            rearmar(trabajo, sucias, ++n);
            if (FotoMotor::cambioSinMarcar(publicada, trabajo, sucias) != 0) ++sinMarcar;
            // Todo lo que se rearmó (y cambió) se avisa: comparar TODO da lo mismo que
            // comparar lo que dice la tabla.
            if (FotoMotor::queCambio(publicada, trabajo, sucias) != FotoMotor::partesDistintas(publicada, trabajo, FotoMotor::Todas))
                ++sinAviso;
        }
        CHECK(sinMarcar == 0, "2: con cualquier combinación de partes sucias, nada cambia sin compararse");
        CHECK(sinAviso == 0, "2: lo que avisa la publicación es TODO lo que cambió");
    }

    // 3. Los casos del hallazgo.
    {
        FotoMotor antes;
        FotoMotor ahora = antes;
        // El tema web, desde la vista, la Configuración o el MCP: ajustesAplicados ensucia
        // Ajustes (y más), nunca Superficies.
        ahora.temaWeb = "oscuro";
        CHECK(FotoMotor::partesDistintas(antes, ahora, FotoMotor::Ajustes | FotoMotor::Configuracion) == 0,
              "3: (antes) comparando solo lo sucio, el tema web nuevo no se avisaba");
        CHECK((FotoMotor::queCambio(antes, ahora, FotoMotor::Ajustes | FotoMotor::Configuracion) & FotoMotor::Superficies) != 0,
              "3: el tema web cambiado con los ajustes avisa Superficies");
        // El legalizador y «Mejorar imagen» (Ajustes): la parte Video.
        ahora = antes;
        ahora.configImagen["legalizar"] = true;
        CHECK((FotoMotor::queCambio(antes, ahora, FotoMotor::Ajustes) & FotoMotor::Video) != 0,
              "3: el legalizador (Ajustes) avisa Video");
        // Los ajustes de una cámara (Cambio::Capa: Escenas y Sala).
        ahora = antes;
        ahora.fuentesVideo["Cámara 1"] = {{"ajustes", {{"brillo", 3}}}};
        CHECK((FotoMotor::queCambio(antes, ahora, FotoMotor::Escenas | FotoMotor::Sala) & FotoMotor::Video) != 0,
              "3: los ajustes de una cámara (Capa) avisan Video");
        // El final de un fundido DSK, en una vuelta del motor (Salidas, Estado, Medidas).
        ahora = antes;
        ahora.dsk["salidas"] = {{"principal", {{"visible", true}}}};
        CHECK((FotoMotor::queCambio(antes, ahora, FotoMotor::Salidas | FotoMotor::Estado | FotoMotor::Medidas) &
               FotoMotor::Superficies) != 0,
              "3: el final de un fundido DSK (la vuelta del motor) avisa Superficies");
        // Lo que está al aire, con un corte.
        ahora = antes;
        ahora.alAire = {"Cámara 2"};
        CHECK((FotoMotor::queCambio(antes, ahora, FotoMotor::Salidas | FotoMotor::Estado) & FotoMotor::Video) != 0,
              "3: un corte (Salidas) avisa Video por lo que está al aire");
        // Un micrófono de la máquina agregado (una entrada global: no toca ninguna escena; el
        // motor lo avisa como alta de audio, que ensucia Mezcla). La lista de fuentes es de
        // Escenas y el rack la mira para saber si la fuente existe: decía «no existe la
        // fuente» hasta que algo tocara una escena.
        ahora = antes;
        ahora.fuentes = {{"Micrófono de MacBook Pro", "ba_gst_fuente"}};
        CHECK(FotoMotor::rearmar(FotoMotor::kFuentes, FotoMotor::Mezcla),
              "3: un alta o una baja de audio (Mezcla) rearma la lista de fuentes");
        CHECK((FotoMotor::queCambio(antes, ahora, FotoMotor::Mezcla) & FotoMotor::Escenas) != 0,
              "3: una fuente global nueva (Mezcla) avisa Escenas por la lista de fuentes");
        // Y lo que no se tocó no se avisa: el reloj del motor no despierta a las vistas de Video
        // si nada de Video cambió.
        CHECK(FotoMotor::queCambio(antes, antes, FotoMotor::Salidas | FotoMotor::Estado | FotoMotor::Medidas) == 0,
              "3: si nada cambió, nada se avisa (aunque se compare más)");
    }

    // 4. Por partes compartidas, con 120 invitados.
    {
        using F = FotoMotor;
        using nlohmann::json;
        constexpr int kInvitados = 120;
        // La foto de trabajo del dueño, como la arma la publicación (PublicadorFoto::armarFoto).
        F trabajo;
        {
            std::vector<F::Escena> escenas;
            std::vector<std::pair<std::string, std::string>> fuentes;
            std::vector<F::CanalAudio> canales;
            std::vector<std::string> audio;
            json video = json::object(), fuentesVideo = json::object(), retornos = json::object();
            F::Escena grilla{"Grilla", "", false, {}, "esc-1"};
            for (int i = 1; i <= kInvitados; ++i) {
                const std::string n = "Invitado " + std::to_string(i);
                grilla.fuentes.push_back(F::Fuente{n, "guest", true, {}, json{{"opacidad", 1}}, "guest:" + std::to_string(i)});
                fuentes.emplace_back(n, "guest");
                F::CanalAudio c;
                c.nombre = n;
                c.ajustes = {{"slot", i}};
                c.volumen["principal"] = 1.0f;
                canales.push_back(c);
                audio.push_back(n);
                video["estados"][n] = {{"sin_datos_s", 0.1}};
                fuentesVideo[n] = {{"tipo", "guest"}, {"ajustes", {{"slot", i}}}};
                retornos[std::to_string(i)] = "mix_minus";
            }
            escenas.push_back(grilla);
            trabajo.escenas = std::move(escenas);
            trabajo.fuentes = std::move(fuentes);
            trabajo.mezcla.canales = std::move(canales);
            trabajo.mezcla.porOmision = "principal";
            trabajo.sala.audio = std::move(audio);
            trabajo.video = std::move(video);
            trabajo.fuentesVideo = std::move(fuentesVideo);
            trabajo.retornos = std::move(retornos);
        }
        // Publicar: la foto nueva es una copia de la de trabajo.
        const auto publicada = std::make_shared<const F>(trabajo);
        CHECK(publicada->escenas.mismo(trabajo.escenas) && publicada->mezcla.canales.mismo(trabajo.mezcla.canales) &&
                  publicada->video.mismo(trabajo.video) && publicada->fuentesVideo.mismo(trabajo.fuentesVideo) &&
                  publicada->retornos.mismo(trabajo.retornos) && publicada->sala.audio.mismo(trabajo.sala.audio) &&
                  publicada->fuentes.mismo(trabajo.fuentes),
              "4: copiar la foto comparte lo que crece con los invitados (no lo copia)");
        CHECK(publicada->escenas.usos() == 2 && publicada->mezcla.canales.usos() == 2,
              "4: el mismo valor, en la publicada y en la de trabajo");
        // La próxima vuelta del motor rearma lo que se mueve siempre (Salidas, Estado, Medidas)
        // y, por ejemplo, la mezcla con un fader: las escenas y el video siguen compartidos.
        trabajo.estado.cuadros = 1234;
        {
            std::vector<F::CanalAudio> canales = *trabajo.mezcla.canales;
            canales[7].volumen["principal"] = 0.5f;
            trabajo.mezcla.canales = std::move(canales);
        }
        const auto siguiente = std::make_shared<const F>(trabajo);
        CHECK(!siguiente->mezcla.canales.mismo(publicada->mezcla.canales) && siguiente->escenas.mismo(publicada->escenas) &&
                  siguiente->video.mismo(publicada->video) && siguiente->fuentesVideo.mismo(publicada->fuentesVideo),
              "4: rearmar una parte (la mezcla) no copia las otras (escenas, video): siguen compartidas");
        CHECK(F::partesDistintas(*publicada, *siguiente, F::Todas) == (F::Mezcla | F::Estado),
              "4: y se ve distinta solo la parte que cambió");
        CHECK(publicada->mezcla.volumen("Invitado 8") == 1.0f && siguiente->mezcla.volumen("Invitado 8") == 0.5f,
              "4: la publicada no cambió (la nueva tiene su valor)");
        // Rearmar IGUAL (lo de cada vuelta): se queda con el valor de antes, sin recorrerlo después.
        trabajo.escenas = std::vector<F::Escena>(*publicada->escenas);
        trabajo.video = json(*publicada->video);
        CHECK(trabajo.escenas.mismo(publicada->escenas) && trabajo.video.mismo(publicada->video),
              "4: rearmar una parte igual se queda con el valor compartido de antes");
        // Editar en el lugar una compartida: se copia antes (la publicada no cambia).
        trabajo.video["estados"]["Invitado 1"]["sin_datos_s"] = 9.0;
        CHECK(!trabajo.video.mismo(publicada->video) &&
                  publicada->video["estados"]["Invitado 1"].value("sin_datos_s", 0.0) == 0.1 &&
                  trabajo.video["estados"]["Invitado 1"].value("sin_datos_s", 0.0) == 9.0,
              "4: editar una parte compartida la copia antes: lo publicado no cambia nunca");
        // Editar la que ya es solo suya: no se copia de nuevo.
        const json* antes = &trabajo.video.get();
        trabajo.video["nuevo"] = true;
        CHECK(&trabajo.video.get() == antes, "4: editar la que ya es solo de la de trabajo no la copia otra vez");
        // Quien lee ve lo mismo que con valores (el contrato de la vista, el MCP…).
        const F& f = *siguiente;
        int vistas = 0;
        for (const F::Escena& e : f.escenas) vistas += static_cast<int>(e.fuentes.size());
        const auto it = f.fuentesVideo.find("Invitado 3");
        const std::vector<std::string>& audio = f.sala.audio;
        const json copia = f.video;
        CHECK(vistas == kInvitados && f.escenas.size() == 1 && f.escenas[0].nombre == "Grilla" &&
                  f.escena("Grilla") != nullptr && f.fuentesDe("Grilla").size() == static_cast<std::size_t>(kInvitados) &&
                  f.hayFuente("Invitado 120") && f.tipoDe("Invitado 2") == "guest" && it != f.fuentesVideo.end() &&
                  (*it)["ajustes"].value("slot", 0) == 3 && audio.size() == static_cast<std::size_t>(kInvitados) &&
                  f.retornos.value("5", std::string()) == "mix_minus" && copia.contains("estados") &&
                  f.mezcla.canal("Invitado 120") != nullptr && f.mezcla.nombres().size() == static_cast<std::size_t>(kInvitados),
              "4: quien lee la foto la lee igual que antes");
        // Lo que cuesta publicar 1000 veces (como 100 s de vueltas del motor): copias de punteros.
        const long usosAntes = trabajo.escenas.usos();
        const auto t0 = std::chrono::steady_clock::now();
        std::size_t total = 0;
        for (int i = 0; i < 1000; ++i) {
            trabajo.estado.cuadros = i;
            const auto p = std::make_shared<const F>(trabajo);
            total += p->escenas.size();
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  medido: 1000 publicaciones con %d invitados: %.2f ms (%zu)\n", kInvitados, ms, total);
        CHECK(trabajo.escenas.usos() == usosAntes && total == 1000,
              "4: publicar 1000 veces no hizo ninguna copia de las escenas (el mismo valor, compartido)");
    }

    std::printf(fallas == 0 ? "test_foto_partes: OK\n" : "test_foto_partes: %d fallas\n", fallas);
    return fallas == 0 ? 0 : 1;
}
