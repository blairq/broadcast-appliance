// ============================================================================
// src/app/evento/TiposDeCue.cpp — ver TiposDeCue.hpp
// ============================================================================
#include "evento/TiposDeCue.hpp"

#include "i18n/I18nTexto.hpp"

namespace ba::evento {

const std::vector<TipoDeCue>& tiposDeCue() {
    static const std::vector<TipoDeCue> v{
        {"escena", QT_TRANSLATE_NOOP("evento", "Escena"), QT_TRANSLATE_NOOP("evento", "Pone una escena al aire."), {"escena"},
         {"transicion", "ms"}},
        {"musica", QT_TRANSLATE_NOOP("evento", "Música"), QT_TRANSLATE_NOOP("evento", "Arranca o para un tema de los medios del evento."),
         {"archivo", "accion", "repetir"}, {}},
        {"video", QT_TRANSLATE_NOOP("evento", "Video"),
         QT_TRANSLATE_NOOP("evento", "Reproduce un video de los medios del evento en su propia escena."),
         {"archivo", "accion", "repetir", "alAire"}, {}},
        {"teleprompter", QT_TRANSLATE_NOOP("evento", "Teleprompter"), QT_TRANSLATE_NOOP("evento", "Carga un discurso y lo deja listo."),
         {"prompter", "discurso", "arrancar"}, {}},
        {"orador", QT_TRANSLATE_NOOP("evento", "Orador"), QT_TRANSLATE_NOOP("evento", "Le da la palabra a alguien, con su tiempo."),
         {"participante"}, {}},
        {"grafico", QT_TRANSLATE_NOOP("evento", "Gráfico"),
         QT_TRANSLATE_NOOP("evento", "Saca al aire un gráfico animado en una fuente de escena."), {"fuente", "grafico", "accion", "seVa"},
         {"valores", "duracionMs", "repetir", "vinculo"}},
        {"carrusel", QT_TRANSLATE_NOOP("evento", "Carrusel"),
         QT_TRANSLATE_NOOP("evento", "Pone un carrusel en una lámina y lo muestra. Si en el medio lo cambiaste a mano desde la tarjeta, al llegar acá vuelve a esta."),
         {"carrusel", "lamina"}, {}},
        {"pantalla", QT_TRANSLATE_NOOP("evento", "Pantalla"), QT_TRANSLATE_NOOP("evento", "Manda contenido a una pantalla."),
         {"pantalla", "contenido"}, {"tipo", "pie"}},
        {"audio", QT_TRANSLATE_NOOP("evento", "Audio"), QT_TRANSLATE_NOOP("evento", "Silencia una fuente, la activa o le cambia el volumen."),
         {"accion", "fuente", "db"}, {}},
        {"pausa", QT_TRANSLATE_NOOP("evento", "Pausa"), QT_TRANSLATE_NOOP("evento", "Espera los segundos que diga la duración."), {}, {}},
        {"espera", QT_TRANSLATE_NOOP("evento", "Espera al operador"), QT_TRANSLATE_NOOP("evento", "Frena el guion hasta que confirmes."), {}, {}},
        {"nota", QT_TRANSLATE_NOOP("evento", "Nota"), QT_TRANSLATE_NOOP("evento", "No hace nada: es una indicación para vos."), {}, {}},
    };
    return v;
}

std::vector<std::string> nombresDeTipos() {
    std::vector<std::string> out;
    for (const TipoDeCue& t : tiposDeCue()) out.emplace_back(t.tipo);
    return out;
}

std::map<std::string, std::vector<std::string>> camposPorTipo() {
    std::map<std::string, std::vector<std::string>> out;
    for (const TipoDeCue& t : tiposDeCue()) {
        auto& v = out[t.tipo];
        v = t.campos;
        v.insert(v.end(), t.avanzados.begin(), t.avanzados.end());
    }
    return out;
}

} // namespace ba::evento
