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
#include "D3D12/D3D12RayTracingPipelineState.h"
#include "D3D12/D3D12PipelineState.h"
#include "D3D12/D3D12ShaderCompiler.h"

#include "Application/Events/EventQueue.h"

#include <algorithm>

/*
* Static Settings
*/

TUniquePtr<Renderer>	Renderer::RendererInstance = nullptr;
LightSettings			Renderer::GlobalLightSettings;

static const EFormat SSAOBufferFormat		= EFormat::Format_R32G32B32A32_Float;
static const EFormat FinalTargetFormat		= EFormat::Format_R16G16B16A16_Float;
static const EFormat RenderTargetFormat		= EFormat::Format_R8G8B8A8_Unorm;
static const EFormat AlbedoFormat			= EFormat::Format_R8G8B8A8_Unorm;
static const EFormat MaterialFormat			= EFormat::Format_R8G8B8A8_Unorm;
static const EFormat NormalFormat			= EFormat::Format_R10G10B10A2_Unorm;
static const EFormat DepthBufferFormat		= EFormat::Format_D32_Float;
static const EFormat LightProbeFormat		= EFormat::Format_R16G16B16A16_Float;
static const EFormat ShadowMapFormat		= EFormat::Format_D32_Float;
static const UInt32	 ShadowMapSampleCount	= 2;

#define GBUFFER_ALBEDO_INDEX	0
#define GBUFFER_NORMAL_INDEX	1
#define GBUFFER_MATERIAL_INDEX	2
#define GBUFFER_DEPTH_INDEX		3

/*
* CameraBufferDesc
*/

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
};

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

	// Start frame
	// TODO: Fix this with viewport
	Texture2D* BackBuffer = nullptr;// RenderingAPI::Get().GetSwapChain()->GetSurfaceResource(CurrentBackBufferIndex);

	CmdList.Begin();
	// Build acceleration structures
	if (RenderingAPI::IsRayTracingSupported() && RayTracingEnabled)
	{
		// Build Bottom-Level
		UInt32 HitGroupIndex = 0;
		for (const MeshDrawCommand& Command : CurrentScene.GetMeshDrawCommands())
		{
			CmdList.BuildRayTracingGeometry(Command.Geometry);

			XMFLOAT4X4 Matrix		= Command.CurrentActor->GetTransform().GetMatrix();
			XMFLOAT3X4 SmallMatrix	= XMFLOAT3X4(reinterpret_cast<Float*>(&Matrix));

			//RayTracingGeometryInstances.EmplaceBack(
			//	Command.Mesh->RayTracingGeometry,
			//	Command.Material,
			//	SmallMatrix,
			//	HitGroupIndex, 0);

			HitGroupIndex++;
		}

		// Build Top-Level
		const bool NeedsBuild = false;// RayTracingScene->NeedsBuild();
		if (NeedsBuild)
		{
			//TArray<BindingTableEntry> BindingTableEntries;
			//BindingTableEntries.Reserve(RayTracingGeometryInstances.Size());

			//BindingTableEntries.EmplaceBack("RayGen", RayGenDescriptorTable, nullptr);
			//for (D3D12RayTracingGeometryInstance& Geometry : RayTracingGeometryInstances)
			//{
			//	BindingTableEntries.EmplaceBack(
			//		"HitGroup", 
			//		Geometry.Material->GetDescriptorTable(),
			//		Geometry.Geometry->GetDescriptorTable());
			//}
			//BindingTableEntries.EmplaceBack("Miss", nullptr, nullptr);

			//const UInt32 NumHitGroups = BindingTableEntries.Size() - 2;
			//RayTracingScene->BuildAccelerationStructure(
			//	CommandList.Get(),
			//	RayTracingGeometryInstances,
			//	BindingTableEntries,
			//	NumHitGroups);

			//GlobalDescriptorTable->SetShaderResourceView(RayTracingScene->GetShaderResourceView(), 0);
			//GlobalDescriptorTable->CopyDescriptors();
		}
	}

	// UpdateLightBuffers
	CmdList.TransitionBuffer(
		PointLightBuffer.Get(), 
		EResourceState::ResourceState_VertexAndConstantBuffer, 
		EResourceState::ResourceState_CopyDest);

	CmdList.TransitionBuffer(
		DirectionalLightBuffer.Get(), 
		EResourceState::ResourceState_VertexAndConstantBuffer, 
		EResourceState::ResourceState_CopyDest);

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
			CmdList.UpdateBuffer(PointLightBuffer.Get(), 0, NumPointLights * SizeInBytes, &Properties);

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
			CmdList.UpdateBuffer(DirectionalLightBuffer.Get(), 0, NumDirLights * SizeInBytes, &Properties);

			NumDirLights++;
		}
	}

	CmdList.TransitionBuffer(
		PointLightBuffer.Get(), 
		EResourceState::ResourceState_CopyDest, 
		EResourceState::ResourceState_VertexAndConstantBuffer);
	
	CmdList.TransitionBuffer(
		DirectionalLightBuffer.Get(), 
		EResourceState::ResourceState_CopyDest, 
		EResourceState::ResourceState_VertexAndConstantBuffer);

	// Transition GBuffer
	CmdList.TransitionTexture(
		GBuffer[GBUFFER_ALBEDO_INDEX].Get(), 
		EResourceState::ResourceState_PixelShaderResource, 
		EResourceState::ResourceState_RenderTarget);

	CmdList.TransitionTexture(
		GBuffer[GBUFFER_NORMAL_INDEX].Get(), 
		EResourceState::ResourceState_PixelShaderResource, 
		EResourceState::ResourceState_RenderTarget);

	CmdList.TransitionTexture(
		GBuffer[GBUFFER_MATERIAL_INDEX].Get(), 
		EResourceState::ResourceState_PixelShaderResource, 
		EResourceState::ResourceState_RenderTarget);

	CmdList.TransitionTexture(
		GBuffer[GBUFFER_DEPTH_INDEX].Get(), 
		EResourceState::ResourceState_PixelShaderResource, 
		EResourceState::ResourceState_DepthWrite);

	// Transition ShadowMaps
	CmdList.TransitionTexture(
		PointLightShadowMaps.Get(), 
		EResourceState::ResourceState_PixelShaderResource, 
		EResourceState::ResourceState_DepthWrite);
	
	CmdList.TransitionTexture(
		DirLightShadowMaps.Get(), 
		EResourceState::ResourceState_PixelShaderResource, 
		EResourceState::ResourceState_DepthWrite);
	
	// Render DirectionalLight ShadowMaps
	CmdList.ClearDepthStencilView(DirLightShadowMapDSV.Get(), DepthStencilClearValue(1.0f, 0));

#if ENABLE_VSM
	CmdList.TransitionTexture(VSMDirLightShadowMaps.Get(), EResourceState::ResourceState_PixelShaderResource, EResourceState::ResourceState_RenderTarget);

	//Float32 DepthClearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	//CmdList.ClearRenderTargetView(VSMDirLightShadowMaps->GetRenderTargetView(0).Get(), DepthClearColor);
	//
	//D3D12RenderTargetView* DirLightRTVS[] =
	//{
	//	VSMDirLightShadowMaps->GetRenderTargetView(0).Get(),
	//};
	//CmdList.BindRenderTargets(DirLightRTVS, 1, DirLightShadowMaps->GetDepthStencilView(0).Get());
	//CmdList.BindGraphicsPipelineState(VSMShadowMapPSO.Get());
#else
	CmdList.BindRenderTargets(nullptr, 0, DirLightShadowMapDSV.Get());
	CmdList.BindGraphicsPipelineState(ShadowMapPSO.Get());
