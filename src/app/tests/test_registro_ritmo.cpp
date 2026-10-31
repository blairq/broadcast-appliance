// ============================================================================
// src/app/tests/test_registro_ritmo.cpp — el ritmo de la conversación de un tótem
//
// registro/RitmoTotem con la hora a mano (sin reloj, sin audio, sin Qt):
//   1. El fin del turno: espera la pausa sin voz; con el VAD oyendo voz espera más, hasta
//      el tope; mientras habla el asistente no cierra.
//   2. Terminar de hablar: espera que la voz salga entera y el margen; manda el `listo`
//      pendiente (una vez) y el estado nuevo de la pantalla (foto, escuchando, documento,
//      dorso), y desde ahí cuentan las esperas de la foto y del documento.
//   3. La foto y el documento: el fallo a su tiempo; el dorso espera más, y con una foto
//      en el modelo espera hasta el tope.
//   4. El silencio largo, una vez por silencio.
//   5. Las gracias vuelven solas a esperar; con canal y sin cara arranca, una vez y no
//      identificando; atendida por una persona no hace nada.
// ============================================================================
#include <cstdio>

#include "registro/RitmoTotem.hpp"

using ba::registro::RitmoTotem;
using Paso = RitmoTotem::Paso;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (false)

RitmoTotem::Mirada enCurso() {
    RitmoTotem::Mirada m;
    m.conversacion = true;
    m.enCurso = true;
    m.hayCanal = true;
    return m;
}

void finDelTurno() {
    RitmoTotem r;
    r.turnoMs = 1000;
    auto m = enCurso();
    m.hayTurno = true;
    CHECK(r.revisar(m, 1000 + RitmoTotem::kFinTurnoMs - 1).paso == Paso::Nada, "antes de la pausa, espera");
    CHECK(r.revisar(m, 1000 + RitmoTotem::kFinTurnoMs).paso == Paso::CerrarTurno, "pasada la pausa, cierra");
    m.oyendoVoz = true;
    CHECK(r.revisar(m, 1000 + RitmoTotem::kFinTurnoMs + 500).paso == Paso::Nada, "con voz sonando, espera");
    CHECK(r.revisar(m, 1000 + RitmoTotem::kTurnoTopeMs).paso == Paso::CerrarTurno, "con ruido sin parar, al tope cierra");
    // Mientras habla el asistente, lo oído no se cierra (y no hay silencio).
    r.hablando = true;
    m.oyendoVoz = false;
    CHECK(r.revisar(m, 1000 + RitmoTotem::kTurnoTopeMs).paso == Paso::Nada, "hablando: no cierra el turno");
}

void terminarDeHablar() {
    RitmoTotem r;
    r.hablando = true;
    r.hayListo = true;
    r.buscandoFoto = true;
    auto m = enCurso();
    CHECK(r.revisar(m, 5000).paso == Paso::Nada, "la voz todavía sale");
    CHECK(r.finHablaMs == 0, "sin salir, no cuenta el margen");
    m.vozSalio = true;
    CHECK(r.revisar(m, 5000).paso == Paso::Nada, "salió: espera el margen");
    CHECK(r.finHablaMs == 5000, "desde que salió");
    const auto d = r.revisar(m, 5000 + RitmoTotem::kMargenVozMs);
    CHECK(d.paso == Paso::TerminoDeHablar, "pasado el margen, vuelve a escuchar");
    CHECK(d.mandarListo && !r.hayListo && r.terminoMs == 5000 + RitmoTotem::kMargenVozMs, "manda el listo, una vez");
    CHECK(d.estado == "foto" && r.fotoDesdeMs == 5000 + RitmoTotem::kMargenVozMs, "pidió la foto: desde ahora se busca");
    CHECK(!r.hablando && r.ultimaVozMs == 5000 + RitmoTotem::kMargenVozMs, "ya no habla; la voz cuenta para el silencio");
    // Los otros estados de la pantalla.
    const auto estado = [](bool foto, bool doc, bool dorso) {
        RitmoTotem x;
        x.hablando = true;
        x.buscandoFoto = foto;
        x.buscandoDocumento = doc;
        auto mm = enCurso();
        mm.vozSalio = true;
        mm.buscandoDorso = dorso;
        x.revisar(mm, 1000);
        return x.revisar(mm, 1000 + RitmoTotem::kMargenVozMs).estado;
    };
    CHECK(estado(false, false, false) == "escuchando", "escuchando");
    CHECK(estado(false, true, false) == "documento", "documento");
    CHECK(estado(false, true, true) == "dorso", "la parte de atrás");
    // Terminada la conversación: no cambia la pantalla (ya mostró las gracias).
    RitmoTotem t;
    t.hablando = true;
    auto fin = enCurso();
    fin.enCurso = false;
    fin.vozSalio = true;
    t.revisar(fin, 1000);
    const auto dt = t.revisar(fin, 1000 + RitmoTotem::kMargenVozMs);
    CHECK(dt.paso == Paso::TerminoDeHablar && dt.estado.empty() && !dt.mandarListo, "sin conversación en curso, sin estado");
}

