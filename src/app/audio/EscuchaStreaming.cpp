// ============================================================================
// src/app/audio/EscuchaStreaming.cpp — ver EscuchaStreaming.hpp
//
// Sin sherpa-onnx: lo de sherpa está en EscuchaNemotron.cpp. Esto es el hilo,
// las corrientes, los márgenes del VAD y el lote.
// ============================================================================
#include "audio/EscuchaStreaming.hpp"
#include "i18n/I18n.hpp"
#include "agent/Vocabulario.hpp"
#include "diag/Registro.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace ba::app::audio {

namespace {

std::string sinEspaciosAlPrincipio(const std::string& t) {
    const auto ini = t.find_first_not_of(' ');
    return ini == std::string::npos ? std::string() : t.substr(ini);
}

std::string corregido(std::string t) {
    t = sinEspaciosAlPrincipio(t);
    // Nemotron deja a veces dos espacios entre oraciones.
    for (auto p = t.find("  "); p != std::string::npos; p = t.find("  ", p)) t.erase(p, 1);
    // Los nombres propios y términos del vocabulario, bien escritos (como en Parakeet).
    if (auto* v = agent::Vocabulario::global(); v != nullptr && !t.empty()) t = v->corregir(t);
    return t;
}

} // namespace

// El de fábrica: promedio de a grupos cuando la frecuencia es múltiplo de 16 kHz
// (48 kHz, el caso de siempre: un filtro de caja de 3, que ya atenúa lo que
// se plegaría) e interpolación lineal en otro caso. sherpa-onnx trae uno mejor
// (EscuchaNemotron lo usa); este es para los tests y por si falta.
std::function<std::vector<float>(const float*, std::size_t)>
EscuchaStreaming::Modelo::remuestreador(std::uint32_t frecuencia) {
    if (frecuencia == kFrecuencia || frecuencia == 0)
        return [](const float* m, std::size_t n) { return std::vector<float>(m, m + n); };
    if (frecuencia % kFrecuencia == 0) {
        const std::size_t f = frecuencia / kFrecuencia;
        auto resto = std::make_shared<std::vector<float>>();
        return [f, resto](const float* m, std::size_t n) {
            resto->insert(resto->end(), m, m + n);
            std::vector<float> out;
            out.reserve(resto->size() / f);
            std::size_t i = 0;
            for (; i + f <= resto->size(); i += f) {
                float s = 0;
                for (std::size_t k = 0; k < f; ++k) s += (*resto)[i + k];
                out.push_back(s / static_cast<float>(f));
            }
            resto->erase(resto->begin(), resto->begin() + static_cast<std::ptrdiff_t>(i));
            return out;
        };
    }
    const double paso = static_cast<double>(frecuencia) / kFrecuencia;
    auto pos = std::make_shared<double>(0.0);
    auto ultima = std::make_shared<float>(0.0f);
    return [paso, pos, ultima](const float* m, std::size_t n) {
        std::vector<float> out;
        // `pos` es relativa al bloque: -1 es la última muestra del anterior.
        while (*pos < static_cast<double>(n) - 1) {
            const double p = *pos;
            const auto i = static_cast<std::ptrdiff_t>(p < 0 ? -1 : static_cast<std::ptrdiff_t>(p));
            const float a = i < 0 ? *ultima : m[i];
            const float b = m[i + 1];
            const float t = static_cast<float>(p - static_cast<double>(i));
            out.push_back(a + (b - a) * t);
            *pos += paso;
        }
        if (n > 0) {
            *ultima = m[n - 1];
            *pos -= static_cast<double>(n);
        }
        return out;
    };
}

