#pragma once
#include "RenderingCore.h"

#include "Core/RefCountedObject.h"

#include "Containers/TArray.h"

/*
* PipelineResource
*/

class PipelineResource : public RefCountedObject
{
public: 
	virtual ~PipelineResource() = default;

	virtual void SetName(const std::string& Name)
	{
	}

	virtual VoidPtr GetNativeResource() const
	{
		return nullptr;
	}
};

class Texture;
class Buffer;
class RayTracingGeometry;
class RayTracingScene;

/*
* Resource
*/

class Resource : public PipelineResource
{
public: 
	virtual ~Resource() = default;

	// Casting Functions
	virtual Texture* AsTexture()
	{
		return nullptr;
	}

	virtual const Texture* AsTexture() const
	{
		return nullptr;
	}

	virtual Buffer* AsBuffer()
	{
		return nullptr;
	}

	virtual const Buffer* AsBuffer() const
	{
		return nullptr;
	}

	virtual RayTracingGeometry* AsRayTracingGeometry()
	{
		return nullptr;
	}

	virtual const RayTracingGeometry* AsRayTracingGeometry() const
	{
		return nullptr;
	}

	virtual RayTracingScene* AsRayTracingScene()
	{
		return nullptr;
	}

	virtual const RayTracingScene* AsRayTracingScene() const
	{
		return nullptr;
	}
};