void fotoYDocumento() {
    RitmoTotem r;
    r.buscandoFoto = true;
    auto m = enCurso();
    CHECK(r.revisar(m, 100).paso == Paso::Nada && r.fotoDesdeMs == 100, "sin voz: la foto se busca desde ya");
    CHECK(r.revisar(m, 100 + RitmoTotem::kFotoMs).paso == Paso::Nada, "justo al límite, todavía no");
    CHECK(r.revisar(m, 101 + RitmoTotem::kFotoMs).paso == Paso::FotoFallo, "pasado, falla la foto");

    RitmoTotem d;
    d.buscandoDocumento = true;
    d.docDesdeMs = 1000;
    CHECK(d.revisar(m, 1001 + RitmoTotem::kDocumentoMs).paso == Paso::DocumentoFallo, "el frente, a los 9 s");
    m.buscandoDorso = true;
    CHECK(d.revisar(m, 1001 + RitmoTotem::kDocumentoMs).paso == Paso::Nada, "el dorso espera más");
    CHECK(d.revisar(m, 1001 + RitmoTotem::kDorsoMs).paso == Paso::DocumentoFallo, "el dorso, a los 25 s");
    d.leyendoDorso = true;
    CHECK(d.revisar(m, 1001 + RitmoTotem::kDorsoMs).paso == Paso::Nada, "con una foto en el modelo, espera su respuesta");
    CHECK(d.revisar(m, 1001 + RitmoTotem::kDorsoTopeMs).paso == Paso::DocumentoFallo, "hasta el tope");
    // Sin conversación, no hay pasos de foto ni de documento.
    RitmoTotem s;
    s.buscandoFoto = true;
    RitmoTotem::Mirada sin;
    CHECK(s.revisar(sin, 100000).paso == Paso::Nada, "sin conversación, nada");
}

void silencio() {
    RitmoTotem r;
    r.ultimaVozMs = 0;
    auto m = enCurso();
    CHECK(r.revisar(m, RitmoTotem::kSilencioMs).paso == Paso::Nada, "al límite, todavía no");
    CHECK(r.revisar(m, RitmoTotem::kSilencioMs + 1).paso == Paso::Silencio, "silencio largo");
    CHECK(r.revisar(m, RitmoTotem::kSilencioMs + 2).paso == Paso::Nada, "uno por silencio (cuenta de nuevo)");
}

void graciasArranqueYAtencion() {
    RitmoTotem r;
    r.terminoMs = 0;
    RitmoTotem::Mirada m;
    m.pantallaListo = true;
    CHECK(!r.revisar(m, RitmoTotem::kListoMs).volverAEsperar, "las gracias, un rato");
    CHECK(r.revisar(m, RitmoTotem::kListoMs + 1).volverAEsperar, "después vuelven solas a esperar");

    RitmoTotem a;
    a.primeraDelCanal = true;
    a.canalDesdeMs = 0;
    RitmoTotem::Mirada c;
    c.hayCanal = true;
    CHECK(a.revisar(c, RitmoTotem::kArranqueSinCaraMs).paso == Paso::Nada, "con canal, espera una cara");
    c.identificando = true;
    CHECK(a.revisar(c, RitmoTotem::kArranqueSinCaraMs + 1).paso == Paso::Nada, "mirando una cara, no arranca");
    c.identificando = false;
    CHECK(a.revisar(c, RitmoTotem::kArranqueSinCaraMs + 1).paso == Paso::Arrancar, "sin cara, arranca igual");
    a.primeraDelCanal = false; // empezar() lo baja
    CHECK(a.revisar(c, RitmoTotem::kArranqueSinCaraMs + 2).paso == Paso::Nada, "una sola vez por canal");

    RitmoTotem t;
    t.hablando = true;
    t.hayListo = true;
    auto at = enCurso();
    at.atendida = true;
    at.vozSalio = true;
    CHECK(t.revisar(at, 100000).paso == Paso::Nada && t.hablando && t.hayListo, "atendida: el ritmo en pausa");
}

} // namespace

int main() {
    finDelTurno();
    terminarDeHablar();
    fotoYDocumento();
    silencio();
    graciasArranqueYAtencion();
    if (g_fallos == 0) std::printf("test_registro_ritmo: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
