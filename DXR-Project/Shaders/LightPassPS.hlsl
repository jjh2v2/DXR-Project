#include "PBRCommon.hlsli"

// Resources
struct PSInput
{
	float2 TexCoord : TEXCOORD0;
};

Texture2D<float4>	Albedo					: register(t0, space0);
Texture2D<float4>	Normal					: register(t1, space0);
Texture2D<float4>	Material				: register(t2, space0);
Texture2D<float>	DepthStencil			: register(t3, space0);
Texture2D<float4>	DXRReflection			: register(t4, space0);
TextureCube<float4>	IrradianceMap			: register(t5, space0);
TextureCube<float4> SpecularIrradianceMap	: register(t6, space0);
Texture2D<float4>	IntegrationLUT			: register(t7, space0);
Texture2D<float>	DirLightShadowMaps		: register(t8, space0);
TextureCube<float>	PointLightShadowMaps	: register(t9, space0);

SamplerState GBufferSampler		: register(s0, space0);
SamplerState LUTSampler			: register(s1, space0);
SamplerState IrradianceSampler	: register(s2, space0);

SamplerComparisonState ShadowMapSampler	: register(s3, space0);

ConstantBuffer<Camera>				CameraBuffer		: register(b0, space0);
ConstantBuffer<PointLight>			PointLightBuffer	: register(b1, space0);
ConstantBuffer<DirectionalLight>	DirLightBuffer		: register(b2, space0);

// Light Calculations
float3 CalcRadiance(float3 F0, float3 InNormal, float3 InViewDir, float3 InLightDir, float3 InRadiance, float3 InAlbedo, float InRoughness, float InMetallic)
{
	float3 HalfVector = normalize(InViewDir + InLightDir);
	
	// Cook-Torrance BRDF
	float NDF	= DistributionGGX(InNormal, HalfVector, InRoughness);
	float G		= GeometrySmith(InNormal, InViewDir, InLightDir, InRoughness);
	float3 F	= FresnelSchlick(saturate(dot(HalfVector, InViewDir)), F0);
	
	float	DotNV		= max(dot(InNormal, InViewDir), MIN_VALUE);
	float3	Nominator	= NDF * G * F;
	float	Denominator	= 4.0f * DotNV * max(dot(InNormal, InLightDir), 0.0f);
	float3	Specular	= Nominator / max(Denominator, MIN_VALUE);
		
	// Ks is equal to Fresnel
	float3 Ks = F;
	float3 Kd = 1.0f - Ks;
	Kd *= 1.0f - InMetallic;

	// Scale light by DotNL
	float DotNL = max(dot(InNormal, InLightDir), 0.0f);

	// Add to outgoing radiance Lo
	return (Kd * InAlbedo / PI + Specular) * InRadiance * DotNL;
}

// Shadow Mapping
float CalculateDirLightShadow(float4 LightSpacePosition, float3 InNormal, float3 InLightDir, float MaxShadowBias, float MinShadowBias)
{
	float3 ProjCoords = LightSpacePosition.xyz / LightSpacePosition.w;
	ProjCoords.xy	= (ProjCoords.xy * 0.5f) + 0.5f;
	ProjCoords.y	= 1.0f - ProjCoords.y;
	if (ProjCoords.z >= 1.0f)
	{
		return 1.0f;
	}

	float Depth			= ProjCoords.z;
	float Shadow		= 0.0f;
	float ShadowBias	= max(MaxShadowBias * (1.0f - (dot(InNormal, InLightDir))), MinShadowBias);
	float BiasedDepth	= (Depth - ShadowBias);
	
	[unroll]
	for (int x = -PCF_RANGE; x <= PCF_RANGE; x++)
	{
		[unroll]
		for (int y = -PCF_RANGE; y <= PCF_RANGE; y++)
		{
			Shadow += DirLightShadowMaps.SampleCmpLevelZero(ShadowMapSampler, ProjCoords.xy, BiasedDepth, int2(x, y)).r;
		}
	}

	Shadow /= (PCF_WIDTH * PCF_WIDTH);
	return min(Shadow, 1.0f);
}

static const float3 SampleOffsetDirections[20] =
{
	float3(1.0f, 1.0f,  1.0f),	float3( 1.0f, -1.0f,  1.0f),	float3(-1.0f, -1.0f,  1.0f),	float3(-1.0f, 1.0f,  1.0f),
	float3(1.0f, 1.0f, -1.0f),	float3( 1.0f, -1.0f, -1.0f),	float3(-1.0f, -1.0f, -1.0f),	float3(-1.0f, 1.0f, -1.0f),
	float3(1.0f, 1.0f,  0.0f),	float3( 1.0f, -1.0f,  0.0f),	float3(-1.0f, -1.0f,  0.0f),	float3(-1.0f, 1.0f,  0.0f),
	float3(1.0f, 0.0f,  1.0f),	float3(-1.0f,  0.0f,  1.0f),	float3( 1.0f,  0.0f, -1.0f),	float3(-1.0f, 0.0f, -1.0f),
	float3(0.0f, 1.0f,  1.0f),	float3( 0.0f, -1.0f, 1.0f),		float3( 0.0f, -1.0f, -1.0f),	float3( 0.0f, 1.0f, -1.0f)
};

#define SAMPLES 20

