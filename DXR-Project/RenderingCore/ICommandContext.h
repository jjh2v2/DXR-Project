#pragma once
#include "RenderingCore.h"

class Buffer;
class VertexBuffer;
class IndexBuffer;
class ConstantBuffer;
class StructuredBuffer;
class Shader;
class RenderTargetView;
class ShaderResourceView;
class DepthStencilView;
class UnorderedAccessView;
class Texture;
class Texture2D;
class RayTracingGeometry;
class RayTracingScene;
class GraphicsPipelineState;
class ComputePipelineState;
class RayTracingPipelineState;

/*
* ICommandContext
*/

class ICommandContext
{
public:
	virtual ~ICommandContext() = default;

	virtual void Begin() = 0;
	virtual void End() = 0;

	virtual void ClearRenderTargetView(RenderTargetView* RenderTargetView, const ColorClearValue& ClearColor)			= 0;
	virtual void ClearDepthStencilView(DepthStencilView* DepthStencilView, const DepthStencilClearValue& ClearValue)	= 0;
	virtual void ClearUnorderedAccessView(UnorderedAccessView* UnorderedAccessView, const ColorClearValue& ClearColor)	= 0;

	virtual void BeginRenderPass()	= 0;
	virtual void EndRenderPass()	= 0;

	virtual void BindViewport(const Viewport& Viewport, UInt32 Slot) = 0;
	virtual void BindScissorRect(const ScissorRect& ScissorRect, UInt32 Slot) = 0;

	virtual void BindBlendFactor(const ColorClearValue& Color) = 0;

	virtual void BindVertexBuffers(
		VertexBuffer* const * VertexBuffers, 
		UInt32 BufferCount, 
		UInt32 BufferSlot) = 0;

	virtual void BindIndexBuffer(IndexBuffer* IndexBuffer) = 0;
	virtual void BindPrimitiveTopology(EPrimitiveTopology PrimitveTopologyType) = 0;
	virtual void BindRayTracingScene(RayTracingScene* RayTracingScene) = 0;

	virtual void BindRenderTargets(
		RenderTargetView* const * RenderTargetViews, 
		UInt32 RenderTargetCount, 
		DepthStencilView* DepthStencilView) = 0;

	virtual void BindGraphicsPipelineState(class GraphicsPipelineState* PipelineState) = 0;
	virtual void BindComputePipelineState(class ComputePipelineState* PipelineState) = 0;
	virtual void BindRayTracingPipelineState(class RayTracingPipelineState* PipelineState) = 0;

	virtual void BindShaderResourceViews(
		Shader* Shader, 
		ShaderResourceView* const* ShaderResourceViews, 
		UInt32 ShaderResourceViewCount, 
		UInt32 StartSlot) = 0;

	virtual void BindUnorderedAccessViews(
		Shader* Shader, 
		UnorderedAccessView* const* UnorderedAccessViews, 
		UInt32 UnorderedAccessViewCount, 
		UInt32 StartSlot) = 0;

	virtual void BindConstantBuffers(
		Shader* Shader,
		ConstantBuffer* const * ConstantBuffers, 
		UInt32 ConstantBufferCount, 
		UInt32 StartSlot) = 0;

	virtual void ResolveTexture(Texture* Destination, Texture* Source) = 0;

	virtual void UpdateBuffer(
		Buffer* Destination, 
		UInt64 OffsetInBytes, 
		UInt64 SizeInBytes, 
		const Void* SourceData) = 0;

	virtual void UpdateTexture2D(
		Texture2D* Destination,
		UInt32 Width,
		UInt32 Height,
		UInt32 MipLevel,
		const Void* SourceData) = 0;

	virtual void CopyBuffer(
		Buffer* Destination, 
		Buffer* Source, 
		const CopyBufferInfo& CopyInfo) = 0;

	virtual void CopyTexture(
		Texture* Destination, 
		Texture* Source, 
		const CopyTextureInfo& CopyTextureInfo) = 0;

	virtual void BuildRayTracingGeometry(RayTracingGeometry* RayTracingGeometry) = 0;
	virtual void BuildRayTracingScene(RayTracingScene* RayTracingScene) = 0;

	virtual void GenerateMips(Texture* Texture) = 0;

	virtual void TransitionTexture(
		Texture* Texture, 
		EResourceState BeforeState, 
		EResourceState AfterState) = 0;

	virtual void TransitionBuffer(
		Buffer* Buffer,
		EResourceState BeforeState,
		EResourceState AfterState) = 0;

	virtual void UnorderedAccessTextureBarrier(Texture* Texture) = 0;

	virtual void Draw(
		UInt32 VertexCount, 
		UInt32 StartVertexLocation) = 0;

	virtual void DrawIndexed(
		UInt32 IndexCount, 
		UInt32 StartIndexLocation, 
		UInt32 BaseVertexLocation) = 0;

	virtual void DrawInstanced(
		UInt32 VertexCountPerInstance, 
		UInt32 InstanceCount, 
		UInt32 StartVertexLocation, 
		UInt32 StartInstanceLocation) = 0;

	virtual void DrawIndexedInstanced(
		UInt32 IndexCountPerInstance, 
		UInt32 InstanceCount, 
		UInt32 StartIndexLocation, 
		UInt32 BaseVertexLocation, 
		UInt32 StartInstanceLocation) = 0;

	virtual void Dispatch(
		UInt32 WorkGroupsX, 
		UInt32 WorkGroupsY, 
		UInt32 WorkGroupsZ) = 0;

	virtual void DispatchRays(	
		UInt32 Width, 
		UInt32 Height, 
		UInt32 Depth) = 0;

	virtual void Flush() = 0;
};