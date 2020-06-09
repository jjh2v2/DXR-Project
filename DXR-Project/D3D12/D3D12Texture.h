#pragma once
#include "D3D12DeviceChild.h"

#include "Types.h"

class D3D12Texture : public D3D12DeviceChild
{
public:
	D3D12Texture(D3D12Device* Device);
	~D3D12Texture();

	bool Init(DXGI_FORMAT Format, D3D12_RESOURCE_FLAGS Flags, Uint16 Width, Uint16 Height, D3D12_HEAP_PROPERTIES HeapProperties);

	ID3D12Resource1* GetResource() const
	{
		return Texture.Get();
	}

private:
	Microsoft::WRL::ComPtr<ID3D12Resource1> Texture;
};