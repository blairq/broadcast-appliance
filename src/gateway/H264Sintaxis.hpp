// ============================================================================
// src/gateway/H264Sintaxis.hpp — Lo que hay que leer de un flujo H.264 para decodificarlo
// con la API de video de la placa (D3D11/DXVA: CodecsWinD3d11.cpp), y el estado que lleva
// el decodificador entre cuadros: el orden de presentación (POC) y las referencias (DPB).
//
// La placa decodifica los slices; lo que NO hace es lo de arriba: hay que darle los
// parámetros de cada cuadro (SPS/PPS), qué cuadros guardados son referencia y su POC. Es lo
// que hace por dentro el decodificador H.264 de Media Foundation (msmpeg2vdec), con su
// maquinaria de hilos; acá es una llamada en el hilo que decodifica.
//
// Solo cuadros (frame_mbs_only o slices de cuadro): los campos entrelazados, los grupos de
// slices (FMO) y MVC no van; `soportado()` lo dice y se cae al MFT. Portátil (sin Windows)
// para poder probarlo en cualquier lado.
// ============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ba::gateway::h264 {

/// Quita los bytes de prevención (00 00 03 → 00 00) de una NAL.
std::vector<std::uint8_t> aRbsp(const std::uint8_t* d, std::size_t n);

/// Lector de bits (Exp-Golomb) sobre un RBSP.
class Bits {
   public:
    Bits(const std::uint8_t* d, std::size_t n);
    std::uint32_t u(int n);
    bool flag() { return u(1) != 0; }
    std::uint32_t ue();
    std::int32_t se();
    /// more_rbsp_data(): quedan bits antes del bit de parada.
    bool masDatos() const;
    bool error() const { return error_; }

   private:
    const std::uint8_t* d_;
    std::size_t n_;
    std::size_t pos_ = 0; ///< en bits
    std::size_t fin_ = 0; ///< posición del bit de parada (el último 1)
    bool error_ = false;
};

/// Listas de escalado en el orden del flujo (zigzag), como las quiere DXVA.
struct Escalado {
    std::array<std::array<std::uint8_t, 16>, 6> l4{};
    std::array<std::array<std::uint8_t, 64>, 6> l8{};
    void plano();
};

struct Sps {
    int id = 0;
    int profile_idc = 0, level_idc = 0;
    int chroma_format_idc = 1;
    bool separate_colour_plane = false;
    int bit_depth_luma_minus8 = 0, bit_depth_chroma_minus8 = 0;
    bool qpprime_y_zero_transform_bypass = false;
    bool hayEscalado = false;
    Escalado escalado;
    int log2_max_frame_num_minus4 = 0;
    int poc_type = 0;
    int log2_max_poc_lsb_minus4 = 0;
    bool delta_pic_order_always_zero = false;
    int offset_for_non_ref_pic = 0, offset_for_top_to_bottom_field = 0;
    std::vector<int> offset_for_ref_frame;
    int max_num_ref_frames = 0;
    bool gaps_allowed = false;
    int ancho_mbs = 0, alto_unidades = 0;
    bool frame_mbs_only = true, mb_adaptive_frame_field = false, direct_8x8_inference = false;
    int crop_izq = 0, crop_der = 0, crop_arr = 0, crop_aba = 0; ///< en píxeles
    // Derivados.
    int maxFrameNum() const { return 1 << (log2_max_frame_num_minus4 + 4); }
    int altoMbs() const { return alto_unidades * (frame_mbs_only ? 1 : 2); }
    int anchoCodificado() const { return ancho_mbs * 16; }
    int altoCodificado() const { return altoMbs() * 16; }
    int ancho() const { return anchoCodificado() - crop_izq - crop_der; }
    int alto() const { return altoCodificado() - crop_arr - crop_aba; }
};

