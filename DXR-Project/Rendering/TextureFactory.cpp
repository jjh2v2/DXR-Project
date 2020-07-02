#include "TextureFactory.h"

#include "D3D12/D3D12Texture.h"
#include "D3D12/D3D12Buffer.h"
#include "D3D12/D3D12Device.h"
#include "D3D12/D3D12CommandList.h"
#include "D3D12/D3D12CommandAllocator.h"
#include "D3D12/D3D12CommandQueue.h"
#include "D3D12/D3D12Fence.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

D3D12Texture* TextureFactory::LoadFromFile(D3D12Device* Device, const std::string& Filepath)
{
	Int32 Width			= 0;
	Int32 Height		= 0;
	Int32 ChannelCount	= 0;
	std::unique_ptr<Byte> Pixels = std::unique_ptr<Byte>(stbi_load(Filepath.c_str(), &Width, &Height, &ChannelCount, 4));
	if (!Pixels)
	{
		::OutputDebugString(("[TextureFactory]: Failed to load image '" + Filepath + "'\n").c_str());
		return nullptr;
	}
	else
	{
		::OutputDebugString(("[TextureFactory]: Loaded image '" + Filepath + "'\n").c_str());
	}

	return LoadFromMemory(Device, Pixels.get(), Width, Height);
}

D3D12Texture* TextureFactory::LoadFromMemory(D3D12Device* Device, const Byte* Pixels, Uint32 Width, Uint32 Height)
{
	TextureProperties TextureProps = { };
	TextureProps.Flags		= D3D12_RESOURCE_FLAG_NONE;
	TextureProps.Width		= Width;
	TextureProps.Height		= Height;
	TextureProps.Format		= DXGI_FORMAT_R8G8B8A8_UNORM;
	TextureProps.MemoryType = EMemoryType::MEMORY_TYPE_DEFAULT;

	std::unique_ptr<D3D12Texture> Texture = std::unique_ptr<D3D12Texture>(new D3D12Texture(Device));
	if (!Texture->Initialize(TextureProps))
	{
		return nullptr;
	}

	Uint32 UploadPitch	= (Width * 4 + (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
	Uint32 UploadSize	= Height * UploadPitch;

	BufferProperties UploadBufferProps = { };
	UploadBufferProps.Name			= "UploadBuffer";
	UploadBufferProps.Flags			= D3D12_RESOURCE_FLAG_NONE;
	UploadBufferProps.InitalState	= D3D12_RESOURCE_STATE_GENERIC_READ;
	UploadBufferProps.SizeInBytes	= UploadSize;
	UploadBufferProps.MemoryType	= EMemoryType::MEMORY_TYPE_UPLOAD;

	std::unique_ptr<D3D12Buffer> UploadBuffer = std::unique_ptr<D3D12Buffer>(new D3D12Buffer(Device));
	if (UploadBuffer->Initialize(UploadBufferProps))
	{
		void* Memory = UploadBuffer->Map();
		for (Int32 Y = 0; Y < Height; Y++)
		{
			memcpy(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(Memory) + Y * UploadPitch), Pixels + Y * Width * 4, Width * 4);
		}
		UploadBuffer->Unmap();
	}
	else
	{
		return nullptr;
	}

	std::unique_ptr<D3D12Fence> Fence = std::unique_ptr<D3D12Fence>(new D3D12Fence(Device));
	if (!Fence->Initialize(0))
	{
		return nullptr;
	}

	std::unique_ptr<D3D12CommandAllocator> Allocator = std::unique_ptr<D3D12CommandAllocator>(new D3D12CommandAllocator(Device));
	if (!Allocator->Initialize(D3D12_COMMAND_LIST_TYPE_DIRECT))
	{
		return nullptr;
	}

	std::unique_ptr<D3D12CommandList> CommandList = std::unique_ptr<D3D12CommandList>(new D3D12CommandList(Device));
	if (!CommandList->Initialize(D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.get(), nullptr))
	{
		return nullptr;
	}

	std::unique_ptr<D3D12CommandQueue> Queue = std::unique_ptr<D3D12CommandQueue>(new D3D12CommandQueue(Device));
	if (!Queue->Initialize(D3D12_COMMAND_LIST_TYPE_DIRECT))
	{
		return nullptr;
	}

	Allocator->Reset();
	CommandList->Reset(Allocator.get());

	CommandList->TransitionBarrier(Texture.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

	D3D12_TEXTURE_COPY_LOCATION SourceLocation = {};
	SourceLocation.pResource							= UploadBuffer->GetResource();
	SourceLocation.Type									= D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	SourceLocation.PlacedFootprint.Footprint.Format		= DXGI_FORMAT_R8G8B8A8_UNORM;
	SourceLocation.PlacedFootprint.Footprint.Width		= Width;
	SourceLocation.PlacedFootprint.Footprint.Height		= Height;
	SourceLocation.PlacedFootprint.Footprint.Depth		= 1;
	SourceLocation.PlacedFootprint.Footprint.RowPitch	= UploadPitch;

	D3D12_TEXTURE_COPY_LOCATION DestLocation = {};
	DestLocation.pResource			= Texture->GetResource();
	DestLocation.Type				= D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	DestLocation.SubresourceIndex	= 0;

	CommandList->CopyTextureRegion(&DestLocation, 0, 0, 0, &SourceLocation, nullptr);
	CommandList->TransitionBarrier(Texture.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	CommandList->Close();

	Queue->ExecuteCommandList(CommandList.get());
	Queue->SignalFence(Fence.get(), 1);

	Fence->WaitForValue(1);

	// Resource View
	D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc = { };
	SrvDesc.Format						= DXGI_FORMAT_R8G8B8A8_UNORM;
	SrvDesc.ViewDimension				= D3D12_SRV_DIMENSION_TEXTURE2D;
	SrvDesc.Shader4ComponentMapping		= D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SrvDesc.Texture2D.MipLevels			= 1;
	SrvDesc.Texture2D.MostDetailedMip	= 0;

	Texture->SetShaderResourceView(std::make_shared<D3D12ShaderResourceView>(Device, Texture->GetResource(), &SrvDesc));

	return Texture.release();
}
