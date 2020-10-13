#pragma once
#include <dxgi1_6.h>
#include <d3d12.h>

#include <wrl/client.h>

#include "Types.h"

class D3D12DescriptorHeap;
class D3D12OfflineDescriptorHeap;
class D3D12OnlineDescriptorHeap;
class D3D12ComputePipelineState;
class D3D12RootSignature;

/*
* D3D12Device
*/

class D3D12Device
{
public:
	D3D12Device();
	~D3D12Device();

	bool Initialize(bool DebugEnable);

	Int32 GetMultisampleQuality(DXGI_FORMAT Format, Uint32 SampleCount);

	std::string GetAdapterName() const;
	
	FORCEINLINE HRESULT CreateCommitedResource(
		const D3D12_HEAP_PROPERTIES* pHeapProperties,
		D3D12_HEAP_FLAGS HeapFlags,
		const D3D12_RESOURCE_DESC* pDesc,
		D3D12_RESOURCE_STATES InitialResourceState,
		const D3D12_CLEAR_VALUE* pOptimizedClearValue,
		REFIID riidResource,
		void** ppvResource)
	{
		return D3DDevice->CreateCommittedResource(pHeapProperties, HeapFlags, pDesc, InitialResourceState, pOptimizedClearValue, riidResource, ppvResource);
	}

	FORCEINLINE void CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
	{
		D3DDevice->CreateConstantBufferView(pDesc, DestDescriptor);
	}

	FORCEINLINE void CreateRenderTargetView(ID3D12Resource* pResource, const D3D12_RENDER_TARGET_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
	{
		D3DDevice->CreateRenderTargetView(pResource, pDesc, DestDescriptor);
	}

	FORCEINLINE void CreateDepthStencilView(ID3D12Resource* pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
	{
		D3DDevice->CreateDepthStencilView(pResource, pDesc, DestDescriptor);
	}

	FORCEINLINE void CreateShaderResourceView(ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc, D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
	{
		D3DDevice->CreateShaderResourceView(pResource, pDesc, DestDescriptor);
	}

	FORCEINLINE void CreateUnorderedAccessView(
		ID3D12Resource* pResource,
		ID3D12Resource* pCounterResource,
		const D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc,
		D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
	{
		D3DDevice->CreateUnorderedAccessView(pResource, pCounterResource, pDesc, DestDescriptor);
	}

	FORCEINLINE ID3D12Device* GetDevice() const
	{
		return D3DDevice.Get();
	}

	FORCEINLINE ID3D12Device5* GetDXRDevice() const
	{
		return DXRDevice.Get();
	}

	FORCEINLINE IDXGIFactory2* GetFactory() const
	{
		return Factory.Get();
	}

	FORCEINLINE bool IsTearingSupported() const
	{
		return AllowTearing;
	}

	FORCEINLINE bool IsRayTracingSupported() const
	{
		return false;// RayTracingSupported;
	}

	FORCEINLINE D3D12OfflineDescriptorHeap* GetGlobalResourceDescriptorHeap() const
	{
		return GlobalResourceDescriptorHeap;
	}

	FORCEINLINE D3D12OfflineDescriptorHeap* GetGlobalRenderTargetDescriptorHeap() const
	{
		return GlobalRenderTargetDescriptorHeap;
	}

	FORCEINLINE D3D12OfflineDescriptorHeap* GetGlobalDepthStencilDescriptorHeap() const
	{
		return GlobalDepthStencilDescriptorHeap;
	}

	FORCEINLINE D3D12OfflineDescriptorHeap* GetGlobalSamplerDescriptorHeap() const
	{
		return GlobalSamplerDescriptorHeap;
	}

	FORCEINLINE D3D12OnlineDescriptorHeap* GetGlobalOnlineResourceHeap() const
	{
		return GlobalOnlineResourceHeap;
	}

	static D3D12Device* Make(bool DebugEnable);

private:
	bool CreateFactory();
	bool ChooseAdapter();

private:
	Microsoft::WRL::ComPtr<IDXGIFactory2>	Factory;
	Microsoft::WRL::ComPtr<IDXGIAdapter1>	Adapter;
	Microsoft::WRL::ComPtr<ID3D12Device>	D3DDevice;
	Microsoft::WRL::ComPtr<ID3D12Device5>	DXRDevice;

	D3D_FEATURE_LEVEL MinFeatureLevel		= D3D_FEATURE_LEVEL_11_0;
	D3D_FEATURE_LEVEL ActiveFeatureLevel	= D3D_FEATURE_LEVEL_11_0;

	D3D12OfflineDescriptorHeap* GlobalResourceDescriptorHeap		= nullptr;
	D3D12OfflineDescriptorHeap* GlobalRenderTargetDescriptorHeap	= nullptr;
	D3D12OfflineDescriptorHeap* GlobalDepthStencilDescriptorHeap	= nullptr;
	D3D12OfflineDescriptorHeap* GlobalSamplerDescriptorHeap			= nullptr;

	D3D12OnlineDescriptorHeap* GlobalOnlineResourceHeap = nullptr;

	Uint32 AdapterID = 0;

	bool DebugEnabled			= false;
	bool RayTracingSupported	= false;
	BOOL AllowTearing			= FALSE;
};