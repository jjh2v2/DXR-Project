/*
* Common Constants
*/

static const float MIN_ROUGHNESS	= 0.05f;
static const float MAX_ROUGHNESS	= 1.0f;
static const float PI				= 3.14159265359f;
static const float MIN_VALUE		= 0.0000001f;
static const float EPSILON			= 0.0001f;
static const float RAY_OFFSET		= 0.2f;

static const float3 LightPosition	= float3(0.0f, 10.0f, -10.0f);
static const float3 LightColor		= float3(400.0f, 400.0f, 400.0f);

/*
* Common Defines
*/

#define GAMMA		2.2f
#define PCF_RANGE	2
#define PCF_WIDTH	float((PCF_RANGE * 2) + 1)

/*
* Common Structs
*/

struct Camera
{
	float4x4 ViewProjection;
	float4x4 View;
	float4x4 ViewInverse;
	float4x4 Projection;
	float4x4 ProjectionInverse;
	float4x4 ViewProjectionInverse;
	float3	Position;
	float	NearPlane;
	float	FarPlane;
	float	AspectRatio;
};

struct PointLight
{
	float3	Color;
	float	ShadowBias;
	float3	Position;
	float	FarPlane;
	float	MaxShadowBias;
};

struct DirectionalLight
{
	float3		Color;
	float		ShadowBias;
	float3		Direction;
	float		MaxShadowBias;
	float4x4	LightMatrix;
};

struct Vertex
{
	float3 Position;
	float3 Normal;
	float3 Tangent;
	float2 TexCoord;
};

struct Transform
{
	float4x4 Transform;
	float4x4 TransformInv;
};

struct Material
{
	float3	Albedo;
	float	Roughness;
	float	Metallic;
	float	AO;
	int		EnableHeight;
};

struct ComputeShaderInput
{
	uint3	GroupID				: SV_GroupID;
	uint3	GroupThreadID		: SV_GroupThreadID;
	uint3	DispatchThreadID	: SV_DispatchThreadID;
	uint	GroupIndex			: SV_GroupIndex;
};

/*
* Position Helper
*/

float3 PositionFromDepth(float Depth, float2 TexCoord, float4x4 ProjectionInverse)
{
	float z = Depth;
	float x = TexCoord.x * 2.0f - 1.0f;
	float y = (1.0f - TexCoord.y) * 2.0f - 1.0f;

	float4 ProjectedPos		= float4(x, y, z, 1.0f);
	float4 FinalPosition	= mul(ProjectedPos, ProjectionInverse);
	
	return FinalPosition.xyz / FinalPosition.w;
}

/*
* Misc Helpers
*/

float2 ToFloat2(float Single)
{
	return float2(Single, Single);
}

float3 ToFloat3(float Single)
{
	return float3(Single, Single, Single);
}

float4 ToFloat4(float Single)
{
	return float4(Single, Single, Single, Single);
}

float CalculateLuminance(float3 Color)
{
	return sqrt(dot(Color, float3(0.299f, 0.587f, 0.114f)));
}

/*
* Random numbers
*/

float Random(float3 Seed, int i)
{
	float4	Seed4		= float4(Seed, i);
	float	DotProduct	= dot(Seed4, float4(12.9898f, 78.233f, 45.164f, 94.673f));
	return frac(sin(DotProduct) * 43758.5453f);
}

float Linstep(float Low, float High, float P)
{
	return saturate((P - Low) / (High - Low));
}

float Lerp(float A, float B, float AmountOfA)
{
	return (-AmountOfA * B) + ((A * AmountOfA) + B);
}

float3 Lerp(float3 A, float3 B, float AmountOfA)
{
	return (ToFloat3(-AmountOfA) * B) + ((A * ToFloat3(AmountOfA)) + B);
}

/*
* PBR Functions
*/

float DistributionGGX(float3 N, float3 H, float Roughness)
{
	float A			= Roughness * Roughness;
	float A2		= A * A;
	float NdotH		= max(dot(N, H), 0.0f);
	float NdotH2	= NdotH * NdotH;

	float Nom	= A2;
	float Denom = (NdotH2 * (A2 - 1.0f) + 1.0f);
	Denom = PI * Denom * Denom;

	return Nom / max(Denom, MIN_VALUE);
}