struct EscuchaStreaming::Corriente {
    std::unique_ptr<Detector> vad;
    std::function<std::vector<float>(const float*, std::size_t)> rs;
    std::uint32_t frecuencia = 0;
    int version = -1;
    std::vector<float> resto;  ///< a 16 kHz, lo que no completa una ventana del VAD
    std::deque<float> antes;   ///< el margen de antes (mientras no hay voz)
    int stream = -1;           ///< -1: no hay frase en curso
    bool cerrando = false;     ///< se le cerró la entrada: falta decodificar lo último
    bool partir = false;       ///< larguísima sin pausas: el turno se reinicia sin cortar el audio
    bool cortarEnPausa = false; ///< pasó el largo máximo e hizo una pausa: sale lo dicho, el stream sigue
    std::size_t cola = 0;      ///< muestras del margen de después que quedan por pasar
    std::size_t enFrase = 0;   ///< muestras pasadas al modelo en esta frase
    std::size_t enStream = 0;  ///< muestras pasadas al stream desde que se creó o se reinició
    /// El texto del stream que ya salió como frase. En un fin de turno el stream NO se reinicia
    /// (reiniciado, el encoder arranca en frío y se come las palabras del medio segundo
    /// siguiente: «Hola, soy Jorge [Ortega], mi correo…»): sale solo lo nuevo.
    std::string emitido;
    bool enFin = false; ///< el stream ya estaba en fin de turno (sale una frase cuando empieza, no mientras dura)
    bool oia = false;          ///< el VAD oía voz en la ventana anterior
    std::string parcial;
};

EscuchaStreaming::EscuchaStreaming() : hilo_([this]() { bucle(); }) {}

EscuchaStreaming::~EscuchaStreaming() {
    {
        std::lock_guard<std::mutex> l(colaLock_);
        salir_ = true;
    }
    colaCv_.notify_all();
    if (hilo_.joinable()) hilo_.join();
}

void EscuchaStreaming::encolar(std::function<void()> trabajo) {
    {
        std::lock_guard<std::mutex> l(colaLock_);
        cola_.push_back(std::move(trabajo));
    }
    colaCv_.notify_one();
}

void EscuchaStreaming::bucle() {
    for (;;) {
        std::function<void()> trabajo;
        {
            std::unique_lock<std::mutex> l(colaLock_);
            // Cada 100 ms: el modelo trabaja de a bloques de 560 ms, y el
            // parcial se ve avanzar sin despertar el hilo por cada bloque de audio.
            colaCv_.wait_for(l, std::chrono::milliseconds(100),
                             [this]() { return salir_ || despertar_ || !cola_.empty(); });
            despertar_ = false;
            if (salir_ && cola_.empty()) break;
            if (!cola_.empty()) {
                trabajo = std::move(cola_.front());
                cola_.pop_front();
            }
        }
        if (trabajo) trabajo();
        atenderCorrientes();
        {
            std::lock_guard<std::mutex> l(contLock_);
            ++vueltas_;
        }
        vueltaCv_.notify_all();
    }
    // Los streams y el modelo se sueltan en el hilo que los creó.
    for (auto& [k, c] : corrientes_)
        if (c->stream >= 0 && modelo_) modelo_->soltarStream(c->stream);
    corrientes_.clear();
    modelo_.reset();
}

void EscuchaStreaming::esperarVuelta() {
    std::unique_lock<std::mutex> l(contLock_);
    const std::uint64_t hasta = vueltas_ + 2; // una entera después de esta llamada
    for (std::uint64_t v = vueltas_; v < hasta;) {
        {
            std::lock_guard<std::mutex> c(colaLock_);
            despertar_ = true;
        }
        colaCv_.notify_one();
        vueltaCv_.wait_for(l, std::chrono::seconds(10), [&]() { return vueltas_ > v; });
        v = vueltas_;
    }
}

void EscuchaStreaming::cargar(FabricaModelo modelo, FabricaDetector detector,
                              std::function<void(const std::string&)> listo) {
    encolar([this, modelo = std::move(modelo), detector = std::move(detector), listo = std::move(listo)]() {
        cargado_ = false;
        for (auto& [k, c] : corrientes_)
            if (c->stream >= 0 && modelo_) modelo_->soltarStream(c->stream);
        corrientes_.clear();
        modelo_.reset();
        std::string error;
        std::unique_ptr<Modelo> m = modelo ? modelo(error) : nullptr;
        if (!m) {
            if (listo) listo(error.empty() ? ba::i18n::traducir("escucha", "no se pudo cargar el reconocedor streaming") : error);
            return;
        }
        modelo_ = std::move(m);
        fabricaDetector_ = detector;
        cargado_ = true;
        if (listo) listo({});
    });
}

