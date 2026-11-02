// ============================================================================
// src/gateway/H264Sintaxis.cpp — SPS, PPS, cabecera de slice, POC y referencias de H.264
// (ITU-T H.264 §7.3, §8.2.1, §8.2.5). Ver H264Sintaxis.hpp.
// ============================================================================

#include "H264Sintaxis.hpp"

#include <algorithm>
#include <climits>

namespace ba::gateway::h264 {

std::vector<std::uint8_t> aRbsp(const std::uint8_t* d, std::size_t n) {
    std::vector<std::uint8_t> r;
    r.reserve(n);
    int ceros = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (ceros >= 2 && d[i] == 3) {
            ceros = 0;
            continue;
        }
        r.push_back(d[i]);
        ceros = d[i] == 0 ? ceros + 1 : 0;
    }
    return r;
}

Bits::Bits(const std::uint8_t* d, std::size_t n) : d_(d), n_(n) {
    // El bit de parada: el último 1 del RBSP.
    std::size_t i = n;
    while (i > 0 && d[i - 1] == 0)
        --i;
    if (i == 0) {
        fin_ = 0;
        return;
    }
    const std::uint8_t b = d[i - 1];
    int k = 0;
    while (((b >> k) & 1) == 0)
        ++k;
    fin_ = (i - 1) * 8 + static_cast<std::size_t>(7 - k);
}

std::uint32_t Bits::u(int n) {
    std::uint32_t v = 0;
    for (int i = 0; i < n; ++i) {
        if (pos_ >= n_ * 8) {
            error_ = true;
            return 0;
        }
        v = (v << 1) | ((d_[pos_ >> 3] >> (7 - (pos_ & 7))) & 1u);
        ++pos_;
    }
    return v;
}

std::uint32_t Bits::ue() {
    int ceros = 0;
    while (!error_ && u(1) == 0) {
        if (++ceros > 31) {
            error_ = true;
            return 0;
        }
    }
    if (ceros == 0) return 0;
    return ((1u << ceros) - 1u) + u(ceros);
}

std::int32_t Bits::se() {
    const std::uint32_t k = ue();
    const std::int32_t m = static_cast<std::int32_t>((k + 1) / 2);
    return (k & 1) ? m : -m;
}

bool Bits::masDatos() const { return !error_ && pos_ < fin_; }

// --- Listas de escalado ------------------------------------------------------------------

