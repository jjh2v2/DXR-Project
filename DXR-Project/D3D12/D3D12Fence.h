#pragma once
#include "D3D12DeviceChild.h"

#include "Types.h"

class D3D12Fence : public D3D12DeviceChild
{
public:
	D3D12Fence(D3D12Device* InDevice);
	~D3D12Fence();

	bool Initialize(Uint64 InitalValue);

	bool WaitForValue(Uint64 FenceValue);

	FORCEINLINE ID3D12Fence* GetFence() const
	{
		return Fence.Get();
	}

public:
	// DeviceChild Interface
	virtual void SetName(const std::string& Name) override;

private:
	Microsoft::WRL::ComPtr<ID3D12Fence> Fence;
	HANDLE Event = 0;
};