void EscuchaStreaming::descargar() {
    encolar([this]() {
        cargado_ = false;
        for (auto& [k, c] : corrientes_)
            if (c->stream >= 0 && modelo_) modelo_->soltarStream(c->stream);
        corrientes_.clear();
        modelo_.reset();
        fabricaDetector_ = {};
    });
}

void EscuchaStreaming::setMargenes(const Margenes& m) {
    std::lock_guard<std::mutex> l(contLock_);
    margenes_.antes = std::clamp(m.antes, 0.0, 2.0);
    margenes_.despues = std::clamp(m.despues, 0.1, 3.0);
}

EscuchaStreaming::Lote EscuchaStreaming::lote() const {
    std::lock_guard<std::mutex> l(contLock_);
    return lote_;
}

void EscuchaStreaming::setCortes(const Cortes& c) {
    std::lock_guard<std::mutex> l(contLock_);
    cortes_.pausa = std::clamp(c.pausa, 0.1, 3.0);
    cortes_.maximo = std::clamp(c.maximo, 2.0, 30.0);
    cortes_.umbral = std::clamp(c.umbral, 0.1, 0.95);
    ++versionCortes_;
}

Reconocedor::Cortes EscuchaStreaming::cortes() const {
    std::lock_guard<std::mutex> l(contLock_);
    return cortes_;
}

std::map<std::string, Reconocedor::Oido> EscuchaStreaming::oido() const {
    std::lock_guard<std::mutex> l(contLock_);
    return oido_;
}

void EscuchaStreaming::setFrase(Frase f) {
    std::lock_guard<std::mutex> l(contLock_);
    frase_ = std::move(f);
}

void EscuchaStreaming::setEmpiezaVoz(EmpiezaVoz f) {
    std::lock_guard<std::mutex> l(contLock_);
    empiezaVoz_ = std::move(f);
}

void EscuchaStreaming::abrirContinuo(const std::string& clave) {
    std::lock_guard<std::mutex> l(contLock_);
    pendientes_[clave].abierta = true;
}

void EscuchaStreaming::cerrarContinuo(const std::string& clave) {
    std::lock_guard<std::mutex> l(contLock_);
    if (auto it = pendientes_.find(clave); it != pendientes_.end()) it->second.abierta = false;
}

void EscuchaStreaming::alimentarContinuo(const std::string& clave, const float* muestras, std::size_t n,
                                         std::uint32_t frecuencia) {
    if (muestras == nullptr || n == 0) return;
    std::lock_guard<std::mutex> l(contLock_);
    const auto it = pendientes_.find(clave);
    if (it == pendientes_.end() || !it->second.abierta) return;
    Pendiente& p = it->second;
    // Tope: si el hilo se atrasa, no crece sin fin (10 s).
    if (p.audio.size() > static_cast<std::size_t>(frecuencia) * 10) return;
    p.frecuencia = frecuencia;
    p.audio.insert(p.audio.end(), muestras, muestras + n);
}

void EscuchaStreaming::decodificarListos(const std::vector<int>& streams) {
    // Mientras alguno tenga un bloque listo: un paso para TODOS los que lo
    // tengan, juntos. Es lo que hace que N voces cuesten casi lo que una.
    for (;;) {
        std::vector<int> listos;
        for (int s : streams)
            if (s >= 0 && modelo_->listo(s)) listos.push_back(s);
        if (listos.empty()) return;
        modelo_->decodificar(listos);
        std::lock_guard<std::mutex> l(contLock_);
        lote_.ultimo = static_cast<int>(listos.size());
        lote_.maximo = std::max(lote_.maximo, lote_.ultimo);
        ++lote_.pasos;
    }
}

std::string EscuchaStreaming::nuevoDe(const Corriente& c, const std::string& texto) {
    // El transducer solo agrega: lo ya emitido es el principio. Si no (no debería), todo.
    return texto.compare(0, c.emitido.size(), c.emitido) == 0 ? texto.substr(c.emitido.size()) : texto;
}