namespace {

constexpr std::uint8_t kDef4Intra[16] = {6, 13, 13, 20, 20, 20, 28, 28, 28, 28, 32, 32, 32, 37, 37, 42};
constexpr std::uint8_t kDef4Inter[16] = {10, 14, 14, 20, 20, 20, 24, 24, 24, 24, 27, 27, 27, 30, 30, 34};
constexpr std::uint8_t kDef8Intra[64] = {6,  10, 10, 13, 11, 13, 16, 16, 16, 16, 18, 18, 18, 18, 18, 23,
                                         23, 23, 23, 23, 23, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27, 27,
                                         27, 27, 27, 27, 29, 29, 29, 29, 29, 29, 29, 31, 31, 31, 31, 31,
                                         31, 33, 33, 33, 33, 33, 36, 36, 36, 36, 38, 38, 38, 40, 40, 42};
constexpr std::uint8_t kDef8Inter[64] = {9,  13, 13, 15, 13, 15, 17, 17, 17, 17, 19, 19, 19, 19, 19, 21,
                                         21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22, 24, 24, 24, 24,
                                         24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27, 27, 27,
                                         27, 28, 28, 28, 28, 28, 30, 30, 30, 30, 32, 32, 32, 33, 33, 35};

/// scaling_list(): {presente, usa la de fábrica}.
template <std::size_t N>
void leerLista(Bits& b, std::array<std::uint8_t, N>& l, bool& deFabrica) {
    int ultimo = 8, siguiente = 8;
    deFabrica = false;
    for (std::size_t j = 0; j < N; ++j) {
        if (siguiente != 0) {
            const int delta = b.se();
            siguiente = (ultimo + delta + 256) % 256;
            deFabrica = j == 0 && siguiente == 0;
        }
        l[j] = static_cast<std::uint8_t>(siguiente == 0 ? ultimo : siguiente);
        ultimo = l[j];
    }
}

/// Las listas de un SPS o un PPS con las reglas de respaldo A (SPS: `base` = nullptr) o B
/// (PPS: `base` = las del SPS).
void leerListas(Bits& b, Escalado& e, int cuantas8, const Escalado* base) {
    for (int i = 0; i < 6; ++i) {
        bool deFabrica = false;
        if (b.flag()) {
            leerLista(b, e.l4[static_cast<std::size_t>(i)], deFabrica);
            if (deFabrica)
                std::copy(i < 3 ? kDef4Intra : kDef4Inter, (i < 3 ? kDef4Intra : kDef4Inter) + 16,
                          e.l4[static_cast<std::size_t>(i)].begin());
        } else if (i == 0 || i == 3) {
            if (base != nullptr) e.l4[static_cast<std::size_t>(i)] = base->l4[static_cast<std::size_t>(i)];
            else
                std::copy(i == 0 ? kDef4Intra : kDef4Inter, (i == 0 ? kDef4Intra : kDef4Inter) + 16,
                          e.l4[static_cast<std::size_t>(i)].begin());
        } else {
            e.l4[static_cast<std::size_t>(i)] = e.l4[static_cast<std::size_t>(i - 1)];
        }
    }
    for (int i = 0; i < 6; ++i) {
        if (i >= cuantas8) {
            // Sin lista: la de fábrica (A) o la del SPS (B); solo importan 0 y 1 (DXVA).
            if (base != nullptr) e.l8[static_cast<std::size_t>(i)] = base->l8[static_cast<std::size_t>(i)];
            else
                std::copy(i % 2 == 0 ? kDef8Intra : kDef8Inter, (i % 2 == 0 ? kDef8Intra : kDef8Inter) + 64,
                          e.l8[static_cast<std::size_t>(i)].begin());
            continue;
        }
        bool deFabrica = false;
        if (b.flag()) {
            leerLista(b, e.l8[static_cast<std::size_t>(i)], deFabrica);
            if (deFabrica)
                std::copy(i % 2 == 0 ? kDef8Intra : kDef8Inter, (i % 2 == 0 ? kDef8Intra : kDef8Inter) + 64,
                          e.l8[static_cast<std::size_t>(i)].begin());
        } else if (i < 2) {
            if (base != nullptr) e.l8[static_cast<std::size_t>(i)] = base->l8[static_cast<std::size_t>(i)];
            else
                std::copy(i == 0 ? kDef8Intra : kDef8Inter, (i == 0 ? kDef8Intra : kDef8Inter) + 64,
                          e.l8[static_cast<std::size_t>(i)].begin());
        } else {
            e.l8[static_cast<std::size_t>(i)] = e.l8[static_cast<std::size_t>(i - 2)];
        }
    }
}

bool perfilAlto(int p) {
    switch (p) {
    case 100: case 110: case 122: case 244: case 44: case 83: case 86: case 118: case 128: case 138: case 139:
    case 134: case 135:
        return true;
    default:
        return false;
    }
}

} // namespace

void Escalado::plano() {
    for (auto& l : l4)
        l.fill(16);
    for (auto& l : l8)
        l.fill(16);
}

// --- SPS / PPS ----------------------------------------------------------------------------

