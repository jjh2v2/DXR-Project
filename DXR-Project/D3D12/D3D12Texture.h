#pragma once
#include "D3D12Resource.h"

struct TextureProperties
{
	std::string				Name;
	DXGI_FORMAT				Format;
	D3D12_RESOURCE_FLAGS	Flags;
	Uint16					Width; 
	Uint16					Height;
	D3D12_RESOURCE_STATES	InitalState;
	EMemoryType				MemoryType;
};

class D3D12RenderTargetView;
class D3D12DepthStencilView;

class D3D12Texture : public D3D12Resource
{
public:
	D3D12Texture(D3D12Device* InDevice);
	~D3D12Texture();

	bool Initialize(const TextureProperties& Properties);

	FORCEINLINE void SetShaderResourceView(std::shared_ptr<D3D12RenderTargetView>& InRenderTargetView)
	{
		RenderTargetView = InRenderTargetView;
	}

	FORCEINLINE std::shared_ptr<D3D12RenderTargetView> GetShaderResourceView() const
	{
		return RenderTargetView;
	}

	FORCEINLINE void SetUnorderedAccessView(std::shared_ptr<D3D12DepthStencilView>& InDepthStencilView)
	{
		DepthStencilView = InDepthStencilView;
	}

	FORCEINLINE std::shared_ptr<D3D12DepthStencilView> GetUnorderedAccessView() const
	{
		return DepthStencilView;
	}

protected:
	std::shared_ptr<D3D12RenderTargetView> RenderTargetView;
	std::shared_ptr<D3D12DepthStencilView> DepthStencilView;
};