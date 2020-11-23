#pragma once
#include "D3D12/D3D12Buffer.h"
#include "D3D12/D3D12Texture.h"
#include "D3D12/D3D12DescriptorHeap.h"

/*
* MaterialProperties
*/

struct MaterialProperties
{
	XMFLOAT3 Albedo			= XMFLOAT3(1.0f, 1.0f, 1.0f);
	Float32 Roughness		= 0.0f;
	Float32 Metallic		= 0.0f;
	Float32 AO				= 0.5f;
	Int32	EnableHeight	= 0;
};

/*
* Material
*/

class Material
{
public:
	Material(const MaterialProperties& InProperties);
	~Material();

	void Initialize();

	void BuildBuffer(class D3D12CommandList* CommandList);

	FORCEINLINE bool IsBufferDirty() const
	{
		return MaterialBufferIsDirty;
	}

	void SetAlbedo(const XMFLOAT3& Albedo);
	void SetAlbedo(Float32 R, Float32 G, Float32 B);

	void SetMetallic(Float32 Metallic);
	void SetRoughness(Float32 Roughness);
	void SetAmbientOcclusion(Float32 AO);

	void EnableHeightMap(bool EnableHeightMap);

	void SetDebugName(const std::string& InDebugName);

	FORCEINLINE bool HasAlphaMask() const
	{
		return AlphaMask != nullptr;
	}

	FORCEINLINE TSharedPtr<D3D12DescriptorTable> GetDescriptorTable() const
	{
		return DescriptorTable;
	}

	FORCEINLINE const MaterialProperties& GetMaterialProperties() const 
	{
		return Properties;
	}

public:
	TSharedPtr<D3D12Texture> AlbedoMap;
	TSharedPtr<D3D12Texture> NormalMap;
	TSharedPtr<D3D12Texture> RoughnessMap;
	TSharedPtr<D3D12Texture> HeightMap;
	TSharedPtr<D3D12Texture> AOMap;
	TSharedPtr<D3D12Texture> MetallicMap;
	TSharedPtr<D3D12Texture> AlphaMask;

private:
	std::string			DebugName;
	MaterialProperties	Properties;
	D3D12Buffer*		MaterialBuffer	= nullptr;
	TSharedPtr<D3D12DescriptorTable> DescriptorTable;

	bool MaterialBufferIsDirty = true;
};