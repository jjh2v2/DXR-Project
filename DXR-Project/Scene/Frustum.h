#pragma once
#include <DirectXMath.h>
using namespace DirectX;

#include "AABB.h"

/*
* Frustum
*/

class Frustum
{
public:
	Frustum();
	Frustum(float ScreenDepth, const XMFLOAT4X4& View, const XMFLOAT4X4& Projection);
	~Frustum() = default;

	void Create(float ScreenDepth, const XMFLOAT4X4& View, const XMFLOAT4X4& Projection);

	bool CheckAABB(const AABB& BoundingBox);

private:
	XMFLOAT4 Planes[6];
};