#endif

	// Setup view
	Viewport ViewPort = { };
	ViewPort.Width		= static_cast<Float>(Renderer::GetGlobalLightSettings().ShadowMapWidth);
	ViewPort.Height		= static_cast<Float>(Renderer::GetGlobalLightSettings().ShadowMapHeight);
	ViewPort.MinDepth	= 0.0f;
	ViewPort.MaxDepth	= 1.0f;
	ViewPort.x			= 0.0f;
	ViewPort.y			= 0.0f;
	CmdList.BindViewport(ViewPort, 1);

	ScissorRect ScissorRect;
	ScissorRect.x		= 0;
	ScissorRect.y		= 0;
	ScissorRect.Width	= Renderer::GetGlobalLightSettings().ShadowMapWidth;
	ScissorRect.Height	= Renderer::GetGlobalLightSettings().ShadowMapHeight;

	CmdList.BindScissorRect(ScissorRect, 1);
	CmdList.BindPrimitiveTopology(EPrimitiveTopology::PrimitiveTopology_TriangleList);

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

	for (Light* Light : CurrentScene.GetLights())
	{
		if (IsSubClassOf<DirectionalLight>(Light))
		{
			DirectionalLight* DirLight = Cast<DirectionalLight>(Light);
			PerLightBuffer.Matrix	= DirLight->GetMatrix();
			PerLightBuffer.Position	= DirLight->GetShadowMapPosition();
			PerLightBuffer.FarPlane	= DirLight->GetShadowFarPlane();
			
			// TODO: Solve this
			// CommandList->SetGraphicsRoot32BitConstants(&PerLightBuffer, 20, 0, 1);

			// Draw all objects to depthbuffer
			for (const MeshDrawCommand& Command : CurrentScene.GetMeshDrawCommands())
			{
				CmdList.BindVertexBuffers(&Command.VertexBuffer, 1, 0);
				CmdList.BindIndexBuffer(Command.IndexBuffer);

				// TODO: Solve this
				// CommandList->SetGraphicsRoot32BitConstants(&ShadowPerObjectBuffer, 16, 0, 0);
				ShadowPerObjectBuffer.Matrix		= Command.CurrentActor->GetTransform().GetMatrix();
				ShadowPerObjectBuffer.ShadowOffset	= Command.Mesh->ShadowOffset;

				CmdList.DrawIndexedInstanced(Command.IndexCount, 1, 0, 0, 0);
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
	ViewPort.x			= 0.0f;
	ViewPort.y			= 0.0f;
	CmdList.BindViewport(ViewPort, 1);

	ScissorRect.x		= 0;
	ScissorRect.y		= 0;
	ScissorRect.Width	= PointLightShadowSize;
	ScissorRect.Height	= PointLightShadowSize;
	CmdList.BindScissorRect(ScissorRect, 1);

	CmdList.BindGraphicsPipelineState(LinearShadowMapPSO.Get());

	for (Light* Light : CurrentScene.GetLights())
	{
		if (IsSubClassOf<PointLight>(Light))
		{
			PointLight* PoiLight = Cast<PointLight>(Light);
			for (UInt32 i = 0; i < 6; i++)
			{
				CmdList.ClearDepthStencilView(PointLightShadowMapsDSVs[i].Get(), DepthStencilClearValue(1.0f, 0));
				CmdList.BindRenderTargets(nullptr, 0, PointLightShadowMapsDSVs[i].Get());

				PerLightBuffer.Matrix	= PoiLight->GetMatrix(i);
				PerLightBuffer.Position	= PoiLight->GetPosition();
				PerLightBuffer.FarPlane	= PoiLight->GetShadowFarPlane();
				// TODO: Solve this
				// CommandList->SetGraphicsRoot32BitConstants(&PerLightBuffer, 20, 0, 1);

				// Draw all objects to depthbuffer
				if (FrustumCullEnabled)
				{
					Frustum CameraFrustum = Frustum(PoiLight->GetShadowFarPlane(), PoiLight->GetViewMatrix(i), PoiLight->GetProjectionMatrix(i));
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
							CmdList.BindVertexBuffers(&Command.VertexBuffer, 1, 0);
							CmdList.BindIndexBuffer(Command.IndexBuffer);

							// TODO: Solve this
							ShadowPerObjectBuffer.Matrix		= Command.CurrentActor->GetTransform().GetMatrix();
							ShadowPerObjectBuffer.ShadowOffset	= Command.Mesh->ShadowOffset;
							// CommandList->SetGraphicsRoot32BitConstants(&PerObjectBuffer, 16, 0, 0);

							CmdList.DrawIndexedInstanced(Command.IndexCount, 1, 0, 0, 0);
						}
					}
				}
				else
				{
					for (const MeshDrawCommand& Command : CurrentScene.GetMeshDrawCommands())
					{
						CmdList.BindVertexBuffers(&Command.VertexBuffer, 1, 0);
						CmdList.BindIndexBuffer(Command.IndexBuffer);

						// TODO: Solve this
						ShadowPerObjectBuffer.Matrix		= Command.CurrentActor->GetTransform().GetMatrix();
						ShadowPerObjectBuffer.ShadowOffset	= Command.Mesh->ShadowOffset;
						// CommandList->SetGraphicsRoot32BitConstants(&PerObjectBuffer, 16, 0, 0);

						CmdList.DrawIndexedInstanced(Command.IndexCount, 1, 0, 0, 0);
					}
				}
			}

			break;
		}
	}

	// Transition ShadowMaps
#if ENABLE_VSM
	CmdList.TransitionTexture(
		VSMDirLightShadowMaps.Get(), 
		EResourceState::ResourceState_RenderTarget, 
		EResourceState::ResourceState_PixelShaderResource);
#endif
	CmdList.TransitionTexture(
		DirLightShadowMaps.Get(), 
		EResourceState::ResourceState_DepthWrite, 
		EResourceState::ResourceState_PixelShaderResource);
	
	CmdList.TransitionTexture(
		PointLightShadowMaps.Get(), 
		EResourceState::ResourceState_DepthWrite, 
		EResourceState::ResourceState_PixelShaderResource);

	// Update camerabuffer
	CameraBufferDesc CamBuff;
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

	CmdList.TransitionBuffer(
		CameraBuffer.Get(), 
		EResourceState::ResourceState_VertexAndConstantBuffer, 
		EResourceState::ResourceState_CopyDest);

	CmdList.UpdateBuffer(CameraBuffer.Get(), 0, sizeof(CameraBufferDesc), &CamBuff);
	
	CmdList.TransitionBuffer(
		CameraBuffer.Get(), 
		EResourceState::ResourceState_CopyDest, 
		EResourceState::ResourceState_VertexAndConstantBuffer);

	// Clear GBuffer
	ColorClearValue BlackClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	CmdList.ClearRenderTargetView(GBufferRTVs[GBUFFER_ALBEDO_INDEX].Get(), BlackClearColor);
	CmdList.ClearRenderTargetView(GBufferRTVs[GBUFFER_NORMAL_INDEX].Get(), BlackClearColor);
	CmdList.ClearRenderTargetView(GBufferRTVs[GBUFFER_MATERIAL_INDEX].Get(), BlackClearColor);
	CmdList.ClearDepthStencilView(GBufferDSV.Get(), DepthStencilClearValue(1.0f, 0));

	// Setup view
	
	// TODO: Solve this
	// ViewPort.Width		= static_cast<Float32>(RenderingAPI::Get().GetSwapChain()->GetWidth());
	// ViewPort.Height		= static_cast<Float32>(RenderingAPI::Get().GetSwapChain()->GetHeight());
	ViewPort.MinDepth	= 0.0f;
	ViewPort.MaxDepth	= 1.0f;
	ViewPort.x			= 0.0f;
	ViewPort.y			= 0.0f;
	CmdList.BindViewport(ViewPort, 0);

	ScissorRect.x = 0;
	ScissorRect.y = 0;
	// TODO: Solve this
	// static_cast<LONG>(RenderingAPI::Get().GetSwapChain()->GetWidth()),
	// static_cast<LONG>(RenderingAPI::Get().GetSwapChain()->GetHeight());
	CmdList.BindScissorRect(ScissorRect, 0);

	// Perform PrePass
	if (PrePassEnabled)
	{
		struct PerObject
		{
			XMFLOAT4X4 Matrix;
		} PerObjectBuffer;

		// Setup Pipeline
		CmdList.BindRenderTargets(nullptr, 0, GBufferDSV.Get());

		CmdList.BindGraphicsPipelineState(PrePassPSO.Get());
		// CommandList->SetGraphicsRootSignature(PrePassRootSignature->GetRootSignature());
		// CommandList->SetGraphicsRootDescriptorTable(PrePassDescriptorTable->GetGPUTableStartHandle(), 1);

		// Draw all objects to depthbuffer
		for (const MeshDrawCommand& Command : DeferredVisibleCommands)
		{
			CmdList.BindVertexBuffers(&Command.VertexBuffer, 1, 0);
			CmdList.BindIndexBuffer(Command.IndexBuffer);

			PerObjectBuffer.Matrix = Command.CurrentActor->GetTransform().GetMatrix();
			// CommandList->SetGraphicsRoot32BitConstants(&PerObjectBuffer, 16, 0, 0);

			CmdList.DrawIndexedInstanced(Command.IndexCount, 1, 0, 0, 0);
		}
	}

	// Render all objects to the GBuffer
	RenderTargetView* RenderTargets[] =
	{
		GBufferRTVs[GBUFFER_ALBEDO_INDEX].Get(),
		GBufferRTVs[GBUFFER_NORMAL_INDEX].Get(),
		GBufferRTVs[GBUFFER_MATERIAL_INDEX].Get()
	};
	CmdList.BindRenderTargets(RenderTargets, 3, GBufferDSV.Get());

	// Setup Pipeline
	CmdList.BindGraphicsPipelineState(GeometryPSO.Get());
	// CommandList->SetGraphicsRootSignature(GeometryRootSignature->GetRootSignature());
	// CommandList->SetGraphicsRootDescriptorTable(GeometryDescriptorTable->GetGPUTableStartHandle(), 1);

	struct TransformBuffer
	{
		XMFLOAT4X4 Transform;
		XMFLOAT4X4 TransformInv;
	} TransformPerObject;

	for (const MeshDrawCommand& Command : DeferredVisibleCommands)
	{
		CmdList.BindVertexBuffers(&Command.VertexBuffer, 1, 0);
		CmdList.BindIndexBuffer(Command.IndexBuffer);

		if (Command.Material->IsBufferDirty())
		{
			Command.Material->BuildBuffer(CmdList);
		}
		// CommandList->SetGraphicsRootDescriptorTable(Command.Material->GetDescriptorTable()->GetGPUTableStartHandle(), 2);

		TransformPerObject.Transform	= Command.CurrentActor->GetTransform().GetMatrix();
		TransformPerObject.TransformInv	= Command.CurrentActor->GetTransform().GetMatrixInverse();
		//CommandList->SetGraphicsRoot32BitConstants(&TransformPerObject, 32, 0, 0);

		CmdList.DrawIndexedInstanced(Command.IndexCount, 1, 0, 0, 0);
	}

	// Setup GBuffer for Read
	CmdList.TransitionTexture(
		GBuffer[GBUFFER_ALBEDO_INDEX].Get(), 
		EResourceState::ResourceState_RenderTarget, 
		EResourceState::ResourceState_PixelShaderResource);
	
	CmdList.TransitionTexture(
		GBuffer[GBUFFER_NORMAL_INDEX].Get(), 
		EResourceState::ResourceState_RenderTarget, 
		EResourceState::ResourceState_PixelShaderResource);

	CmdList.TransitionTexture(
		GBuffer[GBUFFER_MATERIAL_INDEX].Get(), 
		EResourceState::ResourceState_RenderTarget, 
		EResourceState::ResourceState_PixelShaderResource);

	CmdList.TransitionTexture(
		GBuffer[GBUFFER_DEPTH_INDEX].Get(), 
		EResourceState::ResourceState_DepthWrite, 
		EResourceState::ResourceState_PixelShaderResource);

	// RayTracing
	if (RenderingAPI::IsRayTracingSupported() && RayTracingEnabled)
	{
		TraceRays(BackBuffer, CmdList);
		RayTracingGeometryInstances.Clear();
	}

	// SSAO
	CmdList.TransitionTexture(
	SSAOBuffer.Get(), 
	EResourceState::ResourceState_PixelShaderResource, 
	EResourceState::ResourceState_UnorderedAccess);

	const Float WhiteColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	//CmdList.ClearUnorderedAccessView(, WhiteColor);

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

		const UInt32 Width	= 800;// RenderingAPI::Get().GetSwapChain()->GetWidth();
		const UInt32 Height	= 600;// RenderingAPI::Get().GetSwapChain()->GetHeight();
		SSAOSettings.ScreenSize	= XMFLOAT2(Float(Width), Float(Height));
		SSAOSettings.NoiseSize	= XMFLOAT2(4.0f, 4.0f);
		SSAOSettings.Radius		= SSAORadius;
		SSAOSettings.KernelSize = SSAOKernelSize;
		SSAOSettings.Bias		= SSAOBias;

		//CommandList->SetComputeRootSignature(SSAORootSignature->GetRootSignature());
		//CommandList->SetComputeRootDescriptorTable(SSAODescriptorTable->GetGPUTableStartHandle(), 0);
		//CommandList->SetComputeRoot32BitConstants(&SSAOSettings, 7, 0, 1);

		CmdList.BindComputePipelineState(SSAOPSO.Get());

		constexpr UInt32 ThreadCount = 32;
		const UInt32 DispatchWidth	= Math::AlignUp<UInt32>(Width, ThreadCount) / ThreadCount;
		const UInt32 DispatchHeight = Math::AlignUp<UInt32>(Height, ThreadCount) / ThreadCount;
		CmdList.Dispatch(DispatchWidth, DispatchHeight, 1);

		CmdList.UnorderedAccessTextureBarrier(SSAOBuffer.Get());

		//CommandList->SetComputeRootSignature(BlurRootSignature->GetRootSignature());
		//CommandList->SetComputeRootDescriptorTable(SSAOBlurDescriptorTable->GetGPUTableStartHandle(), 0);
		//CommandList->SetComputeRoot32BitConstants(&SSAOSettings.ScreenSize, 2, 0, 1);

		CmdList.BindComputePipelineState(SSAOBlur.Get());
		CmdList.Dispatch(DispatchWidth, DispatchHeight, 1);

		CmdList.UnorderedAccessTextureBarrier(SSAOBuffer.Get());
	}

	CmdList.TransitionTexture(
		SSAOBuffer.Get(),
		EResourceState::ResourceState_UnorderedAccess,
		EResourceState::ResourceState_PixelShaderResource);

	// Render to final output
	CmdList.TransitionTexture(
		FinalTarget.Get(), 
		EResourceState::ResourceState_PixelShaderResource, 
		EResourceState::ResourceState_RenderTarget);
	
	//CmdList.TransitionTexture(
	//	BackBuffer, 
	//	EResourceState::ResourceState_Present, 
	//	EResourceState::ResourceState_RenderTarget);

	RenderTargetView* RenderTarget[] = { FinalTargetRTV.Get() };
	CmdList.BindRenderTargets(RenderTarget, 1, nullptr);

	CmdList.BindViewport(ViewPort, 0);
	CmdList.BindScissorRect(ScissorRect, 0);

	// Setup LightPass
	CmdList.BindPrimitiveTopology(EPrimitiveTopology::PrimitiveTopology_TriangleList);

	CmdList.BindGraphicsPipelineState(LightPassPSO.Get());
	// CommandList->SetGraphicsRootSignature(LightRootSignature->GetRootSignature());
	// CommandList->SetGraphicsRootDescriptorTable(LightDescriptorTable->GetGPUTableStartHandle(), 0);

	// Perform LightPass
	CmdList.DrawInstanced(3, 1, 0, 0);

	// Draw skybox
	CmdList.TransitionTexture(
		GBuffer[GBUFFER_DEPTH_INDEX].Get(), 
		EResourceState::ResourceState_PixelShaderResource, 
		EResourceState::ResourceState_DepthWrite);
	
	CmdList.BindRenderTargets(RenderTarget, 1, GBufferDSV.Get());
	
	CmdList.BindPrimitiveTopology(EPrimitiveTopology::PrimitiveTopology_TriangleList);
	CmdList.BindVertexBuffers(&SkyboxVertexBuffer, 1, 0);
	CmdList.BindIndexBuffer(SkyboxIndexBuffer.Get());
	CmdList.BindGraphicsPipelineState(SkyboxPSO.Get());
	// CommandList->SetGraphicsRootSignature(SkyboxRootSignature->GetRootSignature());
	
	struct SimpleCameraBuffer
	{
		XMFLOAT4X4 Matrix;
	} SimpleCamera;
	SimpleCamera.Matrix = CurrentScene.GetCamera()->GetViewProjectionWitoutTranslateMatrix();
	// CommandList->SetGraphicsRoot32BitConstants(&SimpleCamera, 16, 0, 0);
	// CommandList->SetGraphicsRootDescriptorTable(SkyboxDescriptorTable->GetGPUTableStartHandle(), 1);

	CmdList.DrawIndexedInstanced(static_cast<UInt32>(SkyboxMesh.Indices.Size()), 1, 0, 0, 0);

	// Render to BackBuffer
	CmdList.TransitionTexture(
		FinalTarget.Get(), 
		EResourceState::ResourceState_RenderTarget, 
		EResourceState::ResourceState_PixelShaderResource);
	
	// TODO: Solve this
	//RenderTarget[0] = BackBuffer->GetRenderTargetView(0).Get();
	//CmdList.BindRenderTargets(RenderTarget, 1, nullptr);

	CmdList.BindVertexBuffers(nullptr, 0, 0);
	CmdList.BindIndexBuffer(nullptr);

	// CommandList->SetGraphicsRootSignature(PostRootSignature->GetRootSignature());
	// CommandList->SetGraphicsRootDescriptorTable(PostDescriptorTable->GetGPUTableStartHandle(), 0);
	
	if (FXAAEnabled)
	{
		struct FXAASettings
		{
			Float Width;
			Float Height;
		} Settings;

		Settings.Width	= 800; //Float(RenderingAPI::Get().GetSwapChain()->GetWidth());
		Settings.Height	= 600;// Float(RenderingAPI::Get().GetSwapChain()->GetHeight());

		// CommandList->SetGraphicsRoot32BitConstants(&Settings, 2, 0, 1);
		CmdList.BindGraphicsPipelineState(FXAAPSO.Get());
	}
	else
	{
		CmdList.BindGraphicsPipelineState(PostPSO.Get());
	}

	CmdList.DrawInstanced(3, 1, 0, 0);

	// Forward Pass
	ViewPort.Width		= 800;// static_cast<Float>(RenderingAPI::Get().GetSwapChain()->GetWidth());
	ViewPort.Height		= 600;// static_cast<Float>(RenderingAPI::Get().GetSwapChain()->GetHeight());
	ViewPort.MinDepth	= 0.0f;
	ViewPort.MaxDepth	= 1.0f;
	ViewPort.x			= 0.0f;
	ViewPort.y			= 0.0f;
	CmdList.BindViewport(ViewPort, 1);

	ScissorRect =
	{
		0,
		0,
		800,// static_cast<Float>(RenderingAPI::Get().GetSwapChain()->GetWidth());
		600,// static_cast<Float>(RenderingAPI::Get().GetSwapChain()->GetHeight());
	};
	CmdList.BindScissorRect(ScissorRect, 1);

	// Render all transparent objects
	//RenderTarget[0] = BackBuffer->GetRenderTargetView(0).Get();
	//CmdList.BindRenderTargets(RenderTarget, 1, GBuffer[GBUFFER_DEPTH_INDEX]->GetDepthStencilView(0).Get());

	// Setup Pipeline
	//CommandList->SetGraphicsRootSignature(ForwardRootSignature->GetRootSignature());
	//CommandList->SetGraphicsRootDescriptorTable(ForwardDescriptorTable->GetGPUTableStartHandle(), 1);

	CmdList.BindGraphicsPipelineState(ForwardPSO.Get());
	for (const MeshDrawCommand& Command : ForwardVisibleCommands)
	{
		//VBO.BufferLocation	= Command.VertexBuffer->GetGPUVirtualAddress();
		//VBO.SizeInBytes		= Command.VertexBuffer->GetSizeInBytes();
		//VBO.StrideInBytes	= sizeof(Vertex);
		CmdList.BindVertexBuffers(&Command.VertexBuffer, 1, 0);

		//IBV.BufferLocation	= Command.IndexBuffer->GetGPUVirtualAddress();
		//IBV.SizeInBytes		= Command.IndexBuffer->GetSizeInBytes();
		//IBV.Format			= DXGI_FORMAT_R32_UINT;
		CmdList.BindIndexBuffer(Command.IndexBuffer);

		if (Command.Material->IsBufferDirty())
		{
			Command.Material->BuildBuffer(CmdList);
		}
		//CommandList->SetGraphicsRootDescriptorTable(Command.Material->GetDescriptorTable()->GetGPUTableStartHandle(), 2);

		TransformPerObject.Transform	= Command.CurrentActor->GetTransform().GetMatrix();
		TransformPerObject.TransformInv	= Command.CurrentActor->GetTransform().GetMatrixInverse();
		//CommandList->SetGraphicsRoot32BitConstants(&TransformPerObject, 32, 0, 0);

		CmdList.DrawIndexedInstanced(Command.IndexCount, 1, 0, 0, 0);
	}

	// Draw DebugBoxes
	if (DrawAABBs)
	{
		CmdList.BindGraphicsPipelineState(DebugBoxPSO.Get());
		// CommandList->SetGraphicsRootSignature(DebugRootSignature->GetRootSignature());
		CmdList.BindPrimitiveTopology(EPrimitiveTopology::PrimitiveTopology_LineList);

		SimpleCamera.Matrix = CurrentScene.GetCamera()->GetViewProjectionMatrix();
		// CommandList->SetGraphicsRoot32BitConstants(&SimpleCamera, 16, 0, 1);

		CmdList.BindVertexBuffers(&AABBVertexBuffer, 1, 0);
		CmdList.BindIndexBuffer(AABBIndexBuffer.Get());

		for (const MeshDrawCommand& Command : DeferredVisibleCommands)
		{
			AABB& Box = Command.Mesh->BoundingBox;
			XMFLOAT3 Scale		= XMFLOAT3(Box.GetWidth(), Box.GetHeight(), Box.GetDepth());
			XMFLOAT3 Position	= Box.GetCenter();

			XMMATRIX XmTranslation	= XMMatrixTranslation(Position.x, Position.y, Position.z);
			XMMATRIX XmScale		= XMMatrixScaling(Scale.x, Scale.y, Scale.z);

			XMFLOAT4X4 Transform = Command.CurrentActor->GetTransform().GetMatrix();
			XMMATRIX XmTransform = XMMatrixTranspose(XMLoadFloat4x4(&Transform));
			XMStoreFloat4x4(&Transform, XMMatrixMultiplyTranspose(XMMatrixMultiply(XmScale, XmTranslation), XmTransform));

			// CommandList->SetGraphicsRoot32BitConstants(&Transform, 16, 0, 0);
			CmdList.DrawIndexedInstanced(24, 1, 0, 0, 0);
		}
	}

	// Render UI
	//DebugUI::DrawDebugString("DrawCall Count: " + std::to_string(CommandList->GetNumDrawCalls()));
	DebugUI::Render(CmdList);

	// Finalize Commandlist
	CmdList.TransitionTexture(
		GBuffer[GBUFFER_DEPTH_INDEX].Get(), 
		EResourceState::ResourceState_DepthWrite, 
		EResourceState::ResourceState_PixelShaderResource);

	//CmdList.TransitionTexture(BackBuffer, EResourceState::ResourceState_RenderTarget, EResourceState::ResourceState_Present);
	
	CmdList.End();
	CommandListExecutor::ExecuteCommandList(CmdList);

	// Present
	
	// TODO: Fix this
	// RenderingAPI::Get().GetSwapChain()->Present(VSyncEnabled ? 1 : 0);

	// Wait for next frame
	//const Uint64 CurrentFenceValue = FenceValues[CurrentBackBufferIndex];
	//RenderingAPI::Get().GetQueue()->SignalFence(Fence.Get(), CurrentFenceValue);

	// CurrentBackBufferIndex = RenderingAPI::Get().GetSwapChain()->GetCurrentBackBufferIndex();
	// if (Fence->WaitForValue(CurrentFenceValue))
	{
		//FenceValues[CurrentBackBufferIndex] = CurrentFenceValue + 1;
	}
}

