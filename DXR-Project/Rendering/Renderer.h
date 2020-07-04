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

class D3D12Texture;

class Renderer
{
public:
	Renderer();
	~Renderer();
	
	void Tick();
	
	void OnResize(Int32 Width, Int32 Height);
	void OnMouseMove(Int32 X, Int32 Y);
	void OnKeyPressed(EKey KeyCode);

	FORCEINLINE std::shared_ptr<D3D12Device> GetDevice() const
	{
		return Device;
	}

	static Renderer* Make(std::shared_ptr<WindowsWindow> RendererWindow);
	static Renderer* Get();
	
private:
	bool Initialize(std::shared_ptr<WindowsWindow> RendererWindow);

	bool CreateResultTexture();

	void WaitForPendingFrames();

	void TraceRays(D3D12Texture* BackBuffer, D3D12CommandList* CommandList);

private:
	std::shared_ptr<D3D12Device>		Device;
	std::shared_ptr<D3D12CommandQueue>	Queue;
	std::shared_ptr<D3D12CommandQueue>	ComputeQueue;
	std::shared_ptr<D3D12CommandList>	CommandList;
	std::shared_ptr<D3D12Fence>			Fence;
	std::shared_ptr<D3D12SwapChain>		SwapChain;

	std::vector<std::shared_ptr<D3D12CommandAllocator>> CommandAllocators;

	std::shared_ptr<D3D12Texture> Panorama;
	std::shared_ptr<D3D12Texture> ResultTexture;

	std::vector<Uint64>	FenceValues;
	Uint32				CurrentBackBufferIndex = 0;

	std::shared_ptr<D3D12Buffer>					CameraBuffer;
	std::shared_ptr<D3D12RayTracingScene>			Scene;
	std::shared_ptr<D3D12RayTracingPipelineState>	PipelineState;
	std::shared_ptr<D3D12ConstantBufferView> CameraBufferView;	

	std::vector<std::shared_ptr<D3D12UploadStack>> UploadBuffers;

	Camera SceneCamera;

	bool IsCameraAcive = false;

	static std::unique_ptr<Renderer> RendererInstance;
};