void EscuchaStreaming::emitir(const std::string& clave, Corriente& c, const Frase& frase) {
    Resultado r;
    const std::string todo = modelo_->texto(c.stream);
    // La continuación de una frase cortada en una pausa puede empezar con su signo («, y mi…»).
    std::string nuevo = nuevoDe(c, todo);
    nuevo.erase(0, std::min(nuevo.size(), nuevo.find_first_not_of(" ,.;:")));
    r.texto = corregido(nuevo);
    c.emitido = todo;
    r.segundosDeAudio = static_cast<double>(c.enFrase) / kFrecuencia;
    c.parcial.clear();
    c.enFrase = 0;
    if (!r.texto.empty() && frase) frase(clave, r);
}

void EscuchaStreaming::atenderCorrientes() {
    std::map<std::string, Pendiente> lote;
    Frase frase;
    EmpiezaVoz empieza;
    Cortes cortes;
    Margenes margenes;
    int version = 0;
    {
        std::lock_guard<std::mutex> l(contLock_);
        for (auto it = pendientes_.begin(); it != pendientes_.end();) {
            if (!it->second.abierta) {
                if (auto c = corrientes_.find(it->first); c != corrientes_.end()) {
                    if (c->second->stream >= 0 && modelo_) modelo_->soltarStream(c->second->stream);
                    corrientes_.erase(c);
                }
                oido_.erase(it->first);
                it = pendientes_.erase(it);
                continue;
            }
            Pendiente& p = lote[it->first];
            p.frecuencia = it->second.frecuencia;
            p.audio.swap(it->second.audio);
            ++it;
        }
        frase = frase_;
        empieza = empiezaVoz_;
        cortes = cortes_;
        margenes = margenes_;
        version = versionCortes_;
    }
    if (lote.empty() || !cargado_ || !modelo_ || !fabricaDetector_) return;

    const auto nAntes = static_cast<std::size_t>(margenes.antes * kFrecuencia);
    const auto nDespues = static_cast<std::size_t>(std::max(margenes.despues, cortes.pausa) * kFrecuencia);
    // Pasado el máximo, la frase sale en la primera pausa (la del VAD, que ya es
    // la pausa de los cortes), no en seco a mitad de palabra, y sin cerrar el
    // stream: cortado o reiniciado, el modelo pierde las palabras de los dos
    // lados del corte (se vio con Nemotron: «y [vengo por] la sesión», «Hola,
    // soy Jorge O[rtega, mi correo es]…»). Y si no hay pausa, al tope se
    // reinicia el turno sin cortar el audio.
    const auto nLargo = static_cast<std::size_t>(cortes.maximo * kFrecuencia);
    const auto nTope = static_cast<std::size_t>((cortes.maximo * 2 + 4) * kFrecuencia);
    double oidos = 0, alModelo = 0;

    // 1. El audio de cada corriente pasa por su VAD; al modelo, lo que tiene voz y sus márgenes.
    for (auto& [clave, p] : lote) {
        auto& cp = corrientes_[clave];
        if (!cp) cp = std::make_unique<Corriente>();
        Corriente& c = *cp;
        if (c.version != version || !c.vad) {
            c.vad = fabricaDetector_(cortes);
            c.version = version;
            if (!c.vad) continue;
        }
        if (p.audio.empty()) continue;
        if (c.frecuencia != p.frecuencia || !c.rs) {
            c.frecuencia = p.frecuencia;
            c.rs = modelo_->remuestreador(p.frecuencia);
        }
        const std::vector<float> a16 = c.rs(p.audio.data(), p.audio.size());
        oidos += static_cast<double>(a16.size()) / kFrecuencia;
        c.resto.insert(c.resto.end(), a16.begin(), a16.end());
        const std::size_t w = std::max<std::size_t>(c.vad->ventana(), 1);
        std::size_t usado = 0;
        for (; usado + w <= c.resto.size(); usado += w) {
            const float* v = c.resto.data() + usado;
            const bool hayVoz = c.vad->voz(v);
            if (hayVoz && !c.oia && empieza) empieza(clave);
            c.oia = hayVoz;
            if (c.stream < 0 || c.cerrando) {
                if (!hayVoz || c.cerrando) {
                    // Sin voz (o la frase anterior todavía cerrándose, hasta el
                    // paso 3): solo se guarda el margen de antes, que será el
                    // comienzo de la próxima frase.
                    c.antes.insert(c.antes.end(), v, v + w);
                    while (c.antes.size() > nAntes) c.antes.pop_front();
                    continue;
                }
                // Empieza una frase: el stream nuevo recibe el margen de antes y la ventana.
                c.stream = modelo_->nuevoStreamDe(clave);
                ba::diag::registro().linea(ba::diag::Nivel::Depuracion, "escucha", 0,
                                           ba::i18n::llenar(ba::i18n::traducir("escucha", "%1: voz, frase nueva (%2 ms antes)"),
                                                          {clave, std::to_string(c.antes.size() * 1000 / kFrecuencia)}));
                const std::vector<float> previo(c.antes.begin(), c.antes.end());
                c.antes.clear();
                if (!previo.empty()) modelo_->aceptar(c.stream, previo.data(), previo.size());
                c.enFrase = previo.size();
                c.enStream = previo.size();
                c.emitido.clear();
                c.enFin = false;
                alModelo += static_cast<double>(previo.size()) / kFrecuencia;
            }
            modelo_->aceptar(c.stream, v, w);
            c.enFrase += w;
            c.enStream += w;
            alModelo += static_cast<double>(w) / kFrecuencia;
            if (hayVoz)
                c.cola = nDespues;
            else
                c.cola = c.cola > w ? c.cola - w : 0;
            if (!hayVoz && c.cola > 0 && c.enFrase >= nLargo) {
                // Larga y con una pausa: sale lo dicho hasta acá, pero el stream NO se cierra
                // (uno nuevo arranca en frío y se come lo que la persona sigue diciendo).
                c.cortarEnPausa = true;
            }
            if (c.cola == 0) {
                // Se acabó el margen de después sin voz (o, larga, hizo una pausa): se cierra.
                modelo_->terminarEntrada(c.stream);
                c.cerrando = true;
                ba::diag::registro().linea(ba::diag::Nivel::Depuracion, "escucha", 0,
                                           ba::i18n::llenar(ba::i18n::traducir("escucha", "%1: sin voz, se cierra la frase (%2 ms)"),
                                                          {clave, std::to_string(c.enFrase * 1000 / kFrecuencia)}));
            } else if (c.enFrase >= nTope) {
                c.partir = true;
            }
        }
        c.resto.erase(c.resto.begin(), c.resto.begin() + static_cast<std::ptrdiff_t>(usado));
    }

    // 2. Un lote con todos los streams que tienen un bloque listo.
    std::vector<int> streams;
    for (auto& [clave, c] : corrientes_)
        if (c->stream >= 0) streams.push_back(c->stream);
    decodificarListos(streams);

    // 3. Fin de turno del modelo (el stream sigue) o entrada cerrada (el stream se suelta).
    for (auto& [clave, cp] : corrientes_) {
        Corriente& c = *cp;
        if (c.stream < 0) continue;
        if (c.cerrando) {
            emitir(clave, c, frase);
            modelo_->soltarStream(c.stream);
            c.stream = -1;
            c.cerrando = false;
            c.cola = 0;
            c.emitido.clear();
            c.cortarEnPausa = false;
            continue;
        }
        // Fin de turno: sale lo dicho desde la frase anterior y el stream sigue. Se reinicia
        // solo si ya es muy largo (antes de la regla del largo máximo del modelo, que si no
        // daría fin de turno a cada palabra), y entonces en una pausa.
        const bool fin = modelo_->finDeTurno(c.stream);
        const bool empiezaFin = fin && !c.enFin;
        c.enFin = fin;
        if (c.partir || (empiezaFin && c.enStream >= nTope)) {
            emitir(clave, c, frase);
            modelo_->reiniciar(c.stream);
            c.partir = false;
            c.enStream = 0;
            c.emitido.clear();
            c.enFin = false;
            continue;
        }
        if (empiezaFin || c.cortarEnPausa) {
            emitir(clave, c, frase);
            c.cortarEnPausa = false;
            continue;
        }
        c.parcial = sinEspaciosAlPrincipio(nuevoDe(c, modelo_->texto(c.stream)));
    }

    std::lock_guard<std::mutex> l(contLock_);
    lote_.segundosAlModelo += alModelo;
    lote_.segundosOidos += oidos;
    for (auto& [clave, cp] : corrientes_) {
        if (!pendientes_.count(clave)) continue;
        oido_[clave] = Oido{cp->stream >= 0, static_cast<double>(cp->enFrase) / kFrecuencia, cp->parcial};
    }
}