void Renderer::TraceRays(Texture2D* BackBuffer, CommandList& InCmdList)
{
	InCmdList.TransitionTexture(
		ReflectionTexture.Get(), 
		EResourceState::ResourceState_CopySource, 
		EResourceState::ResourceState_UnorderedAccess);

	UNREFERENCED_VARIABLE(BackBuffer);

	//D3D12_DISPATCH_RAYS_DESC raytraceDesc = {};
	//raytraceDesc.Width	= static_cast<UInt32>(ReflectionTexture->GetDesc().Width);
	//raytraceDesc.Height = static_cast<UInt32>(ReflectionTexture->GetDesc().Height);
	//raytraceDesc.Depth	= 1;

	// Set shader tables
	InCmdList.BindRayTracingScene(RayTracingScene.Get());

	// Bind the empty root signature
	// InCommandList->SetComputeRootSignature(GlobalRootSignature->GetRootSignature());
	// InCommandList->SetComputeRootDescriptorTable(GlobalDescriptorTable->GetGPUTableStartHandle(), 0);

	// Dispatch
	InCmdList.BindRayTracingPipelineState(RaytracingPSO.Get());

	// TODO: Input width and height
	InCmdList.DispatchRays(0, 0, 1);

	// Copy the results to the back-buffer
	InCmdList.TransitionTexture(
		ReflectionTexture.Get(), 
		EResourceState::ResourceState_UnorderedAccess, 
		EResourceState::ResourceState_CopySource);
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

	//RenderingAPI::Get().GetSwapChain()->Resize(Width, Height);

	// Update deferred
	InitGBuffer();

	//if (RenderingAPI::Get().IsRayTracingSupported())
	{
		//RayGenDescriptorTable->SetUnorderedAccessView(ReflectionTexture->GetUnorderedAccessView(0).Get(), 0);
		//RayGenDescriptorTable->CopyDescriptors();
		//
		//GlobalDescriptorTable->SetShaderResourceView(GBuffer[1]->GetShaderResourceView(0).Get(), 5);
		//GlobalDescriptorTable->SetShaderResourceView(GBuffer[3]->GetShaderResourceView(0).Get(), 6);
		//GlobalDescriptorTable->CopyDescriptors();
	}

	//LightDescriptorTable->SetShaderResourceView(GBuffer[0]->GetShaderResourceView(0).Get(), 0);
	//LightDescriptorTable->SetShaderResourceView(GBuffer[1]->GetShaderResourceView(0).Get(), 1);
	//LightDescriptorTable->SetShaderResourceView(GBuffer[2]->GetShaderResourceView(0).Get(), 2);
	//LightDescriptorTable->SetShaderResourceView(GBuffer[3]->GetShaderResourceView(0).Get(), 3);
	//LightDescriptorTable->CopyDescriptors();
	//
	//CurrentBackBufferIndex = RenderingAPI::Get().GetSwapChain()->GetCurrentBackBufferIndex();
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

		CommandList& CmdList = CurrentRenderer->CmdList;
#if ENABLE_VSM
		CmdList.TransitionTexture(
			CurrentRenderer->VSMDirLightShadowMaps.Get(), 
			EResourceState::ResourceState_Common, 
			EResourceState::ResourceState_PixelShaderResource);
#endif
		CmdList.TransitionTexture(
			CurrentRenderer->DirLightShadowMaps.Get(), 
			EResourceState::ResourceState_Common, 
			EResourceState::ResourceState_PixelShaderResource);
		
		CmdList.TransitionTexture(
			CurrentRenderer->PointLightShadowMaps.Get(),
			EResourceState::ResourceState_Common, 
			EResourceState::ResourceState_PixelShaderResource);

		CommandListExecutor::ExecuteCommandList(CmdList);
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
	// Create mesh
	SkyboxMesh	= MeshFactory::CreateSphere(1);

	// Create camera
	CameraBuffer = RenderingAPI::CreateConstantBuffer<CameraBufferDesc>(nullptr, BufferUsage_Default);
	if (!CameraBuffer)
	{
		LOG_ERROR("[Renderer]: Failed to create camerabuffer");
		return false;
	}
	else
	{
		CameraBuffer->SetName("CameraBuffer");
	}

	// Create VertexBuffers
	{
		ResourceData VertexData = ResourceData(SkyboxMesh.Vertices.Data());
		SkyboxVertexBuffer = RenderingAPI::CreateVertexBuffer<Vertex>(
			&VertexData, 
			SkyboxMesh.Vertices.Size(), 
			BufferUsage_Dynamic);
		if (!SkyboxVertexBuffer)
		{
			return false;
		}
		else
		{
			SkyboxVertexBuffer->SetName("SkyboxVertexBuffer");
		}
	}

	// Create indexbuffers
	{
		ResourceData IndexData = ResourceData(SkyboxMesh.Indices.Data());

		const UInt32 SizeInBytes = sizeof(UInt32) * static_cast<UInt64>(SkyboxMesh.Indices.Size());
		SkyboxIndexBuffer = RenderingAPI::CreateIndexBuffer(
			&IndexData, 
			SizeInBytes, 
			EIndexFormat::IndexFormat_UInt32, 
			BufferUsage_Dynamic);
		if (!SkyboxIndexBuffer)
		{
			return false;
		}
		else
		{
			SkyboxIndexBuffer->SetName("SkyboxIndexBuffer");
		}
	}

	// Create Texture Cube
	TSharedRef<Texture2D> Panorama = TextureFactory::LoadFromFile("../Assets/Textures/arches.hdr", 0, EFormat::Format_R32G32B32A32_Float);
	if (!Panorama)
	{
		return false;	
	}

	Skybox = TextureFactory::CreateTextureCubeFromPanorma(
		Panorama.Get(), 
		1024, 
		TextureFactoryFlag_GenerateMips, 
		EFormat::Format_R16G16B16A16_Float);
	if (!Skybox)
	{
		return false;
	}
	else
	{
		Skybox->SetName("Skybox");
	}

	// Generate global irradiance (From Skybox)
	const UInt16 IrradianceSize = 32;
	IrradianceMap = RenderingAPI::CreateTextureCube(
		nullptr, 
		EFormat::Format_R16G16B16A16_Float, 
		TextureUsage_Default | TextureUsage_RWTexture,
		IrradianceSize, 
		1, 
		1);
	if (IrradianceMap)
	{
		IrradianceMap->SetName("Irradiance Map");
	}
	else
	{
		return false;
	}

	IrradianceMapUAV = RenderingAPI::CreateUnorderedAccessView(
		IrradianceMap.Get(), 
		EFormat::Format_R16G16B16A16_Float, 0);
	if (!IrradianceMapUAV)
	{
		return false;
	}

	IrradianceMapSRV = RenderingAPI::CreateShaderResourceView(
		IrradianceMap.Get(), 
		EFormat::Format_R16G16B16A16_Float, 
		0, 1);
	if (!IrradianceMapSRV)
	{
		return false;
	}

	// Generate global specular irradiance (From Skybox)
	const UInt16 SpecularIrradianceSize			= 128;
	const UInt16 SpecularIrradianceMiplevels	= std::max<UInt32>(std::log2<UInt32>(SpecularIrradianceSize), 1U);
	SpecularIrradianceMap = RenderingAPI::CreateTextureCube(
		nullptr, 
		EFormat::Format_R16G16B16A16_Float, 
		TextureUsage_Default | TextureUsage_RWTexture, 
		SpecularIrradianceSize, 
		SpecularIrradianceMiplevels, 
		1);
	if (SpecularIrradianceMap)
	{
		SpecularIrradianceMap->SetName("Specular Irradiance Map");
	}
	else
	{
		Debug::DebugBreak();
		return false;
	}

	for (UInt32 MipLevel = 0; MipLevel < SpecularIrradianceMiplevels; MipLevel++)
	{
		TSharedRef<UnorderedAccessView> Uav = RenderingAPI::CreateUnorderedAccessView(
			SpecularIrradianceMap.Get(), 
			EFormat::Format_R16G16B16A16_Float, 
			MipLevel);
		if (Uav)
		{
			SpecularIrradianceMapUAVs.EmplaceBack(Uav);
		}
	}

	SpecularIrradianceMapSRV = RenderingAPI::CreateShaderResourceView(
		SpecularIrradianceMap.Get(), 
		EFormat::Format_R16G16B16A16_Float, 
		0, 
		SpecularIrradianceMiplevels);
	if (!SpecularIrradianceMapSRV)
	{
		return false;
	}

	GenerateIrradianceMap(Skybox.Get(), IrradianceMap.Get(), CmdList);
	GenerateSpecularIrradianceMap(Skybox.Get(), SpecularIrradianceMap.Get(), CmdList);

	// Init standard inputlayout
	InputLayoutStateCreateInfo InputLayout =
	{
		{ "POSITION",	0, EFormat::Format_R32G32B32_Float,	0, 0,	EInputClassification::InputClassification_Vertex, 0 },
		{ "NORMAL",		0, EFormat::Format_R32G32B32_Float,	0, 12,	EInputClassification::InputClassification_Vertex, 0 },
		{ "TANGENT",	0, EFormat::Format_R32G32B32_Float,	0, 24,	EInputClassification::InputClassification_Vertex, 0 },
		{ "TEXCOORD",	0, EFormat::Format_R32G32_Float,	0, 36,	EInputClassification::InputClassification_Vertex, 0 },
	};

	StdInputLayout = RenderingAPI::CreateInputLayout(InputLayout);
	if (!StdInputLayout)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		StdInputLayout->SetName("Standard InputLayoutState");
	}

	// Init Deferred Rendering
	if (!InitLightBuffers())
	{
		return false;
	}

	if (!InitShadowMapPass())
	{
		return false;
	}

	CmdList.Begin();
	
	CmdList.TransitionBuffer(
		PointLightBuffer.Get(), 
		EResourceState::ResourceState_Common, 
		EResourceState::ResourceState_VertexAndConstantBuffer);
	
	CmdList.TransitionBuffer(
		DirectionalLightBuffer.Get(), 
		EResourceState::ResourceState_Common, 
		EResourceState::ResourceState_VertexAndConstantBuffer);
	
	CmdList.TransitionTexture(
		PointLightShadowMaps.Get(), 
		EResourceState::ResourceState_Common, 
		EResourceState::ResourceState_PixelShaderResource);

	CmdList.TransitionTexture(
		DirLightShadowMaps.Get(), 
		EResourceState::ResourceState_Common, 
		EResourceState::ResourceState_PixelShaderResource);

	if (VSMDirLightShadowMaps)
	{
		CmdList.TransitionTexture(
			VSMDirLightShadowMaps.Get(), 
			EResourceState::ResourceState_Common, 
			EResourceState::ResourceState_PixelShaderResource);
	}
	
	CommandListExecutor::ExecuteCommandList(CmdList);

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
	if (RenderingAPI::IsRayTracingSupported() && RayTracingEnabled)
	{
		if (!InitRayTracing())
		{
			return false;
		}
	}

	//LightDescriptorTable->SetShaderResourceView(ReflectionTexture->GetShaderResourceView(0).Get(), 4);
	//LightDescriptorTable->SetShaderResourceView(IrradianceMap->GetShaderResourceView(0).Get(), 5);
	//LightDescriptorTable->SetShaderResourceView(SpecularIrradianceMap->GetShaderResourceView(0).Get(), 6);
	//LightDescriptorTable->SetShaderResourceView(IntegrationLUT->GetShaderResourceView(0).Get(), 7);
	//LightDescriptorTable->SetShaderResourceView(SSAOBuffer->GetShaderResourceView(0).Get(), 13);
	//LightDescriptorTable->CopyDescriptors();

	//ForwardDescriptorTable->SetShaderResourceView(IrradianceMap->GetShaderResourceView(0).Get(), 3);
	//ForwardDescriptorTable->SetShaderResourceView(SpecularIrradianceMap->GetShaderResourceView(0).Get(), 4);
	//ForwardDescriptorTable->SetShaderResourceView(IntegrationLUT->GetShaderResourceView(0).Get(), 5);
	//ForwardDescriptorTable->CopyDescriptors();

	//if (SSAOEnabled)
	//{
	//	SSAODescriptorTable = RenderingAPI::Get().CreateDescriptorTable(6);
	//	SSAODescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_NORMAL_INDEX]->GetShaderResourceView(0).Get(), 0);
	//	SSAODescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_DEPTH_INDEX]->GetShaderResourceView(0).Get(), 1);
	//	SSAODescriptorTable->SetShaderResourceView(SSAONoiseTex->GetShaderResourceView(0).Get(), 2);
	//	SSAODescriptorTable->SetUnorderedAccessView(SSAOBuffer->GetUnorderedAccessView(0).Get(), 3);
	//	SSAODescriptorTable->SetShaderResourceView(SSAOSamples->GetShaderResourceView(0).Get(), 4);
	//	SSAODescriptorTable->SetConstantBufferView(CameraBuffer->GetConstantBufferView().Get(), 5);
	//	SSAODescriptorTable->CopyDescriptors();

	//	SSAOBlurDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(1);
	//	SSAOBlurDescriptorTable->SetUnorderedAccessView(SSAOBuffer->GetUnorderedAccessView(0).Get(), 0);
	//	SSAOBlurDescriptorTable->CopyDescriptors();
	//}

	//WriteShadowMapDescriptors();

	// Register EventFunc
	EventQueue::RegisterEventHandler(this, EEventCategory::EVENT_CATEGORY_WINDOW);

	return true;
}

