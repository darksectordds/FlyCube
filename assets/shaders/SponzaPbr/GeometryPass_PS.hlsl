struct VS_OUTPUT
{
    float4 pos       : SV_POSITION;
    float3 fragPos   : POSITION;
    float3 normal    : NORMAL;
    float3 tangent   : TANGENT;
    float2 texCoord  : TEXCOORD;
};

Texture2D albedoMap;
Texture2D normalMap;
Texture2D glossMap;
Texture2D roughnessMap;
Texture2D metalnessMap;
Texture2D aoMap;
Texture2D alphaMap;

SamplerState g_sampler;

cbuffer Settings
{
    bool use_normal_mapping;
    bool use_gloss_instead_of_roughness;
};

float4 getTexture(Texture2D _texture, SamplerState _sample, float2 _tex_coord, bool _need_gamma = false)
{
    float4 _color = _texture.Sample(_sample, _tex_coord);
    if (_need_gamma)
        _color = float4(pow(abs(_color.rgb), 2.2), _color.a);
    return _color;
}

struct PS_OUT
{
    float4 gPosition : SV_Target0;
    float4 gNormal   : SV_Target1;
    float4 gAlbedo   : SV_Target2;
    float4 gMaterial : SV_Target3;
};

float3 CalcBumpedNormal(VS_OUTPUT input)
{
    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent);
    T = normalize(T - dot(T, N) * N);
    float3 B = normalize(cross(T, N));
    float3x3 tbn = float3x3(T, B, N);
    float3 normal = normalMap.Sample(g_sampler, input.texCoord).rgb;
    normal = normalize(2.0 * normal - 1.0);
    normal = normalize(mul(normal, tbn));
    return normal;
}

PS_OUT main(VS_OUTPUT input)
{
    if (getTexture(alphaMap, g_sampler, input.texCoord).r < 0.5)
        discard;

    PS_OUT output;
    output.gPosition = float4(input.fragPos.xyz, 1);

    if (use_normal_mapping)
        output.gNormal.rgb = CalcBumpedNormal(input);
    else
        output.gNormal.rgb = normalize(input.normal);
    output.gNormal.a = 1.0;

    output.gAlbedo = float4(getTexture(albedoMap, g_sampler, input.texCoord, true).rgb, 1.0);
    if (use_gloss_instead_of_roughness)
        output.gMaterial.r = 1.0 - getTexture(glossMap, g_sampler, input.texCoord).r;
    else
        output.gMaterial.r = getTexture(roughnessMap, g_sampler, input.texCoord).r;
    output.gMaterial.g = getTexture(metalnessMap, g_sampler, input.texCoord).r;
    output.gMaterial.b = getTexture(aoMap, g_sampler, input.texCoord).r;
    output.gMaterial.a = 1.0;

    return output;
}