void EscuchaStreaming::transcribir(std::vector<float> audio, std::uint32_t frecuencia, Listo listo) {
    encolar([this, audio = std::move(audio), frecuencia, listo = std::move(listo)]() {
        Resultado r;
        if (!cargado_ || !modelo_ || !fabricaDetector_) {
            r.error = ba::i18n::traducir("escucha", "el reconocedor de voz no está cargado");
            if (listo) listo(r);
            return;
        }
        if (audio.empty()) {
            r.error = ba::i18n::traducir("escucha", "no llegó audio del micrófono");
            if (listo) listo(r);
            return;
        }
        const auto t0 = std::chrono::steady_clock::now();
        Cortes cortes;
        Margenes margenes;
        {
            std::lock_guard<std::mutex> l(contLock_);
            cortes = cortes_;
            margenes = margenes_;
        }
        const std::vector<float> a16 = modelo_->remuestreador(frecuencia)(audio.data(), audio.size());
        // Solo lo hablado, con sus márgenes (como en las corrientes): el silencio
        // de antes de hablar y de después de soltar la tecla no se decodifica.
        std::unique_ptr<Detector> vad = fabricaDetector_(cortes);
        std::vector<float> habla;
        if (vad) {
            const std::size_t w = std::max<std::size_t>(vad->ventana(), 1);
            const std::size_t ventanas = a16.size() / w;
            std::vector<bool> voz(ventanas, false);
            for (std::size_t i = 0; i < ventanas; ++i) voz[i] = vad->voz(a16.data() + i * w);
            const auto antes = static_cast<std::size_t>(margenes.antes * kFrecuencia / static_cast<double>(w)) + 1;
            const auto despues = static_cast<std::size_t>(margenes.despues * kFrecuencia / static_cast<double>(w)) + 1;
            std::vector<bool> usar(ventanas, false);
            for (std::size_t i = 0; i < ventanas; ++i) {
                if (!voz[i]) continue;
                const std::size_t ini = i >= antes ? i - antes : 0;
                const std::size_t fin = std::min(ventanas, i + despues);
                for (std::size_t k = ini; k < fin; ++k) usar[k] = true;
            }
            for (std::size_t i = 0; i < ventanas; ++i)
                if (usar[i]) habla.insert(habla.end(), a16.begin() + static_cast<std::ptrdiff_t>(i * w),
                                          a16.begin() + static_cast<std::ptrdiff_t>((i + 1) * w));
            if (habla.empty()) {
                r.error = ba::i18n::traducir("escucha", "no se escuchó a nadie hablar");
                if (listo) listo(r);
                return;
            }
        } else {
            habla = a16;
        }
        r.segundosDeAudio = static_cast<double>(habla.size()) / kFrecuencia;
        const int s = modelo_->nuevoStreamDe("dictado"); // lo de a una frase es el dictado (F9)
        modelo_->aceptar(s, habla.data(), habla.size());
        modelo_->terminarEntrada(s);
        decodificarListos({s});
        r.texto = corregido(modelo_->texto(s));
        modelo_->soltarStream(s);
        r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (r.texto.empty()) r.error = ba::i18n::traducir("escucha", "no se entendió nada");
        if (listo) listo(r);
    });
}

} // namespace ba::app::audio