bool Renderer::InitRayTracing()
{
	// Create RootSignatures
	//TUniquePtr<D3D12RootSignature> RayGenLocalRoot;
	//{
	//	D3D12_DESCRIPTOR_RANGE Ranges[1] = {};
	//	Ranges[0].BaseShaderRegister				= 0;
	//	Ranges[0].NumDescriptors					= 1;
	//	Ranges[0].RegisterSpace						= 0;
	//	Ranges[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	//	Ranges[0].OffsetInDescriptorsFromTableStart	= 0;

	//	D3D12_ROOT_PARAMETER RootParams = { };
	//	RootParams.ParameterType						= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	//	RootParams.ShaderVisibility						= D3D12_SHADER_VISIBILITY_ALL;
	//	RootParams.DescriptorTable.NumDescriptorRanges	= 1;
	//	RootParams.DescriptorTable.pDescriptorRanges	= Ranges;

	//	D3D12_ROOT_SIGNATURE_DESC RayGenLocalRootDesc = {};
	//	RayGenLocalRootDesc.NumParameters	= 1;
	//	RayGenLocalRootDesc.pParameters		= &RootParams;
	//	RayGenLocalRootDesc.Flags			= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

	//	RayGenLocalRoot = RenderingAPI::Get().CreateRootSignature(RayGenLocalRootDesc);
	//	if (RayGenLocalRoot)
	//	{
	//		RayGenLocalRoot->SetDebugName("RayGen Local RootSignature");
	//	}
	//	else
	//	{
	//		return false;
	//	}
	//}

	//TUniquePtr<D3D12RootSignature> HitLocalRoot;
	//{
	//	constexpr UInt32 NumRanges0 = 7;
	//	D3D12_DESCRIPTOR_RANGE Ranges0[NumRanges0] = {};
	//	// Albedo
	//	Ranges0[0].BaseShaderRegister					= 0;
	//	Ranges0[0].NumDescriptors						= 1;
	//	Ranges0[0].RegisterSpace						= 1;
	//	Ranges0[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges0[0].OffsetInDescriptorsFromTableStart	= 0;

	//	// Normal
	//	Ranges0[1].BaseShaderRegister					= 1;
	//	Ranges0[1].NumDescriptors						= 1;
	//	Ranges0[1].RegisterSpace						= 1;
	//	Ranges0[1].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges0[1].OffsetInDescriptorsFromTableStart	= 1;

	//	// Roughness
	//	Ranges0[2].BaseShaderRegister					= 2;
	//	Ranges0[2].NumDescriptors						= 1;
	//	Ranges0[2].RegisterSpace						= 1;
	//	Ranges0[2].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges0[2].OffsetInDescriptorsFromTableStart	= 2;

	//	// Height
	//	Ranges0[3].BaseShaderRegister					= 3;
	//	Ranges0[3].NumDescriptors						= 1;
	//	Ranges0[3].RegisterSpace						= 1;
	//	Ranges0[3].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges0[3].OffsetInDescriptorsFromTableStart	= 3;

	//	// Metallic
	//	Ranges0[4].BaseShaderRegister					= 4;
	//	Ranges0[4].NumDescriptors						= 1;
	//	Ranges0[4].RegisterSpace						= 1;
	//	Ranges0[4].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges0[4].OffsetInDescriptorsFromTableStart	= 4;

	//	// AO
	//	Ranges0[5].BaseShaderRegister					= 5;
	//	Ranges0[5].NumDescriptors						= 1;
	//	Ranges0[5].RegisterSpace						= 1;
	//	Ranges0[5].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges0[5].OffsetInDescriptorsFromTableStart	= 5;

	//	// MaterialBuffer
	//	Ranges0[6].BaseShaderRegister					= 0;
	//	Ranges0[6].NumDescriptors						= 1;
	//	Ranges0[6].RegisterSpace						= 1;
	//	Ranges0[6].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	//	Ranges0[6].OffsetInDescriptorsFromTableStart	= 6;

	//	constexpr UInt32 NumRanges1 = 2;
	//	D3D12_DESCRIPTOR_RANGE Ranges1[NumRanges1] = {};
	//	// VertexBuffer
	//	Ranges1[0].BaseShaderRegister					= 6;
	//	Ranges1[0].NumDescriptors						= 1;
	//	Ranges1[0].RegisterSpace						= 1;
	//	Ranges1[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges1[0].OffsetInDescriptorsFromTableStart	= 0;

	//	// IndexBuffer
	//	Ranges1[1].BaseShaderRegister					= 7;
	//	Ranges1[1].NumDescriptors						= 1;
	//	Ranges1[1].RegisterSpace						= 1;
	//	Ranges1[1].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges1[1].OffsetInDescriptorsFromTableStart	= 1;

	//	const UInt32 NumRootParams = 2;
	//	D3D12_ROOT_PARAMETER RootParams[NumRootParams];
	//	RootParams[0].ParameterType							= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	//	RootParams[0].DescriptorTable.NumDescriptorRanges	= NumRanges0;
	//	RootParams[0].DescriptorTable.pDescriptorRanges		= Ranges0;
	//	RootParams[0].ShaderVisibility						= D3D12_SHADER_VISIBILITY_ALL;

	//	RootParams[1].ParameterType							= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	//	RootParams[1].DescriptorTable.NumDescriptorRanges	= NumRanges1;
	//	RootParams[1].DescriptorTable.pDescriptorRanges		= Ranges1;
	//	RootParams[1].ShaderVisibility						= D3D12_SHADER_VISIBILITY_ALL;

	//	D3D12_ROOT_SIGNATURE_DESC HitLocalRootDesc = {};
	//	HitLocalRootDesc.NumParameters	= NumRootParams;
	//	HitLocalRootDesc.pParameters	= RootParams;
	//	HitLocalRootDesc.Flags			= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

	//	HitLocalRoot = RenderingAPI::Get().CreateRootSignature(HitLocalRootDesc);
	//	if (HitLocalRoot)
	//	{
	//		HitLocalRoot->SetDebugName("Closest Hit Local RootSignature");
	//	}
	//	else
	//	{
	//		return false;
	//	}
	//}

	TUniquePtr<D3D12RootSignature> MissLocalRoot;
	{
		D3D12_ROOT_SIGNATURE_DESC MissLocalRootDesc = {};
		MissLocalRootDesc.NumParameters	= 0;
		MissLocalRootDesc.pParameters	= nullptr;
		MissLocalRootDesc.Flags			= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

	//TUniquePtr<D3D12RootSignature> HitLocalRoot;
	//{
	//	D3D12_DESCRIPTOR_RANGE Ranges[2] = {};
	//	// VertexBuffer
	//	Ranges[0].BaseShaderRegister				= 2;
	//	Ranges[0].NumDescriptors					= 1;
	//	Ranges[0].RegisterSpace						= 0;
	//	Ranges[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges[0].OffsetInDescriptorsFromTableStart = 0;

	//	// IndexBuffer
	//	Ranges[1].BaseShaderRegister				= 3;
	//	Ranges[1].NumDescriptors					= 1;
	//	Ranges[1].RegisterSpace						= 0;
	//	Ranges[1].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges[1].OffsetInDescriptorsFromTableStart	= 1;

	//	D3D12_ROOT_PARAMETER RootParams = { };
	//	RootParams.ParameterType						= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	//	RootParams.DescriptorTable.NumDescriptorRanges	= 2;
	//	RootParams.DescriptorTable.pDescriptorRanges	= Ranges;

	//	D3D12_ROOT_SIGNATURE_DESC HitLocalRootDesc = {};
	//	HitLocalRootDesc.NumParameters	= 1;
	//	HitLocalRootDesc.pParameters	= &RootParams;
	//	HitLocalRootDesc.Flags			= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

	//	HitLocalRoot = RenderingAPI::Get().CreateRootSignature(HitLocalRootDesc);
	//	if (HitLocalRoot)
	//	{
	//		HitLocalRoot->SetName("Closest Hit Local RootSignature");
	//	}
	//	else
	//	{
	//		return false;
	//	}
	//}

	// Global RootSignature
	//{
	//	constexpr UInt32 NumRanges = 5;
	//	D3D12_DESCRIPTOR_RANGE Ranges[NumRanges] = {};
	//	// AccelerationStructure
	//	Ranges[0].BaseShaderRegister				= 0;
	//	Ranges[0].NumDescriptors					= 1;
	//	Ranges[0].RegisterSpace						= 0;
	//	Ranges[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges[0].OffsetInDescriptorsFromTableStart	= 0;

	//	// Camera Buffer
	//	Ranges[1].BaseShaderRegister				= 0;
	//	Ranges[1].NumDescriptors					= 1;
	//	Ranges[1].RegisterSpace						= 0;
	//	Ranges[1].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	//	Ranges[1].OffsetInDescriptorsFromTableStart	= 1;

	//	// GBuffer NormalMap
	//	Ranges[2].BaseShaderRegister				= 6;
	//	Ranges[2].NumDescriptors					= 1;
	//	Ranges[2].RegisterSpace						= 0;
	//	Ranges[2].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges[2].OffsetInDescriptorsFromTableStart = 2;

	//	// GBuffer Depth
	//	Ranges[3].BaseShaderRegister				= 7;
	//	Ranges[3].NumDescriptors					= 1;
	//	Ranges[3].RegisterSpace						= 0;
	//	Ranges[3].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges[3].OffsetInDescriptorsFromTableStart = 3;

	//	// Skybox
	//	Ranges[4].BaseShaderRegister				= 1;
	//	Ranges[4].NumDescriptors					= 1;
	//	Ranges[4].RegisterSpace						= 0;
	//	Ranges[4].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges[4].OffsetInDescriptorsFromTableStart	= 4;

	//	D3D12_ROOT_PARAMETER RootParams = { };
	//	RootParams.ParameterType						= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	//	RootParams.ShaderVisibility						= D3D12_SHADER_VISIBILITY_ALL;
	//	RootParams.DescriptorTable.NumDescriptorRanges	= NumRanges;
	//	RootParams.DescriptorTable.pDescriptorRanges	= Ranges;

	//	D3D12_STATIC_SAMPLER_DESC Samplers[2] = { };
	//	// Generic Sampler
	//	Samplers[0].ShaderVisibility	= D3D12_SHADER_VISIBILITY_ALL;
	//	Samplers[0].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	//	Samplers[0].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	//	Samplers[0].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	//	Samplers[0].Filter				= D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	//	Samplers[0].ShaderRegister		= 0;
	//	Samplers[0].RegisterSpace		= 0;
	//	Samplers[0].MinLOD				= 0.0f;
	//	Samplers[0].MaxLOD				= FLT_MAX;
 //
	//	// GBuffer Sampler
	//	Samplers[1].ShaderVisibility	= D3D12_SHADER_VISIBILITY_ALL;
	//	Samplers[1].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	//	Samplers[1].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	//	Samplers[1].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	//	Samplers[1].Filter				= D3D12_FILTER_MIN_MAG_MIP_POINT;
	//	Samplers[1].ShaderRegister		= 1;
	//	Samplers[1].RegisterSpace		= 0;
	//	Samplers[1].MinLOD				= 0.0f;
	//	Samplers[1].MaxLOD				= FLT_MAX;

	//	D3D12_ROOT_SIGNATURE_DESC GlobalRootDesc = {};
	//	GlobalRootDesc.NumStaticSamplers	= 2;
	//	GlobalRootDesc.pStaticSamplers		= Samplers;
	//	GlobalRootDesc.NumParameters		= 1;
	//	GlobalRootDesc.pParameters			= &RootParams;
	//	GlobalRootDesc.Flags				= D3D12_ROOT_SIGNATURE_FLAG_NONE;

	//	GlobalRootSignature = RenderingAPI::Get().CreateRootSignature(GlobalRootDesc);
	//	if (!GlobalRootSignature)
	//	{
	//		return false;
	//	}
	}

	//// Global RootSignature
	//{
	//	D3D12_DESCRIPTOR_RANGE Ranges[7] = {};
	//	// AccelerationStructure
	//	Ranges[0].BaseShaderRegister				= 0;
	//	Ranges[0].NumDescriptors					= 1;
	//	Ranges[0].RegisterSpace						= 0;
	//	Ranges[0].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges[0].OffsetInDescriptorsFromTableStart	= 0;

	//	// Camera Buffer
	//	Ranges[1].BaseShaderRegister				= 0;
	//	Ranges[1].NumDescriptors					= 1;
	//	Ranges[1].RegisterSpace						= 0;
	//	Ranges[1].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	//	Ranges[1].OffsetInDescriptorsFromTableStart	= 1;

	//	// Skybox
	//	Ranges[2].BaseShaderRegister				= 1;
	//	Ranges[2].NumDescriptors					= 1;
	//	Ranges[2].RegisterSpace						= 0;
	//	Ranges[2].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges[2].OffsetInDescriptorsFromTableStart	= 2;

	//	// Albedo
	//	Ranges[3].BaseShaderRegister				= 4;
	//	Ranges[3].NumDescriptors					= 1;
	//	Ranges[3].RegisterSpace						= 0;
	//	Ranges[3].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges[3].OffsetInDescriptorsFromTableStart	= 3;

	//	// Normal
	//	Ranges[4].BaseShaderRegister				= 5;
	//	Ranges[4].NumDescriptors					= 1;
	//	Ranges[4].RegisterSpace						= 0;
	//	Ranges[4].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges[4].OffsetInDescriptorsFromTableStart	= 4;

	//	// GBuffer NormalMap
	//	Ranges[5].BaseShaderRegister				= 6;
	//	Ranges[5].NumDescriptors					= 1;
	//	Ranges[5].RegisterSpace						= 0;
	//	Ranges[5].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges[5].OffsetInDescriptorsFromTableStart = 5;

	//	// GBuffer Depth
	//	Ranges[6].BaseShaderRegister				= 7;
	//	Ranges[6].NumDescriptors					= 1;
	//	Ranges[6].RegisterSpace						= 0;
	//	Ranges[6].RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	//	Ranges[6].OffsetInDescriptorsFromTableStart = 6;

	//	D3D12_ROOT_PARAMETER RootParams = { };
	//	RootParams.ParameterType						= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	//	RootParams.DescriptorTable.NumDescriptorRanges	= 7;
	//	RootParams.DescriptorTable.pDescriptorRanges	= Ranges;

	//	D3D12_STATIC_SAMPLER_DESC Samplers[2] = { };
	//	// Generic Sampler
	//	Samplers[0].ShaderVisibility	= D3D12_SHADER_VISIBILITY_ALL;
	//	Samplers[0].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	//	Samplers[0].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	//	Samplers[0].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	//	Samplers[0].Filter				= D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	//	Samplers[0].ShaderRegister		= 0;
	//	Samplers[0].RegisterSpace		= 0;
	//	Samplers[0].MinLOD				= 0.0f;
	//	Samplers[0].MaxLOD				= FLT_MAX;
 //
	//	// GBuffer Sampler
	//	Samplers[1].ShaderVisibility	= D3D12_SHADER_VISIBILITY_ALL;
	//	Samplers[1].AddressU			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	//	Samplers[1].AddressV			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	//	Samplers[1].AddressW			= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	//	Samplers[1].Filter				= D3D12_FILTER_MIN_MAG_MIP_POINT;
	//	Samplers[1].ShaderRegister		= 1;
	//	Samplers[1].RegisterSpace		= 0;
	//	Samplers[1].MinLOD				= 0.0f;
	//	Samplers[1].MaxLOD				= FLT_MAX;

	//	D3D12_ROOT_SIGNATURE_DESC GlobalRootDesc = {};
	//	GlobalRootDesc.NumStaticSamplers	= 2;
	//	GlobalRootDesc.pStaticSamplers		= Samplers;
	//	GlobalRootDesc.NumParameters		= 1;
	//	GlobalRootDesc.pParameters			= &RootParams;
	//	GlobalRootDesc.Flags				= D3D12_ROOT_SIGNATURE_FLAG_NONE;

	//	GlobalRootSignature = RenderingAPI::Get().CreateRootSignature(GlobalRootDesc);
	//	if (!GlobalRootSignature)
	//	{
	//		return false;
	//	}
	//}

	//// Create Pipeline
	//RayTracingPipelineStateProperties PipelineProperties;
	//PipelineProperties.DebugName				= "RayTracing PipelineState";
	//PipelineProperties.RayGenRootSignature		= RayGenLocalRoot.Get();
	//PipelineProperties.HitGroupRootSignature	= HitLocalRoot.Get();
	//PipelineProperties.MissRootSignature		= MissLocalRoot.Get();
	//PipelineProperties.GlobalRootSignature		= GlobalRootSignature.Get();
	//PipelineProperties.MaxRecursions			= 4;

	//RaytracingPSO = RenderingAPI::Get().CreateRayTracingPipelineState(PipelineProperties);
	//if (!RaytracingPSO)
	//{
	//	return false;
	//}

	//// Build Acceleration Structures
	//RenderingAPI::Get().GetImmediateCommandList()->TransitionBarrier(CameraBuffer.Get(), EResourceState::ResourceState_Common, EResourceState::ResourceState_VertexAndConstantBuffer);

	// Create DescriptorTables
	//RayGenDescriptorTable = TSharedPtr(RenderingAPI::Get().CreateDescriptorTable(1));
	//GlobalDescriptorTable = TSharedPtr(RenderingAPI::Get().CreateDescriptorTable(7));

	// Create TLAS
	RayTracingScene = RenderingAPI::CreateRayTracingScene();
	if (!RayTracingScene)
	{
		return false;
	}

	// Populate descriptors
	//RayGenDescriptorTable->SetUnorderedAccessView(ReflectionTexture->GetUnorderedAccessView(0).Get(), 0);
	//RayGenDescriptorTable->CopyDescriptors();

	//GlobalDescriptorTable->SetConstantBufferView(CameraBuffer->GetConstantBufferView().Get(), 1);
	//GlobalDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_NORMAL_INDEX]->GetShaderResourceView(0).Get(), 2);
	//GlobalDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_DEPTH_INDEX]->GetShaderResourceView(0).Get(), 3);
	//GlobalDescriptorTable->SetShaderResourceView(Skybox->GetShaderResourceView(0).Get(), 4);
	//GlobalDescriptorTable->CopyDescriptors();

	return true;
}