std::optional<Sps> leerSps(const std::uint8_t* rbsp, std::size_t n) {
    Bits b(rbsp, n);
    Sps s;
    s.escalado.plano();
    s.profile_idc = static_cast<int>(b.u(8));
    b.u(8); // constraint_set*_flag
    s.level_idc = static_cast<int>(b.u(8));
    s.id = static_cast<int>(b.ue());
    if (s.id > 31) return std::nullopt;
    if (perfilAlto(s.profile_idc)) {
        s.chroma_format_idc = static_cast<int>(b.ue());
        if (s.chroma_format_idc == 3) s.separate_colour_plane = b.flag();
        s.bit_depth_luma_minus8 = static_cast<int>(b.ue());
        s.bit_depth_chroma_minus8 = static_cast<int>(b.ue());
        s.qpprime_y_zero_transform_bypass = b.flag();
        s.hayEscalado = b.flag();
        if (s.hayEscalado) leerListas(b, s.escalado, s.chroma_format_idc != 3 ? 2 : 6, nullptr);
    }
    s.log2_max_frame_num_minus4 = static_cast<int>(b.ue());
    if (s.log2_max_frame_num_minus4 > 12) return std::nullopt;
    s.poc_type = static_cast<int>(b.ue());
    if (s.poc_type == 0) {
        s.log2_max_poc_lsb_minus4 = static_cast<int>(b.ue());
        if (s.log2_max_poc_lsb_minus4 > 12) return std::nullopt;
    } else if (s.poc_type == 1) {
        s.delta_pic_order_always_zero = b.flag();
        s.offset_for_non_ref_pic = b.se();
        s.offset_for_top_to_bottom_field = b.se();
        const std::uint32_t k = b.ue();
        if (k > 255) return std::nullopt;
        for (std::uint32_t i = 0; i < k; ++i)
            s.offset_for_ref_frame.push_back(b.se());
    } else if (s.poc_type != 2) {
        return std::nullopt;
    }
    s.max_num_ref_frames = static_cast<int>(b.ue());
    s.gaps_allowed = b.flag();
    s.ancho_mbs = static_cast<int>(b.ue()) + 1;
    s.alto_unidades = static_cast<int>(b.ue()) + 1;
    s.frame_mbs_only = b.flag();
    if (!s.frame_mbs_only) s.mb_adaptive_frame_field = b.flag();
    s.direct_8x8_inference = b.flag();
    if (b.flag()) {
        const int cx = s.chroma_format_idc == 0 || s.separate_colour_plane ? 1 : (s.chroma_format_idc == 3 ? 1 : 2);
        const int cy = (s.chroma_format_idc == 1 && !s.separate_colour_plane ? 2 : 1) * (s.frame_mbs_only ? 1 : 2);
        s.crop_izq = static_cast<int>(b.ue()) * cx;
        s.crop_der = static_cast<int>(b.ue()) * cx;
        s.crop_arr = static_cast<int>(b.ue()) * cy;
        s.crop_aba = static_cast<int>(b.ue()) * cy;
    }
    if (b.error() || s.max_num_ref_frames > 16 || s.ancho_mbs > 512 || s.alto_unidades > 512) return std::nullopt;
    if (s.ancho() <= 0 || s.alto() <= 0) return std::nullopt;
    return s;
}

std::optional<Pps> leerPps(const std::uint8_t* rbsp, std::size_t n, const Sps* const* spsPorId) {
    Bits b(rbsp, n);
    Pps p;
    p.id = static_cast<int>(b.ue());
    p.sps_id = static_cast<int>(b.ue());
    if (p.id > 255 || p.sps_id > 31) return std::nullopt;
    const Sps* sps = spsPorId[p.sps_id];
    if (sps == nullptr) return std::nullopt;
    p.entropy_coding_mode = b.flag();
    p.bottom_field_pic_order_in_frame_present = b.flag();
    p.num_slice_groups_minus1 = static_cast<int>(b.ue());
    if (p.num_slice_groups_minus1 > 0) return p; // FMO: no se sigue leyendo (no soportado)
    p.num_ref_idx_l0_default_minus1 = static_cast<int>(b.ue());
    p.num_ref_idx_l1_default_minus1 = static_cast<int>(b.ue());
    p.weighted_pred = b.flag();
    p.weighted_bipred_idc = static_cast<int>(b.u(2));
    p.pic_init_qp_minus26 = b.se();
    p.pic_init_qs_minus26 = b.se();
    p.chroma_qp_index_offset = b.se();
    p.deblocking_filter_control_present = b.flag();
    p.constrained_intra_pred = b.flag();
    p.redundant_pic_cnt_present = b.flag();
    p.second_chroma_qp_index_offset = p.chroma_qp_index_offset;
    p.escalado = sps->escalado; // las del SPS (o planas)
    if (b.masDatos()) {
        p.transform_8x8_mode = b.flag();
        if (b.flag()) {
            // Regla B: lo que falta sale del SPS; si el SPS no trae listas, las de fábrica
            // del SPS son las planas... salvo que la norma dice «Default» para 0/3/6/7 cuando
            // el SPS no trae: respaldo A.
            Escalado e;
            e.plano();
            leerListas(b, e, p.transform_8x8_mode ? (sps->chroma_format_idc != 3 ? 2 : 6) : 0,
                       sps->hayEscalado ? &sps->escalado : nullptr);
            p.escalado = e;
        }
        p.second_chroma_qp_index_offset = b.se();
    }
    if (b.error()) return std::nullopt;
    return p;
}

// --- Slice ---------------------------------------------------------------------------------

