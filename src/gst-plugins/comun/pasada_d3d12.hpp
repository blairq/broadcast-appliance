// ============================================================================
// src/gst-plugins/comun/pasada_d3d12.hpp — una pasada de shader en Direct3D 12
//
// Lo que necesita un filtro de GStreamer que dibuja su salida con UN shader de
// píxeles leyendo UNA textura de entrada (D3D12Memory → D3D12Memory, sin bajar
// a la memoria de la máquina): el dispositivo, la tubería armada desde el
// bytecode ya compilado (DXC: la firma raíz viene adentro), el anillo de
// constant buffers con los números de cada cuadro, los pools de buffers y el
// dibujo con sus vallas. Es lo mismo que hace ba360d3d12 (vista360.cpp), para
// que un filtro nuevo no lo repita.
//
// La firma raíz que espera (en el .hlsl):
//     CBV(b0), DescriptorTable(SRV(t0)), StaticSampler(s0) — los tres para el
//     shader de píxeles; el de vértices es el triángulo que tapa la pantalla.
//
// Uso (en el GstBaseTransform del filtro):
//     start      → empezar(elemento)
//     stop       → parar()
//     set_context, query de contexto → contexto(), consulta()
//     set_caps   → ponerCaps(entra, sale, vs, ps)
//     propose_allocation / decide_allocation → proponer(), decidir()
//     transform  → dibujar(elemento, in, out, numeros, cuantos)
// ============================================================================
#pragma once

#ifdef _WIN32

#include <gst/d3d12/gstd3d12.h>
#include <gst/video/video.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace ba::d3d12 {

using Microsoft::WRL::ComPtr;

struct Bytecode {
    const void* datos;
    size_t largo;
};

struct Pasada {
    GstD3D12Device* device = nullptr;
    GstVideoInfo entra{}, sale{};
    DXGI_FORMAT formato = DXGI_FORMAT_UNKNOWN; ///< el de la tubería armada
    ComPtr<ID3D12RootSignature> firma;
    ComPtr<ID3D12PipelineState> tuberia;
    ComPtr<ID3D12GraphicsCommandList> lista;
    GstD3D12CmdAllocPool* asignadores = nullptr;
    GstD3D12DescHeapPool* descriptores = nullptr;
    GstD3D12FenceDataPool* pendientes = nullptr;
    guint64 valla = 0; ///< la del último cuadro mandado
    // Los números de cada cuadro: un anillo de constant buffers en memoria de subida,
    // mapeado siempre; cada lugar recuerda la valla del cuadro que lo usó.
    static constexpr int kLugares = 64, kLugarBytes = 1024;
    ComPtr<ID3D12Resource> numeros;
    std::uint8_t* numerosMapa = nullptr;
    guint64 vallaLugar[kLugares] = {};
    int lugar = 0;

    void soltar() {
        if (device != nullptr && valla > 0) gst_d3d12_device_fence_wait(device, D3D12_COMMAND_LIST_TYPE_DIRECT, valla);
        valla = 0;
        if (numeros && numerosMapa != nullptr) numeros->Unmap(0, nullptr);
        numerosMapa = nullptr;
        numeros.Reset();
        std::fill(std::begin(vallaLugar), std::end(vallaLugar), 0);
        lugar = 0;
        lista.Reset();
        tuberia.Reset();
        firma.Reset();
        formato = DXGI_FORMAT_UNKNOWN;
        gst_clear_object(&asignadores);
        gst_clear_object(&descriptores);
        gst_clear_object(&pendientes);
    }

    void parar() {
        soltar();
        gst_clear_object(&device);
    }

    void contexto(GstElement* e, GstContext* c) { gst_d3d12_handle_set_context(e, c, -1, &device); }

    bool consulta(GstElement* e, GstQuery* q) {
        return GST_QUERY_TYPE(q) == GST_QUERY_CONTEXT && gst_d3d12_handle_context_query(e, q, device);
    }