struct Pps {
    int id = 0, sps_id = 0;
    bool entropy_coding_mode = false;
    bool bottom_field_pic_order_in_frame_present = false;
    int num_slice_groups_minus1 = 0;
    int num_ref_idx_l0_default_minus1 = 0, num_ref_idx_l1_default_minus1 = 0;
    bool weighted_pred = false;
    int weighted_bipred_idc = 0;
    int pic_init_qp_minus26 = 0, pic_init_qs_minus26 = 0;
    int chroma_qp_index_offset = 0, second_chroma_qp_index_offset = 0;
    bool deblocking_filter_control_present = false, constrained_intra_pred = false, redundant_pic_cnt_present = false;
    bool transform_8x8_mode = false;
    Escalado escalado; ///< ya resuelto (las del SPS si el PPS no trae; planas si ninguno)
};

/// rbsp: sin el byte de cabecera de la NAL.
std::optional<Sps> leerSps(const std::uint8_t* rbsp, std::size_t n);
/// Necesita los SPS (por las listas de escalado y el chroma_format_idc).
std::optional<Pps> leerPps(const std::uint8_t* rbsp, std::size_t n, const Sps* const* spsPorId);

struct Mmco {
    int op = 0;
    int dif_pic_nums_minus1 = 0, long_term_pic_num = 0, long_term_frame_idx = 0, max_long_term_frame_idx_plus1 = 0;
};

struct Slice {
    int nal_ref_idc = 0, nal_type = 0;
    bool idr = false;
    int first_mb = 0, slice_type = 0; ///< slice_type % 5: 0 P, 1 B, 2 I, 3 SP, 4 SI
    int pps_id = 0;
    int frame_num = 0;
    bool field_pic = false;
    int idr_pic_id = 0;
    int poc_lsb = 0, delta_poc_bottom = 0;
    int delta_poc[2] = {0, 0};
    bool long_term_reference = false; ///< IDR
    bool adaptive = false;
    std::vector<Mmco> mmco;
    bool intra() const { return slice_type == 2 || slice_type == 4; }
};

/// rbsp de una NAL de slice (tipo 1 o 5), SIN el byte de cabecera; ref_idc y tipo, del byte.
std::optional<Slice> leerSlice(const std::uint8_t* rbsp, std::size_t n, int nalRefIdc, int nalTipo,
                               const Sps* const* spsPorId, const Pps* const* ppsPorId);

/// Un cuadro guardado en el DPB (solo cuadros, sin campos).
struct Ref {
    int superficie = -1;
    int frame_num = 0;
    int frame_num_wrap = 0;
    bool largo = false;
    int idx_largo = 0;
    int poc_arriba = 0, poc_abajo = 0;
};

/// El estado entre cuadros: POC (8.2.1) y marcado de referencias (8.2.5). Uso, por cuadro:
/// `empezar(sps, slice)` (calcula el POC y las FrameNumWrap), decodificar con `refs()`, y
/// `terminar(superficie)` (marca el cuadro actual y suelta lo que deja de ser referencia).
class Dpb {
   public:
    void reiniciar();
    void empezar(const Sps& sps, const Slice& s);
    void terminar(int superficie);
    const std::vector<Ref>& refs() const { return refs_; }
    int pocArriba() const { return pocArriba_; }
    int pocAbajo() const { return pocAbajo_; }
    /// ¿La superficie la usa alguna referencia?
    bool usa(int superficie) const;

   private:
    void ventana(int maxRefs);
    void mmco(const Mmco& m, int frameNum);

    std::vector<Ref> refs_;
    // Del cuadro en curso.
    Slice s_{};
    int maxFrameNum_ = 16, maxRefs_ = 1, pocArriba_ = 0, pocAbajo_ = 0;
    // POC tipo 0.
    int prevPocMsb_ = 0, prevPocLsb_ = 0;
    // POC tipos 1 y 2.
    int prevFrameNum_ = 0, prevFrameNumOffset_ = 0, frameNumOffset_ = 0;
    bool prevMmco5_ = false;
    int maxIdxLargo_ = -1; ///< -1: «sin índices de largo plazo»
};

/// Las NAL de un cuadro Annex B (00 00 01 / 00 00 00 01): {inicio del código de arranque,
/// inicio de la NAL (el byte de cabecera), largo de la NAL}.
struct Nal {
    std::size_t arranque = 0, inicio = 0, largo = 0;
};
std::vector<Nal> partirAnnexB(const std::uint8_t* d, std::size_t n);

} // namespace ba::gateway::h264
