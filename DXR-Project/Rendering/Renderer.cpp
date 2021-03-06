#include "Renderer.h"
#include "TextureFactory.h"
#include "DebugUI.h"
#include "Mesh.h"

#include "Scene/Frustum.h"
#include "Scene/Lights/PointLight.h"
#include "Scene/Lights/DirectionalLight.h"

#include "D3D12/D3D12Texture.h"
#include "D3D12/D3D12Views.h"
#include "D3D12/D3D12RootSignature.h"
#include "D3D12/D3D12GraphicsPipelineState.h"
#include "D3D12/D3D12RayTracingPipelineState.h"
#include "D3D12/D3D12ComputePipelineState.h"
#include "D3D12/D3D12ShaderCompiler.h"

#include "Application/Events/EventQueue.h"

#include <algorithm>

/*
* Static Settings
*/
TUniquePtr<Renderer>	Renderer::RendererInstance = nullptr;
LightSettings			Renderer::GlobalLightSettings;

static const DXGI_FORMAT	RenderTargetFormat		= DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT	MaterialFormat			= DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT	AlbedoFormat			= DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT	LightProbeFormat		= DXGI_FORMAT_R16G16B16A16_FLOAT;
static const DXGI_FORMAT	NormalFormat			= DXGI_FORMAT_R10G10B10A2_UNORM;
static const DXGI_FORMAT	DepthBufferFormat		= DXGI_FORMAT_D32_FLOAT;
static const DXGI_FORMAT	ShadowMapFormat			= DXGI_FORMAT_D32_FLOAT;
static const UInt32			ShadowMapSampleCount	= 2;

#define GBUFFER_ALBEDO_INDEX		0
#define GBUFFER_NORMAL_INDEX		1
#define GBUFFER_MATERIAL_INDEX		2
#define GBUFFER_DEPTH_INDEX			3

/*
* Renderer
*/

Renderer::Renderer()
{
}

Renderer::~Renderer()
{
}

