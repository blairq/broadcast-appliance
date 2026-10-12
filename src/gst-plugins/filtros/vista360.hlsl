// src/gst-plugins/filtros/vista360.hlsl — unir y enderezar cámaras en D3D12 (ba360d3d12)
//
// La misma cuenta que el shader de GL y que la de la CPU (vista360.cpp: ver ahí
// qué es cada número de c[]): equirectangular, ojos de pez o varias cámaras lado
// a lado (cada una con su lente y su montaje); la vista, la tira, el panorama o
// la entrada; y la guía para calibrar.
//
// Se compila con DXC a DXIL firmado y va adentro del plugin como bytes
// (vista360_d3d12.h): en Windows no hace falta ningún compilador de shaders.
// Para regenerarlo: tools/shaders/compilar-vista360.sh
//
// Un triángulo que tapa la pantalla (sin vértices: SV_VertexID); la firma raíz va
// en el mismo bytecode (la lee CreateRootSignature). Los 48 vec4 llegan en un
// constant buffer (un lugar del anillo de ba360d3d12).

#define RS "CBV(b0, visibility=SHADER_VISIBILITY_PIXEL), " \
           "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), " \
           "StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU=TEXTURE_ADDRESS_CLAMP, " \
           "addressV=TEXTURE_ADDRESS_CLAMP, addressW=TEXTURE_ADDRESS_CLAMP, visibility=SHADER_VISIBILITY_PIXEL)"

cbuffer Vista : register(b0) {
    float4 c[48];
};
Texture2D tex : register(t0);
SamplerState muestreo : register(s0);

static const float PI = 3.14159265;

