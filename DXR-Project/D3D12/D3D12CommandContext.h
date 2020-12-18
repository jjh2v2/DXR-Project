#pragma once
#include "RenderingCore/ICommandContext.h"

#include "D3D12DeviceChild.h"
#include "D3D12RootSignature.h"

class D3D12CommandQueue;
class D3D12CommandList;
class D3D12CommandAllocator;

/*
* D3D12GenerateMipsHelper
*/

struct D3D12GenerateMipsHelper
{
	TUniquePtr<class D3D12ComputePipelineState>		GenerateMipsTex2D_PSO;
	TUniquePtr<class D3D12ComputePipelineState>		GenerateMipsTexCube_PSO;
	TUniquePtr<class D3D12RootSignature>			GenerateMipsRootSignature;
	TUniquePtr<class D3D12DescriptorTable>			SRVDescriptorTable;
	TArray<TUniquePtr<class D3D12DescriptorTable>>	UAVDescriptorTables;
	TUniquePtr<class D3D12UnorderedAccessView>		NULLView;
};

/*
* D3D12CommandContext 
*/

class D3D12CommandContext : public ICommandContext, public D3D12DeviceChild
{
public:
	D3D12CommandContext(class D3D12Device* InDevice, D3D12CommandQueue* InCmdQueue, const D3D12DefaultRootSignatures& InDefaultRootSignatures);
	~D3D12CommandContext();

	bool Initialize();

	FORCEINLINE D3D12CommandList& GetCommandList() const
	{
		VALIDATE(CmdList != nullptr);
		return *CmdList;
	}

public:
	virtual void Begin() override final;
	virtual void End() override final;

	virtual void ClearRenderTargetView(RenderTargetView* RenderTargetView, const ColorClearValue& ClearColor) override final;
	virtual void ClearDepthStencilView(DepthStencilView* DepthStencilView, const DepthStencilClearValue& ClearValue) override final;
	virtual void ClearUnorderedAccessView(UnorderedAccessView* UnorderedAccessView, const ColorClearValue& ClearColor) override final;

	virtual void BeginRenderPass() override final;
	virtual void EndRenderPass() override final;

	virtual void BindViewport(const Viewport& Viewport, Uint32 Slot) override final;
	virtual void BindScissorRect(const ScissorRect& ScissorRect, Uint32 Slot) override final;

	virtual void BindBlendFactor(const ColorClearValue& Color) override final;

	virtual void BindPrimitiveTopology(EPrimitiveTopology PrimitveTopologyType) override final;
	virtual void BindVertexBuffers(
		VertexBuffer* const * VertexBuffers, 
		Uint32 BufferCount, 
		Uint32 BufferSlot) override final;

	virtual void BindIndexBuffer(IndexBuffer* IndexBuffer) override final;
	virtual void BindRayTracingScene(RayTracingScene* RayTracingScene) override final;

	virtual void BindRenderTargets(
		RenderTargetView* const * RenderTargetViews, 
		Uint32 RenderTargetCount, 
		DepthStencilView* DepthStencilView) override final;

	virtual void BindGraphicsPipelineState(class GraphicsPipelineState* PipelineState) override final;
	virtual void BindComputePipelineState(class ComputePipelineState* PipelineState) override final;
	virtual void BindRayTracingPipelineState(class RayTracingPipelineState* PipelineState) override final;

	virtual void BindConstantBuffers(
		Shader* Shader, 
		ConstantBuffer* const * ConstantBuffers, 
		Uint32 ConstantBufferCount, 
		Uint32 StartSlot) override final;

	virtual void BindShaderResourceViews(
		Shader* Shader, 
		ShaderResourceView* const* ShaderResourceViews, 
		Uint32 ShaderResourceViewCount, 
		Uint32 StartSlot) override final;

	virtual void BindUnorderedAccessViews(
		Shader* Shader, 
		UnorderedAccessView* const* UnorderedAccessViews, 
		Uint32 UnorderedAccessViewCount, 
		Uint32 StartSlot) override final;

	virtual void ResolveTexture(Texture* Destination, Texture* Source) override final;

	virtual void UpdateBuffer(
		Buffer* Destination, 
		Uint64 OffsetInBytes, 
		Uint64 SizeInBytes, 
		const VoidPtr SourceData) override final;

	virtual void UpdateTexture2D(
		Texture2D* Destination,
		Uint32 Width,
		Uint32 Height,
		Uint32 MipLevel,
		const VoidPtr SourceData) override final;

	virtual void CopyBuffer(
		Buffer* Destination, 
		Buffer* Source, 
		const CopyBufferInfo& CopyInfo) override final;

	virtual void CopyTexture(
		Texture* Destination, 
		Texture* Source, 
		const CopyTextureInfo& CopyTextureInfo) override final;

	virtual void BuildRayTracingGeometry(RayTracingGeometry* RayTracingGeometry) 	override final;
	virtual void BuildRayTracingScene(RayTracingScene* RayTracingScene) 			override final;

	virtual void GenerateMips(Texture* Texture) override final;

	virtual void TransitionTexture(
		Texture* Texture,
		EResourceState BeforeState,
		EResourceState AfterState) override final;

	virtual void TransitionBuffer(
		Buffer* Buffer,
		EResourceState BeforeState,
		EResourceState AfterState) override final;

	virtual void UnorderedAccessTextureBarrier(Texture* Texture) override final;

	virtual void Draw(Uint32 VertexCount, Uint32 StartVertexLocation) override final;
	
	virtual void DrawIndexed(
		Uint32 IndexCount, 
		Uint32 StartIndexLocation, 
		Uint32 BaseVertexLocation) override final;
	
	virtual void DrawInstanced(
		Uint32 VertexCountPerInstance, 
		Uint32 InstanceCount, 
		Uint32 StartVertexLocation, 
		Uint32 StartInstanceLocation) override final;
	
	virtual void DrawIndexedInstanced(
		Uint32 IndexCountPerInstance, 
		Uint32 InstanceCount, 
		Uint32 StartIndexLocation, 
		Uint32 BaseVertexLocation, 
		Uint32 StartInstanceLocation) override final;

	virtual void Dispatch(
		Uint32 WorkGroupsX, 
		Uint32 WorkGroupsY, 
		Uint32 WorkGroupsZ) override final;

	virtual void DispatchRays(Uint32 Width, Uint32 Height, Uint32 Depth) override final;

	virtual void Flush() override final;

private:
	D3D12CommandQueue* CmdQueue;
	TArray<D3D12CommandAllocator*> CmdAllocators;
	class D3D12CommandList* CmdList; 
	class D3D12Fence* Fence;
	bool IsReady = false;
	
	D3D12DefaultRootSignatures DefaultRootSignatures;
};