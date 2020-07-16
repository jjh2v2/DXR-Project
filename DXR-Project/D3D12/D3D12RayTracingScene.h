#pragma once
#include "D3D12DeviceChild.h"
#include "D3D12Buffer.h"
#include "D3D12Views.h"

#include "Defines.h"

#include <memory>

class D3D12CommandList;
class D3D12DescriptorTable;

/*
* D3D12RayTracingGeometry - Equal to the Bottom-Level AccelerationStructure
*/

class D3D12RayTracingGeometry : public D3D12DeviceChild
{
public:
	D3D12RayTracingGeometry(D3D12Device* InDevice);
	~D3D12RayTracingGeometry();

	bool BuildAccelerationStructure(D3D12CommandList* CommandList, std::shared_ptr<D3D12Buffer>& InVertexBuffer, Uint32 InVertexCount, std::shared_ptr<D3D12Buffer>& IndexBuffer, Uint32 InIndexCount);

	// DeviceChild Interface
	virtual void SetName(const std::string& Name) override;

	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const;

private:
	std::shared_ptr<D3D12Buffer> VertexBuffer;
	std::shared_ptr<D3D12Buffer> IndexBuffer;
	
	D3D12Buffer* ResultBuffer	= nullptr;
	D3D12Buffer* ScratchBuffer	= nullptr;
	
	Uint32 VertexCount	= 0;
	Uint32 IndexCount	= 0;
};

/*
* D3D12RayTracingGeometryInstance
*/

class D3D12RayTracingGeometryInstance
{
public:
	D3D12RayTracingGeometryInstance(std::shared_ptr<D3D12RayTracingGeometry>& InGeometry, XMFLOAT3X4 InTransform, Uint32 InHitGroupIndex, Uint32 InInstanceID)
		: Geometry(InGeometry)
		, Transform(InTransform)
		, HitGroupIndex(InHitGroupIndex)
		, InstanceID(InInstanceID)
	{
	}

	~D3D12RayTracingGeometryInstance()
	{
	}

public:
	std::shared_ptr<D3D12RayTracingGeometry> Geometry;
	
	XMFLOAT3X4	Transform;
	Uint32		HitGroupIndex;
	Uint32		InstanceID;
};

/*
* D3D12RayTracingScene - Equal to the Top-Level AccelerationStructure
*/

struct BindingTableEntry
{
public:
	BindingTableEntry()
		: ShaderExportName()
		, DescriptorTable(nullptr)
	{
	}

	BindingTableEntry(std::string InShaderExportName, std::shared_ptr<D3D12DescriptorTable> InDescriptorTable)
		: ShaderExportName(InShaderExportName)
		, DescriptorTable(InDescriptorTable)
	{
	}

public:
	std::string ShaderExportName;
	std::shared_ptr<D3D12DescriptorTable> DescriptorTable;
};

class D3D12RayTracingScene : public D3D12DeviceChild
{
public:
	D3D12RayTracingScene(D3D12Device* InDevice);
	~D3D12RayTracingScene();

	bool Initialize(class D3D12RayTracingPipelineState* PipelineState, std::vector<BindingTableEntry>& InBindingTableEntries, Uint32 InNumHitGroups);

	bool BuildAccelerationStructure(D3D12CommandList* CommandList, std::vector<D3D12RayTracingGeometryInstance>& InInstances);

	// DeviceChild Interface
	virtual void SetName(const std::string& Name) override;

	D3D12_GPU_VIRTUAL_ADDRESS_RANGE				GetRayGenerationShaderRecord()	const;
	D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE	GetMissShaderTable()			const;
	D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE	GetHitGroupTable()				const;
	
	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const;

	FORCEINLINE D3D12ShaderResourceView* GetShaderResourceView() const
	{
		return View.get();
	}

private:
	D3D12Buffer* ResultBuffer	= nullptr;
	D3D12Buffer* ScratchBuffer	= nullptr;
	D3D12Buffer* InstanceBuffer	= nullptr;

	D3D12Buffer*	BindingTable		= nullptr;
	Uint32			BindingTableStride	= 0;
	Uint32			NumHitGroups		= 0;

	std::shared_ptr<D3D12ShaderResourceView>		View;
	std::vector<D3D12RayTracingGeometryInstance>	Instances;
	std::vector<BindingTableEntry>					BindingTableEntries;
};