std::optional<Slice> leerSlice(const std::uint8_t* rbsp, std::size_t n, int nalRefIdc, int nalTipo,
                               const Sps* const* spsPorId, const Pps* const* ppsPorId) {
    Bits b(rbsp, n);
    Slice s;
    s.nal_ref_idc = nalRefIdc;
    s.nal_type = nalTipo;
    s.idr = nalTipo == 5;
    s.first_mb = static_cast<int>(b.ue());
    s.slice_type = static_cast<int>(b.ue()) % 5;
    s.pps_id = static_cast<int>(b.ue());
    if (s.pps_id > 255 || ppsPorId[s.pps_id] == nullptr) return std::nullopt;
    const Pps& pps = *ppsPorId[s.pps_id];
    if (spsPorId[pps.sps_id] == nullptr) return std::nullopt;
    const Sps& sps = *spsPorId[pps.sps_id];
    if (sps.separate_colour_plane) b.u(2);
    s.frame_num = static_cast<int>(b.u(sps.log2_max_frame_num_minus4 + 4));
    if (!sps.frame_mbs_only) {
        s.field_pic = b.flag();
        if (s.field_pic) b.flag();
    }
    if (s.idr) s.idr_pic_id = static_cast<int>(b.ue());
    if (sps.poc_type == 0) {
        s.poc_lsb = static_cast<int>(b.u(sps.log2_max_poc_lsb_minus4 + 4));
        if (pps.bottom_field_pic_order_in_frame_present && !s.field_pic) s.delta_poc_bottom = b.se();
    }
    if (sps.poc_type == 1 && !sps.delta_pic_order_always_zero) {
        s.delta_poc[0] = b.se();
        if (pps.bottom_field_pic_order_in_frame_present && !s.field_pic) s.delta_poc[1] = b.se();
    }
    if (pps.redundant_pic_cnt_present) b.ue();
    const bool esB = s.slice_type == 1;
    const bool esP = s.slice_type == 0 || s.slice_type == 3;
    if (esB) b.flag(); // direct_spatial_mv_pred_flag
    int refsL0 = pps.num_ref_idx_l0_default_minus1 + 1, refsL1 = pps.num_ref_idx_l1_default_minus1 + 1;
    if (esP || esB) {
        if (b.flag()) {
            refsL0 = static_cast<int>(b.ue()) + 1;
            if (esB) refsL1 = static_cast<int>(b.ue()) + 1;
        }
    }
    if (refsL0 > 32 || refsL1 > 32) return std::nullopt;
    // ref_pic_list_modification()
    const auto modificacion = [&b] {
        if (!b.flag()) return;
        for (int vueltas = 0; vueltas < 100 && !b.error(); ++vueltas) {
            const std::uint32_t idc = b.ue();
            if (idc == 3) return;
            b.ue(); // abs_diff_pic_num_minus1 o long_term_pic_num
        }
    };
    if (!s.intra()) modificacion();
    if (esB) modificacion();
    // pred_weight_table()
    if ((pps.weighted_pred && esP) || (pps.weighted_bipred_idc == 1 && esB)) {
        const int chromaArrayType = sps.separate_colour_plane ? 0 : sps.chroma_format_idc;
        b.ue();
        if (chromaArrayType != 0) b.ue();
        const auto tabla = [&](int refs) {
            for (int i = 0; i < refs && !b.error(); ++i) {
                if (b.flag()) {
                    b.se();
                    b.se();
                }
                if (chromaArrayType != 0 && b.flag())
                    for (int j = 0; j < 4; ++j)
                        b.se();
            }
        };
        tabla(refsL0);
        if (esB) tabla(refsL1);
    }
    // dec_ref_pic_marking()
    if (nalRefIdc != 0) {
        if (s.idr) {
            b.flag(); // no_output_of_prior_pics_flag
            s.long_term_reference = b.flag();
        } else {
            s.adaptive = b.flag();
            if (s.adaptive) {
                for (int vueltas = 0; vueltas < 66 && !b.error(); ++vueltas) {
                    Mmco m;
                    m.op = static_cast<int>(b.ue());
                    if (m.op == 0) break;
                    if (m.op == 1 || m.op == 3) m.dif_pic_nums_minus1 = static_cast<int>(b.ue());
                    if (m.op == 2) m.long_term_pic_num = static_cast<int>(b.ue());
                    if (m.op == 3 || m.op == 6) m.long_term_frame_idx = static_cast<int>(b.ue());
                    if (m.op == 4) m.max_long_term_frame_idx_plus1 = static_cast<int>(b.ue());
                    if (m.op > 6) return std::nullopt;
                    s.mmco.push_back(m);
                }
            }
        }
    }
    if (b.error()) return std::nullopt;
    return s;
}

