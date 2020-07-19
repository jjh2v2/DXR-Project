
// Common Constants
static const float MIN_ROUGHNESS    = 0.025f;
static const float PI               = 3.14159265359f;
static const float MIN_VALUE        = 0.00000000001f;
static const float RAY_OFFSET       = 0.01f;

static const float3 LightPosition   = float3(0.0f, 10.0f, -10.0f);
static const float3 LightColor      = float3(300.0f, 300.0f, 300.0f);

// Common Structs
struct Camera
{
    float4x4    ViewProjection;
    float4x4    ViewProjectionInverse;
    float3      Position;
};

struct Vertex
{
    float3 Position;
    float3 Normal;
    float3 Tangent;
    float2 TexCoord;
};

// Position Helper
float3 PositionFromDepth(float Depth, float2 TexCoord, float4x4 ViewProjectionInverse)
{
    float Z = Depth;
    float X = TexCoord.x * 2.0f - 1.0f;
    float Y = (1 - TexCoord.y) * 2.0f - 1.0f;

    float4 ProjectedPos     = float4(X, Y, Z, 1.0f);
    float4 WorldPosition    = mul(ProjectedPos, ViewProjectionInverse);
    
    return WorldPosition.xyz / WorldPosition.w;
}

// PBR Functions
float DistributionGGX(float3 N, float3 H, float Roughness)
{
    float A         = Roughness * Roughness;
    float A2        = A * A;
    float NdotH     = max(dot(N, H), 0.0f);
    float NdotH2    = NdotH * NdotH;

    float Nom   = A2;
    float Denom = (NdotH2 * (A2 - 1.0f) + 1.0f);
    Denom = PI * Denom * Denom;

    return Nom / max(Denom, MIN_VALUE);
}

float GeometrySchlickGGX(float NdotV, float Roughness)
{
    float R = (Roughness + 1.0f);
    float K = (R * R) / 8.0f;

    return NdotV / ((NdotV * (1.0f - K)) + K);
}

float GeometrySchlickGGX_IBL(float NdotV, float Roughness)
{
    float R = Roughness;
    float K = (R * R) / 2.0f;

    return NdotV / ((NdotV * (1.0f - K)) + K);
}

float GeometrySmith(float3 N, float3 V, float3 L, float Roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);

    return GeometrySchlickGGX(NdotV, Roughness) * GeometrySchlickGGX(NdotL, Roughness);
}

float GeometrySmith_IBL(float3 N, float3 V, float3 L, float Roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);

    return GeometrySchlickGGX_IBL(NdotV, Roughness) * GeometrySchlickGGX_IBL(NdotL, Roughness);
}

float3 FresnelSchlick(float CosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - CosTheta, 5.0f);
}

float3 FresnelSchlickRoughness(float CosTheta, float3 F0, float Roughness)
{
    float R = 1.0f - Roughness;
    return F0 + (max(float3(R, R, R), F0) - F0) * pow(1.0f - CosTheta, 5.0f);
}