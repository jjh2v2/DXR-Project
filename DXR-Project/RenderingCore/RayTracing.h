#pragma once
#include "Resource.h"

/*
* RayTracing Geometry (Bottom Level Acceleration Structure)
*/

class RayTracingGeometry : public Resource
{
public:
	RayTracingGeometry()	= default;
	~RayTracingGeometry()	= default;

	virtual RayTracingGeometry* AsRayTracingGeometry() override
	{
		return this;
	}

	virtual const RayTracingGeometry* AsRayTracingGeometry() const override
	{
		return this;
	}
};

/*
* RayTracing Scene (Top Level Acceleration Structure)
*/

class RayTracingScene : public Resource
{
public:
	RayTracingScene()	= default;
	~RayTracingScene()	= default;

	virtual RayTracingScene* AsRayTracingScene() override
	{
		return this;
	}

	virtual const RayTracingScene* AsRayTracingScene() const override
	{
		return this;
	}
};

/*
* RayTracingGeometryInstance
*/

struct RayTracingGeometryInstance
{
	TSharedRef<RayTracingGeometry> Geometry;
	XMFLOAT3X4 TransformMatrix;
};