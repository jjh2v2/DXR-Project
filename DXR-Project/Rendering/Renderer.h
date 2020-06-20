#pragma once
#include "D3D12/D3D12Device.h"
#include "D3D12/D3D12CommandQueue.h"
#include "D3D12/D3D12CommandAllocator.h"
#include "D3D12/D3D12CommandList.h"
#include "D3D12/D3D12DescriptorHeap.h"
#include "D3D12/D3D12Fence.h"
#include "D3D12/D3D12SwapChain.h"
#include "D3D12/D3D12Buffer.h"
#include "D3D12/D3D12RayTracingScene.h"
#include "D3D12/D3D12RayTracingPipelineState.h"
#include "D3D12/D3D12UploadStack.h"

#include "Windows/WindowsWindow.h"

#include "Application/InputCodes.h"

#include <memory>
#include <vector>

#include "Camera.h"

class Renderer
{
public:
	Renderer();
	~Renderer();
	
	void Tick();
	
	void OnResize(Int32 NewWidth, Int32 NewHeight);
	void OnMouseMove(Int32 X, Int32 Y);
	void OnKeyDown(EKey KeyCode);

	static Renderer* Create(std::shared_ptr<WindowsWindow> RendererWindow);
	static Renderer* Get();
	
private:
	bool Initialize(std::shared_ptr<WindowsWindow> RendererWindow);

	bool CreateResultTexture();
	void CreateRenderTargetViews();

	void WaitForPendingFrames();

private:
	std::shared_ptr<D3D12Device>			Device;
	std::shared_ptr<D3D12CommandQueue>		Queue;
	std::shared_ptr<D3D12CommandList>		CommandList;
	std::shared_ptr<D3D12DescriptorHeap>	RenderTargetHeap;
	std::shared_ptr<D3D12DescriptorHeap>	DepthStencilHeap;
	std::shared_ptr<D3D12Fence>				Fence;
	std::shared_ptr<D3D12SwapChain>			SwapChain;

	std::vector<std::shared_ptr<D3D12CommandAllocator>> CommandAllocators;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>			BackBufferHandles;
	
	std::vector<Uint64>	FenceValues;
	Uint32				CurrentBackBufferIndex = 0;

	class D3D12Texture* ResultTexture;

	D3D12_CPU_DESCRIPTOR_HANDLE VertexBufferCPUHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE IndexBufferCPUHandle;

	std::shared_ptr<D3D12Buffer>					CameraBuffer;
	std::shared_ptr<D3D12RayTracingScene>			Scene;
	std::shared_ptr<D3D12RayTracingPipelineState>	PipelineState;

	std::vector<std::shared_ptr<D3D12UploadStack>> UploadBuffers;

	Camera SceneCamera;

	D3D12_GPU_DESCRIPTOR_HANDLE CameraBufferGPUHandle;

	D3D12_CPU_DESCRIPTOR_HANDLE CameraBufferCPUHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE OutputCPUHandle;

	bool IsCameraAcive = false;

	static std::unique_ptr<Renderer> RendererInstance;
};