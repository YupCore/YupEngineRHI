// by YupCore & yabadabu(https://github.com/yabadabu)
// https://en.wikipedia.org/wiki/Rec._709
Texture2D txYUV : register(t0);
SamplerState samLinear : register(s0);

float3 YUVToRGB(float3 yuv)
{
    // BT.709 coefficients
    static const float3 yuvCoef_r = { 1.164f, 0.000f, 1.793f };
    static const float3 yuvCoef_g = { 1.164f, -0.213f, -0.533f };
    static const float3 yuvCoef_b = { 1.164f, 2.112f, 0.000f };
    // Adjust input YUV values: subtract BT.709 offsets
    yuv -= float3(0.0625f, 0.5f, 0.5f);
    // Perform YUV to RGB conversion
    return saturate(float3(
        dot(yuv, yuvCoef_r),
        dot(yuv, yuvCoef_g),
        dot(yuv, yuvCoef_b)
    ));
}

// useful to reduce whitness
float3 GammaCorrect(float3 color, float gamma)
{
    // Inverse gamma correction: raise each color channel to the power of gamma
    return pow(color, gamma);
}

void main_ps(
    in float4 i_pos : SV_Position,
    in float2 i_uv : UV,
    out float4 o_color : SV_Target
)
{
    // Y plane is in top 2/3 of texture
    float y = txYUV.Sample(samLinear, float2(i_uv.x, i_uv.y * (2.0 / 3.0))).r;
    
    // U plane starts at 2/3 height
    float u = txYUV.Sample(samLinear, float2(i_uv.x * 0.5, (2.0 / 3.0) + i_uv.y * (1.0 / 6.0))).r;
    
    // V plane starts at 5/6 height
    float v = txYUV.Sample(samLinear, float2(i_uv.x * 0.5, (5.0 / 6.0) + i_uv.y * (1.0 / 6.0))).r;
    
    // perform yuv to rgb, then inverse gamma correct
    o_color = float4(GammaCorrect(YUVToRGB(float3(y, u, v)), 2.2f /*SRGB constant*/), 1.0f);
}