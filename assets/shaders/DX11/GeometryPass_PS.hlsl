struct VS_OUTPUT
{
    float4 pos: SV_POSITION;
    float3 fragPos : POSITION;
    float3 normal : NORMAL;
    float2 texCoord: TEXCOORD;
    float3 tangent: TANGENT;
    float3 bitangent: BITANGENT;
};

Texture2D diffuseMap : register(t0);
Texture2D normalMap : register(t1);
Texture2D specularMap : register(t2);
Texture2D glossMap : register(t3);
Texture2D ambientMap : register(t4);

SamplerState g_sampler : register(s0);

#define USE_CAMMA_RT
#define USE_CAMMA_TEX

float4 getTexture(Texture2D _texture, SamplerState _sample, float2 _tex_coord, bool _need_gamma = false)
{
    float4 _color = _texture.Sample(_sample, _tex_coord);
#ifdef USE_CAMMA_TEX
    if (_need_gamma)
        _color = float4(pow(abs(_color.rgb), 2.2), _color.a);
#endif
    return _color;
}


struct PS_OUT
{   float4 gPosition : SV_Target0;
    float3 gNormal   : SV_Target1;
    float3 gAmbient  : SV_Target2; 
    float3 gDiffuse  : SV_Target3;
    float3 gSpecular : SV_Target4;
    float3 gGloss    : SV_Target5;
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
    PS_OUT output;
    output.gPosition = float4(input.fragPos.xyz, input.pos.z);
    output.gNormal = CalcBumpedNormal(input);
    output.gAmbient = getTexture(ambientMap, g_sampler, input.texCoord, true).rgb;
    output.gDiffuse = getTexture(diffuseMap, g_sampler, input.texCoord, true).rgb;
    output.gSpecular = getTexture(specularMap, g_sampler, input.texCoord, true).rgb;
    output.gGloss = getTexture(glossMap, g_sampler, input.texCoord, true).rgb;
    return output;
}