// --- DPB ------------------------------------------------------------------------------------

void Dpb::reiniciar() {
    refs_.clear();
    prevPocMsb_ = prevPocLsb_ = 0;
    prevFrameNum_ = prevFrameNumOffset_ = frameNumOffset_ = 0;
    prevMmco5_ = false;
    maxIdxLargo_ = -1;
}

bool Dpb::usa(int superficie) const {
    return std::any_of(refs_.begin(), refs_.end(), [&](const Ref& r) { return r.superficie == superficie; });
}

void Dpb::empezar(const Sps& sps, const Slice& s) {
    s_ = s;
    maxFrameNum_ = sps.maxFrameNum();
    maxRefs_ = std::max(1, sps.max_num_ref_frames);
    if (s.idr) {
        // Las referencias se sueltan al terminar; para el POC, desde cero.
        prevPocMsb_ = prevPocLsb_ = 0;
        prevFrameNumOffset_ = 0;
    } else if (prevMmco5_) {
        prevFrameNumOffset_ = 0;
    }
    // 8.2.4.1: FrameNumWrap de las de corto plazo.
    for (Ref& r : refs_)
        if (!r.largo) r.frame_num_wrap = r.frame_num > s.frame_num ? r.frame_num - maxFrameNum_ : r.frame_num;

    // 8.2.1: el POC.
    if (sps.poc_type == 0) {
        const int maxLsb = 1 << (sps.log2_max_poc_lsb_minus4 + 4);
        int msb = prevPocMsb_;
        if (s.poc_lsb < prevPocLsb_ && prevPocLsb_ - s.poc_lsb >= maxLsb / 2) msb = prevPocMsb_ + maxLsb;
        else if (s.poc_lsb > prevPocLsb_ && s.poc_lsb - prevPocLsb_ > maxLsb / 2) msb = prevPocMsb_ - maxLsb;
        pocArriba_ = msb + s.poc_lsb;
        pocAbajo_ = pocArriba_ + s.delta_poc_bottom;
        if (s.nal_ref_idc != 0) {
            prevPocMsb_ = msb;
            prevPocLsb_ = s.poc_lsb;
        }
    } else {
        if (s.idr) frameNumOffset_ = 0;
        else if (prevFrameNum_ > s.frame_num) frameNumOffset_ = prevFrameNumOffset_ + maxFrameNum_;
        else frameNumOffset_ = prevFrameNumOffset_;
        if (sps.poc_type == 1) {
            const int ciclo = static_cast<int>(sps.offset_for_ref_frame.size());
            int abs = ciclo != 0 ? frameNumOffset_ + s.frame_num : 0;
            if (s.nal_ref_idc == 0 && abs > 0) --abs;
            int esperado = 0;
            if (abs > 0) {
                int deltaCiclo = 0;
                for (int v : sps.offset_for_ref_frame)
                    deltaCiclo += v;
                const int vueltas = (abs - 1) / ciclo, enCiclo = (abs - 1) % ciclo;
                esperado = vueltas * deltaCiclo;
                for (int i = 0; i <= enCiclo; ++i)
                    esperado += sps.offset_for_ref_frame[static_cast<std::size_t>(i)];
            }
            if (s.nal_ref_idc == 0) esperado += sps.offset_for_non_ref_pic;
            pocArriba_ = esperado + s.delta_poc[0];
            pocAbajo_ = pocArriba_ + sps.offset_for_top_to_bottom_field + s.delta_poc[1];
        } else {
            int t = 0;
            if (!s.idr) t = 2 * (frameNumOffset_ + s.frame_num) - (s.nal_ref_idc == 0 ? 1 : 0);
            pocArriba_ = pocAbajo_ = t;
        }
        prevFrameNumOffset_ = frameNumOffset_;
        prevFrameNum_ = s.frame_num;
    }
    prevMmco5_ = false;
}

void Dpb::ventana(int maxRefs) {
    int largos = 0, cortos = 0;
    for (const Ref& r : refs_)
        (r.largo ? largos : cortos)++;
    while (cortos > 0 && cortos + largos >= maxRefs) {
        auto peor = refs_.end();
        for (auto it = refs_.begin(); it != refs_.end(); ++it)
            if (!it->largo && (peor == refs_.end() || it->frame_num_wrap < peor->frame_num_wrap)) peor = it;
        refs_.erase(peor);
        --cortos;
    }
}