void Renderer::Tick(const Scene& CurrentScene)
{
	// Start frame
	D3D12Texture* BackBuffer = RenderingAPI::Get().GetSwapChain()->GetSurfaceResource(CurrentBackBufferIndex);
	CommandAllocators[CurrentBackBufferIndex]->Reset();
	CommandList->Reset(CommandAllocators[CurrentBackBufferIndex].Get());

	// Release deferred resources
	for (auto& Resource : DeferredResources)
	{
		CommandList->DeferDestruction(Resource.Get());
	}
	DeferredResources.Clear();

	// Perform frustum culling
	DeferredVisibleCommands.Clear();
	ForwardVisibleCommands.Clear();

	if (FrustumCullEnabled)
	{
		Camera* Camera = CurrentScene.GetCamera();
		Frustum CameraFrustum = Frustum(Camera->GetFarPlane(), Camera->GetViewMatrix(), Camera->GetProjectionMatrix());
		for (const MeshDrawCommand& Command : CurrentScene.GetMeshDrawCommands())
		{
			const XMFLOAT4X4& Transform = Command.CurrentActor->GetTransform().GetMatrix();
			XMMATRIX XmTransform	= XMMatrixTranspose(XMLoadFloat4x4(&Transform));
			XMVECTOR XmTop			= XMVectorSetW(XMLoadFloat3(&Command.Mesh->BoundingBox.Top), 1.0f);
			XMVECTOR XmBottom		= XMVectorSetW(XMLoadFloat3(&Command.Mesh->BoundingBox.Bottom), 1.0f);
			XmTop		= XMVector4Transform(XmTop, XmTransform);
			XmBottom	= XMVector4Transform(XmBottom, XmTransform);

			AABB Box;
			XMStoreFloat3(&Box.Top, XmTop);
			XMStoreFloat3(&Box.Bottom, XmBottom);
			if (CameraFrustum.CheckAABB(Box))
			{
				if (Command.Material->HasAlphaMask())
				{
					ForwardVisibleCommands.EmplaceBack(Command);
				}
				else
				{
					DeferredVisibleCommands.EmplaceBack(Command);
				}
			}
		}
	}
	else
	{
		for (const MeshDrawCommand& Command : CurrentScene.GetMeshDrawCommands())
		{
			if (Command.Material->HasAlphaMask())
			{
				ForwardVisibleCommands.EmplaceBack(Command);
			}
			else
			{
				DeferredVisibleCommands.EmplaceBack(Command);
			}
		}
	}

	// Build acceleration structures
	if (RenderingAPI::Get().IsRayTracingSupported() && RayTracingEnabled)
	{
		// Build Bottom-Level
		UInt32 HitGroupIndex = 0;
		for (const MeshDrawCommand& Command : CurrentScene.GetMeshDrawCommands())
		{
			Command.Geometry->BuildAccelerationStructure(
				CommandList.Get(),
				Command.Mesh->VertexBuffer,
				Command.Mesh->VertexCount,
				Command.Mesh->IndexBuffer,
				Command.Mesh->IndexCount);

			XMFLOAT4X4 Matrix		= Command.CurrentActor->GetTransform().GetMatrix();
			XMFLOAT3X4 SmallMatrix	= XMFLOAT3X4(reinterpret_cast<Float*>(&Matrix));

			RayTracingGeometryInstances.EmplaceBack(
				Command.Mesh->RayTracingGeometry,
				Command.Material,
				SmallMatrix,
				HitGroupIndex, 0);

			HitGroupIndex++;
		}

		// Build Top-Level
		const bool NeedsBuild = RayTracingScene->NeedsBuild();
		if (NeedsBuild)
		{
			TArray<BindingTableEntry> BindingTableEntries;
			BindingTableEntries.Reserve(RayTracingGeometryInstances.Size());

			BindingTableEntries.EmplaceBack("RayGen", RayGenDescriptorTable, nullptr);
			for (D3D12RayTracingGeometryInstance& Geometry : RayTracingGeometryInstances)
			{
				BindingTableEntries.EmplaceBack(
					"HitGroup", 
					Geometry.Material->GetDescriptorTable(),
					Geometry.Geometry->GetDescriptorTable());
			}
			BindingTableEntries.EmplaceBack("Miss", nullptr, nullptr);

			const UInt32 NumHitGroups = BindingTableEntries.Size() - 2;
			RayTracingScene->BuildAccelerationStructure(
				CommandList.Get(),
				RayTracingGeometryInstances,
				BindingTableEntries,
				NumHitGroups);

			GlobalDescriptorTable->SetShaderResourceView(RayTracingScene->GetShaderResourceView(), 0);
			GlobalDescriptorTable->CopyDescriptors();
		}
	}

	// UpdateLightBuffers
	CommandList->TransitionBarrier(PointLightBuffer.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COPY_DEST);
	CommandList->TransitionBarrier(DirectionalLightBuffer.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COPY_DEST);

	UInt32 NumPointLights	= 0;
	UInt32 NumDirLights		= 0;
	for (Light* Light : CurrentScene.GetLights())
	{
		XMFLOAT3	Color		= Light->GetColor();
		Float		Intensity	= Light->GetIntensity();
		if (IsSubClassOf<PointLight>(Light))
		{
			PointLight* PoiLight = Cast<PointLight>(Light);
			VALIDATE(PoiLight != nullptr);

			PointLightProperties Properties;
			Properties.Color			= XMFLOAT3(Color.x * Intensity, Color.y * Intensity, Color.z * Intensity);
			Properties.Position			= PoiLight->GetPosition();
			Properties.ShadowBias		= PoiLight->GetShadowBias();
			Properties.MaxShadowBias	= PoiLight->GetMaxShadowBias();
			Properties.FarPlane			= PoiLight->GetShadowFarPlane();

			constexpr UInt32 SizeInBytes = sizeof(PointLightProperties);
			CommandList->UploadBufferData(PointLightBuffer.Get(), NumPointLights * SizeInBytes, &Properties, SizeInBytes);

			NumPointLights++;
		}
		else if (IsSubClassOf<DirectionalLight>(Light))
		{
			DirectionalLight* DirLight = Cast<DirectionalLight>(Light);
			VALIDATE(DirLight != nullptr);

			DirectionalLightProperties Properties;
			Properties.Color			= XMFLOAT3(Color.x * Intensity, Color.y * Intensity, Color.z * Intensity);
			Properties.ShadowBias		= DirLight->GetShadowBias();
			Properties.Direction		= DirLight->GetDirection();
			Properties.LightMatrix		= DirLight->GetMatrix();
			Properties.MaxShadowBias	= DirLight->GetMaxShadowBias();

			constexpr UInt32 SizeInBytes = sizeof(DirectionalLightProperties);
			CommandList->UploadBufferData(DirectionalLightBuffer.Get(), NumDirLights * SizeInBytes, &Properties, SizeInBytes);

			NumDirLights++;
		}
	}

	CommandList->TransitionBarrier(PointLightBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	CommandList->TransitionBarrier(DirectionalLightBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

	// Set constant buffer descriptor heap
	CommandList->BindGlobalOnlineDescriptorHeaps();

	// Transition GBuffer
	CommandList->TransitionBarrier(GBuffer[GBUFFER_ALBEDO_INDEX].Get(),		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	CommandList->TransitionBarrier(GBuffer[GBUFFER_NORMAL_INDEX].Get(),		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	CommandList->TransitionBarrier(GBuffer[GBUFFER_MATERIAL_INDEX].Get(),	D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	CommandList->TransitionBarrier(GBuffer[GBUFFER_DEPTH_INDEX].Get(),		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

	// Transition ShadowMaps
	CommandList->TransitionBarrier(PointLightShadowMaps.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	CommandList->TransitionBarrier(DirLightShadowMaps.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	
	// Render DirectionalLight ShadowMaps
	CommandList->ClearDepthStencilView(DirLightShadowMaps->GetDepthStencilView(0).Get(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0);

#if ENABLE_VSM
	CommandList->TransitionBarrier(VSMDirLightShadowMaps.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	Float DepthClearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	CommandList->ClearRenderTargetView(VSMDirLightShadowMaps->GetRenderTargetView(0).Get(), DepthClearColor);
	
	D3D12RenderTargetView* DirLightRTVS[] =
	{
		VSMDirLightShadowMaps->GetRenderTargetView(0).Get(),
	};
	CommandList->OMSetRenderTargets(DirLightRTVS, 1, DirLightShadowMaps->GetDepthStencilView(0).Get());
	CommandList->SetPipelineState(VSMShadowMapPSO->GetPipelineState());
#else
	CommandList->OMSetRenderTargets(nullptr, 0, DirLightShadowMaps->GetDepthStencilView(0).Get());
	CommandList->SetPipelineState(ShadowMapPSO->GetPipelineState());
#endif

	// Setup view
	D3D12_VIEWPORT ViewPort = { };
	ViewPort.Width		= static_cast<Float>(Renderer::GetGlobalLightSettings().ShadowMapWidth);
	ViewPort.Height		= static_cast<Float>(Renderer::GetGlobalLightSettings().ShadowMapHeight);
	ViewPort.MinDepth	= 0.0f;
	ViewPort.MaxDepth	= 1.0f;
	ViewPort.TopLeftX	= 0.0f;
	ViewPort.TopLeftY	= 0.0f;
	CommandList->RSSetViewports(&ViewPort, 1);

	D3D12_RECT ScissorRect =
	{
		0,
		0,
		static_cast<LONG>(Renderer::GetGlobalLightSettings().ShadowMapWidth),
		static_cast<LONG>(Renderer::GetGlobalLightSettings().ShadowMapHeight)
	};
	CommandList->RSSetScissorRects(&ScissorRect, 1);
	CommandList->SetGraphicsRootSignature(ShadowMapRootSignature->GetRootSignature());
	CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// PerObject Structs
	struct ShadowPerObject
	{
		XMFLOAT4X4 Matrix;
		Float ShadowOffset;
	} ShadowPerObjectBuffer;

	struct PerLight
	{
		XMFLOAT4X4	Matrix;
		XMFLOAT3	Position;
		Float		FarPlane;
	} PerLightBuffer;

	D3D12_VERTEX_BUFFER_VIEW	VBO = { };
	D3D12_INDEX_BUFFER_VIEW		IBV = { };
	for (Light* Light : CurrentScene.GetLights())
	{
		if (IsSubClassOf<DirectionalLight>(Light))
		{
			DirectionalLight* DirLight = Cast<DirectionalLight>(Light);
			PerLightBuffer.Matrix	= DirLight->GetMatrix();
			PerLightBuffer.Position	= DirLight->GetShadowMapPosition();
			PerLightBuffer.FarPlane	= DirLight->GetShadowFarPlane();
			CommandList->SetGraphicsRoot32BitConstants(&PerLightBuffer, 20, 0, 1);

			// Draw all objects to depthbuffer
			for (const MeshDrawCommand& Command : CurrentScene.GetMeshDrawCommands())
			{
				VBO.BufferLocation	= Command.VertexBuffer->GetGPUVirtualAddress();
				VBO.SizeInBytes		= Command.VertexBuffer->GetSizeInBytes();
				VBO.StrideInBytes	= sizeof(Vertex);
				CommandList->IASetVertexBuffers(0, &VBO, 1);

				IBV.BufferLocation	= Command.IndexBuffer->GetGPUVirtualAddress();
				IBV.SizeInBytes		= Command.IndexBuffer->GetSizeInBytes();
				IBV.Format			= DXGI_FORMAT_R32_UINT;
				CommandList->IASetIndexBuffer(&IBV);

				ShadowPerObjectBuffer.Matrix		= Command.CurrentActor->GetTransform().GetMatrix();
				ShadowPerObjectBuffer.ShadowOffset	= Command.Mesh->ShadowOffset;
				CommandList->SetGraphicsRoot32BitConstants(&ShadowPerObjectBuffer, 17, 0, 0);

				CommandList->DrawIndexedInstanced(Command.IndexCount, 1, 0, 0, 0);
			}

			break;
		}
	}

	// Render PointLight ShadowMaps
	const UInt32 PointLightShadowSize = Renderer::GetGlobalLightSettings().PointLightShadowSize;
	ViewPort.Width		= static_cast<Float>(PointLightShadowSize);
	ViewPort.Height		= static_cast<Float>(PointLightShadowSize);
	ViewPort.MinDepth	= 0.0f;
	ViewPort.MaxDepth	= 1.0f;
	ViewPort.TopLeftX	= 0.0f;
	ViewPort.TopLeftY	= 0.0f;
	CommandList->RSSetViewports(&ViewPort, 1);

	ScissorRect =
	{
		0,
		0,
		static_cast<LONG>(PointLightShadowSize),
		static_cast<LONG>(PointLightShadowSize)
	};
	CommandList->RSSetScissorRects(&ScissorRect, 1);

	CommandList->SetPipelineState(LinearShadowMapPSO->GetPipelineState());
	for (Light* Light : CurrentScene.GetLights())
	{
		if (IsSubClassOf<PointLight>(Light))
		{
			PointLight* PoiLight = Cast<PointLight>(Light);
			for (UInt32 I = 0; I < 6; I++)
			{
				CommandList->ClearDepthStencilView(PointLightShadowMaps->GetDepthStencilView(I).Get(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0);
				CommandList->OMSetRenderTargets(nullptr, 0, PointLightShadowMaps->GetDepthStencilView(I).Get());

				PerLightBuffer.Matrix	= PoiLight->GetMatrix(I);
				PerLightBuffer.Position	= PoiLight->GetPosition();
				PerLightBuffer.FarPlane	= PoiLight->GetShadowFarPlane();
				CommandList->SetGraphicsRoot32BitConstants(&PerLightBuffer, 20, 0, 1);

				// Draw all objects to depthbuffer
				if (FrustumCullEnabled)
				{
					Frustum CameraFrustum = Frustum(PoiLight->GetShadowFarPlane(), PoiLight->GetViewMatrix(I), PoiLight->GetProjectionMatrix(I));
					for (const MeshDrawCommand& Command : CurrentScene.GetMeshDrawCommands())
					{
						const XMFLOAT4X4& Transform = Command.CurrentActor->GetTransform().GetMatrix();
						XMMATRIX XmTransform	= XMMatrixTranspose(XMLoadFloat4x4(&Transform));
						XMVECTOR XmTop			= XMVectorSetW(XMLoadFloat3(&Command.Mesh->BoundingBox.Top), 1.0f);
						XMVECTOR XmBottom		= XMVectorSetW(XMLoadFloat3(&Command.Mesh->BoundingBox.Bottom), 1.0f);
						XmTop		= XMVector4Transform(XmTop, XmTransform);
						XmBottom	= XMVector4Transform(XmBottom, XmTransform);

						AABB Box;
						XMStoreFloat3(&Box.Top, XmTop);
						XMStoreFloat3(&Box.Bottom, XmBottom);
						if (CameraFrustum.CheckAABB(Box))
						{
							VBO.BufferLocation	= Command.VertexBuffer->GetGPUVirtualAddress();
							VBO.SizeInBytes		= Command.VertexBuffer->GetSizeInBytes();
							VBO.StrideInBytes	= sizeof(Vertex);
							CommandList->IASetVertexBuffers(0, &VBO, 1);

							IBV.BufferLocation	= Command.IndexBuffer->GetGPUVirtualAddress();
							IBV.SizeInBytes		= Command.IndexBuffer->GetSizeInBytes();
							IBV.Format			= DXGI_FORMAT_R32_UINT;
							CommandList->IASetIndexBuffer(&IBV);

							ShadowPerObjectBuffer.Matrix		= Command.CurrentActor->GetTransform().GetMatrix();
							ShadowPerObjectBuffer.ShadowOffset	= Command.Mesh->ShadowOffset;
							CommandList->SetGraphicsRoot32BitConstants(&ShadowPerObjectBuffer, 17, 0, 0);

							CommandList->DrawIndexedInstanced(Command.IndexCount, 1, 0, 0, 0);
						}
					}
				}
				else
				{
					for (const MeshDrawCommand& Command : CurrentScene.GetMeshDrawCommands())
					{
						VBO.BufferLocation	= Command.VertexBuffer->GetGPUVirtualAddress();
						VBO.SizeInBytes		= Command.VertexBuffer->GetSizeInBytes();
						VBO.StrideInBytes	= sizeof(Vertex);
						CommandList->IASetVertexBuffers(0, &VBO, 1);

						IBV.BufferLocation	= Command.IndexBuffer->GetGPUVirtualAddress();
						IBV.SizeInBytes		= Command.IndexBuffer->GetSizeInBytes();
						IBV.Format			= DXGI_FORMAT_R32_UINT;
						CommandList->IASetIndexBuffer(&IBV);

						ShadowPerObjectBuffer.Matrix		= Command.CurrentActor->GetTransform().GetMatrix();
						ShadowPerObjectBuffer.ShadowOffset	= Command.Mesh->ShadowOffset;
						CommandList->SetGraphicsRoot32BitConstants(&ShadowPerObjectBuffer, 17, 0, 0);

						CommandList->DrawIndexedInstanced(Command.IndexCount, 1, 0, 0, 0);
					}
				}
			}

			break;
		}
	}

	// Transition ShadowMaps
#if ENABLE_VSM
	CommandList->TransitionBarrier(VSMDirLightShadowMaps.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
#endif
	CommandList->TransitionBarrier(DirLightShadowMaps.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	CommandList->TransitionBarrier(PointLightShadowMaps.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	// Update camerabuffer
	struct CameraBufferDesc
	{
		XMFLOAT4X4 ViewProjection;
		XMFLOAT4X4 View;
		XMFLOAT4X4 ViewInv;
		XMFLOAT4X4 Projection;
		XMFLOAT4X4 ProjectionInv;
		XMFLOAT4X4 ViewProjectionInv;
		XMFLOAT3 Position;
		Float NearPlane;
		Float FarPlane;
		Float AspectRatio;
	} CamBuff;

	CamBuff.ViewProjection		= CurrentScene.GetCamera()->GetViewProjectionMatrix();
	CamBuff.View				= CurrentScene.GetCamera()->GetViewMatrix();
	CamBuff.ViewInv				= CurrentScene.GetCamera()->GetViewInverseMatrix();
	CamBuff.Projection			= CurrentScene.GetCamera()->GetProjectionMatrix();
	CamBuff.ProjectionInv		= CurrentScene.GetCamera()->GetProjectionInverseMatrix();
	CamBuff.ViewProjectionInv	= CurrentScene.GetCamera()->GetViewProjectionInverseMatrix();
	CamBuff.Position			= CurrentScene.GetCamera()->GetPosition();
	CamBuff.NearPlane			= CurrentScene.GetCamera()->GetNearPlane();
	CamBuff.FarPlane			= CurrentScene.GetCamera()->GetFarPlane();
	CamBuff.AspectRatio			= CurrentScene.GetCamera()->GetAspectRatio();

	CommandList->TransitionBarrier(CameraBuffer.Get(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_COPY_DEST);
	CommandList->UploadBufferData(CameraBuffer.Get(), 0, &CamBuff, sizeof(CameraBufferDesc));
	CommandList->TransitionBarrier(CameraBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

	// Clear GBuffer
	const Float BlackClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	CommandList->ClearRenderTargetView(GBuffer[GBUFFER_ALBEDO_INDEX]->GetRenderTargetView(0).Get(), BlackClearColor);
	CommandList->ClearRenderTargetView(GBuffer[GBUFFER_NORMAL_INDEX]->GetRenderTargetView(0).Get(), BlackClearColor);
	CommandList->ClearRenderTargetView(GBuffer[GBUFFER_MATERIAL_INDEX]->GetRenderTargetView(0).Get(), BlackClearColor);
	CommandList->ClearDepthStencilView(GBuffer[GBUFFER_DEPTH_INDEX]->GetDepthStencilView(0).Get(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0);

	// Setup view
	ViewPort.Width		= static_cast<Float>(RenderingAPI::Get().GetSwapChain()->GetWidth());
	ViewPort.Height		= static_cast<Float>(RenderingAPI::Get().GetSwapChain()->GetHeight());
	ViewPort.MinDepth	= 0.0f;
	ViewPort.MaxDepth	= 1.0f;
	ViewPort.TopLeftX	= 0.0f;
	ViewPort.TopLeftY	= 0.0f;
	CommandList->RSSetViewports(&ViewPort, 1);

	ScissorRect =
	{
		0,
		0,
		static_cast<LONG>(RenderingAPI::Get().GetSwapChain()->GetWidth()),
		static_cast<LONG>(RenderingAPI::Get().GetSwapChain()->GetHeight())
	};
	CommandList->RSSetScissorRects(&ScissorRect, 1);

	// Perform PrePass
	if (PrePassEnabled)
	{
		struct PerObject
		{
			XMFLOAT4X4 Matrix;
		} PerObjectBuffer;

		// Setup Pipeline
		CommandList->OMSetRenderTargets(nullptr, 0, GBuffer[GBUFFER_DEPTH_INDEX]->GetDepthStencilView(0).Get());

		CommandList->SetPipelineState(PrePassPSO->GetPipelineState());
		CommandList->SetGraphicsRootSignature(PrePassRootSignature->GetRootSignature());
		CommandList->SetGraphicsRootDescriptorTable(PrePassDescriptorTable->GetGPUTableStartHandle(), 1);

		// Draw all objects to depthbuffer
		for (const MeshDrawCommand& Command : DeferredVisibleCommands)
		{
			VBO.BufferLocation	= Command.VertexBuffer->GetGPUVirtualAddress();
			VBO.SizeInBytes		= Command.VertexBuffer->GetSizeInBytes();
			VBO.StrideInBytes	= sizeof(Vertex);
			CommandList->IASetVertexBuffers(0, &VBO, 1);

			IBV.BufferLocation	= Command.IndexBuffer->GetGPUVirtualAddress();
			IBV.SizeInBytes		= Command.IndexBuffer->GetSizeInBytes();
			IBV.Format			= DXGI_FORMAT_R32_UINT;
			CommandList->IASetIndexBuffer(&IBV);

			PerObjectBuffer.Matrix = Command.CurrentActor->GetTransform().GetMatrix();
			CommandList->SetGraphicsRoot32BitConstants(&PerObjectBuffer, 16, 0, 0);

			CommandList->DrawIndexedInstanced(Command.IndexCount, 1, 0, 0, 0);
		}
	}

	// Render all objects to the GBuffer
	constexpr UInt32 NumRTVs = 3;
	D3D12RenderTargetView* GBufferRTVS[NumRTVs] =
	{
		GBuffer[GBUFFER_ALBEDO_INDEX]->GetRenderTargetView(0).Get(),
		GBuffer[GBUFFER_NORMAL_INDEX]->GetRenderTargetView(0).Get(),
		GBuffer[GBUFFER_MATERIAL_INDEX]->GetRenderTargetView(0).Get()
	};
	CommandList->OMSetRenderTargets(GBufferRTVS, NumRTVs, GBuffer[GBUFFER_DEPTH_INDEX]->GetDepthStencilView(0).Get());

	// Setup Pipeline
	CommandList->SetPipelineState(GeometryPSO->GetPipelineState());
	CommandList->SetGraphicsRootSignature(GeometryRootSignature->GetRootSignature());
	CommandList->SetGraphicsRootDescriptorTable(GeometryDescriptorTable->GetGPUTableStartHandle(), 1);

	struct TransformBuffer
	{
		XMFLOAT4X4 Transform;
		XMFLOAT4X4 TransformInv;
	} TransformPerObject;

	for (const MeshDrawCommand& Command : DeferredVisibleCommands)
	{
		VBO.BufferLocation	= Command.VertexBuffer->GetGPUVirtualAddress();
		VBO.SizeInBytes		= Command.VertexBuffer->GetSizeInBytes();
		VBO.StrideInBytes	= sizeof(Vertex);
		CommandList->IASetVertexBuffers(0, &VBO, 1);

		IBV.BufferLocation	= Command.IndexBuffer->GetGPUVirtualAddress();
		IBV.SizeInBytes		= Command.IndexBuffer->GetSizeInBytes();
		IBV.Format			= DXGI_FORMAT_R32_UINT;
		CommandList->IASetIndexBuffer(&IBV);

		if (Command.Material->IsBufferDirty())
		{
			Command.Material->BuildBuffer(CommandList.Get());
		}
		CommandList->SetGraphicsRootDescriptorTable(Command.Material->GetDescriptorTable()->GetGPUTableStartHandle(), 2);

		TransformPerObject.Transform	= Command.CurrentActor->GetTransform().GetMatrix();
		TransformPerObject.TransformInv	= Command.CurrentActor->GetTransform().GetMatrixInverse();
		CommandList->SetGraphicsRoot32BitConstants(&TransformPerObject, 32, 0, 0);

		CommandList->DrawIndexedInstanced(Command.IndexCount, 1, 0, 0, 0);
	}

	// Setup GBuffer for Read
	CommandList->TransitionBarrier(GBuffer[GBUFFER_ALBEDO_INDEX].Get(),		D3D12_RESOURCE_STATE_RENDER_TARGET,	D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	CommandList->TransitionBarrier(GBuffer[GBUFFER_NORMAL_INDEX].Get(),		D3D12_RESOURCE_STATE_RENDER_TARGET,	D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	CommandList->TransitionBarrier(GBuffer[GBUFFER_MATERIAL_INDEX].Get(),	D3D12_RESOURCE_STATE_RENDER_TARGET,	D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	CommandList->TransitionBarrier(GBuffer[GBUFFER_DEPTH_INDEX].Get(),		D3D12_RESOURCE_STATE_DEPTH_WRITE,	D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	// RayTracing
	if (RenderingAPI::Get().IsRayTracingSupported() && RayTracingEnabled)
	{
		TraceRays(BackBuffer, CommandList.Get());
		RayTracingGeometryInstances.Clear();
	}

	// SSAO
	CommandList->TransitionBarrier(SSAOBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	const Float WhiteColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	CommandList->ClearUnorderedAccessViewFloat(
		SSAODescriptorTable->GetGPUTableHandle(3),
		SSAOBuffer->GetUnorderedAccessView(0).Get(),
		WhiteColor);

	if (SSAOEnabled)
	{
		struct SSAOSettings
		{
			XMFLOAT2 ScreenSize;
			XMFLOAT2 NoiseSize;
			Float Radius;
			Float Bias;
			Int32 KernelSize;
		} SSAOSettings;

		const UInt32 Width	= RenderingAPI::Get().GetSwapChain()->GetWidth();
		const UInt32 Height	= RenderingAPI::Get().GetSwapChain()->GetHeight();
		SSAOSettings.ScreenSize	= XMFLOAT2(Float(Width), Float(Height));
		SSAOSettings.NoiseSize	= XMFLOAT2(4.0f, 4.0f);
		SSAOSettings.Radius		= SSAORadius;
		SSAOSettings.KernelSize = SSAOKernelSize;
		SSAOSettings.Bias		= SSAOBias;

		CommandList->SetComputeRootSignature(SSAORootSignature->GetRootSignature());
		CommandList->SetComputeRootDescriptorTable(SSAODescriptorTable->GetGPUTableStartHandle(), 0);
		CommandList->SetComputeRoot32BitConstants(&SSAOSettings, 7, 0, 1);

		CommandList->SetPipelineState(SSAOPSO->GetPipeline());

		constexpr UInt32 ThreadCount = 32;
		const UInt32 DispatchWidth	= Math::AlignUp<UInt32>(Width, ThreadCount) / ThreadCount;
		const UInt32 DispatchHeight = Math::AlignUp<UInt32>(Height, ThreadCount) / ThreadCount;
		CommandList->Dispatch(DispatchWidth, DispatchHeight, 1);

		CommandList->UnorderedAccessBarrier(SSAOBuffer.Get());

		CommandList->SetComputeRootSignature(BlurRootSignature->GetRootSignature());
		CommandList->SetComputeRootDescriptorTable(SSAOBlurDescriptorTable->GetGPUTableStartHandle(), 0);
		CommandList->SetComputeRoot32BitConstants(&SSAOSettings.ScreenSize, 2, 0, 1);

		CommandList->SetPipelineState(SSAOBlur->GetPipeline());

		CommandList->Dispatch(DispatchWidth, DispatchHeight, 1);

		CommandList->UnorderedAccessBarrier(SSAOBuffer.Get());
	}

	CommandList->TransitionBarrier(SSAOBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	// Render to final output
	CommandList->TransitionBarrier(FinalTarget.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	CommandList->TransitionBarrier(BackBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	D3D12RenderTargetView* RenderTarget[] = { FinalTarget->GetRenderTargetView(0).Get() };
	CommandList->OMSetRenderTargets(RenderTarget, 1, nullptr);

	CommandList->RSSetViewports(&ViewPort, 1);
	CommandList->RSSetScissorRects(&ScissorRect, 1);

	// Setup LightPass
	CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	CommandList->SetPipelineState(LightPassPSO->GetPipelineState());
	CommandList->SetGraphicsRootSignature(LightRootSignature->GetRootSignature());
	CommandList->SetGraphicsRootDescriptorTable(LightDescriptorTable->GetGPUTableStartHandle(), 0);

	// Perform LightPass
	CommandList->DrawInstanced(3, 1, 0, 0);

	// Draw skybox
	CommandList->TransitionBarrier(GBuffer[GBUFFER_DEPTH_INDEX].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	CommandList->OMSetRenderTargets(RenderTarget, 1, GBuffer[GBUFFER_DEPTH_INDEX]->GetDepthStencilView(0).Get());
	
	CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3D12_VERTEX_BUFFER_VIEW SkyboxVBO = { };
	SkyboxVBO.BufferLocation	= SkyboxVertexBuffer->GetGPUVirtualAddress();
	SkyboxVBO.SizeInBytes		= SkyboxVertexBuffer->GetSizeInBytes();
	SkyboxVBO.StrideInBytes		= sizeof(Vertex);
	CommandList->IASetVertexBuffers(0, &SkyboxVBO, 1);

	D3D12_INDEX_BUFFER_VIEW SkyboxIBV = { };
	SkyboxIBV.BufferLocation	= SkyboxIndexBuffer->GetGPUVirtualAddress();
	SkyboxIBV.SizeInBytes		= SkyboxIndexBuffer->GetSizeInBytes();
	SkyboxIBV.Format			= DXGI_FORMAT_R32_UINT;
	CommandList->IASetIndexBuffer(&SkyboxIBV);

	CommandList->SetPipelineState(SkyboxPSO->GetPipelineState());
	CommandList->SetGraphicsRootSignature(SkyboxRootSignature->GetRootSignature());
	
	struct SimpleCameraBuffer
	{
		XMFLOAT4X4 Matrix;
	} SimpleCamera;
	SimpleCamera.Matrix = CurrentScene.GetCamera()->GetViewProjectionWitoutTranslateMatrix();
	CommandList->SetGraphicsRoot32BitConstants(&SimpleCamera, 16, 0, 0);
	CommandList->SetGraphicsRootDescriptorTable(SkyboxDescriptorTable->GetGPUTableStartHandle(), 1);

	CommandList->DrawIndexedInstanced(static_cast<UInt32>(SkyboxMesh.Indices.Size()), 1, 0, 0, 0);

	// Render to BackBuffer
	CommandList->TransitionBarrier(FinalTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	
	RenderTarget[0] = BackBuffer->GetRenderTargetView(0).Get();
	CommandList->OMSetRenderTargets(RenderTarget, 1, nullptr);

	CommandList->IASetVertexBuffers(0, nullptr, 0);
	CommandList->IASetIndexBuffer(nullptr);

	CommandList->SetGraphicsRootSignature(PostRootSignature->GetRootSignature());
	CommandList->SetGraphicsRootDescriptorTable(PostDescriptorTable->GetGPUTableStartHandle(), 0);
	
	if (FXAAEnabled)
	{
		struct FXAASettings
		{
			Float Width;
			Float Height;
		} Settings;

		Settings.Width	= Float(RenderingAPI::Get().GetSwapChain()->GetWidth());
		Settings.Height	= Float(RenderingAPI::Get().GetSwapChain()->GetHeight());

		CommandList->SetGraphicsRoot32BitConstants(&Settings, 2, 0, 1);
		CommandList->SetPipelineState(FXAAPSO->GetPipelineState());
	}
	else
	{
		CommandList->SetPipelineState(PostPSO->GetPipelineState());
	}

	CommandList->DrawInstanced(3, 1, 0, 0);

	// Forward Pass
	ViewPort.Width		= static_cast<Float>(RenderingAPI::Get().GetSwapChain()->GetWidth());
	ViewPort.Height		= static_cast<Float>(RenderingAPI::Get().GetSwapChain()->GetHeight());
	ViewPort.MinDepth	= 0.0f;
	ViewPort.MaxDepth	= 1.0f;
	ViewPort.TopLeftX	= 0.0f;
	ViewPort.TopLeftY	= 0.0f;
	CommandList->RSSetViewports(&ViewPort, 1);

	ScissorRect =
	{
		0,
		0,
		static_cast<LONG>(RenderingAPI::Get().GetSwapChain()->GetWidth()),
		static_cast<LONG>(RenderingAPI::Get().GetSwapChain()->GetHeight())
	};
	CommandList->RSSetScissorRects(&ScissorRect, 1);

	// Render all transparent objects
	RenderTarget[0] = BackBuffer->GetRenderTargetView(0).Get();
	CommandList->OMSetRenderTargets(RenderTarget, 1, GBuffer[GBUFFER_DEPTH_INDEX]->GetDepthStencilView(0).Get());

	// Setup Pipeline
	CommandList->SetGraphicsRootSignature(ForwardRootSignature->GetRootSignature());
	CommandList->SetGraphicsRootDescriptorTable(ForwardDescriptorTable->GetGPUTableStartHandle(), 1);

	CommandList->SetPipelineState(ForwardPSO->GetPipelineState());
	for (const MeshDrawCommand& Command : ForwardVisibleCommands)
	{
		VBO.BufferLocation	= Command.VertexBuffer->GetGPUVirtualAddress();
		VBO.SizeInBytes		= Command.VertexBuffer->GetSizeInBytes();
		VBO.StrideInBytes	= sizeof(Vertex);
		CommandList->IASetVertexBuffers(0, &VBO, 1);

		IBV.BufferLocation	= Command.IndexBuffer->GetGPUVirtualAddress();
		IBV.SizeInBytes		= Command.IndexBuffer->GetSizeInBytes();
		IBV.Format			= DXGI_FORMAT_R32_UINT;
		CommandList->IASetIndexBuffer(&IBV);

		if (Command.Material->IsBufferDirty())
		{
			Command.Material->BuildBuffer(CommandList.Get());
		}
		CommandList->SetGraphicsRootDescriptorTable(Command.Material->GetDescriptorTable()->GetGPUTableStartHandle(), 2);

		TransformPerObject.Transform	= Command.CurrentActor->GetTransform().GetMatrix();
		TransformPerObject.TransformInv	= Command.CurrentActor->GetTransform().GetMatrixInverse();
		CommandList->SetGraphicsRoot32BitConstants(&TransformPerObject, 32, 0, 0);

		CommandList->DrawIndexedInstanced(Command.IndexCount, 1, 0, 0, 0);
	}

	// Draw DebugBoxes
	if (DrawAABBs)
	{
		CommandList->SetPipelineState(DebugBoxPSO->GetPipelineState());
		CommandList->SetGraphicsRootSignature(DebugRootSignature->GetRootSignature());
		CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

		SimpleCamera.Matrix = CurrentScene.GetCamera()->GetViewProjectionMatrix();
		CommandList->SetGraphicsRoot32BitConstants(&SimpleCamera, 16, 0, 1);

		D3D12_VERTEX_BUFFER_VIEW DebugVBO = { };
		DebugVBO.BufferLocation = AABBVertexBuffer->GetGPUVirtualAddress();
		DebugVBO.SizeInBytes	= AABBVertexBuffer->GetSizeInBytes();
		DebugVBO.StrideInBytes	= sizeof(XMFLOAT3);
		CommandList->IASetVertexBuffers(0, &DebugVBO, 1);

		D3D12_INDEX_BUFFER_VIEW DebugIBV = { };
		DebugIBV.BufferLocation = AABBIndexBuffer->GetGPUVirtualAddress();
		DebugIBV.SizeInBytes	= AABBIndexBuffer->GetSizeInBytes();
		DebugIBV.Format			= DXGI_FORMAT_R16_UINT;
		CommandList->IASetIndexBuffer(&DebugIBV);

		for (const MeshDrawCommand& Command : DeferredVisibleCommands)
		{
			AABB& Box = Command.Mesh->BoundingBox;
			XMFLOAT3 Scale = XMFLOAT3(Box.GetWidth(), Box.GetHeight(), Box.GetDepth());
			XMFLOAT3 Position = Box.GetCenter();

			XMMATRIX XmTranslation = XMMatrixTranslation(Position.x, Position.y, Position.z);
			XMMATRIX XmScale = XMMatrixScaling(Scale.x, Scale.y, Scale.z);

			XMFLOAT4X4 Transform = Command.CurrentActor->GetTransform().GetMatrix();
			XMMATRIX XmTransform = XMMatrixTranspose(XMLoadFloat4x4(&Transform));
			XMStoreFloat4x4(&Transform, XMMatrixMultiplyTranspose(XMMatrixMultiply(XmScale, XmTranslation), XmTransform));

			CommandList->SetGraphicsRoot32BitConstants(&Transform, 16, 0, 0);
			CommandList->DrawIndexedInstanced(24, 1, 0, 0, 0);
		}
	}

	// Render UI
	DebugUI::DrawDebugString("DrawCall Count: " + std::to_string(CommandList->GetNumDrawCalls()));
	DebugUI::Render(CommandList.Get());

	// Finalize Commandlist
	CommandList->TransitionBarrier(GBuffer[GBUFFER_DEPTH_INDEX].Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	CommandList->TransitionBarrier(BackBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	CommandList->Close();

	// Execute
	RenderingAPI::Get().GetQueue()->ExecuteCommandList(CommandList.Get());

	// Present
	RenderingAPI::Get().GetSwapChain()->Present(VSyncEnabled ? 1 : 0);

	// Wait for next frame
	const UInt64 CurrentFenceValue = FenceValues[CurrentBackBufferIndex];
	RenderingAPI::Get().GetQueue()->SignalFence(Fence.Get(), CurrentFenceValue);

	CurrentBackBufferIndex = RenderingAPI::Get().GetSwapChain()->GetCurrentBackBufferIndex();
	if (Fence->WaitForValue(CurrentFenceValue))
	{
		FenceValues[CurrentBackBufferIndex] = CurrentFenceValue + 1;
	}
}

void Renderer::TraceRays(D3D12Texture* BackBuffer, D3D12CommandList* InCommandList)
{
	UNREFERENCED_VARIABLE(BackBuffer);

	InCommandList->TransitionBarrier(ReflectionTexture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	D3D12_DISPATCH_RAYS_DESC raytraceDesc = {};
	raytraceDesc.Width	= static_cast<UInt32>(ReflectionTexture->GetDesc().Width);
	raytraceDesc.Height = static_cast<UInt32>(ReflectionTexture->GetDesc().Height);
	raytraceDesc.Depth	= 1;

	// Set shader tables
	raytraceDesc.RayGenerationShaderRecord	= RayTracingScene->GetRayGenerationShaderRecord();
	raytraceDesc.MissShaderTable			= RayTracingScene->GetMissShaderTable();
	raytraceDesc.HitGroupTable				= RayTracingScene->GetHitGroupTable();

	// Bind the empty root signature
	InCommandList->SetComputeRootSignature(GlobalRootSignature->GetRootSignature());
	InCommandList->SetComputeRootDescriptorTable(GlobalDescriptorTable->GetGPUTableStartHandle(), 0);

	// Dispatch
	InCommandList->SetStateObject(RaytracingPSO->GetStateObject());
	InCommandList->DispatchRays(&raytraceDesc);

	// Copy the results to the back-buffer
	InCommandList->TransitionBarrier(ReflectionTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
}

bool Renderer::OnEvent(const Event& Event)
{
	if (!IsOfEventType<WindowResizeEvent>(Event))
	{
		return false;
	}

	return false;

	const WindowResizeEvent& ResizeEvent = EventCast<WindowResizeEvent>(Event);
	const UInt32 Width	= ResizeEvent.GetWidth();
	const UInt32 Height	= ResizeEvent.GetHeight();

	WaitForPendingFrames();

	RenderingAPI::Get().GetSwapChain()->Resize(Width, Height);

	// Update deferred
	InitGBuffer();

	if (RenderingAPI::Get().IsRayTracingSupported() && RayTracingEnabled)
	{
		RayGenDescriptorTable->SetUnorderedAccessView(ReflectionTexture->GetUnorderedAccessView(0).Get(), 0);
		RayGenDescriptorTable->CopyDescriptors();

		GlobalDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_NORMAL_INDEX]->GetShaderResourceView(0).Get(), 5);
		GlobalDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_DEPTH_INDEX]->GetShaderResourceView(0).Get(), 6);
		GlobalDescriptorTable->CopyDescriptors();
	}

	LightDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_ALBEDO_INDEX]->GetShaderResourceView(0).Get(), 0);
	LightDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_NORMAL_INDEX]->GetShaderResourceView(0).Get(), 1);
	LightDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_MATERIAL_INDEX]->GetShaderResourceView(0).Get(), 2);
	LightDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_DEPTH_INDEX]->GetShaderResourceView(0).Get(), 3);
	LightDescriptorTable->CopyDescriptors();

	CurrentBackBufferIndex = RenderingAPI::Get().GetSwapChain()->GetCurrentBackBufferIndex();

	return true;
}

void Renderer::SetPrePassEnable(bool Enabled)
{
	PrePassEnabled = Enabled;
}

void Renderer::SetVerticalSyncEnable(bool Enabled)
{
	VSyncEnabled = Enabled;
}

void Renderer::SetDrawAABBsEnable(bool Enabled)
{
	DrawAABBs = Enabled;
}

void Renderer::SetFrustumCullEnable(bool Enabled)
{
	FrustumCullEnabled = Enabled;
}

void Renderer::SetFXAAEnable(bool Enabled)
{
	FXAAEnabled = Enabled;
}

void Renderer::SetSSAOEnable(bool Enabled)
{
	SSAOEnabled = Enabled;
}

void Renderer::SetGlobalLightSettings(const LightSettings& InGlobalLightSettings)
{
	// Set Settings
	GlobalLightSettings = InGlobalLightSettings;

	// Recreate ShadowMaps
	Renderer* CurrentRenderer = Renderer::Get();
	if (CurrentRenderer)
	{
		CurrentRenderer->WaitForPendingFrames();

		CurrentRenderer->CreateShadowMaps();
		CurrentRenderer->WriteShadowMapDescriptors();

#if ENABLE_VSM
		RenderingAPI::StaticGetImmediateCommandList()->TransitionBarrier(CurrentRenderer->VSMDirLightShadowMaps.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
#endif
		RenderingAPI::StaticGetImmediateCommandList()->TransitionBarrier(CurrentRenderer->DirLightShadowMaps.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		RenderingAPI::StaticGetImmediateCommandList()->TransitionBarrier(CurrentRenderer->PointLightShadowMaps.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		RenderingAPI::StaticGetImmediateCommandList()->Flush();
	}
}

Renderer* Renderer::Make()
{
	RendererInstance = MakeUnique<Renderer>();
	if (RendererInstance->Initialize())
	{
		return RendererInstance.Get();
	}
	else
	{
		return nullptr;
	}
}

Renderer* Renderer::Get()
{
	return RendererInstance.Get();
}

void Renderer::Release()
{
	RendererInstance.Reset();
}

bool Renderer::Initialize()
{
	const UInt32 BackBufferCount = RenderingAPI::Get().GetSwapChain()->GetSurfaceCount();
	CommandAllocators.Resize(BackBufferCount);
	for (UInt32 i = 0; i < BackBufferCount; i++)
	{
		CommandAllocators[i] = RenderingAPI::Get().CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT);
		if (!CommandAllocators[i])
		{
			return false;
		}
	}

	CommandList = RenderingAPI::Get().CreateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, CommandAllocators[0].Get(), nullptr);
	if (!CommandList)
	{
		return false;
	}

	Fence = RenderingAPI::Get().CreateFence(0);
	if (!Fence)
	{
		return false;
	}

	FenceValues.Resize(RenderingAPI::Get().GetSwapChain()->GetSurfaceCount());

	// Create CameraBuffer
	BufferProperties BufferProps = { };
	BufferProps.SizeInBytes		= 512; // Must be multiple of 256
	BufferProps.Flags			= D3D12_RESOURCE_FLAG_NONE;
	BufferProps.InitalState		= D3D12_RESOURCE_STATE_COMMON;
	BufferProps.MemoryType		= EMemoryType::MEMORY_TYPE_DEFAULT;

	CameraBuffer = RenderingAPI::Get().CreateBuffer(BufferProps);
	if (CameraBuffer)
	{
		D3D12_CONSTANT_BUFFER_VIEW_DESC CameraViewDesc = { };
		CameraViewDesc.BufferLocation	= CameraBuffer->GetGPUVirtualAddress();
		CameraViewDesc.SizeInBytes		= static_cast<UInt32>(BufferProps.SizeInBytes);

		CameraBuffer->SetConstantBufferView(TSharedPtr(RenderingAPI::Get().CreateConstantBufferView(CameraBuffer->GetResource(), &CameraViewDesc)));
	}
	else
	{
		return false;
	}

	// Create mesh
	SkyboxMesh	= MeshFactory::CreateSphere(1);

	// Create vertexbuffer
	BufferProps.InitalState	= D3D12_RESOURCE_STATE_GENERIC_READ;
	BufferProps.SizeInBytes = sizeof(Vertex) * static_cast<UInt64>(SkyboxMesh.Vertices.Size());
	BufferProps.MemoryType	= EMemoryType::MEMORY_TYPE_UPLOAD;

	SkyboxVertexBuffer = RenderingAPI::Get().CreateBuffer(BufferProps);
	if (SkyboxVertexBuffer)
	{
		Void* BufferMemory = SkyboxVertexBuffer->Map();
		memcpy(BufferMemory, SkyboxMesh.Vertices.Data(), BufferProps.SizeInBytes);
		SkyboxVertexBuffer->Unmap();
	}
	else
	{
		return false;
	}

	// Create indexbuffer
	BufferProps.SizeInBytes = sizeof(UInt32) * static_cast<UInt64>(SkyboxMesh.Indices.Size());
	SkyboxIndexBuffer = RenderingAPI::Get().CreateBuffer(BufferProps);
	if (SkyboxIndexBuffer)
	{
		Void* BufferMemory = SkyboxIndexBuffer->Map();
		memcpy(BufferMemory, SkyboxMesh.Indices.Data(), BufferProps.SizeInBytes);
		SkyboxIndexBuffer->Unmap();
	}
	else
	{
		return false;
	}

	// Create Texture Cube
	TUniquePtr<D3D12Texture> Panorama = TUniquePtr<D3D12Texture>(TextureFactory::LoadFromFile(
		"../Assets/Textures/arches.hdr", 
		0, 
		DXGI_FORMAT_R32G32B32A32_FLOAT));
	if (!Panorama)
	{
		return false;
	}

	Skybox = TSharedPtr<D3D12Texture>(
		TextureFactory::CreateTextureCubeFromPanorma(
			Panorama.Get(), 
			1280,
			TEXTURE_FACTORY_FLAGS_GENERATE_MIPS, 
			LightProbeFormat));
	if (!Skybox)
	{
		return false;
	}
	else
	{
		Skybox->SetDebugName("Skybox");
	}

	// Generate global irradiance (From Skybox)
	const UInt16 IrradianceSize = 32;
	TextureProperties IrradianceMapProps = { };
	IrradianceMapProps.DebugName	= "Irradiance Map";
	IrradianceMapProps.Flags		= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	IrradianceMapProps.Width		= IrradianceSize;
	IrradianceMapProps.Height		= IrradianceSize;
	IrradianceMapProps.ArrayCount	= 6;
	IrradianceMapProps.MipLevels	= 1;
	IrradianceMapProps.Format		= LightProbeFormat;
	IrradianceMapProps.MemoryType	= EMemoryType::MEMORY_TYPE_DEFAULT;
	IrradianceMapProps.InitalState	= D3D12_RESOURCE_STATE_COMMON;
	IrradianceMapProps.SampleCount	= 1;

	IrradianceMap = RenderingAPI::Get().CreateTexture(IrradianceMapProps);
	if (!IrradianceMap)
	{
		return false;
	}

	D3D12_UNORDERED_ACCESS_VIEW_DESC UavDesc = { };
	UavDesc.ViewDimension					= D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
	UavDesc.Texture2DArray.ArraySize		= 6;
	UavDesc.Texture2DArray.FirstArraySlice	= 0;
	UavDesc.Texture2DArray.MipSlice			= 0;
	UavDesc.Texture2DArray.PlaneSlice		= 0;
	IrradianceMap->SetUnorderedAccessView(TSharedPtr(RenderingAPI::Get().CreateUnorderedAccessView(nullptr, IrradianceMap->GetResource(), &UavDesc)), 0);

	D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc = { };
	SrvDesc.Format							= IrradianceMapProps.Format;
	SrvDesc.ViewDimension					= D3D12_SRV_DIMENSION_TEXTURECUBE;
	SrvDesc.Shader4ComponentMapping			= D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SrvDesc.TextureCube.MipLevels			= 1;
	SrvDesc.TextureCube.MostDetailedMip		= 0;
	SrvDesc.TextureCube.ResourceMinLODClamp	= 0.0f;
	IrradianceMap->SetShaderResourceView(TSharedPtr(RenderingAPI::Get().CreateShaderResourceView(IrradianceMap->GetResource(), &SrvDesc)), 0);

	// Generate global specular irradiance (From Skybox)
	const UInt16 SpecularIrradianceSize = 128;
	const UInt32 Miplevels				= std::max<UInt32>(UInt32(std::log2<UInt32>(SpecularIrradianceSize)), 1U);
	TextureProperties SpecualarIrradianceMapProps = { };
	SpecualarIrradianceMapProps.DebugName	= "Specular Irradiance Map";
	SpecualarIrradianceMapProps.Flags		= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	SpecualarIrradianceMapProps.Width		= SpecularIrradianceSize;
	SpecualarIrradianceMapProps.Height		= SpecularIrradianceSize;
	SpecualarIrradianceMapProps.ArrayCount	= 6;
	SpecualarIrradianceMapProps.MipLevels	= static_cast<UInt16>(Miplevels);
	SpecualarIrradianceMapProps.Format		= LightProbeFormat;
	SpecualarIrradianceMapProps.MemoryType	= EMemoryType::MEMORY_TYPE_DEFAULT;
	SpecualarIrradianceMapProps.InitalState	= D3D12_RESOURCE_STATE_COMMON;
	SpecualarIrradianceMapProps.SampleCount = 1;

	SpecularIrradianceMap = RenderingAPI::Get().CreateTexture(SpecualarIrradianceMapProps);
	if (!SpecularIrradianceMap)
	{
		Debug::DebugBreak();
		return false;
	}

	UavDesc.ViewDimension					= D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
	UavDesc.Texture2DArray.ArraySize		= 6;
	UavDesc.Texture2DArray.FirstArraySlice	= 0;
	UavDesc.Texture2DArray.PlaneSlice		= 0;
	for (UInt32 Miplevel = 0; Miplevel < Miplevels; Miplevel++)
	{
		UavDesc.Texture2DArray.MipSlice = Miplevel;
		SpecularIrradianceMap->SetUnorderedAccessView(TSharedPtr(RenderingAPI::Get().CreateUnorderedAccessView(nullptr, SpecularIrradianceMap->GetResource(), &UavDesc)), Miplevel);
	}

	SrvDesc.Format							= SpecualarIrradianceMapProps.Format;
	SrvDesc.ViewDimension					= D3D12_SRV_DIMENSION_TEXTURECUBE;
	SrvDesc.Shader4ComponentMapping			= D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SrvDesc.TextureCube.MipLevels			= SpecualarIrradianceMapProps.MipLevels;
	SrvDesc.TextureCube.MostDetailedMip		= 0;
	SrvDesc.TextureCube.ResourceMinLODClamp	= 0.0f;
	SpecularIrradianceMap->SetShaderResourceView(TSharedPtr(RenderingAPI::Get().CreateShaderResourceView(SpecularIrradianceMap->GetResource(), &SrvDesc)), 0);

	GenerateIrradianceMap(Skybox.Get(), IrradianceMap.Get(), RenderingAPI::Get().GetImmediateCommandList().Get());
	RenderingAPI::Get().GetImmediateCommandList()->Flush();

	GenerateSpecularIrradianceMap(Skybox.Get(), SpecularIrradianceMap.Get(), RenderingAPI::Get().GetImmediateCommandList().Get());
	RenderingAPI::Get().GetImmediateCommandList()->Flush();

	// Init Deferred Rendering
	if (!InitLightBuffers())
	{
		return false;
	}

	if (!InitShadowMapPass())
	{
		return false;
	}

	RenderingAPI::Get().GetImmediateCommandList()->TransitionBarrier(PointLightBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	RenderingAPI::Get().GetImmediateCommandList()->TransitionBarrier(DirectionalLightBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	RenderingAPI::Get().GetImmediateCommandList()->TransitionBarrier(PointLightShadowMaps.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	RenderingAPI::Get().GetImmediateCommandList()->TransitionBarrier(DirLightShadowMaps.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	if (VSMDirLightShadowMaps)
	{
		RenderingAPI::Get().GetImmediateCommandList()->TransitionBarrier(VSMDirLightShadowMaps.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
	
	RenderingAPI::Get().GetImmediateCommandList()->Flush();

	if (!InitGBuffer())
	{
		return false;
	}

	if (!InitPrePass())
	{
		return false;
	}
	
	if (!InitDeferred())
	{
		return false;
	}

	if (!InitIntegrationLUT())
	{
		return false;
	}

	if (!InitDebugStates())
	{
		return false;
	}

	if (!InitAA())
	{
		return false;
	}

	if (!InitForwardPass())
	{
		return false;
	}

	if (!InitSSAO())
	{
		return false;
	}

	// Init RayTracing if supported
	if (RenderingAPI::Get().IsRayTracingSupported() && RayTracingEnabled)
	{
		if (!InitRayTracing())
		{
			return false;
		}
	}

	LightDescriptorTable->SetShaderResourceView(ReflectionTexture->GetShaderResourceView(0).Get(), 4);
	LightDescriptorTable->SetShaderResourceView(IrradianceMap->GetShaderResourceView(0).Get(), 5);
	LightDescriptorTable->SetShaderResourceView(SpecularIrradianceMap->GetShaderResourceView(0).Get(), 6);
	LightDescriptorTable->SetShaderResourceView(IntegrationLUT->GetShaderResourceView(0).Get(), 7);
	LightDescriptorTable->SetShaderResourceView(SSAOBuffer->GetShaderResourceView(0).Get(), 13);
	LightDescriptorTable->CopyDescriptors();

	ForwardDescriptorTable->SetShaderResourceView(IrradianceMap->GetShaderResourceView(0).Get(), 3);
	ForwardDescriptorTable->SetShaderResourceView(SpecularIrradianceMap->GetShaderResourceView(0).Get(), 4);
	ForwardDescriptorTable->SetShaderResourceView(IntegrationLUT->GetShaderResourceView(0).Get(), 5);
	ForwardDescriptorTable->CopyDescriptors();

	if (SSAOEnabled)
	{
		SSAODescriptorTable = RenderingAPI::Get().CreateDescriptorTable(6);
		SSAODescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_NORMAL_INDEX]->GetShaderResourceView(0).Get(), 0);
		SSAODescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_DEPTH_INDEX]->GetShaderResourceView(0).Get(), 1);
		SSAODescriptorTable->SetShaderResourceView(SSAONoiseTex->GetShaderResourceView(0).Get(), 2);
		SSAODescriptorTable->SetUnorderedAccessView(SSAOBuffer->GetUnorderedAccessView(0).Get(), 3);
		SSAODescriptorTable->SetShaderResourceView(SSAOSamples->GetShaderResourceView(0).Get(), 4);
		SSAODescriptorTable->SetConstantBufferView(CameraBuffer->GetConstantBufferView().Get(), 5);
		SSAODescriptorTable->CopyDescriptors();

		SSAOBlurDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(1);
		SSAOBlurDescriptorTable->SetUnorderedAccessView(SSAOBuffer->GetUnorderedAccessView(0).Get(), 0);
		SSAOBlurDescriptorTable->CopyDescriptors();
	}

	WriteShadowMapDescriptors();

	// Register EventFunc
	EventQueue::RegisterEventHandler(this, EEventCategory::EVENT_CATEGORY_WINDOW);

	return true;
}

bool Renderer::InitRayTracing()
{
	// Create RootSignatures
	TUniquePtr<D3D12RootSignature> RayGenLocalRoot;
	{
		D3D12_DESCRIPTOR_RANGE Ranges[1] = {};
		Ranges[0].BaseShaderRegister				= 0;
		Ranges[0].NumDescriptors					= 1;
		Ranges[0].RegisterSpace						= 0;
		Ranges[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		Ranges[0].OffsetInDescriptorsFromTableStart	= 0;

		D3D12_ROOT_PARAMETER RootParams = { };
		RootParams.ParameterType						= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		RootParams.ShaderVisibility						= D3D12_SHADER_VISIBILITY_ALL;
		RootParams.DescriptorTable.NumDescriptorRanges	= 1;
		RootParams.DescriptorTable.pDescriptorRanges	= Ranges;

		D3D12_ROOT_SIGNATURE_DESC RayGenLocalRootDesc = {};
		RayGenLocalRootDesc.NumParameters	= 1;
		RayGenLocalRootDesc.pParameters		= &RootParams;
		RayGenLocalRootDesc.Flags			= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

		RayGenLocalRoot = RenderingAPI::Get().CreateRootSignature(RayGenLocalRootDesc);
		if (RayGenLocalRoot)
		{
			RayGenLocalRoot->SetDebugName("RayGen Local RootSignature");
		}
		else
		{
			return false;
		}
	}

	TUniquePtr<D3D12RootSignature> HitLocalRoot;
	{
		constexpr UInt32 NumRanges0 = 7;
		D3D12_DESCRIPTOR_RANGE Ranges0[NumRanges0] = {};
		// Albedo
		Ranges0[0].BaseShaderRegister					= 0;
		Ranges0[0].NumDescriptors						= 1;
		Ranges0[0].RegisterSpace						= 1;
		Ranges0[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges0[0].OffsetInDescriptorsFromTableStart	= 0;

		// Normal
		Ranges0[1].BaseShaderRegister					= 1;
		Ranges0[1].NumDescriptors						= 1;
		Ranges0[1].RegisterSpace						= 1;
		Ranges0[1].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges0[1].OffsetInDescriptorsFromTableStart	= 1;

		// Roughness
		Ranges0[2].BaseShaderRegister					= 2;
		Ranges0[2].NumDescriptors						= 1;
		Ranges0[2].RegisterSpace						= 1;
		Ranges0[2].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges0[2].OffsetInDescriptorsFromTableStart	= 2;

		// Height
		Ranges0[3].BaseShaderRegister					= 3;
		Ranges0[3].NumDescriptors						= 1;
		Ranges0[3].RegisterSpace						= 1;
		Ranges0[3].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges0[3].OffsetInDescriptorsFromTableStart	= 3;

		// Metallic
		Ranges0[4].BaseShaderRegister					= 4;
		Ranges0[4].NumDescriptors						= 1;
		Ranges0[4].RegisterSpace						= 1;
		Ranges0[4].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges0[4].OffsetInDescriptorsFromTableStart	= 4;

		// AO
		Ranges0[5].BaseShaderRegister					= 5;
		Ranges0[5].NumDescriptors						= 1;
		Ranges0[5].RegisterSpace						= 1;
		Ranges0[5].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges0[5].OffsetInDescriptorsFromTableStart	= 5;

		// MaterialBuffer
		Ranges0[6].BaseShaderRegister					= 0;
		Ranges0[6].NumDescriptors						= 1;
		Ranges0[6].RegisterSpace						= 1;
		Ranges0[6].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		Ranges0[6].OffsetInDescriptorsFromTableStart	= 6;

		constexpr UInt32 NumRanges1 = 2;
		D3D12_DESCRIPTOR_RANGE Ranges1[NumRanges1] = {};
		// VertexBuffer
		Ranges1[0].BaseShaderRegister					= 6;
		Ranges1[0].NumDescriptors						= 1;
		Ranges1[0].RegisterSpace						= 1;
		Ranges1[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges1[0].OffsetInDescriptorsFromTableStart	= 0;

		// IndexBuffer
		Ranges1[1].BaseShaderRegister					= 7;
		Ranges1[1].NumDescriptors						= 1;
		Ranges1[1].RegisterSpace						= 1;
		Ranges1[1].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges1[1].OffsetInDescriptorsFromTableStart	= 1;

		const UInt32 NumRootParams = 2;
		D3D12_ROOT_PARAMETER RootParams[NumRootParams];
		RootParams[0].ParameterType							= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		RootParams[0].DescriptorTable.NumDescriptorRanges	= NumRanges0;
		RootParams[0].DescriptorTable.pDescriptorRanges		= Ranges0;
		RootParams[0].ShaderVisibility						= D3D12_SHADER_VISIBILITY_ALL;

		RootParams[1].ParameterType							= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		RootParams[1].DescriptorTable.NumDescriptorRanges	= NumRanges1;
		RootParams[1].DescriptorTable.pDescriptorRanges		= Ranges1;
		RootParams[1].ShaderVisibility						= D3D12_SHADER_VISIBILITY_ALL;

		D3D12_ROOT_SIGNATURE_DESC HitLocalRootDesc = {};
		HitLocalRootDesc.NumParameters	= NumRootParams;
		HitLocalRootDesc.pParameters	= RootParams;
		HitLocalRootDesc.Flags			= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

		HitLocalRoot = RenderingAPI::Get().CreateRootSignature(HitLocalRootDesc);
		if (HitLocalRoot)
		{
			HitLocalRoot->SetDebugName("Closest Hit Local RootSignature");
		}
		else
		{
			return false;
		}
	}

	TUniquePtr<D3D12RootSignature> MissLocalRoot;
	{
		D3D12_ROOT_SIGNATURE_DESC MissLocalRootDesc = {};
		MissLocalRootDesc.NumParameters	= 0;
		MissLocalRootDesc.pParameters	= nullptr;
		MissLocalRootDesc.Flags			= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

		MissLocalRoot = RenderingAPI::Get().CreateRootSignature(MissLocalRootDesc);
		if (MissLocalRoot)
		{
			MissLocalRoot->SetDebugName("Miss Local RootSignature");
		}
		else
		{
			return false;
		}
	}

	// Global RootSignature
	{
		constexpr UInt32 NumRanges = 5;
		D3D12_DESCRIPTOR_RANGE Ranges[NumRanges] = {};
		// AccelerationStructure
		Ranges[0].BaseShaderRegister				= 0;
		Ranges[0].NumDescriptors					= 1;
		Ranges[0].RegisterSpace						= 0;
		Ranges[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges[0].OffsetInDescriptorsFromTableStart	= 0;

		// Camera Buffer
		Ranges[1].BaseShaderRegister				= 0;
		Ranges[1].NumDescriptors					= 1;
		Ranges[1].RegisterSpace						= 0;
		Ranges[1].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		Ranges[1].OffsetInDescriptorsFromTableStart	= 1;

		// GBuffer NormalMap
		Ranges[2].BaseShaderRegister				= 6;
		Ranges[2].NumDescriptors					= 1;
		Ranges[2].RegisterSpace						= 0;
		Ranges[2].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges[2].OffsetInDescriptorsFromTableStart = 2;

		// GBuffer Depth
		Ranges[3].BaseShaderRegister				= 7;
		Ranges[3].NumDescriptors					= 1;
		Ranges[3].RegisterSpace						= 0;
		Ranges[3].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges[3].OffsetInDescriptorsFromTableStart = 3;

		// Skybox
		Ranges[4].BaseShaderRegister				= 1;
		Ranges[4].NumDescriptors					= 1;
		Ranges[4].RegisterSpace						= 0;
		Ranges[4].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges[4].OffsetInDescriptorsFromTableStart	= 4;

		D3D12_ROOT_PARAMETER RootParams = { };
		RootParams.ParameterType						= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		RootParams.ShaderVisibility						= D3D12_SHADER_VISIBILITY_ALL;
		RootParams.DescriptorTable.NumDescriptorRanges	= NumRanges;
		RootParams.DescriptorTable.pDescriptorRanges	= Ranges;

		D3D12_STATIC_SAMPLER_DESC Samplers[2] = { };
		// Generic Sampler
		Samplers[0].ShaderVisibility	= D3D12_SHADER_VISIBILITY_ALL;
		Samplers[0].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		Samplers[0].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		Samplers[0].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		Samplers[0].Filter				= D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		Samplers[0].ShaderRegister		= 0;
		Samplers[0].RegisterSpace		= 0;
		Samplers[0].MinLOD				= 0.0f;
		Samplers[0].MaxLOD				= FLT_MAX;
 
		// GBuffer Sampler
		Samplers[1].ShaderVisibility	= D3D12_SHADER_VISIBILITY_ALL;
		Samplers[1].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		Samplers[1].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		Samplers[1].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		Samplers[1].Filter				= D3D12_FILTER_MIN_MAG_MIP_POINT;
		Samplers[1].ShaderRegister		= 1;
		Samplers[1].RegisterSpace		= 0;
		Samplers[1].MinLOD				= 0.0f;
		Samplers[1].MaxLOD				= FLT_MAX;

		D3D12_ROOT_SIGNATURE_DESC GlobalRootDesc = {};
		GlobalRootDesc.NumStaticSamplers	= 2;
		GlobalRootDesc.pStaticSamplers		= Samplers;
		GlobalRootDesc.NumParameters		= 1;
		GlobalRootDesc.pParameters			= &RootParams;
		GlobalRootDesc.Flags				= D3D12_ROOT_SIGNATURE_FLAG_NONE;

		GlobalRootSignature = RenderingAPI::Get().CreateRootSignature(GlobalRootDesc);
		if (!GlobalRootSignature)
		{
			return false;
		}
	}

	// Create Pipeline
	RayTracingPipelineStateProperties PipelineProperties;
	PipelineProperties.DebugName				= "RayTracing PipelineState";
	PipelineProperties.RayGenRootSignature		= RayGenLocalRoot.Get();
	PipelineProperties.HitGroupRootSignature	= HitLocalRoot.Get();
	PipelineProperties.MissRootSignature		= MissLocalRoot.Get();
	PipelineProperties.GlobalRootSignature		= GlobalRootSignature.Get();
	PipelineProperties.MaxRecursions			= 4;

	RaytracingPSO = RenderingAPI::Get().CreateRayTracingPipelineState(PipelineProperties);
	if (!RaytracingPSO)
	{
		return false;
	}

	// Build Acceleration Structures
	RenderingAPI::Get().GetImmediateCommandList()->TransitionBarrier(CameraBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

	// Create DescriptorTables
	RayGenDescriptorTable = TSharedPtr(RenderingAPI::Get().CreateDescriptorTable(1));
	GlobalDescriptorTable = TSharedPtr(RenderingAPI::Get().CreateDescriptorTable(7));

	// Create TLAS
	RayTracingScene = RenderingAPI::Get().CreateRayTracingScene(RaytracingPSO.Get());
	if (!RayTracingScene)
	{
		return false;
	}

	RenderingAPI::Get().GetImmediateCommandList()->Flush();
	RenderingAPI::Get().GetImmediateCommandList()->WaitForCompletion();

	// Populate descriptors
	RayGenDescriptorTable->SetUnorderedAccessView(ReflectionTexture->GetUnorderedAccessView(0).Get(), 0);
	RayGenDescriptorTable->CopyDescriptors();

	GlobalDescriptorTable->SetConstantBufferView(CameraBuffer->GetConstantBufferView().Get(), 1);
	GlobalDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_NORMAL_INDEX]->GetShaderResourceView(0).Get(), 2);
	GlobalDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_DEPTH_INDEX]->GetShaderResourceView(0).Get(), 3);
	GlobalDescriptorTable->SetShaderResourceView(Skybox->GetShaderResourceView(0).Get(), 4);
	GlobalDescriptorTable->CopyDescriptors();

	return true;
}

bool Renderer::InitLightBuffers()
{
	const UInt32 NumPointLights = 1;
	const UInt32 NumDirLights	= 1;

	// LightBuffers
	BufferProperties Props = { };
	Props.Name			= "PointLight Buffer";
	Props.InitalState	= D3D12_RESOURCE_STATE_COMMON;
	Props.SizeInBytes	= Math::AlignUp<UInt32>(NumPointLights * sizeof(PointLightProperties), 256U);
	Props.MemoryType	= EMemoryType::MEMORY_TYPE_DEFAULT;
	Props.Flags			= D3D12_RESOURCE_FLAG_NONE;

	PointLightBuffer = RenderingAPI::Get().CreateBuffer(Props);
	if (PointLightBuffer)
	{
		D3D12_CONSTANT_BUFFER_VIEW_DESC CBVDesc = { };
		CBVDesc.BufferLocation	= PointLightBuffer->GetGPUVirtualAddress();
		CBVDesc.SizeInBytes		= static_cast<UInt32>(Props.SizeInBytes);
		PointLightBuffer->SetConstantBufferView(TSharedPtr(RenderingAPI::Get().CreateConstantBufferView(PointLightBuffer->GetResource(), &CBVDesc)));
	}
	else
	{
		return false;
	}

	Props.Name			= "DirectionalLight Buffer";
	Props.SizeInBytes	= Math::AlignUp<UInt32>(NumDirLights * sizeof(DirectionalLightProperties), 256U);

	DirectionalLightBuffer = RenderingAPI::Get().CreateBuffer(Props);
	if (DirectionalLightBuffer)
	{
		D3D12_CONSTANT_BUFFER_VIEW_DESC CBVDesc = { };
		CBVDesc.BufferLocation	= DirectionalLightBuffer->GetGPUVirtualAddress();
		CBVDesc.SizeInBytes		= static_cast<UInt32>(Props.SizeInBytes);
		DirectionalLightBuffer->SetConstantBufferView(TSharedPtr(RenderingAPI::Get().CreateConstantBufferView(DirectionalLightBuffer->GetResource(), &CBVDesc)));
	}
	else
	{
		return false;
	}

	return CreateShadowMaps();
}

bool Renderer::InitPrePass()
{
	using namespace Microsoft::WRL;

	ComPtr<IDxcBlob> VSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/PrePass.hlsl", "Main", "vs_6_0");
	if (!VSBlob)
	{
		return false;
	}

	// Init RootSignature
	D3D12_DESCRIPTOR_RANGE PerFrameRanges[1] = {};
	// Camera Buffer
	PerFrameRanges[0].BaseShaderRegister				= 1;
	PerFrameRanges[0].NumDescriptors					= 1;
	PerFrameRanges[0].RegisterSpace						= 0;
	PerFrameRanges[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	PerFrameRanges[0].OffsetInDescriptorsFromTableStart	= 0;

	// Transform Constants
	D3D12_ROOT_PARAMETER Parameters[2];
	Parameters[0].ParameterType				= D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	Parameters[0].Constants.ShaderRegister	= 0;
	Parameters[0].Constants.RegisterSpace	= 0;
	Parameters[0].Constants.Num32BitValues	= 16;
	Parameters[0].ShaderVisibility			= D3D12_SHADER_VISIBILITY_VERTEX;

	// PerFrame DescriptorTable
	Parameters[1].ParameterType							= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	Parameters[1].DescriptorTable.NumDescriptorRanges	= 1;
	Parameters[1].DescriptorTable.pDescriptorRanges		= PerFrameRanges;
	Parameters[1].ShaderVisibility						= D3D12_SHADER_VISIBILITY_VERTEX;

	D3D12_ROOT_SIGNATURE_DESC RootSignatureDesc = { };
	RootSignatureDesc.NumParameters		= 2;
	RootSignatureDesc.pParameters		= Parameters;
	RootSignatureDesc.NumStaticSamplers = 0;
	RootSignatureDesc.pStaticSamplers	= nullptr;
	RootSignatureDesc.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT	|
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS			|
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS		|
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS		|
		D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

	PrePassRootSignature = RenderingAPI::Get().CreateRootSignature(RootSignatureDesc);
	if (!PrePassRootSignature)
	{
		return false;
	}

	// Init PipelineState
	D3D12_INPUT_ELEMENT_DESC InputElementDesc[] =
	{
		{ "POSITION",	0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 0,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",		0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 12,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",	0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 24,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",	0, DXGI_FORMAT_R32G32_FLOAT,	0, 36,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	GraphicsPipelineStateProperties PSOProperties = { };
	PSOProperties.DebugName			= "PrePass PipelineState";
	PSOProperties.VSBlob			= VSBlob.Get();
	PSOProperties.PSBlob			= nullptr;
	PSOProperties.RootSignature		= PrePassRootSignature.Get();
	PSOProperties.InputElements		= InputElementDesc;
	PSOProperties.NumInputElements	= 4;
	PSOProperties.EnableDepth		= true;
	PSOProperties.EnableBlending	= false;
	PSOProperties.DepthBufferFormat = DepthBufferFormat;
	PSOProperties.DepthFunc			= D3D12_COMPARISON_FUNC_LESS;
	PSOProperties.CullMode			= D3D12_CULL_MODE_BACK;
	PSOProperties.RTFormats			= nullptr;
	PSOProperties.NumRenderTargets	= 0;

	PrePassPSO = RenderingAPI::Get().CreateGraphicsPipelineState(PSOProperties);
	if (!PrePassPSO)
	{
		return false;
	}

	// Init descriptortable
	PrePassDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(1);
	PrePassDescriptorTable->SetConstantBufferView(CameraBuffer->GetConstantBufferView().Get(), 0);
	PrePassDescriptorTable->CopyDescriptors();

	return true;
}

bool Renderer::InitShadowMapPass()
{
	using namespace Microsoft::WRL;

	// Directional Shadows
#if ENABLE_VSM
	ComPtr<IDxcBlob> VSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/ShadowMap.hlsl", "VSM_VSMain", "vs_6_0");
	if (!VSBlob)
	{
		return false;
	}

	ComPtr<IDxcBlob> PSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/ShadowMap.hlsl", "VSM_PSMain", "ps_6_0");
	if (!PSBlob)
	{
		return false;
	}
#else
	ComPtr<IDxcBlob> VSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/ShadowMap.hlsl", "Main", "vs_6_0");
	if (!VSBlob)
	{
		return false;
	}
#endif

	D3D12_ROOT_PARAMETER Parameters[2];
	// Transform Constants
	Parameters[0].ParameterType				= D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	Parameters[0].Constants.ShaderRegister	= 0;
	Parameters[0].Constants.RegisterSpace	= 0;
	Parameters[0].Constants.Num32BitValues	= 17;
	Parameters[0].ShaderVisibility			= D3D12_SHADER_VISIBILITY_VERTEX;

	// Camera
	Parameters[1].ParameterType				= D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	Parameters[1].Constants.ShaderRegister	= 1;
	Parameters[1].Constants.RegisterSpace	= 0;
	Parameters[1].Constants.Num32BitValues	= 20;
	Parameters[1].ShaderVisibility			= D3D12_SHADER_VISIBILITY_ALL;

	D3D12_ROOT_SIGNATURE_DESC RootSignatureDesc = { };
	RootSignatureDesc.NumParameters		= 2;
	RootSignatureDesc.pParameters		= Parameters;
	RootSignatureDesc.NumStaticSamplers	= 0;
	RootSignatureDesc.pStaticSamplers	= nullptr;
	RootSignatureDesc.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT	|
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS			|
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS		|
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	ShadowMapRootSignature = RenderingAPI::Get().CreateRootSignature(RootSignatureDesc);
	if (!ShadowMapRootSignature->Initialize(RootSignatureDesc))
	{
		return false;
	}

	// Init PipelineState
	D3D12_INPUT_ELEMENT_DESC InputElementDesc[] =
	{
		{ "POSITION",	0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 0,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",		0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 12,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",	0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 24,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",	0, DXGI_FORMAT_R32G32_FLOAT,	0, 36,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	GraphicsPipelineStateProperties PSOProperties = { };
	PSOProperties.DebugName				= "ShadowMap PipelineState";
	PSOProperties.VSBlob				= VSBlob.Get();
#if ENABLE_VSM
	PSOProperties.PSBlob				= PSBlob.Get();

	DXGI_FORMAT Format = DXGI_FORMAT_R32G32_FLOAT;
	PSOProperties.RTFormats			= &Format;
	PSOProperties.NumRenderTargets	= 1;
	PSOProperties.MultiSampleEnable	= false;
	PSOProperties.SampleCount		= 1;
	PSOProperties.SampleQuality		= 0;
#else
	PSOProperties.PSBlob			= nullptr;
	PSOProperties.RTFormats			= nullptr;
	PSOProperties.NumRenderTargets	= 0;
#endif
	PSOProperties.DepthBufferFormat		= ShadowMapFormat;
	PSOProperties.RootSignature			= ShadowMapRootSignature.Get();
	PSOProperties.InputElements			= InputElementDesc;
	PSOProperties.NumInputElements		= 4;
	PSOProperties.EnableDepth			= true;
	//PSOProperties.DepthBias			= 3;
	//PSOProperties.SlopeScaleDepthBias	= 0.01f;
	//PSOProperties.DepthBiasClamp		= 0.1f;
	PSOProperties.EnableBlending		= false;
	PSOProperties.DepthFunc				= D3D12_COMPARISON_FUNC_LESS_EQUAL;
	PSOProperties.CullMode				= D3D12_CULL_MODE_BACK;

#if ENABLE_VSM
	VSMShadowMapPSO = MakeShared<D3D12GraphicsPipelineState>(Device.Get());
	if (!VSMShadowMapPSO->Initialize(PSOProperties))
	{
		return false;
	}
#else
	ShadowMapPSO = RenderingAPI::Get().CreateGraphicsPipelineState(PSOProperties);
	if (!ShadowMapPSO->Initialize(PSOProperties))
	{
		return false;
	}
#endif

	// Linear Shadow Maps
	VSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/ShadowMap.hlsl", "VSMain", "vs_6_0");
	if (!VSBlob)
	{
		return false;
	}

#if !ENABLE_VSM
	ComPtr<IDxcBlob> PSBlob;
#endif
	PSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/ShadowMap.hlsl", "PSMain", "ps_6_0");
	if (!PSBlob)
	{
		return false;
	}

	PSOProperties.DebugName			= "Linear ShadowMap PipelineState";
	PSOProperties.VSBlob			= VSBlob.Get();
	PSOProperties.PSBlob			= PSBlob.Get();
	PSOProperties.DepthBufferFormat = ShadowMapFormat;
	PSOProperties.RTFormats			= nullptr;
	PSOProperties.NumRenderTargets	= 0;
	PSOProperties.MultiSampleEnable = false;
	PSOProperties.SampleCount		= 1;
	PSOProperties.SampleQuality		= 0;

	LinearShadowMapPSO = RenderingAPI::Get().CreateGraphicsPipelineState(PSOProperties);
	if (!LinearShadowMapPSO)
	{
		return false;
	}

	return true;
}

bool Renderer::InitDeferred()
{
	using namespace Microsoft::WRL;

	// Init PipelineState
	DxcDefine Defines[] =
	{
		{ L"ENABLE_PARALLAX_MAPPING",	L"1" },
		{ L"ENABLE_NORMAL_MAPPING",		L"1" },
	};

	ComPtr<IDxcBlob> VSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/GeometryPass.hlsl", "VSMain", "vs_6_0", Defines, 2);
	if (!VSBlob)
	{
		return false;
	}

	ComPtr<IDxcBlob> PSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/GeometryPass.hlsl", "PSMain", "ps_6_0", Defines, 2);
	if (!PSBlob)
	{
		return false;
	}

	// Init RootSignatures
	{
		D3D12_DESCRIPTOR_RANGE PerFrameRanges[1] = {};

		// Camera Buffer
		PerFrameRanges[0].BaseShaderRegister				= 1;
		PerFrameRanges[0].NumDescriptors					= 1;
		PerFrameRanges[0].RegisterSpace						= 0;
		PerFrameRanges[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		PerFrameRanges[0].OffsetInDescriptorsFromTableStart = 0;

		D3D12_DESCRIPTOR_RANGE PerObjectRanges[7] = {};
		// Albedo Map
		PerObjectRanges[0].BaseShaderRegister					= 0;
		PerObjectRanges[0].NumDescriptors						= 1;
		PerObjectRanges[0].RegisterSpace						= 0;
		PerObjectRanges[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerObjectRanges[0].OffsetInDescriptorsFromTableStart	= 0;

		// Normal Map
		PerObjectRanges[1].BaseShaderRegister					= 1;
		PerObjectRanges[1].NumDescriptors						= 1;
		PerObjectRanges[1].RegisterSpace						= 0;
		PerObjectRanges[1].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerObjectRanges[1].OffsetInDescriptorsFromTableStart	= 1;

		// Roughness Map
		PerObjectRanges[2].BaseShaderRegister					= 2;
		PerObjectRanges[2].NumDescriptors						= 1;
		PerObjectRanges[2].RegisterSpace						= 0;
		PerObjectRanges[2].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerObjectRanges[2].OffsetInDescriptorsFromTableStart	= 2;

		// Height Map
		PerObjectRanges[3].BaseShaderRegister					= 3;
		PerObjectRanges[3].NumDescriptors						= 1;
		PerObjectRanges[3].RegisterSpace						= 0;
		PerObjectRanges[3].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerObjectRanges[3].OffsetInDescriptorsFromTableStart	= 3;

		// Metallic Map
		PerObjectRanges[4].BaseShaderRegister					= 4;
		PerObjectRanges[4].NumDescriptors						= 1;
		PerObjectRanges[4].RegisterSpace						= 0;
		PerObjectRanges[4].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerObjectRanges[4].OffsetInDescriptorsFromTableStart	= 4;

		// AO Map
		PerObjectRanges[5].BaseShaderRegister					= 5;
		PerObjectRanges[5].NumDescriptors						= 1;
		PerObjectRanges[5].RegisterSpace						= 0;
		PerObjectRanges[5].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerObjectRanges[5].OffsetInDescriptorsFromTableStart	= 5;

		// Material Buffer
		PerObjectRanges[6].BaseShaderRegister					= 2;
		PerObjectRanges[6].NumDescriptors						= 1;
		PerObjectRanges[6].RegisterSpace						= 0;
		PerObjectRanges[6].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		PerObjectRanges[6].OffsetInDescriptorsFromTableStart	= 6;

		D3D12_ROOT_PARAMETER Parameters[3];
		// Transform Constants
		Parameters[0].ParameterType				= D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		Parameters[0].Constants.ShaderRegister	= 0;
		Parameters[0].Constants.RegisterSpace	= 0;
		Parameters[0].Constants.Num32BitValues	= 32;
		Parameters[0].ShaderVisibility			= D3D12_SHADER_VISIBILITY_ALL;

		// PerFrame DescriptorTable
		Parameters[1].ParameterType							= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		Parameters[1].DescriptorTable.NumDescriptorRanges	= 1;
		Parameters[1].DescriptorTable.pDescriptorRanges		= PerFrameRanges;
		Parameters[1].ShaderVisibility						= D3D12_SHADER_VISIBILITY_ALL;

		// PerObject DescriptorTable
		Parameters[2].ParameterType							= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		Parameters[2].DescriptorTable.NumDescriptorRanges	= 7;
		Parameters[2].DescriptorTable.pDescriptorRanges		= PerObjectRanges;
		Parameters[2].ShaderVisibility						= D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_STATIC_SAMPLER_DESC MaterialSampler = { };
		MaterialSampler.Filter				= D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		MaterialSampler.AddressU			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		MaterialSampler.AddressV			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		MaterialSampler.AddressW			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		MaterialSampler.MipLODBias			= 0.0f;
		MaterialSampler.MaxAnisotropy		= 0;
		MaterialSampler.ComparisonFunc		= D3D12_COMPARISON_FUNC_NEVER;
		MaterialSampler.BorderColor			= D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		MaterialSampler.MinLOD				= 0.0f;
		MaterialSampler.MaxLOD				= FLT_MAX;
		MaterialSampler.ShaderRegister		= 0;
		MaterialSampler.RegisterSpace		= 0;
		MaterialSampler.ShaderVisibility	= D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC RootSignatureDesc = { };
		RootSignatureDesc.NumParameters		= 3;
		RootSignatureDesc.pParameters		= Parameters;
		RootSignatureDesc.NumStaticSamplers	= 1;
		RootSignatureDesc.pStaticSamplers	= &MaterialSampler;
		RootSignatureDesc.Flags				= 
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		GeometryRootSignature = RenderingAPI::Get().CreateRootSignature(RootSignatureDesc);
		if (!GeometryRootSignature->Initialize(RootSignatureDesc))
		{
			return false;
		}
	}

	{
		D3D12_DESCRIPTOR_RANGE Ranges[1] = {};
		// Skybox Buffer
		Ranges[0].BaseShaderRegister				= 0;
		Ranges[0].NumDescriptors					= 1;
		Ranges[0].RegisterSpace						= 0;
		Ranges[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges[0].OffsetInDescriptorsFromTableStart	= 0;

		D3D12_ROOT_PARAMETER Parameters[2];
		Parameters[0].ParameterType				= D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		Parameters[0].Constants.ShaderRegister	= 0;
		Parameters[0].Constants.RegisterSpace	= 0;
		Parameters[0].Constants.Num32BitValues	= 16;
		Parameters[0].ShaderVisibility			= D3D12_SHADER_VISIBILITY_VERTEX;

		Parameters[1].ParameterType							= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		Parameters[1].DescriptorTable.NumDescriptorRanges	= 1;
		Parameters[1].DescriptorTable.pDescriptorRanges		= Ranges;
		Parameters[1].ShaderVisibility						= D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_STATIC_SAMPLER_DESC SkyboxSampler = { };
		SkyboxSampler.Filter			= D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		SkyboxSampler.AddressU			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		SkyboxSampler.AddressV			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		SkyboxSampler.AddressW			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		SkyboxSampler.MipLODBias		= 0.0f;
		SkyboxSampler.MaxAnisotropy		= 0;
		SkyboxSampler.ComparisonFunc	= D3D12_COMPARISON_FUNC_NEVER;
		SkyboxSampler.BorderColor		= D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		SkyboxSampler.MinLOD			= 0.0f;
		SkyboxSampler.MaxLOD			= 0.0f;
		SkyboxSampler.ShaderRegister	= 0;
		SkyboxSampler.RegisterSpace		= 0;
		SkyboxSampler.ShaderVisibility	= D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC RootSignatureDesc = { };
		RootSignatureDesc.NumParameters		= 2;
		RootSignatureDesc.pParameters		= Parameters;
		RootSignatureDesc.NumStaticSamplers	= 1;
		RootSignatureDesc.pStaticSamplers	= &SkyboxSampler;
		RootSignatureDesc.Flags				=
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		SkyboxRootSignature = RenderingAPI::Get().CreateRootSignature(RootSignatureDesc);
		if (!SkyboxRootSignature)
		{
			return false;
		}
	}

	{
		constexpr UInt32 NumRanges = 14;
		D3D12_DESCRIPTOR_RANGE Ranges[NumRanges] = { };
		// Albedo
		Ranges[0].BaseShaderRegister				= 0;
		Ranges[0].NumDescriptors					= 1;
		Ranges[0].RegisterSpace						= 0;
		Ranges[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges[0].OffsetInDescriptorsFromTableStart = 0;

		// Normal
		Ranges[1].BaseShaderRegister				= 1;
		Ranges[1].NumDescriptors					= 1;
		Ranges[1].RegisterSpace						= 0;
		Ranges[1].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges[1].OffsetInDescriptorsFromTableStart	= 1;

		// Material
		Ranges[2].BaseShaderRegister				= 2;
		Ranges[2].NumDescriptors					= 1;
		Ranges[2].RegisterSpace						= 0;
		Ranges[2].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges[2].OffsetInDescriptorsFromTableStart	= 2;

		// Depth
		Ranges[3].BaseShaderRegister				= 3;
		Ranges[3].NumDescriptors					= 1;
		Ranges[3].RegisterSpace						= 0;
		Ranges[3].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges[3].OffsetInDescriptorsFromTableStart	= 3;

		// DXR-Reflections
		Ranges[4].BaseShaderRegister				= 4;
		Ranges[4].NumDescriptors					= 1;
		Ranges[4].RegisterSpace						= 0;
		Ranges[4].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges[4].OffsetInDescriptorsFromTableStart = 4;

		// IrradianceMap
		Ranges[5].BaseShaderRegister				= 5;
		Ranges[5].NumDescriptors					= 1;
		Ranges[5].RegisterSpace						= 0;
		Ranges[5].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges[5].OffsetInDescriptorsFromTableStart = 5;

		// Specular IrradianceMap
		Ranges[6].BaseShaderRegister				= 6;
		Ranges[6].NumDescriptors					= 1;
		Ranges[6].RegisterSpace						= 0;
		Ranges[6].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges[6].OffsetInDescriptorsFromTableStart = 6;

		// Integration LUT
		Ranges[7].BaseShaderRegister				= 7;
		Ranges[7].NumDescriptors					= 1;
		Ranges[7].RegisterSpace						= 0;
		Ranges[7].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges[7].OffsetInDescriptorsFromTableStart = 7;

		// Directional ShadowMaps
		Ranges[8].BaseShaderRegister				= 8;
		Ranges[8].NumDescriptors					= 1;
		Ranges[8].RegisterSpace						= 0;
		Ranges[8].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges[8].OffsetInDescriptorsFromTableStart = 8;

		// PointLight ShadowMaps
		Ranges[9].BaseShaderRegister				= 9;
		Ranges[9].NumDescriptors					= 1;
		Ranges[9].RegisterSpace						= 0;
		Ranges[9].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges[9].OffsetInDescriptorsFromTableStart = 9;

		// Camera
		Ranges[10].BaseShaderRegister					= 0;
		Ranges[10].NumDescriptors						= 1;
		Ranges[10].RegisterSpace						= 0;
		Ranges[10].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		Ranges[10].OffsetInDescriptorsFromTableStart	= 10;

		// PointLights
		Ranges[11].BaseShaderRegister					= 1;
		Ranges[11].NumDescriptors						= 1;
		Ranges[11].RegisterSpace						= 0;
		Ranges[11].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		Ranges[11].OffsetInDescriptorsFromTableStart	= 11;

		// DirectionalLights
		Ranges[12].BaseShaderRegister					= 2;
		Ranges[12].NumDescriptors						= 1;
		Ranges[12].RegisterSpace						= 0;
		Ranges[12].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		Ranges[12].OffsetInDescriptorsFromTableStart	= 12;

		// PointLight ShadowMaps
		Ranges[13].BaseShaderRegister					= 10;
		Ranges[13].NumDescriptors						= 1;
		Ranges[13].RegisterSpace						= 0;
		Ranges[13].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges[13].OffsetInDescriptorsFromTableStart	= 13;

		D3D12_ROOT_PARAMETER Parameters[1];
		Parameters[0].ParameterType							= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		Parameters[0].DescriptorTable.NumDescriptorRanges	= NumRanges;
		Parameters[0].DescriptorTable.pDescriptorRanges		= Ranges;
		Parameters[0].ShaderVisibility						= D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_STATIC_SAMPLER_DESC Samplers[5] = { };
		Samplers[0].Filter				= D3D12_FILTER_MIN_MAG_MIP_POINT;
		Samplers[0].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		Samplers[0].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		Samplers[0].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		Samplers[0].MipLODBias			= 0.0f;
		Samplers[0].MaxAnisotropy		= 0;
		Samplers[0].ComparisonFunc		= D3D12_COMPARISON_FUNC_NEVER;
		Samplers[0].BorderColor			= D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		Samplers[0].MinLOD				= 0.0f;
		Samplers[0].MaxLOD				= FLT_MAX;
		Samplers[0].ShaderRegister		= 0;
		Samplers[0].RegisterSpace		= 0;
		Samplers[0].ShaderVisibility	= D3D12_SHADER_VISIBILITY_PIXEL;

		Samplers[1].Filter				= D3D12_FILTER_MIN_MAG_MIP_POINT;
		Samplers[1].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		Samplers[1].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		Samplers[1].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		Samplers[1].MipLODBias			= 0.0f;
		Samplers[1].MaxAnisotropy		= 0;
		Samplers[1].ComparisonFunc		= D3D12_COMPARISON_FUNC_NEVER;
		Samplers[1].BorderColor			= D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		Samplers[1].MinLOD				= 0.0f;
		Samplers[1].MaxLOD				= FLT_MAX;
		Samplers[1].ShaderRegister		= 1;
		Samplers[1].RegisterSpace		= 0;
		Samplers[1].ShaderVisibility	= D3D12_SHADER_VISIBILITY_PIXEL;

		Samplers[2].Filter				= D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		Samplers[2].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		Samplers[2].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		Samplers[2].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		Samplers[2].MipLODBias			= 0.0f;
		Samplers[2].MaxAnisotropy		= 0;
		Samplers[2].ComparisonFunc		= D3D12_COMPARISON_FUNC_NEVER;
		Samplers[2].BorderColor			= D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		Samplers[2].MinLOD				= 0.0f;
		Samplers[2].MaxLOD				= FLT_MAX;
		Samplers[2].ShaderRegister		= 2;
		Samplers[2].RegisterSpace		= 0;
		Samplers[2].ShaderVisibility	= D3D12_SHADER_VISIBILITY_PIXEL;

		Samplers[3].Filter				= D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
		Samplers[3].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		Samplers[3].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		Samplers[3].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		Samplers[3].MipLODBias			= 0.0f;
		Samplers[3].MaxAnisotropy		= 0;
		Samplers[3].ComparisonFunc		= D3D12_COMPARISON_FUNC_LESS;
		Samplers[3].BorderColor			= D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
		Samplers[3].MinLOD				= 0.0f;
		Samplers[3].MaxLOD				= FLT_MAX;
		Samplers[3].ShaderRegister		= 3;
		Samplers[3].RegisterSpace		= 0;
		Samplers[3].ShaderVisibility	= D3D12_SHADER_VISIBILITY_PIXEL;

		Samplers[4].Filter				= D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		Samplers[4].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		Samplers[4].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		Samplers[4].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		Samplers[4].MipLODBias			= 0.0f;
		Samplers[4].MaxAnisotropy		= 0;
		Samplers[4].ComparisonFunc		= D3D12_COMPARISON_FUNC_NEVER;
		Samplers[4].BorderColor			= D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
		Samplers[4].MinLOD				= 0.0f;
		Samplers[4].MaxLOD				= FLT_MAX;
		Samplers[4].ShaderRegister		= 4;
		Samplers[4].RegisterSpace		= 0;
		Samplers[4].ShaderVisibility	= D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC RootSignatureDesc = { };
		RootSignatureDesc.NumParameters			= 1;
		RootSignatureDesc.pParameters			= Parameters;
		RootSignatureDesc.NumStaticSamplers		= 5;
		RootSignatureDesc.pStaticSamplers		= Samplers;
		RootSignatureDesc.Flags					= 
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		LightRootSignature = RenderingAPI::Get().CreateRootSignature(RootSignatureDesc);
		if (!LightRootSignature)
		{
			return false;
		}
	}

	// Init PipelineState
	D3D12_INPUT_ELEMENT_DESC InputElementDesc[] =
	{
		{ "POSITION",	0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 0,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",		0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 12,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",	0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 24,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",	0, DXGI_FORMAT_R32G32_FLOAT,	0, 36,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	GraphicsPipelineStateProperties PSOProperties = { };
	PSOProperties.DebugName			= "GeometryPass PipelineState";
	PSOProperties.VSBlob			= VSBlob.Get();
	PSOProperties.PSBlob			= PSBlob.Get();
	PSOProperties.RootSignature		= GeometryRootSignature.Get();
	PSOProperties.InputElements		= InputElementDesc;
	PSOProperties.NumInputElements	= 4;
	PSOProperties.EnableDepth		= true;
	PSOProperties.EnableBlending	= false;
	PSOProperties.DepthFunc			= D3D12_COMPARISON_FUNC_LESS_EQUAL;
	PSOProperties.DepthBufferFormat = DepthBufferFormat;
	PSOProperties.CullMode			= D3D12_CULL_MODE_BACK;

	DXGI_FORMAT Formats[] =
	{
		AlbedoFormat,
		NormalFormat,
		MaterialFormat
	};

	PSOProperties.RTFormats			= Formats;
	PSOProperties.NumRenderTargets	= 3;

	GeometryPSO = RenderingAPI::Get().CreateGraphicsPipelineState(PSOProperties);
	if (!GeometryPSO)
	{
		return false;
	}

	VSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/FullscreenVS.hlsl", "Main", "vs_6_0");
	if (!VSBlob)
	{
		return false;
	}

	LPCWSTR Value = (RenderingAPI::Get().IsRayTracingSupported() && RayTracingEnabled) ? L"1" : L"0";
	DxcDefine LightPassDefines[] =
	{
		{ L"ENABLE_RAYTRACING",	Value },
	};

	PSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/LightPassPS.hlsl", "Main", "ps_6_0", LightPassDefines, 1);
	if (!PSBlob)
	{
		return false;
	}

	PSOProperties.DebugName			= "LightPass PipelineState";
	PSOProperties.VSBlob			= VSBlob.Get();
	PSOProperties.PSBlob			= PSBlob.Get();
	PSOProperties.RootSignature		= LightRootSignature.Get();
	PSOProperties.InputElements		= nullptr;
	PSOProperties.NumInputElements	= 0;
	PSOProperties.EnableDepth		= false;
	PSOProperties.DepthBufferFormat = DXGI_FORMAT_UNKNOWN;

	DXGI_FORMAT FinalFormat[] =
	{
		RenderTargetFormat
	};

	PSOProperties.RTFormats			= FinalFormat;
	PSOProperties.NumRenderTargets	= 1;
	PSOProperties.CullMode			= D3D12_CULL_MODE_NONE;

	LightPassPSO = RenderingAPI::Get().CreateGraphicsPipelineState(PSOProperties);
	if (!LightPassPSO)
	{
		return false;
	}

	VSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/Skybox.hlsl", "VSMain", "vs_6_0");
	if (!VSBlob)
	{
		return false;
	}

	PSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/Skybox.hlsl", "PSMain", "ps_6_0");
	if (!PSBlob)
	{
		return false;
	}

	PSOProperties.DebugName			= "Skybox PipelineState";
	PSOProperties.VSBlob			= VSBlob.Get();
	PSOProperties.PSBlob			= PSBlob.Get();
	PSOProperties.RootSignature		= SkyboxRootSignature.Get();
	PSOProperties.InputElements		= InputElementDesc;
	PSOProperties.NumInputElements	= 4;
	PSOProperties.EnableDepth		= true;
	PSOProperties.DepthFunc			= D3D12_COMPARISON_FUNC_LESS_EQUAL;
	PSOProperties.DepthWriteMask	= D3D12_DEPTH_WRITE_MASK_ZERO;
	PSOProperties.DepthBufferFormat = DepthBufferFormat;
	PSOProperties.RTFormats			= FinalFormat;
	PSOProperties.NumRenderTargets	= 1;

	SkyboxPSO = RenderingAPI::Get().CreateGraphicsPipelineState(PSOProperties);
	if (!SkyboxPSO)
	{
		return false;
	}

	// Init descriptortable
	GeometryDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(1);
	GeometryDescriptorTable->SetConstantBufferView(CameraBuffer->GetConstantBufferView().Get(), 0);
	GeometryDescriptorTable->CopyDescriptors();

	ForwardDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(8);
	ForwardDescriptorTable->SetConstantBufferView(CameraBuffer->GetConstantBufferView().Get(), 0);
	ForwardDescriptorTable->SetConstantBufferView(PointLightBuffer->GetConstantBufferView().Get(), 1);
	ForwardDescriptorTable->SetConstantBufferView(DirectionalLightBuffer->GetConstantBufferView().Get(), 2);

	LightDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(14);
	LightDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_ALBEDO_INDEX]->GetShaderResourceView(0).Get(), 0);
	LightDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_NORMAL_INDEX]->GetShaderResourceView(0).Get(), 1);
	LightDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_MATERIAL_INDEX]->GetShaderResourceView(0).Get(), 2);
	LightDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_DEPTH_INDEX]->GetShaderResourceView(0).Get(), 3);
	// #4 is set after deferred and raytracing
	// #5 is set after deferred and raytracing
	// #6 is set after deferred and raytracing
	// #7 is set after deferred and raytracing
	// #8 is set after deferred and raytracing
	LightDescriptorTable->SetConstantBufferView(CameraBuffer->GetConstantBufferView().Get(), 10);
	LightDescriptorTable->SetConstantBufferView(PointLightBuffer->GetConstantBufferView().Get(), 11);
	LightDescriptorTable->SetConstantBufferView(DirectionalLightBuffer->GetConstantBufferView().Get(), 12);

	SkyboxDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(2);
	SkyboxDescriptorTable->SetShaderResourceView(Skybox->GetShaderResourceView(0).Get(), 0);
	SkyboxDescriptorTable->SetConstantBufferView(CameraBuffer->GetConstantBufferView().Get(), 1);
	SkyboxDescriptorTable->CopyDescriptors();

	return true;
}

bool Renderer::InitGBuffer()
{
	// Create image
	if (!InitRayTracingTexture())
	{
		return false;
	}

	// Albedo
	TextureProperties GBufferProperties = { };
	GBufferProperties.DebugName		= "GBuffer Albedo";
	GBufferProperties.Format		= AlbedoFormat;
	GBufferProperties.Flags			= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	GBufferProperties.ArrayCount	= 1;
	GBufferProperties.Width			= static_cast<UInt16>(RenderingAPI::Get().GetSwapChain()->GetWidth());
	GBufferProperties.Height		= static_cast<UInt16>(RenderingAPI::Get().GetSwapChain()->GetHeight());
	GBufferProperties.InitalState	= D3D12_RESOURCE_STATE_COMMON;
	GBufferProperties.MemoryType	= EMemoryType::MEMORY_TYPE_DEFAULT;
	GBufferProperties.MipLevels		= 1;
	GBufferProperties.SampleCount	= 1;

	D3D12_CLEAR_VALUE Value;
	Value.Format	= GBufferProperties.Format;
	Value.Color[0]	= 0.0f;
	Value.Color[1]	= 0.0f;
	Value.Color[2]	= 0.0f;
	Value.Color[3]	= 1.0f;
	GBufferProperties.OptimizedClearValue = &Value;
	
	// ShaderResourceView
	D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc = { };
	SrvDesc.Format							= GBufferProperties.Format;
	SrvDesc.ViewDimension					= D3D12_SRV_DIMENSION_TEXTURE2D;
	SrvDesc.Shader4ComponentMapping			= D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SrvDesc.Texture2D.MipLevels				= 1;
	SrvDesc.Texture2D.MostDetailedMip		= 0;
	SrvDesc.Texture2D.PlaneSlice			= 0;
	SrvDesc.Texture2D.ResourceMinLODClamp	= 0.0f;

	D3D12_RENDER_TARGET_VIEW_DESC RtvDesc = { };
	RtvDesc.Format					= GBufferProperties.Format;
	RtvDesc.ViewDimension			= D3D12_RTV_DIMENSION_TEXTURE2D;
	RtvDesc.Texture2D.MipSlice		= 0;
	RtvDesc.Texture2D.PlaneSlice	= 0;

	GBuffer[GBUFFER_ALBEDO_INDEX] = RenderingAPI::Get().CreateTexture(GBufferProperties);
	if (GBuffer[GBUFFER_ALBEDO_INDEX])
	{
		GBuffer[GBUFFER_ALBEDO_INDEX]->SetShaderResourceView(TSharedPtr(RenderingAPI::Get().CreateShaderResourceView(GBuffer[GBUFFER_ALBEDO_INDEX]->GetResource(), &SrvDesc)), 0);
		GBuffer[GBUFFER_ALBEDO_INDEX]->SetRenderTargetView(TSharedPtr(RenderingAPI::Get().CreateRenderTargetView(GBuffer[GBUFFER_ALBEDO_INDEX]->GetResource(), &RtvDesc)), 0);
	}
	else
	{
		return false;
	}

	// Normal
	GBufferProperties.DebugName = "GBuffer Normal";
	GBufferProperties.Format	= NormalFormat;
	
	Value.Format = GBufferProperties.Format;

	SrvDesc.Format = GBufferProperties.Format;
	RtvDesc.Format = GBufferProperties.Format;

	GBuffer[GBUFFER_NORMAL_INDEX] = RenderingAPI::Get().CreateTexture(GBufferProperties);
	if (GBuffer[GBUFFER_NORMAL_INDEX])
	{
		GBuffer[GBUFFER_NORMAL_INDEX]->SetShaderResourceView(TSharedPtr(RenderingAPI::Get().CreateShaderResourceView(GBuffer[GBUFFER_NORMAL_INDEX]->GetResource(), &SrvDesc)), 0);
		GBuffer[GBUFFER_NORMAL_INDEX]->SetRenderTargetView(TSharedPtr(RenderingAPI::Get().CreateRenderTargetView(GBuffer[GBUFFER_NORMAL_INDEX]->GetResource(), &RtvDesc)), 0);
	}
	else
	{
		return false;
	}

	// Material Properties
	GBufferProperties.DebugName = "GBuffer Material";
	GBufferProperties.Format	= MaterialFormat;

	Value.Format = GBufferProperties.Format;

	SrvDesc.Format = GBufferProperties.Format;
	RtvDesc.Format = GBufferProperties.Format;

	GBuffer[GBUFFER_MATERIAL_INDEX] = RenderingAPI::Get().CreateTexture(GBufferProperties);
	if (GBuffer[GBUFFER_MATERIAL_INDEX])
	{
		GBuffer[GBUFFER_MATERIAL_INDEX]->SetShaderResourceView(TSharedPtr(RenderingAPI::Get().CreateShaderResourceView(GBuffer[GBUFFER_MATERIAL_INDEX]->GetResource(), &SrvDesc)), 0);
		GBuffer[GBUFFER_MATERIAL_INDEX]->SetRenderTargetView(TSharedPtr(RenderingAPI::Get().CreateRenderTargetView(GBuffer[GBUFFER_MATERIAL_INDEX]->GetResource(), &RtvDesc)), 0);
	}
	else
	{
		return false;
	}

	// DepthStencil
	GBufferProperties.DebugName	= "GBuffer DepthStencil";
	GBufferProperties.Flags		= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	GBufferProperties.Format	= DXGI_FORMAT_R32_TYPELESS;
	
	D3D12_CLEAR_VALUE ClearValue = { };
	ClearValue.Format				= DepthBufferFormat;
	ClearValue.DepthStencil.Depth	= 1.0f;
	ClearValue.DepthStencil.Stencil	= 0;
	GBufferProperties.OptimizedClearValue = &ClearValue;

	SrvDesc.Format = DXGI_FORMAT_R32_FLOAT;

	D3D12_DEPTH_STENCIL_VIEW_DESC DsvDesc = { };
	DsvDesc.Format				= DepthBufferFormat;
	DsvDesc.ViewDimension		= D3D12_DSV_DIMENSION_TEXTURE2D;
	DsvDesc.Texture2D.MipSlice	= 0;

	GBuffer[GBUFFER_DEPTH_INDEX] = RenderingAPI::Get().CreateTexture(GBufferProperties);
	if (GBuffer[GBUFFER_DEPTH_INDEX])
	{
		GBuffer[GBUFFER_DEPTH_INDEX]->SetShaderResourceView(TSharedPtr(RenderingAPI::Get().CreateShaderResourceView(GBuffer[GBUFFER_DEPTH_INDEX]->GetResource(), &SrvDesc)), 0);
		GBuffer[GBUFFER_DEPTH_INDEX]->SetDepthStencilView(TSharedPtr(RenderingAPI::Get().CreateDepthStencilView(GBuffer[GBUFFER_DEPTH_INDEX]->GetResource(), &DsvDesc)), 0);
	}
	else
	{
		return false;
	}

	GBufferProperties.DebugName				= "Final Target";
	GBufferProperties.Format				= RenderTargetFormat;
	GBufferProperties.Flags					= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	GBufferProperties.ArrayCount			= 1;
	GBufferProperties.Width					= static_cast<UInt16>(RenderingAPI::Get().GetSwapChain()->GetWidth());
	GBufferProperties.Height				= static_cast<UInt16>(RenderingAPI::Get().GetSwapChain()->GetHeight());
	GBufferProperties.InitalState			= D3D12_RESOURCE_STATE_COMMON;
	GBufferProperties.MemoryType			= EMemoryType::MEMORY_TYPE_DEFAULT;
	GBufferProperties.MipLevels				= 1;
	GBufferProperties.SampleCount			= 1;
	GBufferProperties.OptimizedClearValue	= nullptr;

	SrvDesc.Format = GBufferProperties.Format;
	RtvDesc.Format = GBufferProperties.Format;

	FinalTarget = RenderingAPI::Get().CreateTexture(GBufferProperties);
	if (FinalTarget)
	{
		FinalTarget->SetShaderResourceView(TSharedPtr(RenderingAPI::Get().CreateShaderResourceView(FinalTarget->GetResource(), &SrvDesc)), 0);
		FinalTarget->SetRenderTargetView(TSharedPtr(RenderingAPI::Get().CreateRenderTargetView(FinalTarget->GetResource(), &RtvDesc)), 0);
	}
	else
	{
		return false;
	}

	// Final image descriptorset
	PostDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(1);
	PostDescriptorTable->SetShaderResourceView(FinalTarget->GetShaderResourceView(0).Get(), 0);
	PostDescriptorTable->CopyDescriptors();

	return true;
}

bool Renderer::InitIntegrationLUT()
{
	if (!RenderingAPI::Get().UAVSupportsFormat(DXGI_FORMAT_R16G16_FLOAT))
	{
		LOG_ERROR("[Renderer]: DXGI_FORMAT_R16G16_FLOAT is not supported for UAVs");
		return false;
	}

	TextureProperties LUTProperties = { };
	LUTProperties.DebugName		= "IntegrationLUT Staging Texture";
	LUTProperties.Flags			= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	LUTProperties.Width			= 512;
	LUTProperties.Height		= 512;
	LUTProperties.MipLevels		= 1;
	LUTProperties.ArrayCount	= 1;
	LUTProperties.Format		= DXGI_FORMAT_R16G16_FLOAT;
	LUTProperties.MemoryType	= EMemoryType::MEMORY_TYPE_DEFAULT;
	LUTProperties.InitalState	= D3D12_RESOURCE_STATE_COMMON;
	LUTProperties.SampleCount	= 1;

	TUniquePtr<D3D12Texture> StagingTexture = TUniquePtr(RenderingAPI::Get().CreateTexture(LUTProperties));
	if (!StagingTexture)
	{
		return false;
	}
	else
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC UavDesc = { };
		UavDesc.Format					= LUTProperties.Format;
		UavDesc.ViewDimension			= D3D12_UAV_DIMENSION_TEXTURE2D;
		UavDesc.Texture2D.MipSlice		= 0;
		UavDesc.Texture2D.PlaneSlice	= 0;

		StagingTexture->SetUnorderedAccessView(TSharedPtr(RenderingAPI::Get().CreateUnorderedAccessView(nullptr, StagingTexture->GetResource(), &UavDesc)), 0);
	}

	LUTProperties.DebugName	= "IntegrationLUT";
	LUTProperties.Flags		= D3D12_RESOURCE_FLAG_NONE;

	IntegrationLUT = RenderingAPI::Get().CreateTexture(LUTProperties);
	if (!IntegrationLUT)
	{
		return false;
	}
	else
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc = { };
		SrvDesc.Format							= LUTProperties.Format;
		SrvDesc.ViewDimension					= D3D12_SRV_DIMENSION_TEXTURE2D;
		SrvDesc.Shader4ComponentMapping			= D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SrvDesc.Texture2D.MipLevels				= 1;
		SrvDesc.Texture2D.MostDetailedMip		= 0;
		SrvDesc.Texture2D.PlaneSlice			= 0;
		SrvDesc.Texture2D.ResourceMinLODClamp	= 0;

		IntegrationLUT->SetShaderResourceView(TSharedPtr(RenderingAPI::Get().CreateShaderResourceView(IntegrationLUT->GetResource(), &SrvDesc)), 0);
	}

	Microsoft::WRL::ComPtr<IDxcBlob> Shader = D3D12ShaderCompiler::CompileFromFile("Shaders/BRDFIntegationGen.hlsl", "Main", "cs_6_0");
	if (!Shader)
	{
		return false;
	}

	TUniquePtr<D3D12RootSignature> RootSignature = TUniquePtr(RenderingAPI::Get().CreateRootSignature(Shader.Get()));
	if (!RootSignature)
	{
		return false;
	}

	ComputePipelineStateProperties PSOProperties;
	PSOProperties.DebugName		= "IntegrationGen PSO";
	PSOProperties.CSBlob		= Shader.Get();
	PSOProperties.RootSignature	= RootSignature.Get();

	TUniquePtr<D3D12ComputePipelineState> PSO = TUniquePtr(RenderingAPI::Get().CreateComputePipelineState(PSOProperties));
	if (!PSO)
	{
		return false;
	}

	TUniquePtr<D3D12DescriptorTable> DescriptorTable = TUniquePtr(RenderingAPI::Get().CreateDescriptorTable(1));
	DescriptorTable->SetUnorderedAccessView(StagingTexture->GetUnorderedAccessView(0).Get(), 0);
	DescriptorTable->CopyDescriptors();

	RenderingAPI::Get().GetImmediateCommandList()->SetComputeRootSignature(RootSignature->GetRootSignature());
	RenderingAPI::Get().GetImmediateCommandList()->SetPipelineState(PSO->GetPipeline());
				
	RenderingAPI::Get().GetImmediateCommandList()->BindGlobalOnlineDescriptorHeaps();
	RenderingAPI::Get().GetImmediateCommandList()->SetComputeRootDescriptorTable(DescriptorTable->GetGPUTableStartHandle(), 0);
				
	RenderingAPI::Get().GetImmediateCommandList()->Dispatch(LUTProperties.Width, LUTProperties.Height, 1);
	RenderingAPI::Get().GetImmediateCommandList()->UnorderedAccessBarrier(StagingTexture.Get());
				
	RenderingAPI::Get().GetImmediateCommandList()->TransitionBarrier(IntegrationLUT.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	RenderingAPI::Get().GetImmediateCommandList()->TransitionBarrier(StagingTexture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
				
	RenderingAPI::Get().GetImmediateCommandList()->CopyResource(IntegrationLUT.Get(), StagingTexture.Get());
				
	RenderingAPI::Get().GetImmediateCommandList()->TransitionBarrier(IntegrationLUT.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				
	RenderingAPI::Get().GetImmediateCommandList()->Flush();
	RenderingAPI::Get().GetImmediateCommandList()->WaitForCompletion();

	return true;
}

bool Renderer::InitRayTracingTexture()
{
	TextureProperties OutputProperties = { };
	OutputProperties.DebugName		= "RayTracing Output";
	OutputProperties.Flags			= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	OutputProperties.Width			= static_cast<UInt16>(RenderingAPI::Get().GetSwapChain()->GetWidth());
	OutputProperties.Height			= static_cast<UInt16>(RenderingAPI::Get().GetSwapChain()->GetHeight());
	OutputProperties.MipLevels		= 1;
	OutputProperties.ArrayCount		= 1;
	OutputProperties.Format			= DXGI_FORMAT_R8G8B8A8_UNORM;
	OutputProperties.MemoryType		= EMemoryType::MEMORY_TYPE_DEFAULT;
	OutputProperties.SampleCount	= 1;

	ReflectionTexture = RenderingAPI::Get().CreateTexture(OutputProperties);
	if (!ReflectionTexture)
	{
		return false;
	}

	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVView = { };
	UAVView.Format					= OutputProperties.Format;
	UAVView.ViewDimension			= D3D12_UAV_DIMENSION_TEXTURE2D;
	UAVView.Texture2D.MipSlice		= 0;
	UAVView.Texture2D.PlaneSlice	= 0;

	ReflectionTexture->SetUnorderedAccessView(TSharedPtr(RenderingAPI::Get().CreateUnorderedAccessView(nullptr, ReflectionTexture->GetResource(), &UAVView)), 0);
	
	D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc = { };
	SrvDesc.Format							= OutputProperties.Format;
	SrvDesc.ViewDimension					= D3D12_SRV_DIMENSION_TEXTURE2D;
	SrvDesc.Shader4ComponentMapping			= D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SrvDesc.Texture2D.MipLevels				= 1;
	SrvDesc.Texture2D.MostDetailedMip		= 0;
	SrvDesc.Texture2D.PlaneSlice			= 0;
	SrvDesc.Texture2D.ResourceMinLODClamp	= 0.0f;
	
	ReflectionTexture->SetShaderResourceView(TSharedPtr(RenderingAPI::Get().CreateShaderResourceView(ReflectionTexture->GetResource(), &SrvDesc)), 0);

	return true;
}

bool Renderer::InitDebugStates()
{
	using namespace Microsoft::WRL;

	DXGI_FORMAT Formats[] =
	{
		DXGI_FORMAT_R8G8B8A8_UNORM,
	};

	ComPtr<IDxcBlob> VSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/Debug.hlsl", "VSMain", "vs_6_0");
	if (!VSBlob)
	{
		return false;
	}

	ComPtr<IDxcBlob> PSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/Debug.hlsl", "PSMain", "ps_6_0");
	if (!PSBlob)
	{
		return false;
	}

	D3D12_ROOT_PARAMETER Parameters[2];
	// Transform Constants
	Parameters[0].ParameterType				= D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	Parameters[0].Constants.ShaderRegister	= 0;
	Parameters[0].Constants.RegisterSpace	= 0;
	Parameters[0].Constants.Num32BitValues	= 16;
	Parameters[0].ShaderVisibility			= D3D12_SHADER_VISIBILITY_VERTEX;

	// Camera
	Parameters[1].ParameterType				= D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	Parameters[1].Constants.ShaderRegister	= 1;
	Parameters[1].Constants.RegisterSpace	= 0;
	Parameters[1].Constants.Num32BitValues	= 16;
	Parameters[1].ShaderVisibility			= D3D12_SHADER_VISIBILITY_ALL;

	D3D12_ROOT_SIGNATURE_DESC RootSignatureDesc = { };
	RootSignatureDesc.NumParameters		= 2;
	RootSignatureDesc.pParameters		= Parameters;
	RootSignatureDesc.NumStaticSamplers	= 0;
	RootSignatureDesc.pStaticSamplers	= nullptr;
	RootSignatureDesc.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT	|
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS			|
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS		|
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	DebugRootSignature = RenderingAPI::Get().CreateRootSignature(RootSignatureDesc);
	if (!DebugRootSignature)
	{
		return false;
	}

	D3D12_INPUT_ELEMENT_DESC InputElementDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	GraphicsPipelineStateProperties PSOProperties = { };
	PSOProperties.DebugName			= "Debug PSO";
	PSOProperties.VSBlob			= VSBlob.Get();
	PSOProperties.PSBlob			= PSBlob.Get();
	PSOProperties.RootSignature		= DebugRootSignature.Get();
	PSOProperties.InputElements		= InputElementDesc;
	PSOProperties.NumInputElements	= 1;
	PSOProperties.EnableDepth		= false;
	PSOProperties.DepthWriteMask	= D3D12_DEPTH_WRITE_MASK_ZERO;
	PSOProperties.EnableBlending	= false;
	PSOProperties.DepthFunc			= D3D12_COMPARISON_FUNC_LESS_EQUAL;
	PSOProperties.DepthBufferFormat	= DepthBufferFormat;
	PSOProperties.CullMode			= D3D12_CULL_MODE_NONE;
	PSOProperties.RTFormats			= Formats;
	PSOProperties.NumRenderTargets	= 1;
	PSOProperties.PrimitiveType		= D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;

	DebugBoxPSO = RenderingAPI::Get().CreateGraphicsPipelineState(PSOProperties);
	if (!DebugBoxPSO)
	{
		return false;
	}

	// Create VertexBuffer
	XMFLOAT3 Vertices[8] =
	{
		{ -0.5f, -0.5f,  0.5f },
		{  0.5f, -0.5f,  0.5f },
		{ -0.5f,  0.5f,  0.5f },
		{  0.5f,  0.5f,  0.5f },

		{  0.5f, -0.5f, -0.5f },
		{ -0.5f, -0.5f, -0.5f },
		{  0.5f,  0.5f, -0.5f },
		{ -0.5f,  0.5f, -0.5f }
	};

	BufferProperties BufferProps = { };
	BufferProps.SizeInBytes = sizeof(Vertices);
	BufferProps.Flags		= D3D12_RESOURCE_FLAG_NONE;
	BufferProps.InitalState = D3D12_RESOURCE_STATE_COMMON;
	BufferProps.MemoryType	= EMemoryType::MEMORY_TYPE_DEFAULT;

	AABBVertexBuffer = RenderingAPI::Get().CreateBuffer(BufferProps);
	if (!AABBVertexBuffer)
	{
		return false;
	}

	// Create IndexBuffer
	UInt16 Indices[24] =
	{
		0, 1,
		1, 3,
		3, 2,
		2, 0,
		1, 4,
		3, 6,
		6, 4,
		4, 5,
		5, 7,
		7, 6,
		0, 5,
		2, 7,
	};

	BufferProps.SizeInBytes = sizeof(Indices);
	BufferProps.Flags		= D3D12_RESOURCE_FLAG_NONE;
	BufferProps.InitalState = D3D12_RESOURCE_STATE_COMMON;
	BufferProps.MemoryType	= EMemoryType::MEMORY_TYPE_DEFAULT;

	AABBIndexBuffer = RenderingAPI::Get().CreateBuffer(BufferProps);
	if (!AABBIndexBuffer)
	{
		return false;
	}

	// Upload Data
	RenderingAPI::Get().GetImmediateCommandList()->TransitionBarrier(AABBVertexBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
	RenderingAPI::Get().GetImmediateCommandList()->TransitionBarrier(AABBIndexBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);

	RenderingAPI::Get().GetImmediateCommandList()->UploadBufferData(AABBVertexBuffer.Get(), 0, Vertices, sizeof(Vertices));
	RenderingAPI::Get().GetImmediateCommandList()->UploadBufferData(AABBIndexBuffer.Get(), 0, Indices, sizeof(Indices));

	RenderingAPI::Get().GetImmediateCommandList()->TransitionBarrier(AABBVertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	RenderingAPI::Get().GetImmediateCommandList()->TransitionBarrier(AABBIndexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);

	RenderingAPI::Get().GetImmediateCommandList()->Flush();
	RenderingAPI::Get().GetImmediateCommandList()->WaitForCompletion();

	return true;
}

bool Renderer::InitAA()
{
	using namespace Microsoft::WRL;

	// No AA
	ComPtr<IDxcBlob> VSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/FullscreenVS.hlsl", "Main", "vs_6_0");
	if (!VSBlob)
	{
		return false;
	}

	ComPtr<IDxcBlob> PSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/PostProcessPS.hlsl", "Main", "ps_6_0");
	if (!PSBlob)
	{
		return false;
	}

	D3D12_DESCRIPTOR_RANGE Ranges[1] = {};
	Ranges[0].BaseShaderRegister				= 0;
	Ranges[0].NumDescriptors					= 1;
	Ranges[0].RegisterSpace						= 0;
	Ranges[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	Ranges[0].OffsetInDescriptorsFromTableStart	= 0;

	D3D12_ROOT_PARAMETER Parameters[2];
	Parameters[0].ParameterType							= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	Parameters[0].ShaderVisibility						= D3D12_SHADER_VISIBILITY_PIXEL;
	Parameters[0].DescriptorTable.NumDescriptorRanges	= 1;
	Parameters[0].DescriptorTable.pDescriptorRanges		= Ranges;

	Parameters[1].ParameterType				= D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	Parameters[1].ShaderVisibility			= D3D12_SHADER_VISIBILITY_PIXEL;
	Parameters[1].Constants.Num32BitValues	= 2;
	Parameters[1].Constants.RegisterSpace	= 0;
	Parameters[1].Constants.ShaderRegister	= 0;

	D3D12_STATIC_SAMPLER_DESC Samplers[2] = { };
	Samplers[0].Filter				= D3D12_FILTER_MIN_MAG_MIP_POINT;
	Samplers[0].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	Samplers[0].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	Samplers[0].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	Samplers[0].MipLODBias			= 0.0f;
	Samplers[0].MaxAnisotropy		= 0;
	Samplers[0].ComparisonFunc		= D3D12_COMPARISON_FUNC_NEVER;
	Samplers[0].BorderColor			= D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
	Samplers[0].MinLOD				= 0.0f;
	Samplers[0].MaxLOD				= 1.0f;
	Samplers[0].ShaderRegister		= 0;
	Samplers[0].RegisterSpace		= 0;
	Samplers[0].ShaderVisibility	= D3D12_SHADER_VISIBILITY_PIXEL;

	Samplers[1].Filter				= D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	Samplers[1].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	Samplers[1].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	Samplers[1].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	Samplers[1].MipLODBias			= 0.0f;
	Samplers[1].MaxAnisotropy		= 0;
	Samplers[1].ComparisonFunc		= D3D12_COMPARISON_FUNC_NEVER;
	Samplers[1].BorderColor			= D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
	Samplers[1].MinLOD				= 0.0f;
	Samplers[1].MaxLOD				= 1.0f;
	Samplers[1].ShaderRegister		= 1;
	Samplers[1].RegisterSpace		= 0;
	Samplers[1].ShaderVisibility	= D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC RootSignatureDesc = { };
	RootSignatureDesc.NumParameters		= 2;
	RootSignatureDesc.pParameters		= Parameters;
	RootSignatureDesc.NumStaticSamplers	= 2;
	RootSignatureDesc.pStaticSamplers	= Samplers;
	RootSignatureDesc.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS		|
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS	|
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	PostRootSignature = RenderingAPI::Get().CreateRootSignature(RootSignatureDesc);
	if (!PostRootSignature)
	{
		return false;
	}

	DXGI_FORMAT Formats[] =
	{
		DXGI_FORMAT_R8G8B8A8_UNORM,
	};

	GraphicsPipelineStateProperties PSOProperties = { };
	PSOProperties.DebugName			= "Post PSO";
	PSOProperties.VSBlob			= VSBlob.Get();
	PSOProperties.PSBlob			= PSBlob.Get();
	PSOProperties.RootSignature		= PostRootSignature.Get();
	PSOProperties.InputElements		= nullptr;
	PSOProperties.NumInputElements	= 0;
	PSOProperties.EnableDepth		= false;
	PSOProperties.DepthWriteMask	= D3D12_DEPTH_WRITE_MASK_ZERO;
	PSOProperties.EnableBlending	= false;
	PSOProperties.DepthFunc			= D3D12_COMPARISON_FUNC_ALWAYS;
	PSOProperties.DepthBufferFormat	= DXGI_FORMAT_UNKNOWN;
	PSOProperties.CullMode			= D3D12_CULL_MODE_NONE;
	PSOProperties.RTFormats			= Formats;
	PSOProperties.NumRenderTargets	= 1;
	PSOProperties.PrimitiveType		= D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	PostPSO = RenderingAPI::Get().CreateGraphicsPipelineState(PSOProperties);
	if (!PostPSO)
	{
		return false;
	}

	// FXAA
	PSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/FXAA_PS.hlsl", "Main", "ps_6_0");
	if (!PSBlob)
	{
		return false;
	}

	PSOProperties.DebugName = "FXAA PSO";
	PSOProperties.VSBlob	= VSBlob.Get();
	PSOProperties.PSBlob	= PSBlob.Get();

	FXAAPSO = RenderingAPI::Get().CreateGraphicsPipelineState(PSOProperties);
	if (!FXAAPSO)
	{
		return false;
	}

	return true;
}

bool Renderer::InitForwardPass()
{
	using namespace Microsoft::WRL;

	// Init PipelineState
	DxcDefine Defines[] =
	{
		{ L"ENABLE_PARALLAX_MAPPING",	L"1" },
		{ L"ENABLE_NORMAL_MAPPING",		L"1" },
	};

	ComPtr<IDxcBlob> VSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/ForwardPass.hlsl", "VSMain", "vs_6_0", Defines, 2);
	if (!VSBlob)
	{
		return false;
	}

	ComPtr<IDxcBlob> PSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/ForwardPass.hlsl", "PSMain", "ps_6_0", Defines, 2);
	if (!PSBlob)
	{
		return false;
	}

	// Init RootSignatures
	{
		constexpr UInt32 NumPerFrameRanges = 8;
		D3D12_DESCRIPTOR_RANGE PerFrameRanges[NumPerFrameRanges] = {};
		// Camera Buffer
		PerFrameRanges[0].BaseShaderRegister				= 0;
		PerFrameRanges[0].NumDescriptors					= 1;
		PerFrameRanges[0].RegisterSpace						= 1;
		PerFrameRanges[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		PerFrameRanges[0].OffsetInDescriptorsFromTableStart	= 0;

		// PointLight Buffer
		PerFrameRanges[1].BaseShaderRegister				= 1;
		PerFrameRanges[1].NumDescriptors					= 1;
		PerFrameRanges[1].RegisterSpace						= 1;
		PerFrameRanges[1].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		PerFrameRanges[1].OffsetInDescriptorsFromTableStart = 1;

		// DirLight Buffer
		PerFrameRanges[2].BaseShaderRegister				= 2;
		PerFrameRanges[2].NumDescriptors					= 1;
		PerFrameRanges[2].RegisterSpace						= 1;
		PerFrameRanges[2].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		PerFrameRanges[2].OffsetInDescriptorsFromTableStart	= 2;

		// Irradiance Map
		PerFrameRanges[3].BaseShaderRegister				= 0;
		PerFrameRanges[3].NumDescriptors					= 1;
		PerFrameRanges[3].RegisterSpace						= 1;
		PerFrameRanges[3].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerFrameRanges[3].OffsetInDescriptorsFromTableStart = 3;

		// Specular Irradiance Map
		PerFrameRanges[4].BaseShaderRegister				= 1;
		PerFrameRanges[4].NumDescriptors					= 1;
		PerFrameRanges[4].RegisterSpace						= 1;
		PerFrameRanges[4].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerFrameRanges[4].OffsetInDescriptorsFromTableStart = 4;

		// Integration LUT
		PerFrameRanges[5].BaseShaderRegister				= 2;
		PerFrameRanges[5].NumDescriptors					= 1;
		PerFrameRanges[5].RegisterSpace						= 1;
		PerFrameRanges[5].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerFrameRanges[5].OffsetInDescriptorsFromTableStart = 5;

		// DirLight ShadowMaps
		PerFrameRanges[6].BaseShaderRegister				= 3;
		PerFrameRanges[6].NumDescriptors					= 1;
		PerFrameRanges[6].RegisterSpace						= 1;
		PerFrameRanges[6].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerFrameRanges[6].OffsetInDescriptorsFromTableStart = 6;

		// PointLight ShadowMaps
		PerFrameRanges[7].BaseShaderRegister				= 4;
		PerFrameRanges[7].NumDescriptors					= 1;
		PerFrameRanges[7].RegisterSpace						= 1;
		PerFrameRanges[7].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerFrameRanges[7].OffsetInDescriptorsFromTableStart = 7;

		constexpr UInt32 NumPerObjectRanges = 8;
		D3D12_DESCRIPTOR_RANGE PerObjectRanges[NumPerObjectRanges] = {};
		// Albedo Map
		PerObjectRanges[0].BaseShaderRegister					= 0;
		PerObjectRanges[0].NumDescriptors						= 1;
		PerObjectRanges[0].RegisterSpace						= 0;
		PerObjectRanges[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerObjectRanges[0].OffsetInDescriptorsFromTableStart	= 0;

		// Normal Map
		PerObjectRanges[1].BaseShaderRegister					= 1;
		PerObjectRanges[1].NumDescriptors						= 1;
		PerObjectRanges[1].RegisterSpace						= 0;
		PerObjectRanges[1].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerObjectRanges[1].OffsetInDescriptorsFromTableStart	= 1;

		// Roughness Map
		PerObjectRanges[2].BaseShaderRegister					= 2;
		PerObjectRanges[2].NumDescriptors						= 1;
		PerObjectRanges[2].RegisterSpace						= 0;
		PerObjectRanges[2].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerObjectRanges[2].OffsetInDescriptorsFromTableStart	= 2;

		// Height Map
		PerObjectRanges[3].BaseShaderRegister					= 3;
		PerObjectRanges[3].NumDescriptors						= 1;
		PerObjectRanges[3].RegisterSpace						= 0;
		PerObjectRanges[3].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerObjectRanges[3].OffsetInDescriptorsFromTableStart	= 3;

		// Metallic Map
		PerObjectRanges[4].BaseShaderRegister					= 4;
		PerObjectRanges[4].NumDescriptors						= 1;
		PerObjectRanges[4].RegisterSpace						= 0;
		PerObjectRanges[4].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerObjectRanges[4].OffsetInDescriptorsFromTableStart	= 4;

		// AO Map
		PerObjectRanges[5].BaseShaderRegister					= 5;
		PerObjectRanges[5].NumDescriptors						= 1;
		PerObjectRanges[5].RegisterSpace						= 0;
		PerObjectRanges[5].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerObjectRanges[5].OffsetInDescriptorsFromTableStart	= 5;

		// Material Buffer
		PerObjectRanges[6].BaseShaderRegister					= 1;
		PerObjectRanges[6].NumDescriptors						= 1;
		PerObjectRanges[6].RegisterSpace						= 0;
		PerObjectRanges[6].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		PerObjectRanges[6].OffsetInDescriptorsFromTableStart	= 6;

		// Alpha Mask
		PerObjectRanges[7].BaseShaderRegister					= 6;
		PerObjectRanges[7].NumDescriptors						= 1;
		PerObjectRanges[7].RegisterSpace						= 0;
		PerObjectRanges[7].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerObjectRanges[7].OffsetInDescriptorsFromTableStart	= 7;

		D3D12_ROOT_PARAMETER Parameters[3];
		// Transform Constants
		Parameters[0].ParameterType				= D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		Parameters[0].Constants.ShaderRegister	= 0;
		Parameters[0].Constants.RegisterSpace	= 0;
		Parameters[0].Constants.Num32BitValues	= 32;
		Parameters[0].ShaderVisibility			= D3D12_SHADER_VISIBILITY_ALL;

		// PerFrame DescriptorTable
		Parameters[1].ParameterType							= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		Parameters[1].DescriptorTable.NumDescriptorRanges	= NumPerFrameRanges;
		Parameters[1].DescriptorTable.pDescriptorRanges		= PerFrameRanges;
		Parameters[1].ShaderVisibility						= D3D12_SHADER_VISIBILITY_ALL;

		// PerObject DescriptorTable
		Parameters[2].ParameterType							= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		Parameters[2].DescriptorTable.NumDescriptorRanges	= NumPerObjectRanges;
		Parameters[2].DescriptorTable.pDescriptorRanges		= PerObjectRanges;
		Parameters[2].ShaderVisibility						= D3D12_SHADER_VISIBILITY_PIXEL;

		constexpr UInt32 NumStaticSamplers = 5;
		D3D12_STATIC_SAMPLER_DESC StaticSamplers[NumStaticSamplers] = { };
		// Material Sampler
		StaticSamplers[0].Filter			= D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		StaticSamplers[0].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		StaticSamplers[0].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		StaticSamplers[0].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		StaticSamplers[0].MipLODBias		= 0.0f;
		StaticSamplers[0].MaxAnisotropy		= 0;
		StaticSamplers[0].ComparisonFunc	= D3D12_COMPARISON_FUNC_NEVER;
		StaticSamplers[0].BorderColor		= D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		StaticSamplers[0].MinLOD			= 0.0f;
		StaticSamplers[0].MaxLOD			= FLT_MAX;
		StaticSamplers[0].ShaderRegister	= 0;
		StaticSamplers[0].RegisterSpace		= 1;
		StaticSamplers[0].ShaderVisibility	= D3D12_SHADER_VISIBILITY_PIXEL;

		// LUT Sampler
		StaticSamplers[1].Filter			= D3D12_FILTER_MIN_MAG_MIP_POINT;
		StaticSamplers[1].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		StaticSamplers[1].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		StaticSamplers[1].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		StaticSamplers[1].MipLODBias		= 0.0f;
		StaticSamplers[1].MaxAnisotropy		= 0;
		StaticSamplers[1].ComparisonFunc	= D3D12_COMPARISON_FUNC_NEVER;
		StaticSamplers[1].BorderColor		= D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		StaticSamplers[1].MinLOD			= 0.0f;
		StaticSamplers[1].MaxLOD			= 1.0f;
		StaticSamplers[1].ShaderRegister	= 1;
		StaticSamplers[1].RegisterSpace		= 1;
		StaticSamplers[1].ShaderVisibility	= D3D12_SHADER_VISIBILITY_PIXEL;

		// Irradiance Sampler
		StaticSamplers[2].Filter			= D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		StaticSamplers[2].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		StaticSamplers[2].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		StaticSamplers[2].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		StaticSamplers[2].MipLODBias		= 0.0f;
		StaticSamplers[2].MaxAnisotropy		= 0;
		StaticSamplers[2].ComparisonFunc	= D3D12_COMPARISON_FUNC_NEVER;
		StaticSamplers[2].BorderColor		= D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		StaticSamplers[2].MinLOD			= 0.0f;
		StaticSamplers[2].MaxLOD			= FLT_MAX;
		StaticSamplers[2].ShaderRegister	= 2;
		StaticSamplers[2].RegisterSpace		= 1;
		StaticSamplers[2].ShaderVisibility	= D3D12_SHADER_VISIBILITY_PIXEL;

		// Comparison ShadowMap Sampler
		StaticSamplers[3].Filter			= D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
		StaticSamplers[3].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		StaticSamplers[3].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		StaticSamplers[3].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		StaticSamplers[3].MipLODBias		= 0.0f;
		StaticSamplers[3].MaxAnisotropy		= 0;
		StaticSamplers[3].ComparisonFunc	= D3D12_COMPARISON_FUNC_LESS;
		StaticSamplers[3].BorderColor		= D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
		StaticSamplers[3].MinLOD			= 0.0f;
		StaticSamplers[3].MaxLOD			= FLT_MAX;
		StaticSamplers[3].ShaderRegister	= 3;
		StaticSamplers[3].RegisterSpace		= 1;
		StaticSamplers[3].ShaderVisibility	= D3D12_SHADER_VISIBILITY_PIXEL;

		// PointLight ShadowMap Sampler
		StaticSamplers[4].Filter			= D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		StaticSamplers[4].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		StaticSamplers[4].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		StaticSamplers[4].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		StaticSamplers[4].MipLODBias		= 0.0f;
		StaticSamplers[4].MaxAnisotropy		= 0;
		StaticSamplers[4].ComparisonFunc	= D3D12_COMPARISON_FUNC_NEVER;
		StaticSamplers[4].BorderColor		= D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
		StaticSamplers[4].MinLOD			= 0.0f;
		StaticSamplers[4].MaxLOD			= FLT_MAX;
		StaticSamplers[4].ShaderRegister	= 4;
		StaticSamplers[4].RegisterSpace		= 1;
		StaticSamplers[4].ShaderVisibility	= D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC RootSignatureDesc = { };
		RootSignatureDesc.NumParameters		= 3;
		RootSignatureDesc.pParameters		= Parameters;
		RootSignatureDesc.NumStaticSamplers	= NumStaticSamplers;
		RootSignatureDesc.pStaticSamplers	= StaticSamplers;
		RootSignatureDesc.Flags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		ForwardRootSignature = RenderingAPI::Get().CreateRootSignature(RootSignatureDesc);
		if (!ForwardRootSignature->Initialize(RootSignatureDesc))
		{
			return false;
		}

		// Init PipelineState
		D3D12_INPUT_ELEMENT_DESC InputElementDesc[] =
		{
			{ "POSITION",	0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 0,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL",		0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 12,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TANGENT",	0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 24,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD",	0, DXGI_FORMAT_R32G32_FLOAT,	0, 36,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		GraphicsPipelineStateProperties PSOProperties = { };
		PSOProperties.DebugName			= "ForwardPass PipelineState";
		PSOProperties.VSBlob			= VSBlob.Get();
		PSOProperties.PSBlob			= PSBlob.Get();
		PSOProperties.RootSignature		= ForwardRootSignature.Get();
		PSOProperties.InputElements		= InputElementDesc;
		PSOProperties.NumInputElements	= 4;
		PSOProperties.EnableDepth		= true;
		PSOProperties.DepthWriteMask	= D3D12_DEPTH_WRITE_MASK_ALL;
		PSOProperties.DepthFunc			= D3D12_COMPARISON_FUNC_LESS;
		PSOProperties.DepthBufferFormat = DepthBufferFormat;
		PSOProperties.CullMode			= D3D12_CULL_MODE_NONE;
		PSOProperties.EnableBlending	= true;

		DXGI_FORMAT Formats[] =
		{
			RenderTargetFormat
		};

		PSOProperties.RTFormats			= Formats;
		PSOProperties.NumRenderTargets	= 1;

		ForwardPSO = RenderingAPI::Get().CreateGraphicsPipelineState(PSOProperties);
		if (!ForwardPSO)
		{
			return false;
		}

		return true;
	}
}

bool Renderer::InitSSAO()
{
	using namespace Microsoft::WRL;

	// Init texture
	{
		TextureProperties TextureProps = { };
		TextureProps.DebugName = "SSAO Buffer";
		TextureProps.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		TextureProps.Width = static_cast<UInt16>(RenderingAPI::Get().GetSwapChain()->GetWidth());
		TextureProps.Height = static_cast<UInt16>(RenderingAPI::Get().GetSwapChain()->GetHeight());
		TextureProps.MipLevels = 1;
		TextureProps.ArrayCount = 1;
		TextureProps.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		TextureProps.MemoryType = EMemoryType::MEMORY_TYPE_DEFAULT;
		TextureProps.SampleCount = 1;

		SSAOBuffer = RenderingAPI::Get().CreateTexture(TextureProps);
		if (!SSAOBuffer)
		{
			Debug::DebugBreak();
			return false;
		}

		D3D12_UNORDERED_ACCESS_VIEW_DESC UAVView = { };
		UAVView.Format = TextureProps.Format;
		UAVView.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		UAVView.Texture2D.MipSlice = 0;
		UAVView.Texture2D.PlaneSlice = 0;

		SSAOBuffer->SetUnorderedAccessView(TSharedPtr(RenderingAPI::Get().CreateUnorderedAccessView(nullptr, SSAOBuffer->GetResource(), &UAVView)), 0);

		D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc = { };
		SrvDesc.Format = TextureProps.Format;
		SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		SrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SrvDesc.Texture2D.MipLevels = 1;
		SrvDesc.Texture2D.MostDetailedMip = 0;
		SrvDesc.Texture2D.PlaneSlice = 0;
		SrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		SSAOBuffer->SetShaderResourceView(TSharedPtr(RenderingAPI::Get().CreateShaderResourceView(SSAOBuffer->GetResource(), &SrvDesc)), 0);
	}

	// Load shader
	ComPtr<IDxcBlob> CSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/SSAO.hlsl", "Main", "cs_6_0");
	if (!CSBlob)
	{
		Debug::DebugBreak();
		return false;
	}

	{
		// Init RootSignatures
		constexpr UInt32 NumPerFrameRanges = 6;
		D3D12_DESCRIPTOR_RANGE PerFrameRanges[NumPerFrameRanges] = {};
		// Normal Buffer
		PerFrameRanges[0].BaseShaderRegister = 0;
		PerFrameRanges[0].NumDescriptors = 1;
		PerFrameRanges[0].RegisterSpace = 0;
		PerFrameRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerFrameRanges[0].OffsetInDescriptorsFromTableStart = 0;

		// Depth Buffer
		PerFrameRanges[1].BaseShaderRegister = 1;
		PerFrameRanges[1].NumDescriptors = 1;
		PerFrameRanges[1].RegisterSpace = 0;
		PerFrameRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerFrameRanges[1].OffsetInDescriptorsFromTableStart = 1;

		// Noise 
		PerFrameRanges[2].BaseShaderRegister = 2;
		PerFrameRanges[2].NumDescriptors = 1;
		PerFrameRanges[2].RegisterSpace = 0;
		PerFrameRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerFrameRanges[2].OffsetInDescriptorsFromTableStart = 2;

		// Output Buffer
		PerFrameRanges[3].BaseShaderRegister = 0;
		PerFrameRanges[3].NumDescriptors = 1;
		PerFrameRanges[3].RegisterSpace = 0;
		PerFrameRanges[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		PerFrameRanges[3].OffsetInDescriptorsFromTableStart = 3;

		// Samples 
		PerFrameRanges[4].BaseShaderRegister = 3;
		PerFrameRanges[4].NumDescriptors = 1;
		PerFrameRanges[4].RegisterSpace = 0;
		PerFrameRanges[4].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		PerFrameRanges[4].OffsetInDescriptorsFromTableStart = 4;

		// Camera
		PerFrameRanges[5].BaseShaderRegister = 1;
		PerFrameRanges[5].NumDescriptors = 1;
		PerFrameRanges[5].RegisterSpace = 0;
		PerFrameRanges[5].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		PerFrameRanges[5].OffsetInDescriptorsFromTableStart = 5;

		constexpr UInt32 NumParameters = 2;
		D3D12_ROOT_PARAMETER Parameters[NumParameters];
		// DescriptorTable
		Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		Parameters[0].DescriptorTable.NumDescriptorRanges = NumPerFrameRanges;
		Parameters[0].DescriptorTable.pDescriptorRanges = PerFrameRanges;
		Parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		// Settings
		Parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		Parameters[1].Constants.Num32BitValues = 7;
		Parameters[1].Constants.RegisterSpace = 0;
		Parameters[1].Constants.ShaderRegister = 0;
		Parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		constexpr UInt32 NumStaticSamplers = 2;
		D3D12_STATIC_SAMPLER_DESC StaticSamplers[NumStaticSamplers] = { };
		// GBuffer Sampler
		StaticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		StaticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		StaticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		StaticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		StaticSamplers[0].MipLODBias = 0.0f;
		StaticSamplers[0].MaxAnisotropy = 0;
		StaticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		StaticSamplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		StaticSamplers[0].MinLOD = 0.0f;
		StaticSamplers[0].MaxLOD = FLT_MAX;
		StaticSamplers[0].ShaderRegister = 0;
		StaticSamplers[0].RegisterSpace = 0;
		StaticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		// Noise Samplers
		StaticSamplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		StaticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		StaticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		StaticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		StaticSamplers[1].MipLODBias = 0.0f;
		StaticSamplers[1].MaxAnisotropy = 0;
		StaticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		StaticSamplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		StaticSamplers[1].MinLOD = 0.0f;
		StaticSamplers[1].MaxLOD = FLT_MAX;
		StaticSamplers[1].ShaderRegister = 1;
		StaticSamplers[1].RegisterSpace = 0;
		StaticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_ROOT_SIGNATURE_DESC RootSignatureDesc = { };
		RootSignatureDesc.NumParameters = NumParameters;
		RootSignatureDesc.pParameters = Parameters;
		RootSignatureDesc.NumStaticSamplers = NumStaticSamplers;
		RootSignatureDesc.pStaticSamplers = StaticSamplers;
		RootSignatureDesc.Flags =
			D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

		SSAORootSignature = RenderingAPI::Get().CreateRootSignature(RootSignatureDesc);
		if (!SSAORootSignature->Initialize(RootSignatureDesc))
		{
			Debug::DebugBreak();
			return false;
		}
	}

	{
		// Init PipelineState
		ComputePipelineStateProperties PSOProperties = { };
		PSOProperties.DebugName		= "SSAO PipelineState";
		PSOProperties.CSBlob		= CSBlob.Get();
		PSOProperties.RootSignature = SSAORootSignature.Get();

		SSAOPSO = RenderingAPI::Get().CreateComputePipelineState(PSOProperties);
		if (!SSAOPSO)
		{
			Debug::DebugBreak();
			return false;
		}
	}

	// Generate SSAO Kernel
	std::uniform_real_distribution<Float> RandomFloats(0.0f, 1.0f);
	std::default_random_engine Generator;
	TArray<XMFLOAT3> SSAOKernel;
	for (UInt32 i = 0; i < 64; ++i)
	{
		XMVECTOR XmSample = XMVectorSet(
			RandomFloats(Generator) * 2.0f - 1.0f,
			RandomFloats(Generator) * 2.0f - 1.0f,
			RandomFloats(Generator), 
			0.0f);

		Float Scale = RandomFloats(Generator);
		XmSample = XMVector3Normalize(XmSample);
		XmSample = XMVectorScale(XmSample, Scale);

		Scale = Float(i) / 64.0f;
		Scale = Math::Lerp(0.1f, 1.0f, Scale * Scale);
		XmSample = XMVectorScale(XmSample, Scale);

		XMFLOAT3 Sample;
		XMStoreFloat3(&Sample, XmSample);
		SSAOKernel.PushBack(Sample);
	}

	// Generate noise
	TArray<Float16> SSAONoise;
	for (UInt32 i = 0; i < 16; i++)
	{
		const Float x = RandomFloats(Generator) * 2.0f - 1.0f;
		const Float y = RandomFloats(Generator) * 2.0f - 1.0f;

		SSAONoise.EmplaceBack(x);
		SSAONoise.EmplaceBack(y);
		SSAONoise.EmplaceBack(0.0f);
		SSAONoise.EmplaceBack(0.0f);
	}

	// Init texture
	{
		TextureProperties TextureProps = { };
		TextureProps.DebugName		= "SSAO Noise";
		TextureProps.Flags			= D3D12_RESOURCE_FLAG_NONE;
		TextureProps.Width			= 4;
		TextureProps.Height			= 4;
		TextureProps.MipLevels		= 1;
		TextureProps.ArrayCount		= 1;
		TextureProps.Format			= DXGI_FORMAT_R16G16B16A16_FLOAT;
		TextureProps.MemoryType		= EMemoryType::MEMORY_TYPE_DEFAULT;
		TextureProps.SampleCount	= 1;

		SSAONoiseTex = RenderingAPI::Get().CreateTexture(TextureProps);
		if (!SSAONoiseTex)
		{
			Debug::DebugBreak();
			return false;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc = { };
		SrvDesc.Format							= TextureProps.Format;
		SrvDesc.ViewDimension					= D3D12_SRV_DIMENSION_TEXTURE2D;
		SrvDesc.Shader4ComponentMapping			= D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SrvDesc.Texture2D.MipLevels				= 1;
		SrvDesc.Texture2D.MostDetailedMip		= 0;
		SrvDesc.Texture2D.PlaneSlice			= 0;
		SrvDesc.Texture2D.ResourceMinLODClamp	= 0.0f;

		SSAONoiseTex->SetShaderResourceView(TSharedPtr(RenderingAPI::Get().CreateShaderResourceView(SSAONoiseTex->GetResource(), &SrvDesc)), 0);

		RenderingAPI::StaticGetImmediateCommandList()->TransitionBarrier(SSAONoiseTex.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
		RenderingAPI::StaticGetImmediateCommandList()->TransitionBarrier(SSAOBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		const UInt32 Stride		= 4 * sizeof(Float16);
		const UInt32 RowPitch	= ((4 * Stride) + (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
		RenderingAPI::StaticGetImmediateCommandList()->UploadTextureData(SSAONoiseTex.Get(), SSAONoise.Data(), TextureProps.Format, 4, 4, 1, Stride, RowPitch);
		
		RenderingAPI::StaticGetImmediateCommandList()->TransitionBarrier(SSAONoiseTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		
		RenderingAPI::StaticGetImmediateCommandList()->Flush();
		RenderingAPI::StaticGetImmediateCommandList()->WaitForCompletion();
	}

	// Init samples
	{
		BufferProperties SamplesProps;
		SamplesProps.Name			= "SSAO Samples Buffer";
		SamplesProps.Flags			= D3D12_RESOURCE_FLAG_NONE;
		SamplesProps.InitalState	= D3D12_RESOURCE_STATE_COMMON;
		SamplesProps.MemoryType		= EMemoryType::MEMORY_TYPE_DEFAULT;
		SamplesProps.SizeInBytes	= sizeof(XMFLOAT3) * SSAOKernel.Size();

		SSAOSamples = RenderingAPI::Get().CreateBuffer(SamplesProps);
		if (!SSAOSamples)
		{
			Debug::DebugBreak();
			return false;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc = { };
		SrvDesc.Format						= DXGI_FORMAT_UNKNOWN;
		SrvDesc.ViewDimension				= D3D12_SRV_DIMENSION_BUFFER;
		SrvDesc.Shader4ComponentMapping		= D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SrvDesc.Buffer.FirstElement			= 0;
		SrvDesc.Buffer.Flags				= D3D12_BUFFER_SRV_FLAG_NONE;
		SrvDesc.Buffer.NumElements			= SSAOKernel.Size();
		SrvDesc.Buffer.StructureByteStride	= sizeof(XMFLOAT3);

		SSAOSamples->SetShaderResourceView(TSharedPtr(RenderingAPI::Get().CreateShaderResourceView(SSAOSamples->GetResource(), &SrvDesc)), 0);

		RenderingAPI::StaticGetImmediateCommandList()->TransitionBarrier(SSAOSamples.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
		RenderingAPI::StaticGetImmediateCommandList()->UploadBufferData(SSAOSamples.Get(), 0, SSAOKernel.Data(), SamplesProps.SizeInBytes);
		RenderingAPI::StaticGetImmediateCommandList()->TransitionBarrier(SSAOSamples.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		
		RenderingAPI::StaticGetImmediateCommandList()->Flush();
		RenderingAPI::StaticGetImmediateCommandList()->WaitForCompletion();
	}

	// Load shader
	CSBlob = D3D12ShaderCompiler::CompileFromFile("Shaders/Blur.hlsl", "Main", "cs_6_0");
	if (!CSBlob)
	{
		Debug::DebugBreak();
		return false;
	}

	{
		// Init RootSignatures
		constexpr UInt32 NumPerFrameRanges = 1;
		D3D12_DESCRIPTOR_RANGE PerFrameRanges[NumPerFrameRanges] = {};
		// Texture
		PerFrameRanges[0].BaseShaderRegister				= 0;
		PerFrameRanges[0].NumDescriptors					= 1;
		PerFrameRanges[0].RegisterSpace						= 0;
		PerFrameRanges[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		PerFrameRanges[0].OffsetInDescriptorsFromTableStart	= 0;

		constexpr UInt32 NumParameters = 2;
		D3D12_ROOT_PARAMETER Parameters[NumParameters];
		// DescriptorTable
		Parameters[0].ParameterType							= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		Parameters[0].DescriptorTable.NumDescriptorRanges	= NumPerFrameRanges;
		Parameters[0].DescriptorTable.pDescriptorRanges		= PerFrameRanges;
		Parameters[0].ShaderVisibility						= D3D12_SHADER_VISIBILITY_ALL;

		// Settings
		Parameters[1].ParameterType				= D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		Parameters[1].Constants.Num32BitValues	= 2;
		Parameters[1].Constants.RegisterSpace	= 0;
		Parameters[1].Constants.ShaderRegister	= 0;
		Parameters[1].ShaderVisibility			= D3D12_SHADER_VISIBILITY_ALL;

		constexpr UInt32 NumStaticSamplers = 1;
		D3D12_STATIC_SAMPLER_DESC StaticSamplers[NumStaticSamplers] = { };
		// Sampler, not used for now
		StaticSamplers[0].Filter			= D3D12_FILTER_MIN_MAG_MIP_POINT;
		StaticSamplers[0].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		StaticSamplers[0].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		StaticSamplers[0].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		StaticSamplers[0].MipLODBias		= 0.0f;
		StaticSamplers[0].MaxAnisotropy		= 0;
		StaticSamplers[0].ComparisonFunc	= D3D12_COMPARISON_FUNC_NEVER;
		StaticSamplers[0].BorderColor		= D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		StaticSamplers[0].MinLOD			= 0.0f;
		StaticSamplers[0].MaxLOD			= FLT_MAX;
		StaticSamplers[0].ShaderRegister	= 0;
		StaticSamplers[0].RegisterSpace		= 0;
		StaticSamplers[0].ShaderVisibility	= D3D12_SHADER_VISIBILITY_ALL;

		D3D12_ROOT_SIGNATURE_DESC RootSignatureDesc = { };
		RootSignatureDesc.NumParameters		= NumParameters;
		RootSignatureDesc.pParameters		= Parameters;
		RootSignatureDesc.NumStaticSamplers	= NumStaticSamplers;
		RootSignatureDesc.pStaticSamplers	= StaticSamplers;
		RootSignatureDesc.Flags =
			D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

		BlurRootSignature = RenderingAPI::Get().CreateRootSignature(RootSignatureDesc);
		if (!BlurRootSignature->Initialize(RootSignatureDesc))
		{
			Debug::DebugBreak();
			return false;
		}
	}

	{
		// Init PipelineState
		ComputePipelineStateProperties PSOProperties = { };
		PSOProperties.DebugName		= "Blur PipelineState";
		PSOProperties.CSBlob		= CSBlob.Get();
		PSOProperties.RootSignature	= BlurRootSignature.Get();

		SSAOBlur = RenderingAPI::Get().CreateComputePipelineState(PSOProperties);
		if (!SSAOBlur)
		{
			Debug::DebugBreak();
			return false;
		}
	}

	return true;
}

bool Renderer::CreateShadowMaps()
{
	// DirLights
	if (DirLightShadowMaps)
	{
		DeferredResources.EmplaceBack(DirLightShadowMaps);
	}

	if (VSMDirLightShadowMaps)
	{
		DeferredResources.EmplaceBack(VSMDirLightShadowMaps);
	}

	TextureProperties ShadowMapProps = { };
	ShadowMapProps.DebugName	= "Directional Light ShadowMaps";
	ShadowMapProps.ArrayCount	= 1;
	ShadowMapProps.Format		= ShadowMapFormat;
	ShadowMapProps.Flags		= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	ShadowMapProps.Height		= Renderer::GetGlobalLightSettings().ShadowMapHeight;
	ShadowMapProps.Width		= Renderer::GetGlobalLightSettings().ShadowMapWidth;
	ShadowMapProps.InitalState	= D3D12_RESOURCE_STATE_COMMON;
	ShadowMapProps.MemoryType	= EMemoryType::MEMORY_TYPE_DEFAULT;
	ShadowMapProps.MipLevels	= 1;
	ShadowMapProps.SampleCount	= 1;
	
	D3D12_CLEAR_VALUE Value0;
	Value0.Format				= ShadowMapFormat;
	Value0.DepthStencil.Depth	= 1.0f;
	Value0.DepthStencil.Stencil	= 0;
	ShadowMapProps.OptimizedClearValue = &Value0;

	DirLightShadowMaps = RenderingAPI::Get().CreateTexture(ShadowMapProps);
	if (DirLightShadowMaps)
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC DSVDesc = { };
		DSVDesc.Format				= ShadowMapFormat;
		DSVDesc.ViewDimension		= D3D12_DSV_DIMENSION_TEXTURE2D;
		DSVDesc.Texture2D.MipSlice	= 0;
		DirLightShadowMaps->SetDepthStencilView(TSharedPtr(RenderingAPI::Get().CreateDepthStencilView(DirLightShadowMaps->GetResource(), &DSVDesc)), 0);

#if !ENABLE_VSM
		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = { };
		SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
		SRVDesc.ViewDimension					= D3D12_SRV_DIMENSION_TEXTURE2D;
		SRVDesc.Texture2D.MipLevels				= 1;
		SRVDesc.Texture2D.MostDetailedMip		= 0;
		SRVDesc.Texture2D.PlaneSlice			= 0;
		SRVDesc.Texture2D.ResourceMinLODClamp	= 0.0f;
		SRVDesc.Shader4ComponentMapping			= D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		DirLightShadowMaps->SetShaderResourceView(TSharedPtr(RenderingAPI::Get().CreateShaderResourceView(DirLightShadowMaps->GetResource(), &SRVDesc)), 0);
#endif
	}
	else
	{
		return false;
	}

#if ENABLE_VSM
	ShadowMapProps.DebugName	= "Directional Light ShadowMaps";
	ShadowMapProps.ArrayCount	= 1;
	ShadowMapProps.Format		= DXGI_FORMAT_R32G32_FLOAT;
	ShadowMapProps.Flags		= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	ShadowMapProps.Height		= Renderer::GetGlobalLightSettings().ShadowMapHeight;
	ShadowMapProps.Width		= Renderer::GetGlobalLightSettings().ShadowMapWidth;
	ShadowMapProps.InitalState	= D3D12_RESOURCE_STATE_COMMON;
	ShadowMapProps.MemoryType	= EMemoryType::MEMORY_TYPE_DEFAULT;
	ShadowMapProps.MipLevels	= 1;
	ShadowMapProps.SampleCount	= 1;

	D3D12_CLEAR_VALUE Value1;
	Value1.Format	= ShadowMapProps.Format;
	Value1.Color[0]	= 1.0f;
	Value1.Color[1]	= 1.0f;
	Value1.Color[2]	= 1.0f;
	Value1.Color[3]	= 1.0f;
	ShadowMapProps.OptimizedClearValue = &Value1;

	VSMDirLightShadowMaps = RenderingAPI::Get().CreateTexture(ShadowMapProps);
	if (VSMDirLightShadowMaps)
	{
		D3D12_RENDER_TARGET_VIEW_DESC RTVDesc = { };
		RTVDesc.Format					= ShadowMapProps.Format;
		RTVDesc.ViewDimension			= D3D12_RTV_DIMENSION_TEXTURE2D;
		RTVDesc.Texture2D.MipSlice		= 0;
		RTVDesc.Texture2D.PlaneSlice	= 0;
		VSMDirLightShadowMaps->SetRenderTargetView(TSharedPtr(RenderingAPI::Get().CreateRenderTargetView(VSMDirLightShadowMaps->GetResource(), &RTVDesc)), 0);
	
		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = { };
		SRVDesc.Format							= ShadowMapProps.Format;
		SRVDesc.ViewDimension					= D3D12_SRV_DIMENSION_TEXTURE2D;
		SRVDesc.Texture2D.MipLevels				= 1;
		SRVDesc.Texture2D.MostDetailedMip		= 0;
		SRVDesc.Texture2D.PlaneSlice			= 0;
		SRVDesc.Texture2D.ResourceMinLODClamp	= 0.0f;
		SRVDesc.Shader4ComponentMapping			= D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		VSMDirLightShadowMaps->SetShaderResourceView(TSharedPtr(RenderingAPI::Get().CreateShaderResourceView(VSMDirLightShadowMaps->GetResource(), &SRVDesc)), 0);
	}
	else
	{
		return false;
	}
#endif

	// PointLights
	if (PointLightShadowMaps)
	{
		DeferredResources.EmplaceBack(PointLightShadowMaps);
	}

	const UInt16 Size = Renderer::GetGlobalLightSettings().PointLightShadowSize;
	ShadowMapProps.DebugName			= "PointLight ShadowMaps";
	ShadowMapProps.ArrayCount			= 6;
	ShadowMapProps.Format				= ShadowMapFormat;
	ShadowMapProps.Flags				= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	ShadowMapProps.Height				= Size;
	ShadowMapProps.Width				= Size;
	ShadowMapProps.InitalState			= D3D12_RESOURCE_STATE_COMMON;
	ShadowMapProps.MemoryType			= EMemoryType::MEMORY_TYPE_DEFAULT;
	ShadowMapProps.MipLevels			= 1;
	ShadowMapProps.OptimizedClearValue	= &Value0;
	PointLightShadowMaps = RenderingAPI::Get().CreateTexture(ShadowMapProps);
	if (PointLightShadowMaps)
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC DSVDesc = { };
		DSVDesc.Format						= ShadowMapFormat;
		DSVDesc.ViewDimension				= D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
		DSVDesc.Texture2DArray.ArraySize	= 1;
		DSVDesc.Texture2DArray.MipSlice		= 0;
		for (UInt32 I = 0; I < 6; I++)
		{
			DSVDesc.Texture2DArray.FirstArraySlice = I;
			PointLightShadowMaps->SetDepthStencilView(TSharedPtr(RenderingAPI::Get().CreateDepthStencilView(PointLightShadowMaps->GetResource(), &DSVDesc)), I);
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = { };
		SRVDesc.Format							= DXGI_FORMAT_R32_FLOAT;
		SRVDesc.ViewDimension					= D3D12_SRV_DIMENSION_TEXTURECUBE;
		SRVDesc.Shader4ComponentMapping			= D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		SRVDesc.TextureCube.MipLevels			= 1;
		SRVDesc.TextureCube.MostDetailedMip		= 0;
		SRVDesc.TextureCube.ResourceMinLODClamp	= 0.0f;
		PointLightShadowMaps->SetShaderResourceView(TSharedPtr(RenderingAPI::Get().CreateShaderResourceView(PointLightShadowMaps->GetResource(), &SRVDesc)), 0);
	}
	else
	{
		return false;
	}

	return true;
}

void Renderer::WriteShadowMapDescriptors()
{
#if ENABLE_VSM
	LightDescriptorTable->SetShaderResourceView(VSMDirLightShadowMaps->GetShaderResourceView(0).Get(), 8);
	ForwardDescriptorTable->SetShaderResourceView(VSMDirLightShadowMaps->GetShaderResourceView(0).Get(), 6);
#else
	LightDescriptorTable->SetShaderResourceView(DirLightShadowMaps->GetShaderResourceView(0).Get(), 8);
	ForwardDescriptorTable->SetShaderResourceView(DirLightShadowMaps->GetShaderResourceView(0).Get(), 6);
#endif
	LightDescriptorTable->SetShaderResourceView(PointLightShadowMaps->GetShaderResourceView(0).Get(), 9);
	ForwardDescriptorTable->SetShaderResourceView(PointLightShadowMaps->GetShaderResourceView(0).Get(), 7);
	LightDescriptorTable->CopyDescriptors();
	ForwardDescriptorTable->CopyDescriptors();
}

void Renderer::GenerateIrradianceMap(D3D12Texture* Source, D3D12Texture* Dest, D3D12CommandList* InCommandList)
{
	const UInt32 Size = static_cast<UInt32>(Dest->GetDesc().Width);

	static TUniquePtr<D3D12DescriptorTable> SrvDescriptorTable;
	if (!SrvDescriptorTable)
	{
		SrvDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(1);
		SrvDescriptorTable->SetShaderResourceView(Source->GetShaderResourceView(0).Get(), 0);
		SrvDescriptorTable->CopyDescriptors();
	}

	static TUniquePtr<D3D12DescriptorTable> UavDescriptorTable;
	if (!UavDescriptorTable)
	{
		UavDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(1);
		UavDescriptorTable->SetUnorderedAccessView(Dest->GetUnorderedAccessView(0).Get(), 0);
		UavDescriptorTable->CopyDescriptors();
	}

	static Microsoft::WRL::ComPtr<IDxcBlob> Shader;
	if (!Shader)
	{
		Shader = D3D12ShaderCompiler::CompileFromFile("Shaders/IrradianceGen.hlsl", "Main", "cs_6_0");
	}

	if (!IrradianceGenRootSignature)
	{
		IrradianceGenRootSignature = RenderingAPI::Get().CreateRootSignature(Shader.Get());
		IrradianceGenRootSignature->SetDebugName("Irradiance Gen RootSignature");
	}

	if (!IrradicanceGenPSO)
	{
		ComputePipelineStateProperties Props = { };
		Props.DebugName		= "Irradiance Gen PSO";
		Props.CSBlob		= Shader.Get();
		Props.RootSignature	= IrradianceGenRootSignature.Get();

		IrradicanceGenPSO = RenderingAPI::Get().CreateComputePipelineState(Props);
	}

	InCommandList->TransitionBarrier(Source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	InCommandList->TransitionBarrier(Dest, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	InCommandList->SetComputeRootSignature(IrradianceGenRootSignature->GetRootSignature());

	InCommandList->BindGlobalOnlineDescriptorHeaps();
	InCommandList->SetComputeRootDescriptorTable(SrvDescriptorTable->GetGPUTableStartHandle(), 0);
	InCommandList->SetComputeRootDescriptorTable(UavDescriptorTable->GetGPUTableStartHandle(), 1);

	InCommandList->SetPipelineState(IrradicanceGenPSO->GetPipeline());

	InCommandList->Dispatch(Size, Size, 6);

	InCommandList->UnorderedAccessBarrier(Dest);

	InCommandList->TransitionBarrier(Source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	InCommandList->TransitionBarrier(Dest, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void Renderer::GenerateSpecularIrradianceMap(D3D12Texture* Source, D3D12Texture* Dest, D3D12CommandList* InCommandList)
{
	const UInt32 Miplevels = Dest->GetDesc().MipLevels;

	static TUniquePtr<D3D12DescriptorTable> SrvDescriptorTable;
	if (!SrvDescriptorTable)
	{
		SrvDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(1);
		SrvDescriptorTable->SetShaderResourceView(Source->GetShaderResourceView(0).Get(), 0);
		SrvDescriptorTable->CopyDescriptors();
	}

	static TUniquePtr<D3D12DescriptorTable> UavDescriptorTable;
	if (!UavDescriptorTable)
	{
		UavDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(Miplevels);
		for (UInt32 Mip = 0; Mip < Miplevels; Mip++)
		{
			UavDescriptorTable->SetUnorderedAccessView(Dest->GetUnorderedAccessView(Mip).Get(), Mip);
		}

		UavDescriptorTable->CopyDescriptors();
	}

	static Microsoft::WRL::ComPtr<IDxcBlob> Shader;
	if (!Shader)
	{
		Shader = D3D12ShaderCompiler::CompileFromFile("Shaders/SpecularIrradianceGen.hlsl", "Main", "cs_6_0");
	}

	if (!SpecIrradianceGenRootSignature)
	{
		SpecIrradianceGenRootSignature = RenderingAPI::Get().CreateRootSignature(Shader.Get());
		SpecIrradianceGenRootSignature->SetDebugName("Specular Irradiance Gen RootSignature");
	}

	if (!SpecIrradicanceGenPSO)
	{
		ComputePipelineStateProperties Props = { };
		Props.DebugName		= "Specular Irradiance Gen PSO";
		Props.CSBlob		= Shader.Get();
		Props.RootSignature = SpecIrradianceGenRootSignature.Get();

		SpecIrradicanceGenPSO = RenderingAPI::Get().CreateComputePipelineState(Props);
	}

	InCommandList->TransitionBarrier(Source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	InCommandList->TransitionBarrier(Dest, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	InCommandList->SetComputeRootSignature(SpecIrradianceGenRootSignature->GetRootSignature());

	InCommandList->BindGlobalOnlineDescriptorHeaps();
	InCommandList->SetComputeRootDescriptorTable(SrvDescriptorTable->GetGPUTableStartHandle(), 1);

	InCommandList->SetPipelineState(SpecIrradicanceGenPSO->GetPipeline());

	UInt32	Width		= static_cast<UInt32>(Dest->GetDesc().Width);
	Float Roughness	= 0.0f;
	const Float RoughnessDelta = 1.0f / (Miplevels - 1);
	for (UInt32 Mip = 0; Mip < Miplevels; Mip++)
	{
		InCommandList->SetComputeRoot32BitConstants(&Roughness, 1, 0, 0);
		InCommandList->SetComputeRootDescriptorTable(UavDescriptorTable->GetGPUTableHandle(Mip), 2);
		
		InCommandList->Dispatch(Width, Width, 6);
		InCommandList->UnorderedAccessBarrier(Dest);

		Width = std::max<UInt32>(Width / 2, 1U);
		Roughness += RoughnessDelta;
	}

	InCommandList->TransitionBarrier(Source, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	InCommandList->TransitionBarrier(Dest, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void Renderer::WaitForPendingFrames()
{
	//const UInt64 CurrentFenceValue = FenceValues[CurrentBackBufferIndex];

	//Queue->SignalFence(Fence.Get(), CurrentFenceValue);
	//if (Fence->WaitForValue(CurrentFenceValue))
	//{
	//	FenceValues[CurrentBackBufferIndex]++;
	//}

	RenderingAPI::Get().GetQueue()->WaitForCompletion();
}
