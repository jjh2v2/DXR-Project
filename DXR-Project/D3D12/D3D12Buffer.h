#pragma once
#include "D3D12Resource.h"

struct BufferProperties
{
	std::string				Name;
	D3D12_RESOURCE_FLAGS	Flags;
	Uint64					SizeInBytes;
	D3D12_RESOURCE_STATES	InitalState;
	EMemoryType				MemoryType;
};

class D3D12ConstantBufferView;

class D3D12Buffer : public D3D12Resource
{
public:
	D3D12Buffer(D3D12Device* InDevice);
	~D3D12Buffer();

	bool Initialize(const BufferProperties& Properties);

	void* Map();
	void Unmap();

	FORCEINLINE void SetConstantBufferView(std::shared_ptr<D3D12ConstantBufferView> InConstantBufferView)
	{
		ConstantBufferView = InConstantBufferView;
	}

	FORCEINLINE std::shared_ptr<D3D12ConstantBufferView> GetConstantBufferView() const
	{
		return ConstantBufferView;
	}

	FORCEINLINE Uint32 GetSizeInBytes() const
	{
		return SizeInBytes;
	}

private:
	std::shared_ptr<D3D12ConstantBufferView> ConstantBufferView;

	Uint32 SizeInBytes = 0;
};