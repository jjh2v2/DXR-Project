#pragma once
#include "Defines.h"
#include "Types.h"

#include "Containers/String.h"

#include <d3d12.h>

#include <wrl/client.h>

class D3D12Device;

/*
* D3D12DeviceChild
*/

class D3D12DeviceChild
{
public:
	 inline D3D12DeviceChild(D3D12Device* InDevice)
		: Device(InDevice)
	{
		 VALIDATE(Device != nullptr);
	}

	inline virtual ~D3D12DeviceChild()
	{
		Device = nullptr;
	}

	FORCEINLINE D3D12Device* GetDevice() const
	{
		return Device;
	}

protected:
	D3D12Device* Device = nullptr;
};