    bool empezar(GstElement* e) {
        if (!gst_d3d12_ensure_element_data(e, -1, &device)) {
            GST_ELEMENT_ERROR(e, RESOURCE, NOT_FOUND, ("no hay un dispositivo de Direct3D 12"), (nullptr));
            return false;
        }
        ID3D12Device* dev = gst_d3d12_device_get_device_handle(device);
        asignadores = gst_d3d12_cmd_alloc_pool_new(dev, D3D12_COMMAND_LIST_TYPE_DIRECT);
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = 1;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        descriptores = gst_d3d12_desc_heap_pool_new(dev, &hd);
        pendientes = gst_d3d12_fence_data_pool_new();
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = kLugares * kLugarBytes;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_UNKNOWN;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                  IID_PPV_ARGS(&numeros));
        const D3D12_RANGE nada{0, 0};
        if (SUCCEEDED(hr)) hr = numeros->Map(0, &nada, reinterpret_cast<void**>(&numerosMapa));
        if (FAILED(hr)) {
            GST_ELEMENT_ERROR(e, RESOURCE, FAILED, ("no se pudo crear el buffer de los números"), ("0x%x", static_cast<unsigned>(hr)));
            return false;
        }
        return true;
    }

    static DXGI_FORMAT formatoDe(GstVideoFormat f) {
        return f == GST_VIDEO_FORMAT_BGRA ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    /// La tubería para el formato de la salida (la firma raíz viene en el bytecode del shader de píxeles).
    bool armar(GstObject* o, Bytecode vs, Bytecode ps, DXGI_FORMAT fmt) {
        if (tuberia && formato == fmt) return true;
        ID3D12Device* dev = gst_d3d12_device_get_device_handle(device);
        HRESULT hr = dev->CreateRootSignature(0, ps.datos, ps.largo, IID_PPV_ARGS(&firma));
        if (FAILED(hr)) {
            GST_ERROR_OBJECT(o, "CreateRootSignature: 0x%x", static_cast<unsigned>(hr));
            return false;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
        d.pRootSignature = firma.Get();
        d.VS = {vs.datos, vs.largo};
        d.PS = {ps.datos, ps.largo};
        auto& rt = d.BlendState.RenderTarget[0];
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        rt.SrcBlend = D3D12_BLEND_ONE;
        rt.DestBlend = D3D12_BLEND_ZERO;
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.LogicOp = D3D12_LOGIC_OP_NOOP;
        d.SampleMask = UINT_MAX;
        d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        d.RasterizerState.DepthClipEnable = TRUE;
        d.DepthStencilState.DepthEnable = FALSE;
        d.DepthStencilState.StencilEnable = FALSE;
        d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets = 1;
        d.RTVFormats[0] = fmt;
        d.SampleDesc.Count = 1;
        hr = dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&tuberia));
        if (FAILED(hr)) {
            GST_ERROR_OBJECT(o, "CreateGraphicsPipelineState: 0x%x", static_cast<unsigned>(hr));
            return false;
        }
        formato = fmt;
        return true;
    }

    bool ponerCaps(GstObject* o, GstCaps* capsEntra, GstCaps* capsSale, Bytecode vs, Bytecode ps) {
        if (!gst_video_info_from_caps(&entra, capsEntra) || !gst_video_info_from_caps(&sale, capsSale)) return false;
        return armar(o, vs, ps, formatoDe(GST_VIDEO_INFO_FORMAT(&sale)));
    }

    /// Un pool de D3D12 de este dispositivo con esos flags (y el tamaño que queda).
    static GstBufferPool* pool(GstD3D12Device* dev, GstCaps* caps, const GstVideoInfo& info, D3D12_RESOURCE_FLAGS flags,
                               GstBufferPool* existente, guint& tamano, guint minimo, guint maximo) {
        GstBufferPool* p = existente;
        if (p != nullptr && (!GST_IS_D3D12_BUFFER_POOL(p) || !gst_d3d12_device_is_equal(GST_D3D12_BUFFER_POOL_CAST(p)->device, dev)))
            gst_clear_object(&p);
        if (p == nullptr) p = gst_d3d12_buffer_pool_new(dev);
        GstStructure* cfg = gst_buffer_pool_get_config(p);
        gst_buffer_pool_config_add_option(cfg, GST_BUFFER_POOL_OPTION_VIDEO_META);
        GstD3D12AllocationParams* ap = gst_buffer_pool_config_get_d3d12_allocation_params(cfg);
        if (ap == nullptr) {
            ap = gst_d3d12_allocation_params_new(dev, &info, GST_D3D12_ALLOCATION_FLAG_DEFAULT, flags, D3D12_HEAP_FLAG_NONE);
        } else {
            gst_d3d12_allocation_params_set_resource_flags(ap, flags);
            gst_d3d12_allocation_params_unset_resource_flags(ap, D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
        }
        gst_buffer_pool_config_set_d3d12_allocation_params(cfg, ap);
        gst_d3d12_allocation_params_free(ap);
        gst_buffer_pool_config_set_params(cfg, caps, std::max<guint>(tamano, static_cast<guint>(info.size)), minimo, maximo);
        if (!gst_buffer_pool_set_config(p, cfg)) {
            gst_object_unref(p);
            return nullptr;
        }
        cfg = gst_buffer_pool_get_config(p);
        gst_buffer_pool_config_get_params(cfg, nullptr, &tamano, nullptr, nullptr);
        gst_structure_free(cfg);
        return p;
    }

    /// propose_allocation, después de llamar a la del padre (que tiene que dar TRUE).
    bool proponer(GstQuery* decidida, GstQuery* q) {
        if (decidida == nullptr) return true;
        GstCaps* caps = nullptr;
        gst_query_parse_allocation(q, &caps, nullptr);
        GstVideoInfo info;
        if (caps == nullptr || !gst_video_info_from_caps(&info, caps)) return false;
        if (gst_query_get_n_allocation_pools(q) == 0) {
            guint tamano = 0;
            GstBufferPool* p = pool(device, caps, info, D3D12_RESOURCE_FLAG_NONE, nullptr, tamano, 0, 0);
            if (p == nullptr) return false;
            gst_query_add_allocation_pool(q, p, tamano, 0, 0);
            gst_object_unref(p);
        }
        gst_query_add_allocation_meta(q, GST_VIDEO_META_API_TYPE, nullptr);
        return true;
    }

    /// decide_allocation, antes de llamar a la del padre.
    bool decidir(GstQuery* q) {
        GstCaps* caps = nullptr;
        gst_query_parse_allocation(q, &caps, nullptr);
        GstVideoInfo info;
        if (caps == nullptr || !gst_video_info_from_caps(&info, caps)) return false;
        GstBufferPool* p = nullptr;
        guint tamano = 0, minimo = 0, maximo = 0;
        const bool hay = gst_query_get_n_allocation_pools(q) > 0;
        if (hay) gst_query_parse_nth_allocation_pool(q, 0, &p, &tamano, &minimo, &maximo);
        // La salida es el destino del dibujo (render target); simultánea, así no hacen
        // falta barreras con los que la leen después.
        p = pool(device, caps, info,
                 static_cast<D3D12_RESOURCE_FLAGS>(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS),
                 p, tamano, minimo, maximo);
        if (p == nullptr) return false;
        if (hay)
            gst_query_set_nth_allocation_pool(q, 0, p, tamano, minimo, maximo);
        else
            gst_query_add_allocation_pool(q, p, tamano, minimo, maximo);
        gst_object_unref(p);
        return true;
    }

    /// Dibuja la salida entera con la tubería, leyendo `in`; `c`: los números del cuadro (hasta kLugarBytes).
    GstFlowReturn dibujar(GstObject* o, GstBuffer* in, GstBuffer* out, const float* c, size_t cuantos) {
        GstD3D12Frame fe, fs;
        if (!gst_d3d12_frame_map(&fe, &entra, in, GST_MAP_READ_D3D12, GST_D3D12_FRAME_MAP_FLAG_SRV)) {
            GST_ERROR_OBJECT(o, "no se pudo mapear la entrada (¿no es D3D12Memory de este dispositivo?)");
            return GST_FLOW_ERROR;
        }
        if (!gst_d3d12_frame_map(&fs, &sale, out, GST_MAP_WRITE_D3D12, GST_D3D12_FRAME_MAP_FLAG_RTV)) {
            gst_d3d12_frame_unmap(&fe);
            GST_ERROR_OBJECT(o, "no se pudo mapear la salida");
            return GST_FLOW_ERROR;
        }
        auto fallar = [&](const char* que, HRESULT hr) {
            GST_ERROR_OBJECT(o, "%s: 0x%x", que, static_cast<unsigned>(hr));
            gst_d3d12_frame_unmap(&fs);
            gst_d3d12_frame_unmap(&fe);
            return GST_FLOW_ERROR;
        };
        ID3D12Device* dev = gst_d3d12_device_get_device_handle(device);
        GstD3D12CmdAlloc* ca = nullptr;
        if (!gst_d3d12_cmd_alloc_pool_acquire(asignadores, &ca)) return fallar("sin asignador de comandos", E_FAIL);
        ID3D12CommandAllocator* asig = gst_d3d12_cmd_alloc_get_handle(ca);
        HRESULT hr = asig->Reset();
        if (SUCCEEDED(hr))
            hr = lista ? lista->Reset(asig, tuberia.Get())
                       : dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, asig, tuberia.Get(), IID_PPV_ARGS(&lista));
        if (FAILED(hr)) {
            gst_d3d12_cmd_alloc_unref(ca);
            return fallar("lista de comandos", hr);
        }
        GstD3D12DescHeap* heap = nullptr;
        if (!gst_d3d12_desc_heap_pool_acquire(descriptores, &heap)) {
            lista->Close();
            gst_d3d12_cmd_alloc_unref(ca);
            return fallar("sin descriptores", E_FAIL);
        }
        ID3D12DescriptorHeap* h = gst_d3d12_desc_heap_get_handle(heap);
        dev->CopyDescriptorsSimple(1, h->GetCPUDescriptorHandleForHeapStart(), fe.srv_desc_handle[0],
                                   D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const int ow = GST_VIDEO_INFO_WIDTH(&sale), oh = GST_VIDEO_INFO_HEIGHT(&sale);
        ID3D12GraphicsCommandList* cl = lista.Get();
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = fs.data[0];
        b.Transition.Subresource = fs.subresource_index[0];
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        cl->ResourceBarrier(1, &b);
        cl->SetGraphicsRootSignature(firma.Get());
        cl->SetDescriptorHeaps(1, &h);
        // Un lugar del anillo que la GPU ya no esté leyendo.
        const int l = lugar;
        lugar = (lugar + 1) % kLugares;
        if (vallaLugar[l] > gst_d3d12_device_get_completed_value(device, D3D12_COMMAND_LIST_TYPE_DIRECT))
            gst_d3d12_device_fence_wait(device, D3D12_COMMAND_LIST_TYPE_DIRECT, vallaLugar[l]);
        std::memcpy(numerosMapa + l * kLugarBytes, c, std::min<size_t>(sizeof(float) * cuantos, kLugarBytes));
        cl->SetGraphicsRootConstantBufferView(0, numeros->GetGPUVirtualAddress() + static_cast<UINT64>(l) * kLugarBytes);
        cl->SetGraphicsRootDescriptorTable(1, h->GetGPUDescriptorHandleForHeapStart());
        cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const D3D12_VIEWPORT vp{0, 0, static_cast<float>(ow), static_cast<float>(oh), 0, 1};
        const D3D12_RECT recorte{0, 0, ow, oh};
        cl->RSSetViewports(1, &vp);
        cl->RSSetScissorRects(1, &recorte);
        cl->OMSetRenderTargets(1, &fs.rtv_desc_handle[0], FALSE, nullptr);
        cl->DrawInstanced(3, 1, 0, 0);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        cl->ResourceBarrier(1, &b);
        hr = cl->Close();
        if (FAILED(hr)) {
            gst_d3d12_desc_heap_unref(heap);
            gst_d3d12_cmd_alloc_unref(ca);
            return fallar("Close", hr);
        }
        // Que la GPU espere lo que estaba escribiendo la entrada (sin frenar la CPU).
        GstD3D12CmdQueue* cola = gst_d3d12_device_get_cmd_queue(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
        gst_d3d12_frame_fence_gpu_wait(&fe, cola);
        ID3D12CommandList* listas[] = {cl};
        hr = gst_d3d12_device_execute_command_lists(device, D3D12_COMMAND_LIST_TYPE_DIRECT, 1, listas, &valla);
        if (FAILED(hr)) {
            gst_d3d12_desc_heap_unref(heap);
            gst_d3d12_cmd_alloc_unref(ca);
            return fallar("ExecuteCommandLists", hr);
        }
        vallaLugar[l] = valla;
        // Lo que usa la GPU vive hasta que termina: el asignador, los descriptores y la entrada.
        GstD3D12FenceData* pend = nullptr;
        gst_d3d12_fence_data_pool_acquire(pendientes, &pend);
        gst_d3d12_fence_data_push(pend, ca, reinterpret_cast<GDestroyNotify>(gst_d3d12_cmd_alloc_unref));
        gst_d3d12_fence_data_push(pend, heap, reinterpret_cast<GDestroyNotify>(gst_d3d12_desc_heap_unref));
        gst_d3d12_fence_data_push(pend, gst_buffer_ref(in), reinterpret_cast<GDestroyNotify>(gst_buffer_unref));
        gst_d3d12_device_set_fence_notify(device, D3D12_COMMAND_LIST_TYPE_DIRECT, valla, pend,
                                          reinterpret_cast<GDestroyNotify>(gst_d3d12_fence_data_unref));
        // El que sigue espera esta valla antes de leer la salida.
        gst_d3d12_buffer_set_fence(out, gst_d3d12_device_get_fence_handle(device, D3D12_COMMAND_LIST_TYPE_DIRECT), valla, FALSE);
        gst_d3d12_frame_unmap(&fs);
        gst_d3d12_frame_unmap(&fe);
        return GST_FLOW_OK;
    }
};

} // namespace ba::d3d12

#endif // _WIN32