bool Renderer::InitLightBuffers()
{
	const UInt32 NumPointLights = 1;
	const UInt32 NumDirLights	= 1;

	PointLightBuffer = RenderingAPI::CreateConstantBuffer<PointLightProperties>(
		nullptr, 
		NumPointLights, 
		BufferUsage_Default);
	if (!PointLightBuffer)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		PointLightBuffer->SetName("PointLight Buffer");
	}

	DirectionalLightBuffer = RenderingAPI::CreateConstantBuffer<DirectionalLightProperties>(
		nullptr, 
		NumDirLights, 
		BufferUsage_Default);
	if (!DirectionalLightBuffer)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		DirectionalLightBuffer->SetName("DirectionalLight Buffer");
	}

	return CreateShadowMaps();
}

bool Renderer::InitPrePass()
{
	using namespace Microsoft::WRL;

	TArray<UInt8> ShaderCode;
	if (!ShaderCompiler::CompileFromFile(
		"Shaders/PrePass.hlsl",
		"Main",
		nullptr,
		EShaderStage::ShaderStage_Vertex,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	TSharedRef<VertexShader> VShader = RenderingAPI::CreateVertexShader(ShaderCode);
	if (!VShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		VShader->SetName("PrePass VertexShader");
	}

	DepthStencilStateCreateInfo DepthStencilStateInfo;
	DepthStencilStateInfo.DepthFunc			= EComparisonFunc::ComparisonFunc_Less;
	DepthStencilStateInfo.DepthEnable		= true;
	DepthStencilStateInfo.DepthWriteMask	= EDepthWriteMask::DepthWriteMask_All;

	TSharedRef<DepthStencilState> DepthStencilState = RenderingAPI::CreateDepthStencilState(DepthStencilStateInfo);
	if (!DepthStencilState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		DepthStencilState->SetName("Prepass DepthStencilState");
	}

	RasterizerStateCreateInfo RasterizerStateInfo;
	RasterizerStateInfo.CullMode = ECullMode::CullMode_Back;

	TSharedRef<RasterizerState> RasterizerState = RenderingAPI::CreateRasterizerState(RasterizerStateInfo);
	if (!RasterizerState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		RasterizerState->SetName("Prepass RasterizerState");
	}

	BlendStateCreateInfo BlendStateInfo;
	BlendStateInfo.IndependentBlendEnable		= false;
	BlendStateInfo.RenderTarget[0].BlendEnable	= false;

	TSharedRef<BlendState> BlendState = RenderingAPI::CreateBlendState(BlendStateInfo);
	if (!BlendState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		BlendState->SetName("Prepass BlendState");
	}

	GraphicsPipelineStateCreateInfo PSOInfo;
	PSOInfo.InputLayoutState	= StdInputLayout.Get();
	PSOInfo.BlendState			= BlendState.Get();
	PSOInfo.DepthStencilState	= DepthStencilState.Get();
	PSOInfo.RasterizerState		= RasterizerState.Get();
	PSOInfo.ShaderState.VertexShader = VShader.Get();
	PSOInfo.PipelineFormats.DepthStencilFormat = DepthBufferFormat;

	PrePassPSO = RenderingAPI::CreateGraphicsPipelineState(PSOInfo);
	if (!PrePassPSO)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		PrePassPSO->SetName("PrePass PipelineState");
	}

	return true;
}

bool Renderer::InitShadowMapPass()
{
	using namespace Microsoft::WRL;

	// Directional Shadows
	TArray<UInt8> ShaderCode;

#if ENABLE_VSM
	if (!ShaderCompiler::CompileFromFile(
		"Shaders/ShadowMap.hlsl",
		"VSM_VSMain",
		nullptr,
		EShaderStage::ShaderStage_Vertex,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	TSharedRef<VertexShader> VSShader = RenderingAPI::CreateVertexShader(ShaderCode);
	if (!VSShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		VSShader->SetName("ShadowMap VertexShader");
	}

	if (!ShaderCompiler::CompileFromFile(
		"Shaders/ShadowMap.hlsl",
		"VSM_PSMain",
		nullptr,
		EShaderStage::ShaderStage_Pixel,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	TSharedRef<PixelShader> PSShader = RenderingAPI::CreatePixelShader(ShaderCode);
	if (!PSShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		PSShader->SetName("ShadowMap PixelShader");
	}
#else
	if (!ShaderCompiler::CompileFromFile(
		"Shaders/ShadowMap.hlsl", 
		"Main",
		nullptr, 
		EShaderStage::ShaderStage_Vertex,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	TSharedRef<VertexShader> VShader = RenderingAPI::CreateVertexShader(ShaderCode);
	if (!VShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		VShader->SetName("ShadowMap VertexShader");
	}
#endif

	// Init PipelineState
	DepthStencilStateCreateInfo DepthStencilStateInfo;
	DepthStencilStateInfo.DepthFunc			= EComparisonFunc::ComparisonFunc_LessEqual;
	DepthStencilStateInfo.DepthEnable		= true;
	DepthStencilStateInfo.DepthWriteMask	= EDepthWriteMask::DepthWriteMask_All;
	
	TSharedRef<DepthStencilState> DepthStencilState = RenderingAPI::CreateDepthStencilState(DepthStencilStateInfo);
	if (!DepthStencilState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		DepthStencilState->SetName("Shadow DepthStencilState");
	}

	RasterizerStateCreateInfo RasterizerStateInfo;
	RasterizerStateInfo.CullMode = ECullMode::CullMode_Back;

	TSharedRef<RasterizerState> RasterizerState = RenderingAPI::CreateRasterizerState(RasterizerStateInfo);
	if (!RasterizerState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		RasterizerState->SetName("Shadow RasterizerState");
	}

	BlendStateCreateInfo BlendStateInfo;
	TSharedRef<BlendState> BlendState = RenderingAPI::CreateBlendState(BlendStateInfo);
	if (!BlendState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		BlendState->SetName("Shadow BlendState");
	}

	GraphicsPipelineStateCreateInfo ShadowMapPSOInfo;
	ShadowMapPSOInfo.BlendState				= BlendState.Get();
	ShadowMapPSOInfo.DepthStencilState		= DepthStencilState.Get();
	ShadowMapPSOInfo.IBStripCutValue		= EIndexBufferStripCutValue::IndexBufferStripCutValue_Disabled;
	ShadowMapPSOInfo.InputLayoutState		= StdInputLayout.Get();
	ShadowMapPSOInfo.PrimitiveTopologyType	= EPrimitiveTopologyType::PrimitiveTopologyType_Triangle;
	ShadowMapPSOInfo.RasterizerState		= RasterizerState.Get();
	ShadowMapPSOInfo.SampleCount			= 1;
	ShadowMapPSOInfo.SampleQuality			= 0;
	ShadowMapPSOInfo.SampleMask				= 0xffffffff;
	ShadowMapPSOInfo.ShaderState.VertexShader	= VShader.Get();
	ShadowMapPSOInfo.ShaderState.PixelShader	= nullptr;
	ShadowMapPSOInfo.PipelineFormats.NumRenderTargets	= 0;
	ShadowMapPSOInfo.PipelineFormats.DepthStencilFormat	= ShadowMapFormat;

#if ENABLE_VSM
	VSMShadowMapPSO = MakeShared<D3D12GraphicsPipelineState>(Device.Get());
	if (!VSMShadowMapPSO->Initialize(PSOProperties))
	{
		return false;
	}
#else
	ShadowMapPSO = RenderingAPI::CreateGraphicsPipelineState(ShadowMapPSOInfo);
	if (!ShadowMapPSO)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		ShadowMapPSO->SetName("ShadowMap PipelineState");
	}
#endif

	// Linear Shadow Maps
	if (!ShaderCompiler::CompileFromFile(
		"Shaders/ShadowMap.hlsl",
		"VSMain",
		nullptr,
		EShaderStage::ShaderStage_Vertex,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	VShader = RenderingAPI::CreateVertexShader(ShaderCode);
	if (!VShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		VShader->SetName("Linear ShadowMap VertexShader");
	}

#if !ENABLE_VSM
	TSharedRef<PixelShader> PShader;
#endif
	if (!ShaderCompiler::CompileFromFile(
		"Shaders/ShadowMap.hlsl",
		"PSMain",
		nullptr,
		EShaderStage::ShaderStage_Pixel,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	PShader = RenderingAPI::CreatePixelShader(ShaderCode);
	if (!PShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		PShader->SetName("Linear ShadowMap PixelShader");
	}

	// Linear ShadowMap PipelineState
	ShadowMapPSOInfo.ShaderState.VertexShader	= VShader.Get();
	ShadowMapPSOInfo.ShaderState.PixelShader	= PShader.Get();

	LinearShadowMapPSO = RenderingAPI::CreateGraphicsPipelineState(ShadowMapPSOInfo);
	if (!LinearShadowMapPSO)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		LinearShadowMapPSO->SetName("Linear ShadowMap PipelineState");
	}

	return true;
}

bool Renderer::InitDeferred()
{
	using namespace Microsoft::WRL;

	// GeometryPass
	TArray<ShaderDefine> Defines =
	{
		{ "ENABLE_PARALLAX_MAPPING", "1" },
		{ "ENABLE_NORMAL_MAPPING",	 "1" },
	};

	TArray<UInt8> ShaderCode;
	if (!ShaderCompiler::CompileFromFile(
		"Shaders/GeometryPass.hlsl", 
		"VSMain",
		&Defines,
		EShaderStage::ShaderStage_Vertex,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	TSharedRef<VertexShader> VShader = RenderingAPI::CreateVertexShader(ShaderCode);
	if (!VShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		VShader->SetName("GeometryPass VertexShader");
	}

	if (!ShaderCompiler::CompileFromFile(
		"Shaders/GeometryPass.hlsl",
		"PSMain",
		&Defines,
		EShaderStage::ShaderStage_Pixel,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	TSharedRef<PixelShader> PShader = RenderingAPI::CreatePixelShader(ShaderCode);
	if (!PShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		PShader->SetName("GeometryPass PixelShader");
	}

	DepthStencilStateCreateInfo DepthStencilStateInfo;
	DepthStencilStateInfo.DepthFunc			= EComparisonFunc::ComparisonFunc_LessEqual;
	DepthStencilStateInfo.DepthEnable		= true;
	DepthStencilStateInfo.DepthWriteMask	= EDepthWriteMask::DepthWriteMask_All;

	TSharedRef<DepthStencilState> DepthStencilState = RenderingAPI::CreateDepthStencilState(DepthStencilStateInfo);
	if (!DepthStencilState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		DepthStencilState->SetName("GeometryPass DepthStencilState");
	}

	RasterizerStateCreateInfo RasterizerStateInfo;
	RasterizerStateInfo.CullMode = ECullMode::CullMode_Back;

	TSharedRef<RasterizerState> RasterizerState = RenderingAPI::CreateRasterizerState(RasterizerStateInfo);
	if (!RasterizerState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		RasterizerState->SetName("GeometryPass RasterizerState");
	}

	BlendStateCreateInfo BlendStateInfo;
	BlendStateInfo.IndependentBlendEnable		= false;
	BlendStateInfo.RenderTarget[0].BlendEnable	= false;

	TSharedRef<BlendState> BlendState = RenderingAPI::CreateBlendState(BlendStateInfo);
	if (!BlendState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		BlendState->SetName("GeometryPass BlendState");
	}

	GraphicsPipelineStateCreateInfo PSOProperties;
	PSOProperties.InputLayoutState	= StdInputLayout.Get();
	PSOProperties.BlendState		= BlendState.Get();
	PSOProperties.DepthStencilState	= DepthStencilState.Get();
	PSOProperties.RasterizerState	= RasterizerState.Get();
	PSOProperties.ShaderState.VertexShader	= VShader.Get();
	PSOProperties.ShaderState.PixelShader	= PShader.Get();
	PSOProperties.PipelineFormats.RenderTargetFormats[0]	= EFormat::Format_R8G8B8A8_Unorm;
	PSOProperties.PipelineFormats.RenderTargetFormats[1]	= NormalFormat;
	PSOProperties.PipelineFormats.RenderTargetFormats[2]	= EFormat::Format_R8G8B8A8_Unorm;
	PSOProperties.PipelineFormats.NumRenderTargets			= 3;
	PSOProperties.PipelineFormats.DepthStencilFormat		= DepthBufferFormat;

	GeometryPSO = RenderingAPI::CreateGraphicsPipelineState(PSOProperties);
	if (!GeometryPSO)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		GeometryPSO->SetName("GeometryPass PipelineState");
	}

	// LightPass
	if (!ShaderCompiler::CompileFromFile(
		"Shaders/FullscreenVS.hlsl",
		"Main",
		nullptr,
		EShaderStage::ShaderStage_Vertex,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	VShader = RenderingAPI::CreateVertexShader(ShaderCode);
	if (!VShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		VShader->SetName("Fullscreen VertexShader");
	}

	const char* Value = (RenderingAPI::IsRayTracingSupported() && RayTracingEnabled) ? "1" : "0";
	Defines =
	{
		{ "ENABLE_RAYTRACING",	Value },
	};

	if (!ShaderCompiler::CompileFromFile(
		"Shaders/LightPassPS.hlsl",
		"Main",
		&Defines,
		EShaderStage::ShaderStage_Pixel,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	PShader = RenderingAPI::CreatePixelShader(ShaderCode);
	if (!PShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		PShader->SetName("LightPass PixelShader");
	}

	PSOProperties.InputLayoutState	= nullptr;
	PSOProperties.ShaderState.VertexShader	= VShader.Get();
	PSOProperties.ShaderState.PixelShader	= PShader.Get();
	PSOProperties.PipelineFormats.RenderTargetFormats[0]	= RenderTargetFormat;
	PSOProperties.PipelineFormats.NumRenderTargets			= 1;
	PSOProperties.PipelineFormats.DepthStencilFormat		= EFormat::Format_Unknown;

	LightPassPSO = RenderingAPI::CreateGraphicsPipelineState(PSOProperties);
	if (!LightPassPSO)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		LightPassPSO->SetName("LightPass PipelineState");
	}

	// SkyboxPass
	if (!ShaderCompiler::CompileFromFile(
		"Shaders/Skybox.hlsl",
		"VSMain",
		nullptr,
		EShaderStage::ShaderStage_Vertex,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	VShader = RenderingAPI::CreateVertexShader(ShaderCode);
	if (!VShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		VShader->SetName("Skybox VertexShader");
	}

	if (!ShaderCompiler::CompileFromFile(
		"Shaders/Skybox.hlsl",
		"PSMain",
		nullptr,
		EShaderStage::ShaderStage_Pixel,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	PShader = RenderingAPI::CreatePixelShader(ShaderCode);
	if (!PShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		PShader->SetName("Skybox PixelShader");
	}

	PSOProperties.InputLayoutState = StdInputLayout.Get();
	PSOProperties.ShaderState.VertexShader	= VShader.Get();
	PSOProperties.ShaderState.PixelShader	= PShader.Get();
	PSOProperties.PipelineFormats.RenderTargetFormats[0]	= EFormat::Format_R8G8B8A8_Unorm;
	PSOProperties.PipelineFormats.NumRenderTargets			= 1;
	PSOProperties.PipelineFormats.DepthStencilFormat		= DepthBufferFormat;

	SkyboxPSO = RenderingAPI::CreateGraphicsPipelineState(PSOProperties);
	if (!SkyboxPSO)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		SkyboxPSO->SetName("SkyboxPSO PipelineState");
	}

	//// Init descriptortable
	//GeometryDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(1);
	//GeometryDescriptorTable->SetConstantBufferView(CameraBuffer->GetConstantBufferView().Get(), 0);
	//GeometryDescriptorTable->CopyDescriptors();
	//
	//LightDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(13);
	//LightDescriptorTable->SetShaderResourceView(GBuffer[0]->GetShaderResourceView(0).Get(), 0);
	//LightDescriptorTable->SetShaderResourceView(GBuffer[1]->GetShaderResourceView(0).Get(), 1);
	//LightDescriptorTable->SetShaderResourceView(GBuffer[2]->GetShaderResourceView(0).Get(), 2);
	//LightDescriptorTable->SetShaderResourceView(GBuffer[3]->GetShaderResourceView(0).Get(), 3);
	//// #4 is set after deferred and raytracing
	//// #5 is set after deferred and raytracing
	//// #6 is set after deferred and raytracing
	//// #7 is set after deferred and raytracing
	//// #8 is set after deferred and raytracing
	//LightDescriptorTable->SetConstantBufferView(CameraBuffer->GetConstantBufferView().Get(), 10);
	//LightDescriptorTable->SetConstantBufferView(PointLightBuffer->GetConstantBufferView().Get(), 11);
	//LightDescriptorTable->SetConstantBufferView(DirectionalLightBuffer->GetConstantBufferView().Get(), 12);

	//SkyboxDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(2);
	//SkyboxDescriptorTable->SetShaderResourceView(Skybox->GetShaderResourceView(0).Get(), 0);
	//SkyboxDescriptorTable->SetConstantBufferView(CameraBuffer->GetConstantBufferView().Get(), 1);
	//SkyboxDescriptorTable->CopyDescriptors();
	// Init descriptortable
	//GeometryDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(1);
	//GeometryDescriptorTable->SetConstantBufferView(CameraBuffer->GetConstantBufferView().Get(), 0);
	//GeometryDescriptorTable->CopyDescriptors();

	//ForwardDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(8);
	//ForwardDescriptorTable->SetConstantBufferView(CameraBuffer->GetConstantBufferView().Get(), 0);
	//ForwardDescriptorTable->SetConstantBufferView(PointLightBuffer->GetConstantBufferView().Get(), 1);
	//ForwardDescriptorTable->SetConstantBufferView(DirectionalLightBuffer->GetConstantBufferView().Get(), 2);

	//LightDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(14);
	//LightDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_ALBEDO_INDEX]->GetShaderResourceView(0).Get(), 0);
	//LightDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_NORMAL_INDEX]->GetShaderResourceView(0).Get(), 1);
	//LightDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_MATERIAL_INDEX]->GetShaderResourceView(0).Get(), 2);
	//LightDescriptorTable->SetShaderResourceView(GBuffer[GBUFFER_DEPTH_INDEX]->GetShaderResourceView(0).Get(), 3);
	// #4 is set after deferred and raytracing
	// #5 is set after deferred and raytracing
	// #6 is set after deferred and raytracing
	// #7 is set after deferred and raytracing
	// #8 is set after deferred and raytracing
	//LightDescriptorTable->SetConstantBufferView(CameraBuffer->GetConstantBufferView().Get(), 10);
	//LightDescriptorTable->SetConstantBufferView(PointLightBuffer->GetConstantBufferView().Get(), 11);
	//LightDescriptorTable->SetConstantBufferView(DirectionalLightBuffer->GetConstantBufferView().Get(), 12);

	//SkyboxDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(2);
	//SkyboxDescriptorTable->SetShaderResourceView(Skybox->GetShaderResourceView(0).Get(), 0);
	//SkyboxDescriptorTable->SetConstantBufferView(CameraBuffer->GetConstantBufferView().Get(), 1);
	//SkyboxDescriptorTable->CopyDescriptors();

	return true;
}

bool Renderer::InitGBuffer()
{
	// Create image
	if (!InitRayTracingTexture())
	{
		return false;
	}

	const UInt32 Width	= 800;// RenderingAPI::Get().GetSwapChain()->GetWidth();
	const UInt32 Height	= 600;// RenderingAPI::Get().GetSwapChain()->GetWidth();
	const UInt32 Usage	= TextureUsage_Default | TextureUsage_RenderTarget;
	const EFormat AlbedoFormat		= EFormat::Format_R8G8B8A8_Unorm;
	const EFormat MaterialFormat	= EFormat::Format_R8G8B8A8_Unorm;

	GBuffer[GBUFFER_ALBEDO_INDEX] = RenderingAPI::CreateTexture2D(
		nullptr, 
		AlbedoFormat, 
		Usage, 
		Width, 
		Height, 
		1, 1, 
		ClearValue(ColorClearValue(0.0f, 0.0f, 0.0f, 1.0f)));
	if (GBuffer[GBUFFER_ALBEDO_INDEX])
	{
		GBuffer[GBUFFER_ALBEDO_INDEX]->SetName("GBuffer Albedo");

		GBufferSRVs[GBUFFER_ALBEDO_INDEX] = RenderingAPI::CreateShaderResourceView(GBuffer[GBUFFER_ALBEDO_INDEX].Get(), AlbedoFormat, 0, 1);
		if (!GBufferSRVs[GBUFFER_ALBEDO_INDEX])
		{
			return false;
		}

		GBufferRTVs[GBUFFER_ALBEDO_INDEX] = RenderingAPI::CreateRenderTargetView(GBuffer[GBUFFER_ALBEDO_INDEX].Get(), AlbedoFormat, 0);
		if (!GBufferSRVs[GBUFFER_ALBEDO_INDEX])
		{
			return false;
		}
	}
	else
	{
		return false;
	}

	// Normal
	GBuffer[GBUFFER_NORMAL_INDEX] = RenderingAPI::CreateTexture2D(
		nullptr, 
		NormalFormat, 
		Usage, 
		Width, 
		Height, 
		1, 1, 
		ClearValue(ColorClearValue(0.0f, 0.0f, 0.0f, 1.0f)));
	if (GBuffer[GBUFFER_NORMAL_INDEX])
	{
		GBuffer[GBUFFER_NORMAL_INDEX]->SetName("GBuffer Normal");

		GBufferSRVs[GBUFFER_NORMAL_INDEX] = RenderingAPI::CreateShaderResourceView(GBuffer[GBUFFER_NORMAL_INDEX].Get(), NormalFormat, 0, 1);
		if (!GBufferSRVs[GBUFFER_NORMAL_INDEX])
		{
			return false;
		}

		GBufferRTVs[GBUFFER_NORMAL_INDEX] = RenderingAPI::CreateRenderTargetView(GBuffer[GBUFFER_NORMAL_INDEX].Get(), NormalFormat, 0);
		if (!GBufferSRVs[GBUFFER_NORMAL_INDEX])
		{
			return false;
		}
	}
	else
	{
		return false;
	}

	// Material Properties
	GBuffer[GBUFFER_MATERIAL_INDEX] = RenderingAPI::CreateTexture2D(
		nullptr, 
		MaterialFormat, 
		Usage, 
		Width, 
		Height, 
		1, 1, 
		ClearValue(ColorClearValue(0.0f, 0.0f, 0.0f, 1.0f)));
	if (GBuffer[GBUFFER_MATERIAL_INDEX])
	{
		GBuffer[GBUFFER_MATERIAL_INDEX]->SetName("GBuffer Material");

		GBufferSRVs[GBUFFER_MATERIAL_INDEX] = RenderingAPI::CreateShaderResourceView(GBuffer[GBUFFER_MATERIAL_INDEX].Get(), MaterialFormat, 0, 1);
		if (!GBufferSRVs[GBUFFER_MATERIAL_INDEX])
		{
			return false;
		}

		GBufferRTVs[GBUFFER_MATERIAL_INDEX] = RenderingAPI::CreateRenderTargetView(GBuffer[GBUFFER_MATERIAL_INDEX].Get(), MaterialFormat, 0);
		if (!GBufferSRVs[GBUFFER_MATERIAL_INDEX])
		{
			return false;
		}
	}
	else
	{
		return false;
	}

	// DepthStencil
	const UInt32 UsageDS = TextureUsage_Default | TextureUsage_DSV | TextureUsage_SRV;
	GBuffer[GBUFFER_DEPTH_INDEX] = RenderingAPI::CreateTexture2D(
		nullptr, 
		EFormat::Format_R32_Typeless, 
		UsageDS, 
		Width, 
		Height, 
		1, 1, 
		ClearValue(DepthStencilClearValue(1.0f, 0)));
	if (GBuffer[GBUFFER_DEPTH_INDEX])
	{
		GBuffer[GBUFFER_DEPTH_INDEX]->SetName("GBuffer DepthStencil");

		GBufferSRVs[GBUFFER_DEPTH_INDEX] = RenderingAPI::CreateShaderResourceView(
			GBuffer[GBUFFER_DEPTH_INDEX].Get(), 
			EFormat::Format_R32_Float, 
			0, 
			1);
		if (!GBufferSRVs[GBUFFER_DEPTH_INDEX])
		{
			return false;
		}

		GBufferDSV = RenderingAPI::CreateDepthStencilView(
			GBuffer[GBUFFER_DEPTH_INDEX].Get(),
			DepthBufferFormat, 
			0);
		if (!GBufferDSV)
		{
			return false;
		}
	}
	else
	{
		return false;
	}

	// Final Image
	FinalTarget = RenderingAPI::CreateTexture2D(
		nullptr, 
		FinalTargetFormat,
		Usage, 
		Width, 
		Height, 
		1, 1, 
		ClearValue(ColorClearValue(0.0f, 0.0f, 0.0f, 1.0f)));
	if (FinalTarget)
	{
		FinalTarget->SetName("Final Target");

		FinalTargetSRV = RenderingAPI::CreateShaderResourceView(FinalTarget.Get(), FinalTargetFormat, 0, 1);
		if (!FinalTargetSRV)
		{
			return false;
		}

		FinalTargetRTV = RenderingAPI::CreateRenderTargetView(FinalTarget.Get(), FinalTargetFormat, 0);
		if (!FinalTargetRTV)
		{
			return false;
		}
	}
	else
	{
		return false;
	}

	// Final image descriptorset
	//PostDescriptorTable = RenderingAPI::Get().CreateDescriptorTable(1);
	//PostDescriptorTable->SetShaderResourceView(FinalTarget->GetShaderResourceView(0).Get(), 0);
	//PostDescriptorTable->CopyDescriptors();

	return true;
}

bool Renderer::InitIntegrationLUT()
{
	constexpr UInt32 LUTSize	= 512;
	constexpr EFormat LUTFormat	= EFormat::Format_R16G16_Float;
	if (!RenderingAPI::UAVSupportsFormat(EFormat::Format_R16G16_Float))
	{
		LOG_ERROR("[Renderer]: Format_R16G16_Float is not supported for UAVs");

		Debug::DebugBreak();
		return false;
	}

	TSharedRef<Texture2D> StagingTexture = RenderingAPI::CreateTexture2D(
		nullptr, 
		LUTFormat, 
		TextureUsage_Default | TextureUsage_UAV, 
		LUTSize, 
		LUTSize, 
		1, 1);
	if (!StagingTexture)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		StagingTexture->SetName("Staging IntegrationLUT");
	}

	TSharedRef<UnorderedAccessView> Uav = RenderingAPI::CreateUnorderedAccessView(StagingTexture.Get(), LUTFormat, 0);
	if (!Uav)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		Uav->SetName("IntegrationLUT UAV");
	}

	IntegrationLUT = RenderingAPI::CreateTexture2D(
		nullptr, 
		LUTFormat, 
		TextureUsage_Default | TextureUsage_SRV,
		LUTSize, 
		LUTSize, 
		1, 1);
	if (!IntegrationLUT)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		IntegrationLUT->SetName("IntegrationLUT");
	}

	IntegrationLUTSRV = RenderingAPI::CreateShaderResourceView(IntegrationLUT.Get(), LUTFormat, 0, 1);
	if (!IntegrationLUTSRV)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		IntegrationLUTSRV->SetName("IntegrationLUT SRV");
	}

	TArray<UInt8> ShaderCode;
	if (!ShaderCompiler::CompileFromFile(
		"Shaders/BRDFIntegationGen.hlsl",
		"Main",
		nullptr,
		EShaderStage::ShaderStage_Compute,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	TSharedRef<ComputeShader> CShader = RenderingAPI::CreateComputeShader(ShaderCode);
	if (!CShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		CShader->SetName("BRDFIntegationGen ComputeShader");
	}

	ComputePipelineStateCreateInfo PSOInfo;
	PSOInfo.Shader = CShader.Get();

	TUniquePtr<ComputePipelineState> PSO = TUniquePtr(RenderingAPI::CreateComputePipelineState(PSOInfo));
	if (!PSO)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		PSO->SetName("BRDFIntegationGen PipelineState");
	}

	//TUniquePtr<D3D12DescriptorTable> DescriptorTable = TUniquePtr(RenderingAPI::Get().CreateDescriptorTable(1));
	//DescriptorTable->SetUnorderedAccessView(StagingTexture->GetUnorderedAccessView(0).Get(), 0);
	//DescriptorTable->CopyDescriptors();

	//RenderingAPI::Get().GetImmediateCommandList()->SetComputeRootSignature(RootSignature->GetRootSignature());
	//RenderingAPI::Get().GetImmediateCommandList()->SetPipelineState(PSO->GetPipeline());
	//			
	//RenderingAPI::Get().GetImmediateCommandList()->BindGlobalOnlineDescriptorHeaps();
	//RenderingAPI::Get().GetImmediateCommandList()->SetComputeRootDescriptorTable(DescriptorTable->GetGPUTableStartHandle(), 0);
	//			
	//RenderingAPI::Get().GetImmediateCommandList()->Dispatch(LUTProperties.Width, LUTProperties.Height, 1);
	//RenderingAPI::Get().GetImmediateCommandList()->UnorderedAccessBarrier(StagingTexture.Get());
	//			
	//RenderingAPI::Get().GetImmediateCommandList()->TransitionBarrier(IntegrationLUT.Get(), EResourceState::ResourceState_Common, EResourceState::ResourceState_CopyDest);
	//RenderingAPI::Get().GetImmediateCommandList()->TransitionBarrier(StagingTexture.Get(), EResourceState::ResourceState_Common, EResourceState::ResourceState_CopySource);
	//			
	//RenderingAPI::Get().GetImmediateCommandList()->CopyResource(IntegrationLUT.Get(), StagingTexture.Get());
	//			
	//RenderingAPI::Get().GetImmediateCommandList()->TransitionBarrier(IntegrationLUT.Get(), EResourceState::ResourceState_CopyDest, EResourceState::ResourceState_PixelShaderResource);
	//			
	//RenderingAPI::Get().GetImmediateCommandList()->Flush();
	//RenderingAPI::Get().GetImmediateCommandList()->WaitForCompletion();

	return true;
}

bool Renderer::InitRayTracingTexture()
{
	const UInt32 Width	= 800;//RenderingAPI::Get().GetSwapChain()->GetWidth();
	const UInt32 Height	= 600;//RenderingAPI::Get().GetSwapChain()->GetHeight();
	const UInt32 Usage	= TextureUsage_Default | TextureUsage_RWTexture;
	ReflectionTexture = RenderingAPI::CreateTexture2D(
		nullptr, 
		EFormat::Format_R8G8B8A8_Unorm, 
		Usage, 
		Width, 
		Height, 
		1, 1);
	if (!ReflectionTexture)
	{
		return false;
	}

	ReflectionTextureUAV = RenderingAPI::CreateUnorderedAccessView(ReflectionTexture.Get(), EFormat::Format_R8G8B8A8_Unorm, 0);
	if (!ReflectionTextureUAV)
	{
		return false;
	}

	ReflectionTextureSRV = RenderingAPI::CreateShaderResourceView(ReflectionTexture.Get(), EFormat::Format_R8G8B8A8_Unorm, 0, 1);
	if (!ReflectionTextureSRV)
	{
		return false;
	}
	return true;
}

bool Renderer::InitDebugStates()
{
	using namespace Microsoft::WRL;

	TArray<UInt8> ShaderCode;
	if (!ShaderCompiler::CompileFromFile(
		"Shaders/Debug.hlsl",
		"VSMain",
		nullptr,
		EShaderStage::ShaderStage_Vertex,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	TSharedRef<VertexShader> VShader = RenderingAPI::CreateVertexShader(ShaderCode);
	if (!VShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		VShader->SetName("Debug VertexShader");
	}

	if (!ShaderCompiler::CompileFromFile(
		"Shaders/Debug.hlsl",
		"PSMain",
		nullptr,
		EShaderStage::ShaderStage_Pixel,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	TSharedRef<PixelShader> PShader = RenderingAPI::CreatePixelShader(ShaderCode);
	if (!PShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		PShader->SetName("Debug PixelShader");
	}

	// Debug Inputlayout
	InputLayoutStateCreateInfo InputLayout =
	{
		{ "POSITION", 0, EFormat::Format_R32G32B32_Float, 0, 0, EInputClassification::InputClassification_Vertex, 0 },
	};

	TSharedRef<InputLayoutState> InputLayoutState = RenderingAPI::CreateInputLayout(InputLayout);
	if (!InputLayoutState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		InputLayoutState->SetName("Debug InputLayoutState");
	}

	DepthStencilStateCreateInfo DepthStencilStateInfo;
	DepthStencilStateInfo.DepthFunc			= EComparisonFunc::ComparisonFunc_LessEqual;
	DepthStencilStateInfo.DepthEnable		= false;
	DepthStencilStateInfo.DepthWriteMask	= EDepthWriteMask::DepthWriteMask_Zero;

	TSharedRef<DepthStencilState> DepthStencilState = RenderingAPI::CreateDepthStencilState(DepthStencilStateInfo);
	if (!DepthStencilState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		DepthStencilState->SetName("Debug DepthStencilState");
	}

	RasterizerStateCreateInfo RasterizerStateInfo;
	RasterizerStateInfo.CullMode = ECullMode::CullMode_None;

	TSharedRef<RasterizerState> RasterizerState = RenderingAPI::CreateRasterizerState(RasterizerStateInfo);
	if (!RasterizerState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		RasterizerState->SetName("Debug RasterizerState");
	}

	// Debug blendstate
	BlendStateCreateInfo BlendStateInfo;

	TSharedRef<BlendState> BlendState = RenderingAPI::CreateBlendState(BlendStateInfo);
	if (!BlendState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		BlendState->SetName("Debug BlendState");
	}

	GraphicsPipelineStateCreateInfo PSOProperties;
	PSOProperties.BlendState		= BlendState.Get();
	PSOProperties.DepthStencilState	= DepthStencilState.Get();
	PSOProperties.InputLayoutState	= InputLayoutState.Get();
	PSOProperties.RasterizerState	= RasterizerState.Get();
	PSOProperties.ShaderState.VertexShader	= VShader.Get();
	PSOProperties.ShaderState.PixelShader	= PShader.Get();
	PSOProperties.PrimitiveTopologyType		= EPrimitiveTopologyType::PrimitiveTopologyType_Line;
	PSOProperties.PipelineFormats.RenderTargetFormats[0]	= FinalTargetFormat;
	PSOProperties.PipelineFormats.NumRenderTargets			= 1;
	PSOProperties.PipelineFormats.DepthStencilFormat		= DepthBufferFormat;

	DebugBoxPSO = RenderingAPI::CreateGraphicsPipelineState(PSOProperties);
	if (!DebugBoxPSO)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		DebugBoxPSO->SetName("Debug PipelineState");
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

	ResourceData VertexData(Vertices);

	AABBVertexBuffer = RenderingAPI::CreateVertexBuffer<XMFLOAT3>(&VertexData, 8, BufferUsage_Default);
	if (!AABBVertexBuffer)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		AABBVertexBuffer->SetName("AABB VertexBuffer");
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

	ResourceData IndexData(Indices);

	AABBIndexBuffer = RenderingAPI::CreateIndexBuffer(
		&IndexData, 
		sizeof(UInt16) * 24, 
		EIndexFormat::IndexFormat_UInt16, 
		BufferUsage_Default);
	if (!AABBIndexBuffer)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		AABBIndexBuffer->SetName("AABB IndexBuffer");
	}

	return true;
}

bool Renderer::InitAA()
{
	using namespace Microsoft::WRL;

	// No AA
	TArray<UInt8> ShaderCode;
	if (!ShaderCompiler::CompileFromFile(
		"Shaders/FullscreenVS.hlsl",
		"Main",
		nullptr,
		EShaderStage::ShaderStage_Vertex,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	TSharedRef<VertexShader> VShader = RenderingAPI::CreateVertexShader(ShaderCode);
	if (!VShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		VShader->SetName("Fullscreen VertexShader");
	}

	if (!ShaderCompiler::CompileFromFile(
		"Shaders/PostProcessPS.hlsl",
		"Main",
		nullptr,
		EShaderStage::ShaderStage_Pixel,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	TSharedRef<PixelShader> PShader = RenderingAPI::CreatePixelShader(ShaderCode);
	if (!PShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		PShader->SetName("PostProcess PixelShader");
	}

	DepthStencilStateCreateInfo DepthStencilStateInfo;
	DepthStencilStateInfo.DepthFunc			= EComparisonFunc::ComparisonFunc_Always;
	DepthStencilStateInfo.DepthEnable		= false;
	DepthStencilStateInfo.DepthWriteMask	= EDepthWriteMask::DepthWriteMask_Zero;

	TSharedRef<DepthStencilState> DepthStencilState = RenderingAPI::CreateDepthStencilState(DepthStencilStateInfo);
	if (!DepthStencilState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		DepthStencilState->SetName("PostProcess DepthStencilState");
	}

	RasterizerStateCreateInfo RasterizerStateInfo;
	RasterizerStateInfo.CullMode = ECullMode::CullMode_None;

	TSharedRef<RasterizerState> RasterizerState = RenderingAPI::CreateRasterizerState(RasterizerStateInfo);
	if (!RasterizerState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		RasterizerState->SetName("PostProcess RasterizerState");
	}

	BlendStateCreateInfo BlendStateInfo;
	BlendStateInfo.IndependentBlendEnable		= false;
	BlendStateInfo.RenderTarget[0].BlendEnable	= false;

	TSharedRef<BlendState> BlendState = RenderingAPI::CreateBlendState(BlendStateInfo);
	if (!BlendState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		BlendState->SetName("PostProcess BlendState");
	}

	GraphicsPipelineStateCreateInfo PSOProperties;
	PSOProperties.InputLayoutState	= nullptr;
	PSOProperties.BlendState		= BlendState.Get();
	PSOProperties.DepthStencilState	= DepthStencilState.Get();
	PSOProperties.RasterizerState	= RasterizerState.Get();
	PSOProperties.ShaderState.VertexShader	= VShader.Get();
	PSOProperties.ShaderState.PixelShader	= PShader.Get();
	PSOProperties.PrimitiveTopologyType		= EPrimitiveTopologyType::PrimitiveTopologyType_Triangle;
	PSOProperties.PipelineFormats.RenderTargetFormats[0]	= EFormat::Format_R8G8B8A8_Unorm;
	PSOProperties.PipelineFormats.NumRenderTargets			= 1;
	PSOProperties.PipelineFormats.DepthStencilFormat		= EFormat::Format_Unknown;

	PostPSO = RenderingAPI::CreateGraphicsPipelineState(PSOProperties);
	if (!PostPSO)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		PostPSO->SetName("PostProcess PipelineState");
	}

	// FXAA
	if (!ShaderCompiler::CompileFromFile(
		"Shaders/FXAA_PS.hlsl",
		"Main",
		nullptr,
		EShaderStage::ShaderStage_Pixel,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	PShader = RenderingAPI::CreatePixelShader(ShaderCode);
	if (!PShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		PShader->SetName("FXAA PixelShader");
	}

	PSOProperties.ShaderState.PixelShader = PShader.Get();

	PostPSO = RenderingAPI::CreateGraphicsPipelineState(PSOProperties);
	if (!PostPSO)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		PostPSO->SetName("FXAA PipelineState");
	}

	return true;
}

bool Renderer::InitForwardPass()
{
	using namespace Microsoft::WRL;

	// Init PipelineState
	TArray<ShaderDefine> Defines =
	{
		{ "ENABLE_PARALLAX_MAPPING", "1" },
		{ "ENABLE_NORMAL_MAPPING",	 "1" },
	};

	TArray<UInt8> ShaderCode;
	if (!ShaderCompiler::CompileFromFile(
		"Shaders/ForwardPass.hlsl",
		"VSMain",
		&Defines,
		EShaderStage::ShaderStage_Vertex,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	TSharedRef<VertexShader> VShader = RenderingAPI::CreateVertexShader(ShaderCode);
	if (!VShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		VShader->SetName("GeometryPass VertexShader");
	}

	if (!ShaderCompiler::CompileFromFile(
		"Shaders/ForwardPass.hlsl",
		"PSMain",
		&Defines,
		EShaderStage::ShaderStage_Pixel,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	TSharedRef<PixelShader> PShader = RenderingAPI::CreatePixelShader(ShaderCode);
	if (!PShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		PShader->SetName("ForwardPass PixelShader");
	}

	DepthStencilStateCreateInfo DepthStencilStateInfo;
	DepthStencilStateInfo.DepthFunc			= EComparisonFunc::ComparisonFunc_LessEqual;
	DepthStencilStateInfo.DepthEnable		= true;
	DepthStencilStateInfo.DepthWriteMask	= EDepthWriteMask::DepthWriteMask_All;

	TSharedRef<DepthStencilState> DepthStencilState = RenderingAPI::CreateDepthStencilState(DepthStencilStateInfo);
	if (!DepthStencilState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		DepthStencilState->SetName("ForwardPass DepthStencilState");
	}

	RasterizerStateCreateInfo RasterizerStateInfo;
	RasterizerStateInfo.CullMode = ECullMode::CullMode_Back;

	TSharedRef<RasterizerState> RasterizerState = RenderingAPI::CreateRasterizerState(RasterizerStateInfo);
	if (!RasterizerState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		RasterizerState->SetName("ForwardPass RasterizerState");
	}

	BlendStateCreateInfo BlendStateInfo;
	BlendStateInfo.IndependentBlendEnable		= false;
	BlendStateInfo.RenderTarget[0].BlendEnable	= true;

	TSharedRef<BlendState> BlendState = RenderingAPI::CreateBlendState(BlendStateInfo);
	if (!BlendState)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		BlendState->SetName("GeometryPass BlendState");
	}

	GraphicsPipelineStateCreateInfo PSOProperties = { };
	PSOProperties.ShaderState.VertexShader	= VShader.Get();
	PSOProperties.ShaderState.PixelShader	= PShader.Get();
	PSOProperties.InputLayoutState	= StdInputLayout.Get();
	PSOProperties.DepthStencilState = DepthStencilState.Get();
	PSOProperties.BlendState		= BlendState.Get();
	PSOProperties.RasterizerState	= RasterizerState.Get();
	PSOProperties.PipelineFormats.RenderTargetFormats[0]	= FinalTargetFormat;
	PSOProperties.PipelineFormats.NumRenderTargets			= 1;
	PSOProperties.PrimitiveTopologyType = EPrimitiveTopologyType::PrimitiveTopologyType_Triangle;

	ForwardPSO = RenderingAPI::CreateGraphicsPipelineState(PSOProperties);
	if (!ForwardPSO)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		ForwardPSO->SetName("Forward PipelineState");
	}

	return true;
}

bool Renderer::InitSSAO()
{
	using namespace Microsoft::WRL;

	// Init texture
	{
		SSAOBuffer = RenderingAPI::CreateTexture2D(
			nullptr,
			SSAOBufferFormat,
			TextureUsage_RWTexture,
			800,
			600,
			1, 1);
		if (!SSAOBuffer)
		{
			Debug::DebugBreak();
			return false;
		}
		else
		{
			SSAOBuffer->SetName("SSAO Buffer");
		}

		SSAOBufferUAV = RenderingAPI::CreateUnorderedAccessView(
			SSAOBuffer.Get(),
			SSAOBufferFormat,
			0);
		if (!SSAOBufferUAV)
		{
			Debug::DebugBreak();
			return false;
		}
		else
		{
			SSAOBufferUAV->SetName("SSAO Buffer UAV");
		}

		SSAOBufferSRV = RenderingAPI::CreateShaderResourceView(
			SSAOBuffer.Get(),
			SSAOBufferFormat,
			0, 1);
		if (!SSAOBufferSRV)
		{
			Debug::DebugBreak();
			return false;
		}
		else
		{
			SSAOBufferSRV->SetName("SSAO Buffer SRV");
		}
	}

	// Load shader
	TArray<UInt8> ShaderCode;
	if (!ShaderCompiler::CompileFromFile(
		"Shaders/SSAO.hlsl",
		"Main",
		nullptr,
		EShaderStage::ShaderStage_Compute,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	TSharedRef<ComputeShader> CShader = RenderingAPI::CreateComputeShader(ShaderCode);
	if (!CShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		CShader->SetName("SSAO Shader");
	}

	// Init PipelineState
	{
		ComputePipelineStateCreateInfo PSOProperties = { };
		PSOProperties.Shader = CShader.Get();

		SSAOPSO = RenderingAPI::CreateComputePipelineState(PSOProperties);
		if (!SSAOPSO)
		{
			Debug::DebugBreak();
			return false;
		}
		else
		{
			SSAOPSO->SetName("SSAO PipelineState");
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
		SSAONoiseTex = RenderingAPI::CreateTexture2D(
			nullptr,
			EFormat::Format_R16G16_Float,
			TextureUsage_SRV,
			4,
			4,
			1, 1);
		if (!SSAONoiseTex)
		{
			Debug::DebugBreak();
			return false;
		}
		else
		{
			SSAONoiseTex->SetName("SSAO Noise Texture");
		}

		SSAONoiseSRV = RenderingAPI::CreateShaderResourceView(
			SSAONoiseTex.Get(),
			EFormat::Format_R16G16_Float,
			0, 1);
		if (!SSAONoiseSRV)
		{
			Debug::DebugBreak();
			return false;
		}
		else
		{
			SSAOBufferSRV->SetName("SSAO Noise Texture SRV");
		}
	}

	CmdList.Begin();
	CmdList.TransitionTexture(
		SSAOBuffer.Get(), 
		EResourceState::ResourceState_Common, 
		EResourceState::ResourceState_PixelShaderResource);

	CmdList.TransitionTexture(
		SSAONoiseTex.Get(),
		EResourceState::ResourceState_Common,
		EResourceState::ResourceState_CopyDest);

	{
		const UInt32 Stride		= 4 * sizeof(Float16);
		const UInt32 RowPitch	= ((4 * Stride) + (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
		CmdList.UpdateTexture2D(
			SSAONoiseTex.Get(),
			4, 4, 0,
			SSAONoise.Data());
	}

	CmdList.TransitionTexture(
		SSAONoiseTex.Get(),
		EResourceState::ResourceState_CopyDest,
		EResourceState::ResourceState_NonPixelShaderResource);

	CmdList.End();

	// Init samples
	{
		const UInt32 Stride			= sizeof(XMFLOAT3);
		const UInt32 SizeInBytes	= Stride * SSAOKernel.Size();
		ResourceData SSAOSampleData(SSAOKernel.Data());
		SSAOSamples = RenderingAPI::CreateStructuredBuffer(
			&SSAOSampleData,
			SizeInBytes,
			Stride,
			BufferUsage_SRV | BufferUsage_Default);
		if (!SSAOSamples)
		{
			Debug::DebugBreak();
			return false;
		}
		else
		{
			SSAOSamples->SetName("SSAO Samples");
		}
	}

	SSAOSamplesSRV = RenderingAPI::CreateShaderResourceView(
		SSAOSamples.Get(),
		0, SSAOKernel.Size(), 
		sizeof(XMFLOAT3));
	if (!SSAOSamplesSRV)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		SSAOSamplesSRV->SetName("SSAO Samples SRV");
	}

	// Load shader
	if (!ShaderCompiler::CompileFromFile(
		"Shaders/Blur.hlsl",
		"Main",
		nullptr,
		EShaderStage::ShaderStage_Compute,
		EShaderModel::ShaderModel_6_0,
		ShaderCode))
	{
		Debug::DebugBreak();
		return false;
	}

	CShader = RenderingAPI::CreateComputeShader(ShaderCode);
	if (!CShader)
	{
		Debug::DebugBreak();
		return false;
	}
	else
	{
		CShader->SetName("SSAO Blur Shader");
	}

	{
		// Init PipelineState
		ComputePipelineStateCreateInfo PSOProperties = { };
		PSOProperties.Shader = CShader.Get();

		SSAOBlur = RenderingAPI::CreateComputePipelineState(PSOProperties);
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
	//if (DirLightShadowMaps)
	//{
	//	DeferredResources.EmplaceBack(DirLightShadowMaps);
	//}

	//if (VSMDirLightShadowMaps)
	//{
	//	DeferredResources.EmplaceBack(VSMDirLightShadowMaps);
	//}

	DirLightShadowMaps = RenderingAPI::CreateTexture2D(
		nullptr,
		ShadowMapFormat,
		TextureUsage_ShadowMap,
		Renderer::GetGlobalLightSettings().ShadowMapWidth,
		Renderer::GetGlobalLightSettings().ShadowMapHeight,
		1, 1,
		ClearValue(DepthStencilClearValue(1.0f, 0)));
	if (DirLightShadowMaps)
	{
		DirLightShadowMaps->SetName("Directional Light ShadowMaps");

		DirLightShadowMapDSV = RenderingAPI::CreateDepthStencilView(
			DirLightShadowMaps.Get(),
			ShadowMapFormat,
			0);
		if (!DirLightShadowMapDSV)
		{
			Debug::DebugBreak();
		}

#if !ENABLE_VSM
		DirLightShadowMapSRV = RenderingAPI::CreateShaderResourceView(
			DirLightShadowMaps.Get(),
			EFormat::Format_R32_Float,
			0, 1);
		if (!DirLightShadowMapSRV)
		{
			Debug::DebugBreak();
		}
#endif
	}
	else
	{
		return false;
	}

#if ENABLE_VSM
	VSMDirLightShadowMaps = RenderingAPI::CreateTexture2D(
		nullptr,
		EFormat::Format_R32G32_Float,
		TextureUsage_RenderTarget,
		Renderer::GetGlobalLightSettings().ShadowMapWidth,
		Renderer::GetGlobalLightSettings().ShadowMapHeight,
		1, 1,
		ClearValue(ColorClearValue(1.0f, 1.0f, 1.0f, 1.0f)));
	if (VSMDirLightShadowMaps)
	{
		VSMDirLightShadowMaps->SetName("Directional Light VSM");

		VSMDirLightShadowMapRTV = RenderingAPI::CreateRenderTargetView(
			VSMDirLightShadowMaps.Get(),
			EFormat::Format_R32G32_Float,
			0);
		if (!VSMDirLightShadowMapRTV)
		{
			Debug::DebugBreak();
		}

		VSMDirLightShadowMapSRV = RenderingAPI::CreateShaderResourceView(
			VSMDirLightShadowMaps.Get(),
			EFormat::Format_R32G32_Float,
			0, 1);
		if (!VSMDirLightShadowMapSRV)
		{
			Debug::DebugBreak();
		}
	}
	else
	{
		return false;
	}
#endif

	// PointLights
	//if (PointLightShadowMaps)
	//{
	//	DeferredResources.EmplaceBack(PointLightShadowMaps);
	//}

	const UInt16 Size = Renderer::GetGlobalLightSettings().PointLightShadowSize;
	PointLightShadowMaps = RenderingAPI::CreateTextureCube(
		nullptr,
		ShadowMapFormat,
		TextureUsage_ShadowMap,
		Size,
		1, 1,
		ClearValue(DepthStencilClearValue(1.0f, 0)));
	if (PointLightShadowMaps)
	{
		PointLightShadowMaps->SetName("PointLight ShadowMaps");

		PointLightShadowMapsDSVs.Resize(6);
		for (UInt32 i = 0; i < 6; i++)
		{
			PointLightShadowMapsDSVs[i] = RenderingAPI::CreateDepthStencilView(
				PointLightShadowMaps.Get(),
				ShadowMapFormat,
				0, i);
		}

		PointLightShadowMapsSRV = RenderingAPI::CreateShaderResourceView(
			PointLightShadowMaps.Get(),
			EFormat::Format_R32_Float,
			0, 1);
	}
	else
	{
		return false;
	}

	return true;
}

void Renderer::GenerateIrradianceMap(TextureCube* Source, TextureCube* Dest, CommandList& InCmdList)
{
	const UInt32 Size = static_cast<UInt32>(Dest->GetWidth());

	// Create irradiancemap if it is not created
	if (!IrradicanceGenPSO)
	{
		TArray<UInt8> Code;
		if (!ShaderCompiler::CompileFromFile(
			"Shaders/IrradianceGen.hlsl",
			"Main",
			nullptr,
			EShaderStage::ShaderStage_Compute,
			EShaderModel::ShaderModel_6_0,
			Code))
		{
			LOG_ERROR("Failed to compile IrradianceGen Shader");
			Debug::DebugBreak();
		}

		IrradianceGenShader = RenderingAPI::CreateComputeShader(Code);
		if (!IrradianceGenShader)
		{
			LOG_ERROR("Failed to create IrradianceGen Shader");
			Debug::DebugBreak();
		}

		IrradicanceGenPSO = RenderingAPI::CreateComputePipelineState(ComputePipelineStateCreateInfo(IrradianceGenShader.Get()));
		if (!IrradicanceGenPSO)
		{
			LOG_ERROR("Failed to create IrradianceGen PipelineState");
			Debug::DebugBreak();
		}
		else
		{
			IrradicanceGenPSO->SetName("Irradiance Gen PSO");
		}
	}

	InCmdList.TransitionTexture(
		Source, 
		EResourceState::ResourceState_PixelShaderResource, 
		EResourceState::ResourceState_NonPixelShaderResource);
	
	InCmdList.TransitionTexture(
		Dest, 
		EResourceState::ResourceState_Common, 
		EResourceState::ResourceState_NonPixelShaderResource);

	//InCmdList->SetComputeRootSignature(IrradianceGenRootSignature->GetRootSignature());

	//InCmdList->BindGlobalOnlineDescriptorHeaps();
	//InCmdList->SetComputeRootDescriptorTable(SrvDescriptorTable->GetGPUTableStartHandle(), 0);
	//InCmdList->SetComputeRootDescriptorTable(UavDescriptorTable->GetGPUTableStartHandle(), 1);

	InCmdList.BindComputePipelineState(IrradicanceGenPSO.Get());

	InCmdList.Dispatch(Size, Size, 6);

	InCmdList.UnorderedAccessTextureBarrier(Dest);

	InCmdList.TransitionTexture(
		Source, 
		EResourceState::ResourceState_NonPixelShaderResource, 
		EResourceState::ResourceState_PixelShaderResource);
	
	InCmdList.TransitionTexture(
		Dest, 
		EResourceState::ResourceState_NonPixelShaderResource, 
		EResourceState::ResourceState_PixelShaderResource);
}

void Renderer::GenerateSpecularIrradianceMap(TextureCube* Source, TextureCube* Dest, CommandList& InCmdList)
{
	const UInt32 Miplevels = Dest->GetMipLevels();

	if (!SpecIrradicanceGenPSO)
	{
		TArray<UInt8> Code;
		if (!ShaderCompiler::CompileFromFile(
			"Shaders/SpecularIrradianceGen.hlsl",
			"Main",
			nullptr,
			EShaderStage::ShaderStage_Compute,
			EShaderModel::ShaderModel_6_0,
			Code))
		{
			LOG_ERROR("Failed to compile SpecularIrradianceGen Shader");
			Debug::DebugBreak();
		}

		SpecIrradianceGenShader = RenderingAPI::CreateComputeShader(Code);
		if (!SpecIrradianceGenShader)
		{
			LOG_ERROR("Failed to create SpecularIrradianceGen Shader");
			Debug::DebugBreak();
		}

		SpecIrradicanceGenPSO = RenderingAPI::CreateComputePipelineState(ComputePipelineStateCreateInfo(SpecIrradianceGenShader.Get()));
		if (!SpecIrradicanceGenPSO)
		{
			LOG_ERROR("Failed to create SpecularIrradianceGen PipelineState");
			Debug::DebugBreak();
		}
		else
		{
			SpecIrradicanceGenPSO->SetName("Specular Irradiance Gen PSO");
		}
	}

	InCmdList.TransitionTexture(
		Source, 
		EResourceState::ResourceState_PixelShaderResource, 
		EResourceState::ResourceState_NonPixelShaderResource);
	
	InCmdList.TransitionTexture(
		Dest, 
		EResourceState::ResourceState_Common, 
		EResourceState::ResourceState_NonPixelShaderResource);

	//InCommandList->SetComputeRootSignature(SpecIrradianceGenRootSignature->GetRootSignature());

	//InCommandList->BindGlobalOnlineDescriptorHeaps();
	//InCommandList->SetComputeRootDescriptorTable(SrvDescriptorTable->GetGPUTableStartHandle(), 1);

	InCmdList.BindComputePipelineState(SpecIrradicanceGenPSO.Get());

	UInt32 Width	= static_cast<UInt32>(Dest->GetWidth());
	Float Roughness	= 0.0f;
	const Float RoughnessDelta = 1.0f / (Miplevels - 1);
	for (UInt32 Mip = 0; Mip < Miplevels; Mip++)
	{
		//InCommandList->SetComputeRoot32BitConstants(&Roughness, 1, 0, 0);
		//InCommandList->SetComputeRootDescriptorTable(UavDescriptorTable->GetGPUTableHandle(Mip), 2);
		
		InCmdList.Dispatch(Width, Width, 6);
		InCmdList.UnorderedAccessTextureBarrier(Dest);

		Width = std::max<UInt32>(Width / 2, 1U);
		Roughness += RoughnessDelta;
	}

	InCmdList.TransitionTexture(
		Source, 
		EResourceState::ResourceState_NonPixelShaderResource, 
		EResourceState::ResourceState_PixelShaderResource);
	
	InCmdList.TransitionTexture(
		Dest, 
		EResourceState::ResourceState_NonPixelShaderResource, 
		EResourceState::ResourceState_PixelShaderResource);
}

void Renderer::WaitForPendingFrames()
{
	//const UInt64 CurrentFenceValue = FenceValues[CurrentBackBufferIndex];

	//Queue->SignalFence(Fence.Get(), CurrentFenceValue);
	//if (Fence->WaitForValue(CurrentFenceValue))
	//{
	//	FenceValues[CurrentBackBufferIndex]++;
	//}

	//RenderingAPI::Get().GetQueue()->WaitForCompletion();
}
