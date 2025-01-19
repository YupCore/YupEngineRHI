// by WzrterFX

Texture2D t_ScreenTexture : register(t0);
SamplerState s_Sampler : register(s0);

// Пиксельный шейдер с эффектом FXAA
void main_ps(
    in float4 i_pos : SV_Position,
    in float2 i_uv : UV,
    out float4 o_color : SV_Target
)
{
    float FXAA_REDUCE_MIN = 1.0 / 128.0;
    float FXAA_REDUCE_MUL = 1.0 / 8.0;
    float FXAA_SPAN_MAX = 8.0;
    
    float width = 0, height = 0;
    t_ScreenTexture.GetDimensions(width, height);

    float2 inv_resolution = 1.0 / float2(width, height);

    // Образцы текстуры в окрестностях
    float3 rgbNW = t_ScreenTexture.Sample(s_Sampler, i_uv + inv_resolution * float2(-1, -1)).rgb;
    float3 rgbNE = t_ScreenTexture.Sample(s_Sampler, i_uv + inv_resolution * float2(1, -1)).rgb;
    float3 rgbSW = t_ScreenTexture.Sample(s_Sampler, i_uv + inv_resolution * float2(-1, 1)).rgb;
    float3 rgbSE = t_ScreenTexture.Sample(s_Sampler, i_uv + inv_resolution * float2(1, 1)).rgb;
    float3 rgbM = t_ScreenTexture.Sample(s_Sampler, i_uv).rgb;

    float3 luma = float3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM = dot(rgbM, luma);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);

    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = min(
        float2(FXAA_SPAN_MAX, FXAA_SPAN_MAX),
        max(
            float2(-FXAA_SPAN_MAX, -FXAA_SPAN_MAX),
            dir * rcpDirMin
        )
    ) * inv_resolution;

    float3 rgbA = 0.5 * (
        t_ScreenTexture.Sample(s_Sampler, i_uv + dir * (1.0 / 3.0 - 0.5)).rgb +
        t_ScreenTexture.Sample(s_Sampler, i_uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    float3 rgbB = rgbA * 0.5 + 0.25 * (
        t_ScreenTexture.Sample(s_Sampler, i_uv + dir * -0.5).rgb +
        t_ScreenTexture.Sample(s_Sampler, i_uv + dir * 0.5).rgb);

    float lumaB = dot(rgbB, luma);
    if ((lumaB < lumaMin) || (lumaB > lumaMax))
    {
        o_color = float4(rgbA, 1.0);
    }
    else
    {
        o_color = float4(rgbB, 1.0);
    }
}
