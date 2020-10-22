#pragma once
#include "Application/Log.h"

#ifndef NOMINMAX
	#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN 1
#endif

#include <Windows.h>
#include <windowsx.h>

#ifdef CreateWindow
	#undef CreateWindow
#endif