void Dpb::mmco(const Mmco& m, int frameNum) {
    const auto quitar = [this](auto pred) { refs_.erase(std::remove_if(refs_.begin(), refs_.end(), pred), refs_.end()); };
    switch (m.op) {
    case 1: {
        const int picNum = frameNum - (m.dif_pic_nums_minus1 + 1);
        quitar([&](const Ref& r) { return !r.largo && r.frame_num_wrap == picNum; });
        break;
    }
    case 2:
        quitar([&](const Ref& r) { return r.largo && r.idx_largo == m.long_term_pic_num; });
        break;
    case 3: {
        const int picNum = frameNum - (m.dif_pic_nums_minus1 + 1);
        quitar([&](const Ref& r) { return r.largo && r.idx_largo == m.long_term_frame_idx; });
        for (Ref& r : refs_)
            if (!r.largo && r.frame_num_wrap == picNum) {
                r.largo = true;
                r.idx_largo = m.long_term_frame_idx;
            }
        break;
    }
    case 4:
        maxIdxLargo_ = m.max_long_term_frame_idx_plus1 - 1;
        quitar([&](const Ref& r) { return r.largo && r.idx_largo > maxIdxLargo_; });
        break;
    case 5:
        refs_.clear();
        maxIdxLargo_ = -1;
        break;
    default:
        break;
    }
}

void Dpb::terminar(int superficie) {
    if (s_.nal_ref_idc == 0) return;
    Ref cur;
    cur.superficie = superficie;
    cur.frame_num = s_.frame_num;
    cur.frame_num_wrap = s_.frame_num;
    cur.poc_arriba = pocArriba_;
    cur.poc_abajo = pocAbajo_;
    if (s_.idr) {
        refs_.clear();
        if (s_.long_term_reference) {
            cur.largo = true;
            cur.idx_largo = 0;
            maxIdxLargo_ = 0;
        } else {
            maxIdxLargo_ = -1;
        }
    } else if (s_.adaptive) {
        bool mmco5 = false;
        for (const Mmco& m : s_.mmco) {
            if (m.op == 6) {
                refs_.erase(std::remove_if(refs_.begin(), refs_.end(),
                                           [&](const Ref& r) { return r.largo && r.idx_largo == m.long_term_frame_idx; }),
                            refs_.end());
                cur.largo = true;
                cur.idx_largo = m.long_term_frame_idx;
            } else {
                mmco(m, s_.frame_num);
                if (m.op == 5) mmco5 = true;
            }
        }
        if (mmco5) {
            // 8.2.1: tras una MMCO 5 el cuadro pasa a tener frame_num 0 y POC relativo.
            const int t = std::min(pocArriba_, pocAbajo_);
            cur.poc_arriba = pocArriba_ - t;
            cur.poc_abajo = pocAbajo_ - t;
            cur.frame_num = cur.frame_num_wrap = 0;
            prevMmco5_ = true;
            prevPocMsb_ = 0;
            prevPocLsb_ = cur.poc_arriba;
            prevFrameNum_ = 0;
        }
    }
    // Sin MMCO, la ventana deslizante (8.2.5.3); con MMCO no hace nada si el flujo cumple (y
    // si no cumple, evita pasarse de max_num_ref_frames).
    if (!s_.idr) ventana(maxRefs_);
    while (refs_.size() >= 16)
        refs_.erase(refs_.begin());
    refs_.push_back(cur);
}

// --- Annex B --------------------------------------------------------------------------------

std::vector<Nal> partirAnnexB(const std::uint8_t* d, std::size_t n) {
    std::vector<Nal> r;
    std::size_t i = 0;
    while (i + 3 <= n) {
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
            Nal x;
            x.arranque = (i > 0 && d[i - 1] == 0) ? i - 1 : i;
            x.inicio = i + 3;
            if (!r.empty()) r.back().largo = x.arranque - r.back().inicio;
            r.push_back(x);
            i += 3;
        } else {
            ++i;
        }
    }
    if (!r.empty()) {
        r.back().largo = n - r.back().inicio;
        // Los ceros del final (trailing_zero_8bits) no son de la NAL.
        while (r.back().largo > 1 && d[r.back().inicio + r.back().largo - 1] == 0)
            --r.back().largo;
    }
    return r;
}

} // namespace ba::gateway::h264
