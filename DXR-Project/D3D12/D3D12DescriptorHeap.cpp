#include "D3D12DescriptorHeap.h"
#include "D3D12Device.h"

D3D12DescriptorHeap::D3D12DescriptorHeap(D3D12Device* InDevice)
	: D3D12DeviceChild(InDevice)
	, Heap(nullptr)
{
}

D3D12DescriptorHeap::~D3D12DescriptorHeap()
{
}

bool D3D12DescriptorHeap::Initialize(D3D12_DESCRIPTOR_HEAP_TYPE Type, Uint32 DescriptorCount, D3D12_DESCRIPTOR_HEAP_FLAGS Flags)
{
	D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
	HeapDesc.Flags				= Flags;
	HeapDesc.NodeMask			= 0;
	HeapDesc.NumDescriptors		= DescriptorCount;
	HeapDesc.Type				= Type;

	HRESULT hResult = Device->GetDevice()->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Heap));
	if (SUCCEEDED(hResult))
	{
		DescriptorSize = Device->GetDevice()->GetDescriptorHandleIncrementSize(Type);
		::OutputDebugString("[D3D12DescrtiptorHeap]: Created DescriptorHeap\n");

		// Create Handles
		D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = Heap->GetCPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle = Heap->GetGPUDescriptorHandleForHeapStart();
		FreeHandles.reserve(DescriptorCount);

		for (Uint32 Index = 0; Index < DescriptorCount; Index++)
		{
			FreeHandles.emplace_back(CPUHandle, GPUHandle);

			CPUHandle.ptr += GetDescriptorSize() * Index;
			GPUHandle.ptr += GetDescriptorSize() * Index;
		}

		return true;
	}
	else
	{
		::OutputDebugString("[D3D12DescrtiptorHeap]: Failed to create DescriptorHeap\n");
		return false;
	}
}

D3D12DescriptorHandle D3D12DescriptorHeap::Allocate()
{
	D3D12DescriptorHandle Handle = FreeHandles.front();
	FreeHandles.erase(FreeHandles.begin());

	return Handle;
}

void D3D12DescriptorHeap::Free(const D3D12DescriptorHandle& DescriptorHandle)
{
	FreeHandles.emplace_back(DescriptorHandle);
}

void D3D12DescriptorHeap::SetName(const std::string& InName)
{
	Heap->SetName(ConvertToWide(InName).c_str());
}

/*
* D3D12OfflineDescriptorHeap
*/

D3D12OfflineDescriptorHeap::D3D12OfflineDescriptorHeap(D3D12Device* InDevice, D3D12_DESCRIPTOR_HEAP_TYPE InType)
	: D3D12DeviceChild(InDevice)
	, Heaps()
	, DebugName()
	, Type(InType)
{
	// Get the size of this type of heap
	DescriptorSize = Device->GetDevice()->GetDescriptorHandleIncrementSize(Type);

	// Allocate a new heap
	AllocateHeap();
}

D3D12OfflineDescriptorHeap::~D3D12OfflineDescriptorHeap()
{
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12OfflineDescriptorHeap::Allocate(Uint32& OutHeapIndex)
{
	// Find a heap that is not empty
	Uint32	HeapIndex = 0;
	bool	FoundHeap = false;
	for (DescriptorHeap& Heap : Heaps)
	{
		if (!Heap.FreeList.empty())
		{
			FoundHeap = true;
			break;
		}
		else
		{
			HeapIndex++;
		}
	}

	// If a heap is not found allocate a new one
	if (!FoundHeap)
	{
		AllocateHeap();
		HeapIndex = Heaps.size() - 1;
	}

	// Get the heap and the first free range
	DescriptorHeap&	Heap	= Heaps[HeapIndex];
	FreeRange&		Range	= Heap.FreeList.front();

	D3D12_CPU_DESCRIPTOR_HANDLE Handle = Range.Begin;
	Range.Begin.ptr += DescriptorSize;

	if (!Range.IsValid())
	{
		Heap.FreeList.erase(Heap.FreeList.begin() + HeapIndex);
	}

	OutHeapIndex = HeapIndex;
	return Handle;
}

void D3D12OfflineDescriptorHeap::Free(D3D12_CPU_DESCRIPTOR_HANDLE Handle, Uint32 HeapIndex)
{
	VALIDATE(HeapIndex < Heaps.size());
	DescriptorHeap&	Heap = Heaps[HeapIndex];

	// Find a suitable range
	bool FoundRange	= false;
	for (FreeRange& Range : Heap.FreeList)
	{
		VALIDATE(Range.IsValid());

		if (Handle.ptr + DescriptorSize == Range.Begin.ptr)
		{
			Range.Begin = Handle;
			FoundRange = true;
		}
		else if (Handle.ptr == Range.End.ptr)
		{
			Range.End.ptr += DescriptorSize;
			FoundRange = true;
		}
	}

	if (!FoundRange)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE End = { Handle.ptr + DescriptorSize };
		Heap.FreeList.emplace_back(Handle, End);
	}
}

void D3D12OfflineDescriptorHeap::SetName(const std::string& InName)
{
	DebugName = ConvertToWide(InName);

	Uint32 HeapIndex = 0;
	for (DescriptorHeap& Heap : Heaps)
	{
		std::wstring Name = DebugName + L"[" + std::to_wstring(HeapIndex) + L"]";
		Heap.Heap->SetName(Name.c_str());
	}
}

void D3D12OfflineDescriptorHeap::AllocateHeap()
{
	constexpr Uint32 DescriptorCount = 32;

	D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
	HeapDesc.Flags			= D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // These heaps are not visible to shaders
	HeapDesc.NodeMask		= 0;
	HeapDesc.NumDescriptors = DescriptorCount;
	HeapDesc.Type			= Type;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> Heap;
	HRESULT hResult = Device->GetDevice()->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Heap));
	if (SUCCEEDED(hResult))
	{
		::OutputDebugString("[D3D12OfflineDescriptorHeap]: Created DescriptorHeap\n");

		if (!DebugName.empty())
		{
			std::wstring Name = DebugName + std::to_wstring(Heaps.size());
			Heap->SetName(Name.c_str());
		}

		FreeRange WholeRange;
		WholeRange.Begin	= Heap->GetCPUDescriptorHandleForHeapStart();
		WholeRange.End.ptr	= WholeRange.Begin.ptr + (DescriptorSize * DescriptorCount);
		Heaps.emplace_back(Heap, WholeRange);
	}
	else
	{
		::OutputDebugString("[D3D12OfflineDescriptorHeap]: Failed to create DescriptorHeap\n");
	}
}
