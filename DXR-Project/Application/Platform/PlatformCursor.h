#pragma once
#ifdef WIN32
	#include "Windows/WindowsCursor.h"
	typedef WindowsCursor PlatformCursor;
#else
	#include "Application/Generic/GenericCursor.h"
	typedef GenericCursor PlatformCursor;
#endif