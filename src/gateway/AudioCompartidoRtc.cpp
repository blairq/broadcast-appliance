// ============================================================================
// src/gateway/AudioCompartidoRtc.cpp — grupos Opus y el codificador de paso (AudioCompartidoRtc.hpp)
// ============================================================================
#include "AudioCompartidoRtc.hpp"

#if BA_GATEWAY_HAVE_WEBRTC

#include "AudioCompartido.hpp"

#include "api/make_ref_counted.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace ba::gateway::audioret {

namespace {

/// Paquetes que guarda cada variante (con 20 ms: 160 ms de historia).
constexpr std::size_t kPaquetesGuardados = 8;
/// Una variante que nadie pide en este tiempo deja de codificarse (3 s).
constexpr std::uint64_t kSinPedirBloques = 300;

std::string minusculas(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// El Opus de libwebrtc de una conexión, de paso: ver AudioCompartidoRtc.hpp.
class EncoderPaso final : public webrtc::AudioEncoder {
   public:
    EncoderPaso(std::unique_ptr<webrtc::AudioEncoder> propio, webrtc::SdpAudioFormat formato, int pt,
                std::shared_ptr<Registro> registro)
        : propio_(std::move(propio)), formato_(std::move(formato)), firma_(audioret::firma(formato_)), pt_(pt),
          registro_(std::move(registro)) {}

    int SampleRateHz() const override { return propio_->SampleRateHz(); }
    size_t NumChannels() const override { return propio_->NumChannels(); }
    int RtpTimestampRateHz() const override { return propio_->RtpTimestampRateHz(); }
    size_t Num10MsFramesInNextPacket() const override { return propio_->Num10MsFramesInNextPacket(); }
    size_t Max10MsFramesInAPacket() const override { return propio_->Max10MsFramesInAPacket(); }
    int GetTargetBitrate() const override { return propio_->GetTargetBitrate(); }
    void Reset() override {
        propio_->Reset();
        salir();
    }
    bool SetFec(bool e) override { return propio_->SetFec(e); }
    bool SetDtx(bool e) override { return propio_->SetDtx(e); }
    bool GetDtx() const override { return propio_->GetDtx(); }
    bool SetApplication(Application a) override { return propio_->SetApplication(a); }
    void SetMaxPlaybackRate(int hz) override { propio_->SetMaxPlaybackRate(hz); }
    void SetTargetBitrate(int bps) override { propio_->SetTargetBitrate(bps); }
    bool EnableAudioNetworkAdaptor(const std::string& c, webrtc::RtcEventLog* l) override {
        return propio_->EnableAudioNetworkAdaptor(c, l);
    }
    void DisableAudioNetworkAdaptor() override { propio_->DisableAudioNetworkAdaptor(); }
    void OnReceivedUplinkPacketLossFraction(float f) override { propio_->OnReceivedUplinkPacketLossFraction(f); }
    void OnReceivedTargetAudioBitrate(int bps) override { propio_->OnReceivedTargetAudioBitrate(bps); }
    void OnReceivedUplinkBandwidth(int bps, absl::optional<int64_t> ventana) override {
        propio_->OnReceivedUplinkBandwidth(bps, ventana);
    }
    void OnReceivedUplinkAllocation(webrtc::BitrateAllocationUpdate u) override { propio_->OnReceivedUplinkAllocation(u); }
    void OnReceivedRtt(int ms) override { propio_->OnReceivedRtt(ms); }
    void OnReceivedOverhead(size_t b) override { propio_->OnReceivedOverhead(b); }
    void SetReceiverFrameLengthRange(int a, int b) override { propio_->SetReceiverFrameLengthRange(a, b); }
    webrtc::ANAStats GetANAStats() const override { return propio_->GetANAStats(); }
    absl::optional<std::pair<webrtc::TimeDelta, webrtc::TimeDelta>> GetFrameLengthRange() const override {
        return propio_->GetFrameLengthRange();
    }
    absl::optional<std::pair<webrtc::DataRate, webrtc::DataRate>> GetBitrateRange() const override {
        return propio_->GetBitrateRange();
    }

   protected:
    EncodedInfo EncodeImpl(uint32_t ts, rtc::ArrayView<const int16_t> audio, rtc::Buffer* encoded) override {
        // ¿De qué grupo es este bloque? (la marca: AudioCompartido.hpp)
        std::shared_ptr<Grupo> g;
        std::uint64_t k = 0;
        const int canales = static_cast<int>(NumChannels());
        std::uint16_t id = 0;
        std::uint32_t k24 = 0;
        if (canales > 0 && leerMarca(audio.data(), audio.size() / static_cast<std::size_t>(canales), canales, id, k24) &&
            (g = registro_->buscar(id))) {
            k = bloqueCompleto(k24, g->ultimo());
            g->pedir(firma_, formato_, pt_);
        }
        if (grupo_) {
            if (enPaquete_ == 0) {
                // Entre dos paquetes del grupo: sigue si el que viene empieza acá.
                if (g == grupo_ && g->empieza(firma_, k)) return empezar(std::move(g), k, ts, audio, encoded);
                salir(); // sigue con el propio, desde un paquete nuevo
            } else {
                guardar(ts, audio);
                if (g == grupo_ && k == inicioK_ + enPaquete_) {
                    ++enPaquete_;
                    EncodedInfo info;
                    if (emitir(encoded, info)) return info;
                    if (enPaquete_ < Num10MsFramesInNextPacket()) return EncodedInfo();
                }
                // El grupo no le sirve más en medio de un paquete: lo rearma el propio con los
                // bloques que ya recibió (ni se pierde ni se repite audio).
                return rearmar(encoded);
            }
        }
        if (g && enPropio_ == 0) {
            if (g->empieza(firma_, k)) {
                saltados_ = 0;
                return empezar(std::move(g), k, ts, audio, encoded);
            }
            // Para entrar hay que empezar con un paquete del grupo, y el grupo está a medias:
            // este bloque no sale (un hueco de 10 ms que el navegador disimula, una vez al
            // entrar) y el próximo ya empieza paquete. Si no hay manera, sigue con el propio.
            if (g->enCurso(firma_, k) && saltados_ + 1 < Num10MsFramesInNextPacket()) {
                ++saltados_;
                return EncodedInfo();
            }
        }
        saltados_ = 0;
        return propio(ts, audio, encoded);
    }

   private:
    struct Bloque {
        uint32_t ts = 0;
        std::vector<int16_t> pcm;
    };

    EncodedInfo empezar(std::shared_ptr<Grupo> g, std::uint64_t k, uint32_t ts, rtc::ArrayView<const int16_t> audio,
                        rtc::Buffer* encoded) {
        grupo_ = std::move(g);
        inicioK_ = k;
        inicioTs_ = ts;
        enPaquete_ = 1;
        guardados_.clear();
        guardar(ts, audio);
        EncodedInfo info;
        if (emitir(encoded, info)) return info; // un paquete de un solo bloque
        return EncodedInfo();
    }

    /// El paquete del grupo que termina en el bloque de ahora, si empezó donde empezó este.
    bool emitir(rtc::Buffer* encoded, EncodedInfo& info) {
        std::uint64_t inicio = 0;
        EncodedInfoLeaf hoja;
        const std::size_t antes = encoded->size();
        if (!grupo_->paquete(firma_, inicioK_ + enPaquete_ - 1, inicio, *encoded, hoja)) return false;
        if (inicio != inicioK_) {
            encoded->SetSize(antes);
            return false;
        }
        info.encoded_bytes = encoded->size() - antes;
        info.encoded_timestamp = inicioTs_;
        info.payload_type = pt_;
        info.send_even_if_empty = hoja.send_even_if_empty;
        info.speech = hoja.speech;
        info.encoder_type = hoja.encoder_type;
        enPaquete_ = 0;
        guardados_.clear();
        grupo_->repartidos.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    EncodedInfo rearmar(rtc::Buffer* encoded) {
        std::vector<Bloque> bloques;
        bloques.swap(guardados_);
        propio_->Reset();
        salir();
        EncodedInfo ultimo;
        const std::size_t antes = encoded->size();
        for (const Bloque& b : bloques) {
            EncodedInfo info = propio(b.ts, b.pcm, encoded);
            if (info.encoded_bytes > 0 || info.send_even_if_empty) ultimo = info;
        }
        ultimo.encoded_bytes = encoded->size() - antes;
        return ultimo;
    }

    EncodedInfo propio(uint32_t ts, rtc::ArrayView<const int16_t> audio, rtc::Buffer* encoded) {
        const std::size_t f = std::max<std::size_t>(1, propio_->Num10MsFramesInNextPacket());
        EncodedInfo info = propio_->Encode(ts, audio, encoded);
        if (++enPropio_ >= f) enPropio_ = 0;
        return info;
    }

    void guardar(uint32_t ts, rtc::ArrayView<const int16_t> audio) {
        Bloque b;
        b.ts = ts;
        b.pcm.assign(audio.begin(), audio.end());
        guardados_.push_back(std::move(b));
    }

    /// Vuelve al propio, que arranca un paquete nuevo (lo que tuviera a medias ya no sirve).
    void salir() {
        if (grupo_) {
            propio_->Reset();
            enPropio_ = 0;
        }
        grupo_.reset();
        enPaquete_ = 0;
        guardados_.clear();
    }

    std::unique_ptr<webrtc::AudioEncoder> propio_;
    const webrtc::SdpAudioFormat formato_;
    const std::string firma_;
    const int pt_;
    const std::shared_ptr<Registro> registro_;
    std::shared_ptr<Grupo> grupo_; ///< el del paquete que sale ahora (vacío: el propio)
    std::uint64_t inicioK_ = 0;    ///< el bloque del grupo en que empezó el paquete
    uint32_t inicioTs_ = 0;        ///< y su sello en ESTA conexión
    std::size_t enPaquete_ = 0;    ///< bloques del paquete del grupo ya recibidos
    std::size_t enPropio_ = 0;     ///< bloques que el propio tiene a medias
    std::size_t saltados_ = 0;     ///< bloques que no salieron para alinearse con el grupo
    std::vector<Bloque> guardados_;
};

class FabricaPaso : public webrtc::AudioEncoderFactory {
   public:
    FabricaPaso(rtc::scoped_refptr<webrtc::AudioEncoderFactory> base, std::shared_ptr<Registro> r)
        : base_(std::move(base)), registro_(std::move(r)) {}
    std::vector<webrtc::AudioCodecSpec> GetSupportedEncoders() override { return base_->GetSupportedEncoders(); }
    absl::optional<webrtc::AudioCodecInfo> QueryAudioEncoder(const webrtc::SdpAudioFormat& f) override {
        return base_->QueryAudioEncoder(f);
    }
    std::unique_ptr<webrtc::AudioEncoder> MakeAudioEncoder(int pt, const webrtc::SdpAudioFormat& f,
                                                           absl::optional<webrtc::AudioCodecPairId> id) override {
        std::unique_ptr<webrtc::AudioEncoder> e = base_->MakeAudioEncoder(pt, f, id);
        if (!e || minusculas(f.name) != "opus") return e;
        return std::make_unique<EncoderPaso>(std::move(e), f, pt, registro_);
    }

   private:
    const rtc::scoped_refptr<webrtc::AudioEncoderFactory> base_;
    const std::shared_ptr<Registro> registro_;
};

} // namespace

std::string firma(const webrtc::SdpAudioFormat& f) {
    std::string s = minusculas(f.name) + "/" + std::to_string(f.clockrate_hz) + "/" + std::to_string(f.num_channels);
    for (const auto& [k, v] : f.parameters) s += ";" + minusculas(k) + "=" + v; // map: ya ordenado
    return s;
}

// --- Grupo -------------------------------------------------------------------------

Grupo::Grupo(std::uint16_t id, rtc::scoped_refptr<webrtc::AudioEncoderFactory> base)
    : id_(id), base_(std::move(base)) {}

std::uint64_t Grupo::ultimo() const {
    std::lock_guard<std::mutex> l(m_);
    return seq_;
}

std::uint64_t Grupo::codificar(const float* pcm, std::size_t n, int rate) {
    std::lock_guard<std::mutex> l(m_);
    const std::uint64_t k = ++seq_;
    for (auto it = variantes_.begin(); it != variantes_.end();) {
        Variante& v = it->second;
        if (k - v.pedida > kSinPedirBloques) {
            it = variantes_.erase(it);
            continue;
        }
        ++it;
        const int canales = static_cast<int>(v.enc->NumChannels());
        const std::size_t cuadros = n / 2;
        if (pcm == nullptr || rate != v.enc->SampleRateHz() || cuadros * 100 != static_cast<std::size_t>(rate) ||
            canales < 1 || canales > 2) {
            v.pendiente = 0; // no se puede: este bloque corta el paquete (nadie lo va a usar)
            continue;
        }
        v.pcm.resize(cuadros * static_cast<std::size_t>(canales));
        const auto a16 = [](float x) {
            x = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
            return static_cast<std::int16_t>(x * 32767.0f);
        };
        for (std::size_t i = 0; i < cuadros; ++i) {
            if (canales == 1) {
                v.pcm[i] = a16(0.5f * (pcm[2 * i] + pcm[2 * i + 1]));
            } else {
                v.pcm[2 * i] = a16(pcm[2 * i]);
                v.pcm[2 * i + 1] = a16(pcm[2 * i + 1]);
            }
        }
        if (v.pendiente == 0) v.pendiente = k;
        const std::size_t porPaquete = std::max<std::size_t>(1, v.enc->Num10MsFramesInNextPacket());
        v.buf.Clear();
        const webrtc::AudioEncoder::EncodedInfo info = v.enc->Encode(v.ts, v.pcm, &v.buf);
        v.ts += static_cast<std::uint32_t>(cuadros);
        if (k - v.pendiente + 1 < porPaquete) continue; // el paquete sigue a medias
        Paquete p;
        p.inicio = v.pendiente;
        p.fin = k;
        p.bytes.assign(v.buf.data(), v.buf.data() + v.buf.size());
        p.info = info;
        v.paquetes.push_back(std::move(p));
        while (v.paquetes.size() > kPaquetesGuardados) v.paquetes.pop_front();
        v.pendiente = 0;
    }
    return k;
}

void Grupo::pedir(const std::string& firma, const webrtc::SdpAudioFormat& formato, int pt) {
    std::lock_guard<std::mutex> l(m_);
    auto it = variantes_.find(firma);
    if (it == variantes_.end()) {
        Variante v;
        v.enc = base_->MakeAudioEncoder(pt, formato, absl::nullopt);
        if (!v.enc) return;
        it = variantes_.emplace(firma, std::move(v)).first;
    }
    it->second.pedida = seq_;
}

bool Grupo::empieza(const std::string& firma, std::uint64_t k) const {
    std::lock_guard<std::mutex> l(m_);
    const auto it = variantes_.find(firma);
    if (it == variantes_.end() || k == 0) return false;
    const Variante& v = it->second;
    if (v.pendiente == k) return true;
    return std::any_of(v.paquetes.begin(), v.paquetes.end(), [k](const Paquete& p) { return p.inicio == k; });
}

bool Grupo::enCurso(const std::string& firma, std::uint64_t k) const {
    std::lock_guard<std::mutex> l(m_);
    const auto it = variantes_.find(firma);
    if (it == variantes_.end()) return false;
    const Variante& v = it->second;
    if (v.pendiente != 0 && v.pendiente < k) return true;
    return std::any_of(v.paquetes.begin(), v.paquetes.end(), [k](const Paquete& p) { return p.inicio < k && p.fin >= k; });
}

bool Grupo::paquete(const std::string& firma, std::uint64_t k, std::uint64_t& inicio, rtc::Buffer& out,
                    webrtc::AudioEncoder::EncodedInfoLeaf& info) const {
    std::lock_guard<std::mutex> l(m_);
    const auto it = variantes_.find(firma);
    if (it == variantes_.end()) return false;
    for (const Paquete& p : it->second.paquetes)
        if (p.fin == k) {
            inicio = p.inicio;
            out.AppendData(p.bytes.data(), p.bytes.size());
            info = p.info;
            return true;
        }
    return false;
}

// --- Registro ----------------------------------------------------------------------

std::shared_ptr<Grupo> Registro::grupo(const std::string& nombre) {
    std::lock_guard<std::mutex> l(m_);
    auto& g = porNombre_[nombre];
    if (!g) {
        const auto id = static_cast<std::uint16_t>(porId_.size() + 1);
        g = std::make_shared<Grupo>(id, base_);
        porId_[id] = g;
    }
    return g;
}

std::size_t Grupo::variantes() const {
    std::lock_guard<std::mutex> l(m_);
    return variantes_.size();
}

nlohmann::json Registro::estado() const {
    std::lock_guard<std::mutex> l(m_);
    nlohmann::json out = nlohmann::json::array();
    for (const auto& [nombre, g] : porNombre_)
        out.push_back({{"nombre", nombre},
                       {"bloques", g->ultimo()},
                       {"repartidos", g->repartidos.load(std::memory_order_relaxed)},
                       {"variantes", g->variantes()}});
    return out;
}

std::shared_ptr<Grupo> Registro::buscar(std::uint16_t id) const {
    std::lock_guard<std::mutex> l(m_);
    const auto it = porId_.find(id);
    return it != porId_.end() ? it->second : nullptr;
}

rtc::scoped_refptr<webrtc::AudioEncoderFactory> fabricaDePaso(rtc::scoped_refptr<webrtc::AudioEncoderFactory> base,
                                                              std::shared_ptr<Registro> registro) {
    return rtc::make_ref_counted<FabricaPaso>(std::move(base), std::move(registro));
}

} // namespace ba::gateway::audioret

#endif // BA_GATEWAY_HAVE_WEBRTC
