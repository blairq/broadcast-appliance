// src/gst-plugins/filtros/sombra.hlsl — sombra y borde difuso en D3D12 (basombrad3d12)
//
// La misma cuenta que el shader de GL y que la de la CPU (sombra.cpp: ver ahí
// qué es cada número de c[]): la imagen, con su borde difuso, encima de su
// sombra (la silueta del alfa, difuminada con una grilla gaussiana, agrandada
// y corrida).
//
// Se compila con DXC a DXIL firmado y va adentro del plugin como bytes
// (sombra_d3d12.h): en Windows no hace falta ningún compilador de shaders.
// Para regenerarlo: tools/shaders/compilar-hlsl.sh sombra Sombra
//
// Un triángulo que tapa la pantalla (sin vértices: SV_VertexID); la firma raíz va
// en el mismo bytecode (la lee CreateRootSignature). Los 8 vec4 llegan en un
// constant buffer (ba::d3d12::Pasada).

#define RS "CBV(b0, visibility=SHADER_VISIBILITY_PIXEL), " \
           "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), " \
           "StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU=TEXTURE_ADDRESS_CLAMP, " \
           "addressV=TEXTURE_ADDRESS_CLAMP, addressW=TEXTURE_ADDRESS_CLAMP, visibility=SHADER_VISIBILITY_PIXEL)"

cbuffer Sombra : register(b0) {
    float4 c[8];
};
Texture2D tex : register(t0);
SamplerState muestreo : register(s0);

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

// Cuánto queda del punto t (píxeles de la entrada): adentro de la imagen y el borde difuso.
float dentro(float2 t) {
    const float2 I = c[0].zw;
    const float2 d = min(t, I - t);
    float a = saturate(min(d.x, d.y) + 0.5);
    const float2 dc = d / c[1].zw;
    if (c[5].x > 0.0) a *= smoothstep(0.0, c[5].x, min(dc.x, dc.y));
    if (c[5].z > 0.0) {
        const float2 k = max(float2(c[5].z, c[5].z) - dc, float2(0.0, 0.0));
        a *= saturate(c[5].z - length(k) + 0.5);
    }
    return a;
}

float4 leer(float2 t) { return tex.SampleLevel(muestreo, t / c[0].zw, 0); }

[RootSignature(RS)]
float4 PSMain(Vert i) : SV_Target {
    const float2 I = c[0].zw;
    const float2 q = i.uv * c[0].xy - c[1].xy;
    const float4 col = leer(q);
    const float a = col.a * dentro(q);
    float s = 0.0;
    if (c[4].a > 0.0) {
        const float2 C = I * 0.5;
        const float2 base = q - c[2].xy - C;
        const float k = c[3].z;
        [loop] for (int j = -8; j <= 8; j++) {
            if (abs(float(j)) > k) continue;
            const float wj = exp(-float(j * j) * c[3].w);
            [loop] for (int n = -8; n <= 8; n++) {
                if (abs(float(n)) > k) continue;
                const float wi = exp(-float(n * n) * c[3].w);
                const float2 t = C + (base - float2(float(n), float(j)) * c[3].xy) * c[2].zw;
                s += wi * wj * leer(t).a * dentro(t);
            }
        }
        s = s / (c[5].y * c[5].y) * c[4].a;
    }
    const float ra = a + s * (1.0 - a);
    const float3 rgb = ra > 0.0 ? (col.rgb * a + c[4].rgb * s * (1.0 - a)) / ra : float3(0.0, 0.0, 0.0);
    return float4(rgb, ra);
}