struct Vert {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[RootSignature(RS)]
Vert VSMain(uint id : SV_VertexID) {
    Vert o;
    const float2 uv = float2((id << 1) & 2, id & 2); // (0,0) (2,0) (0,2)
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = uv; // v = 0 arriba, como la imagen
    return o;
}

float3 rot(float4 a, float4 b, float4 d, float3 v) { return float3(dot(a.xyz, v), dot(b.xyz, v), dot(d.xyz, v)); }
float modulo(float x, float y) { return x - y * floor(x / y); }
float linea(float d, float g) { return saturate(g * 0.5 + 0.5 - abs(d)); }
float4 leer(float2 uv) { return tex.SampleLevel(muestreo, uv, 0); }

// De dónde leer en la lente i (x, y) y su peso (z).
float3 lente(float3 dir, int i) {
    const float4 r0 = c[8 + 5 * i];
    const float4 r1 = c[9 + 5 * i];
    const float4 r2 = c[10 + 5 * i];
    const float4 p = c[11 + 5 * i];
    const float4 e = c[12 + 5 * i];
    const float3 d = rot(r0, r1, r2, dir);
    const float n = c[3].w;
    const float ha = c[3].y / n;
    float xh, yh, w;
    if (e.x > 0.5) {
        if (d.z < 1e-4) return float3(0.0, 0.0, 0.0);
        const float2 u = d.xy / d.z;
        const float r2_ = dot(u, u);
        const float k = 1.0 + p.y * r2_ + p.z * r2_ * r2_;
        // Donde la distorsión deja de crecer, se pliega: lo de afuera caería adentro.
        if (1.0 + 3.0 * p.y * r2_ + 5.0 * p.z * r2_ * r2_ <= 0.0) return float3(0.0, 0.0, 0.0);
        xh = r0.w + e.y * u.x * k;
        yh = r1.w - e.y * ha * u.y * k;
        const float borde = min(min(xh * ha, (1.0 - xh) * ha), min(yh, 1.0 - yh));
        w = saturate(borde / max(c[3].z * e.y * ha, 1e-4));
    } else {
        const float th = acos(clamp(d.z, -1.0, 1.0));
        const float t2 = th * th;
        if (1.0 + 3.0 * p.y * t2 + 5.0 * p.z * t2 * t2 <= 0.0) return float3(0.0, 0.0, 0.0);
        const float r = th / p.x * (1.0 + p.y * t2 + p.z * t2 * t2) * p.w;
        const float rho = length(d.xy);
        const float2 f = rho > 1e-6 ? d.xy / rho : float2(1.0, 0.0);
        xh = r0.w + r * r2.w * f.x / ha;
        yh = r1.w - r * r2.w * f.y;
        w = saturate((p.x - th) / max(c[3].z, 1e-4));
    }
    if (xh < 0.0 || xh > 1.0 || yh < 0.0 || yh > 1.0) w = 0.0;
    return float3((float(i) + xh) / n, yh, w);
}

[RootSignature(RS)]
float4 PSMain(Vert i) : SV_Target {
    const float2 uv = i.uv;
    const float ver = c[3].x;
    float3 dir = float3(0.0, 0.0, 1.0);
    const bool esfera = ver < 1.5 || ver > 2.5;
    if (ver < 0.5) {
        float2 p = uv * 2.0 - 1.0;
        p.y = -p.y;
        dir = rot(c[0], c[1], c[2], normalize(float3(p.x * c[1].w * c[0].w, p.y * c[1].w, 1.0)));
    } else if (ver < 1.5) {
        const float lo = (uv.x - 0.5) * 2.0 * PI;
        const float la = (0.5 - uv.y) * PI;
        dir = float3(cos(la) * sin(lo), sin(la), cos(la) * cos(lo));
    } else if (ver > 2.5) {
        const float lo = (uv.x * 2.0 - 1.0) * c[7].x;
        const float la = atan((0.5 - uv.y) * 2.0 * c[7].x / c[0].w);
        dir = rot(c[0], c[1], c[2], float3(cos(la) * sin(lo), sin(la), cos(la) * cos(lo)));
    }
    float4 col;
    if (!esfera) {
        col = leer(uv);
    } else if (c[2].w < 0.5) {
        col = leer(float2(atan2(dir.x, dir.z) / (2.0 * PI) + 0.5, 0.5 - asin(clamp(dir.y, -1.0, 1.0)) / PI));
    } else {
        float4 acc = float4(0.0, 0.0, 0.0, 0.0);
        float s = 0.0;
        [loop] for (int k = 0; k < 8; k++) {
            if (float(k) >= c[3].w) break;
            const float3 m = lente(dir, k);
            if (m.z > 0.0) {
                acc += leer(m.xy) * m.z;
                s += m.z;
            }
        }
        col = s > 0.0 ? acc / s : float4(0.0, 0.0, 0.0, 1.0);
    }
    const float t = c[4].x;
    const float g = c[5].y;
    const float2 S = c[5].zw;
    const float2 q = uv * S;
    const float2 ce = c[4].yz * S;
    const float2 h = float2(c[4].w, c[5].x) * S * 0.5;
    float al = 0.0;
    if (t > 0.5 && t < 1.5) {
        const float2 k = abs(q - ce) - h;
        al = linea(length(max(k, 0.0)) + min(max(k.x, k.y), 0.0), g);
    } else if (t > 1.5 && t < 2.5) {
        const float2 e = max(h, float2(1.0, 1.0));
        al = linea((length((q - ce) / e) - 1.0) * min(e.x, e.y), g);
    } else if (t > 2.5 && t < 3.5) {
        const float2 pa = max(h * 2.0, float2(4.0, 4.0));
        al = max(linea(modulo(q.x - ce.x + pa.x * 0.5, pa.x) - pa.x * 0.5, g),
                 linea(modulo(q.y - ce.y + pa.y * 0.5, pa.y) - pa.y * 0.5, g));
    } else if (t > 3.5 && t < 4.5) {
        al = max(linea(q.x - ce.x, g), linea(q.y - ce.y, g));
    } else if (t > 4.5 && esfera) {
        const float porPx = ver < 0.5 ? 2.0 * atan(c[1].w) / S.y : (ver > 2.5 ? 2.0 * c[7].x / S.x : PI / S.y);
        const float lo = atan2(dir.x, dir.z);
        const float la = asin(clamp(dir.y, -1.0, 1.0));
        const float pa = radians(15.0);
        al = max(linea((modulo(la + pa * 0.5, pa) - pa * 0.5) / porPx, g),
                 linea((modulo(lo + pa * 0.5, pa) - pa * 0.5) * cos(la) / porPx, g));
    }
    if (!esfera && c[2].w > 0.5) {
        const float n = c[3].w;
        [loop] for (int k = 0; k < 8; k++) {
            if (float(k) >= n) break;
            const float4 r0 = c[8 + 5 * k];
            const float4 r1 = c[9 + 5 * k];
            const float4 r2 = c[10 + 5 * k];
            if (c[12 + 5 * k].x < 0.5) {
                const float2 d = float2((uv.x - (float(k) + r0.w) / n) * c[3].y, uv.y - r1.w) * S.y;
                al = max(al, linea(length(d) - r2.w * S.y, g));
            }
            if (k > 0) al = max(al, linea((uv.x - float(k) / n) * S.x, g));
        }
    }
    return lerp(col, float4(c[6].rgb, 1.0), al * c[6].a);
}
