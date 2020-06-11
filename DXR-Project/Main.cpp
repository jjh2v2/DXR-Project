#include "Windows/WindowsApplication.h"
#include "Windows/WindowsWindow.h"

#include "D3D12/D3D12Device.h"
#include "D3D12/D3D12CommandQueue.h"
#include "D3D12/D3D12DescriptorHeap.h"
#include "D3D12/D3D12SwapChain.h"
#include "D3D12/D3D12CommandAllocator.h"
#include "D3D12/D3D12CommandList.h"
#include "D3D12/D3D12Fence.h"
#include "D3D12/D3D12RayTracer.h"

#include "Application/GenericEventHandler.h"

class A : public GenericEventHandler
{
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR cmdLine, int cmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(cmdLine);
	UNREFERENCED_PARAMETER(cmdShow);

	WindowsApplication* App = WindowsApplication::Create(hInstance);

	D3D12Device*	Device = D3D12Device::Create(true);
	D3D12RayTracer* RayTracer = new D3D12RayTracer(Device);
	App->SetEventHandler(RayTracer);

	WindowsWindow* Window = App->CreateWindow(1280, 720);
	Window->Show();

	D3D12CommandQueue* Queue = new D3D12CommandQueue(Device);
	Queue->Init();

	D3D12DescriptorHeap* RtvHeap = new D3D12DescriptorHeap(Device);
	RtvHeap->Init(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

	D3D12DescriptorHeap* DsvHeap = new D3D12DescriptorHeap(Device);
	DsvHeap->Init(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

	D3D12SwapChain* SwapChain = new D3D12SwapChain(Device);
	SwapChain->Init(Window, Queue);

	const Uint32 BackBufferCount = SwapChain->GetSurfaceCount();
	std::vector<D3D12CommandAllocator*> Allocators(BackBufferCount);
	
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> BackBufferHandles(BackBufferCount);
	for (Uint32 i = 0; i < BackBufferCount; i++)
	{
		Allocators[i] = new D3D12CommandAllocator(Device);
		Allocators[i]->Init(D3D12_COMMAND_LIST_TYPE_DIRECT);

		D3D12_RENDER_TARGET_VIEW_DESC RtvDesc = { };
		RtvDesc.ViewDimension			= D3D12_RTV_DIMENSION_TEXTURE2D;
		RtvDesc.Format					= SwapChain->GetSurfaceFormat();
		RtvDesc.Texture2D.MipSlice		= 0;
		RtvDesc.Texture2D.PlaneSlice	= 0;

		D3D12_CPU_DESCRIPTOR_HANDLE Handle = RtvHeap->GetCPUDescriptorHandleAt(i);
		Device->GetDevice()->CreateRenderTargetView(SwapChain->GetSurfaceResource(i), &RtvDesc, Handle);

		BackBufferHandles[i] = Handle;
	}

	D3D12CommandList* CommandList = new D3D12CommandList(Device);
	CommandList->Init(D3D12_COMMAND_LIST_TYPE_DIRECT, Allocators[0], nullptr);

	D3D12Fence* Fence = new D3D12Fence(Device);
	Fence->Init(0);

	// Init raytracer last
	RayTracer->Init(CommandList, Queue);

	// Run-Loop
	Uint32 BackBufferIndex = SwapChain->GetCurrentBackBufferIndex();
	std::vector<Uint64> FenceValues(BackBufferCount);

	bool IsRunning = true;
	while (IsRunning)
	{
		IsRunning = App->Tick();

		// Render-Loop
		Allocators[BackBufferIndex]->Reset();
		CommandList->Reset(Allocators[BackBufferIndex]);
		
		RayTracer->Render(SwapChain->GetSurfaceResource(BackBufferIndex), CommandList);

		CommandList->Close();

		// Execute
		Queue->ExecuteCommandList(CommandList);

		// Present
		SwapChain->Present(1);
		
		// Wait for next frame
		const Uint64 CurrentFenceValue = FenceValues[BackBufferIndex];
		Queue->SignalFence(Fence, CurrentFenceValue);
		
		BackBufferIndex = SwapChain->GetCurrentBackBufferIndex();
		if (Fence->WaitForValue(CurrentFenceValue))
		{
			FenceValues[BackBufferIndex] = CurrentFenceValue + 1;
		}
	}

	for (D3D12CommandAllocator* Allocator : Allocators)
	{
		delete Allocator;
	}

	delete RayTracer;
	delete Fence;
	delete CommandList;
	delete SwapChain;
	delete DsvHeap;
	delete RtvHeap;
	delete Queue;

	return 0;
}