float GeometrySchlickGGX(float NdotV, float Roughness)
{
	float R = (Roughness + 1.0f);
	float K = (R * R) / 8.0f;

	return NdotV / (NdotV * (1.0f - K) + K);
}

float GeometrySchlickGGX_IBL(float NdotV, float Roughness)
{
	float K = (Roughness * Roughness) / 2.0f;
	return NdotV / (NdotV * (1.0f - K) + K);
}

float GeometrySmith(float3 N, float3 V, float3 L, float Roughness)
{
	float NdotV = max(dot(N, V), 0.0f);
	float NdotL = max(dot(N, L), 0.0f);

	return GeometrySchlickGGX(NdotV, Roughness) * GeometrySchlickGGX(NdotL, Roughness);
}

float GeometrySmith_IBL(float3 N, float3 V, float3 L, float Roughness)
{
	float NdotV = max(dot(N, V), MIN_VALUE);
	float NdotL = max(dot(N, L), MIN_VALUE);

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

/*
* http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
* efficient VanDerCorpus calculation.
*/

float RadicalInverse_VdC(uint Bits)
{
	Bits = (Bits << 16u) | (Bits >> 16u);
	Bits = ((Bits & 0x55555555u) << 1u) | ((Bits & 0xAAAAAAAAu) >> 1u);
	Bits = ((Bits & 0x33333333u) << 2u) | ((Bits & 0xCCCCCCCCu) >> 2u);
	Bits = ((Bits & 0x0F0F0F0Fu) << 4u) | ((Bits & 0xF0F0F0F0u) >> 4u);
	Bits = ((Bits & 0x00FF00FFu) << 8u) | ((Bits & 0xFF00FF00u) >> 8u);
	return float(Bits) * 2.3283064365386963e-10; // 0x100000000
}

float2 Hammersley(uint I, uint N)
{
	return float2(float(I) / float(N), RadicalInverse_VdC(I));
}

float3 ImportanceSampleGGX(float2 Xi, float3 N, float Roughness)
{
	float A = Roughness * Roughness;
	
	float Phi		= 2.0f * PI * Xi.x;
	float CosTheta	= sqrt((1.0f - Xi.y) / (1.0f + (A * A - 1.0f) * Xi.y));
	float SinTheta	= sqrt(1.0f - CosTheta * CosTheta);
	
	// From spherical coordinates to cartesian coordinates
	float3 H;
	H.x = cos(Phi) * SinTheta;
	H.y = sin(Phi) * SinTheta;
	H.z = CosTheta;
	
	// From tangent-space vector to world-space sample vector
	float3 Up			= abs(N.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
	float3 Tangent		= normalize(cross(Up, N));
	float3 Bitangent	= cross(N, Tangent);
	
	float3 SampleVec = Tangent * H.x + Bitangent * H.y + N * H.z;
	return normalize(SampleVec);
}

/*
* HDR Helpers
*/

float3 ApplyGamma(float3 InputColor)
{
	return pow(InputColor, ToFloat3(GAMMA));
}

float3 ApplyGammaInv(float3 InputColor)
{
	return pow(InputColor, ToFloat3(1.0f / GAMMA));
}

float3 ApplyGammaCorrectionAndTonemapping(float3 InputColor)
{
	const float INTENSITY	= 0.75f;
		
	// Gamma correct
	float3 FinalColor	= InputColor;
	FinalColor			= FinalColor / (FinalColor + ToFloat3(INTENSITY));
	FinalColor			= ApplyGammaInv(FinalColor);
	return FinalColor;
}

float3 ApplyNormalMapping(float3 MappedNormal, float3 Normal, float3 Tangent, float3 Bitangent)
{
	float3x3 TBN = float3x3(Tangent, Bitangent, Normal);
	return normalize(mul(MappedNormal, TBN));
}

float3 UnpackNormal(float3 SampledNormal)
{
	return normalize((SampledNormal * 2.0f) - 1.0f);
}

float3 PackNormal(float3 Normal)
{
	return (normalize(Normal) + 1.0f) * 0.5f;
}

/*
* RayTracing Helpers
*/

float3 WorldHitPosition()
{
	return WorldRayOrigin() + (RayTCurrent() * WorldRayDirection());
}