float CalculatePointLightShadow(float3 WorldPosition, float3 LightPosition, float3 InNormal, float MaxShadowBias, float MinShadowBias, float FarPlane)
{
	float3 DirToLight	= WorldPosition - LightPosition;
	float3 LightDir		= normalize(LightPosition - WorldPosition);
	float Depth = length(DirToLight) / FarPlane;

	float ShadowBias = max(MaxShadowBias * (1.0f - (dot(InNormal, LightDir))), MinShadowBias);
	float BiasedDepth	= (Depth - ShadowBias);
	
	float Shadow = 0.0f;
	const float DiskRadius = (1.0f + (Depth)) / FarPlane;
	
	[unroll]
	for (int i = 0; i < SAMPLES; i++)
	{
		Shadow += PointLightShadowMaps.SampleCmpLevelZero(ShadowMapSampler, DirToLight + SampleOffsetDirections[i] * DiskRadius, BiasedDepth);
	}
	
	return Shadow / SAMPLES;
}

// Main
float4 Main(PSInput Input) : SV_TARGET
{
	float2 TexCoord = Input.TexCoord;
	TexCoord.y		= 1.0f - TexCoord.y;
	
	float Depth = DepthStencil.Sample(GBufferSampler, TexCoord).r;
	if (Depth >= 1.0f)
	{
		return float4(0.0f, 0.0f, 0.0f, 1.0f);
	}
	
	float3 WorldPosition		= PositionFromDepth(Depth, TexCoord, CameraBuffer.ViewProjectionInverse);
	float3 SampledAlbedo		= Albedo.Sample(GBufferSampler, TexCoord).rgb;
	//float3 SampledReflection	= DXRReflection.Sample(LUTSampler, TexCoord).rgb;
	float3 SampledMaterial		= Material.Sample(GBufferSampler, TexCoord).rgb;
	float3 SampledNormal		= Normal.Sample(GBufferSampler, TexCoord).rgb;
	
	const float3	Norm		= UnpackNormal(SampledNormal);
	const float3	ViewDir		= normalize(CameraBuffer.Position - WorldPosition);
	const float		Roughness	= SampledMaterial.r;
	const float		Metallic	= SampledMaterial.g;
	const float		SampledAO	= SampledMaterial.b;
	
	float3 F0 = float3(0.04f, 0.04f, 0.04f);
	F0 = lerp(F0, SampledAlbedo, Metallic);

	// Reflectance equation
	float	DotNV	= max(dot(Norm, ViewDir), 0.0f);
	float3	L0		= float3(0.0f, 0.0f, 0.0f);
	
	// PointLight
	{
		// Calculate per-light radiance
		const float3 LightPosition = PointLightBuffer.Position;
		float3	LightDir		= normalize(LightPosition - WorldPosition);
		float3	HalfVec			= normalize(ViewDir + LightDir);
		float	Distance		= length(LightPosition - WorldPosition);
		float	Attenuation		= 1.0f / (Distance * Distance);
		float3	Radiance		= PointLightBuffer.Color * Attenuation;
	
		const float ShadowBias		= PointLightBuffer.ShadowBias;
		const float MaxShadowBias	= PointLightBuffer.MaxShadowBias;
		const float FarPlane		= PointLightBuffer.FarPlane;
		float Shadow = CalculatePointLightShadow(WorldPosition, LightPosition, Norm, MaxShadowBias, ShadowBias, FarPlane);
		L0 += CalcRadiance(F0, Norm, ViewDir, LightDir, Radiance, SampledAlbedo, Roughness, Metallic) * Shadow;
	}
	
	// DirectionalLight
	{
		// Calculate per-light radiance
		float3 LightDir = normalize(-DirLightBuffer.Direction);
		float3 HalfVec	= normalize(ViewDir + LightDir);
		float3 Radiance = DirLightBuffer.Color;
		
		float4 LightSpacePosition	= mul(float4(WorldPosition, 1.0f), DirLightBuffer.LightMatrix);
		const float ShadowBias		= DirLightBuffer.ShadowBias;
		const float MaxShadowBias	= DirLightBuffer.MaxShadowBias;
		float Shadow = CalculateDirLightShadow(LightSpacePosition, Norm, LightDir, MaxShadowBias, ShadowBias);
		L0 += CalcRadiance(F0, Norm, ViewDir, LightDir, Radiance, SampledAlbedo, Roughness, Metallic) * Shadow;
	}
	
	float3 F_IBL	= FresnelSchlickRoughness(DotNV, F0, Roughness);
	float3 Ks_IBL	= F_IBL;
	float3 Kd_IBL	= 1.0f - Ks_IBL;
	Kd_IBL *= 1.0 - Metallic;
	
	float3 Irradiance	= IrradianceMap.Sample(IrradianceSampler, Norm).rgb;
	float3 IBL_Diffuse	= Irradiance * SampledAlbedo * Kd_IBL;
	
	const float MAX_MIPLEVEL = 6.0f;
	float3 Reflection		= reflect(-ViewDir, Norm);
	float3 Prefiltered		= SpecularIrradianceMap.SampleLevel(IrradianceSampler, Reflection, Roughness * MAX_MIPLEVEL).rgb;
	float2 IntegrationBRDF	= IntegrationLUT.Sample(LUTSampler, float2(DotNV, Roughness)).rg;
	float3 IBL_Specular		= Prefiltered * (F_IBL * IntegrationBRDF.x + IntegrationBRDF.y);
	
	float3 Ambient	= (IBL_Diffuse + IBL_Specular) * SampledAO;
	float3 Color	= Ambient + L0;
	
	return float4(ApplyGammaCorrectionAndTonemapping(Color), 1.0f);
}