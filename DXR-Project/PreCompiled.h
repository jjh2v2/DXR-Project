#pragma once
#include <memory>
#include <cassert>
#include <utility>
#include <algorithm>

#define NOMINMAX
#include <dxgi1_6.h>
#include <d3d12.h>

#include <wrl/client.h>

#include <DirectXMath.h>
using namespace DirectX;

#include <imgui.h>

#include "Defines.h"
#include "Types.h"

#include "Debug/Debug.h"

#include "STL/String.h"
#include "STL/TArray.h"

#include "Windows/Windows.h"

#include "Application/Log.h"

#include "Utilities/TUtilities.h"
#include "Utilities/HashUtilities.h"
#include "Utilities/MathUtilities.h"

#include "Core/ClassType.h"
#include "Core/CoreObject.h"
#include "Core/RefCountedObject.h"
#include "Core/TSharedPtr.h"
#include "Core/TSharedRef.h"
#include "Core/TUniquePtr.h"