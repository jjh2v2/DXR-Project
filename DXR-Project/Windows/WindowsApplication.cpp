#include "WindowsApplication.h"
#include "WindowsWindow.h"
#include "WindowsCursor.h"

#include "Application/Input.h"

#include "Application/Generic/GenericApplication.h"

/*
* Static
*/

WindowsApplication* GlobalWindowsApplication = nullptr;

GenericApplication* WindowsApplication::Make()
{
	HINSTANCE hInstance = static_cast<HINSTANCE>(::GetModuleHandle(nullptr));

	GlobalWindowsApplication = DBG_NEW WindowsApplication(hInstance);
	return GlobalWindowsApplication;
}

/*
* Members
*/

WindowsApplication::WindowsApplication(HINSTANCE InInstanceHandle)
	: InstanceHandle(InInstanceHandle)
	, CurrentCursor()
	, Windows()
{
}

WindowsApplication::~WindowsApplication()
{
	GlobalWindowsApplication = nullptr;
}

bool WindowsApplication::Initialize()
{
	if (!RegisterWindowClass())
	{
		return false;
	}

	return true;
}

void WindowsApplication::AddWindow(TSharedRef<WindowsWindow>& Window)
{
	Windows.EmplaceBack(Window);
}

bool WindowsApplication::RegisterWindowClass()
{
	WNDCLASS WindowClass = { };
	WindowClass.hInstance		= InstanceHandle;
	WindowClass.lpszClassName	= "WinClass";
	WindowClass.hbrBackground	= static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
	WindowClass.hCursor			= ::LoadCursor(NULL, IDC_ARROW);
	WindowClass.lpfnWndProc		= WindowsApplication::MessageProc;

	ATOM ClassAtom = ::RegisterClass(&WindowClass);
	if (ClassAtom == 0)
	{
		LOG_ERROR("[WindowsApplication]: FAILED to register WindowClass\n");
		return false;
	}
	else
	{
		return true;
	}
}

TSharedRef<GenericWindow> WindowsApplication::MakeWindow()
{
	TSharedRef<WindowsWindow> Window = DBG_NEW WindowsWindow(this);
	if (Window)
	{
		AddWindow(Window);
		return Window;
	}
	else
	{
		return TSharedRef<GenericWindow>();
	}
}

TSharedRef<GenericCursor> WindowsApplication::MakeCursor()
{
	TSharedRef<WindowsCursor> Cursor = DBG_NEW WindowsCursor(this);
	if (!Cursor)
	{
		return TSharedRef<GenericCursor>();
	}
	else
	{
		return Cursor;
	}
}

ModifierKeyState WindowsApplication::GetModifierKeyState() const
{
	UInt32 ModifierMask = 0;
	if (::GetKeyState(VK_CONTROL) & 0x8000)
	{
		ModifierMask |= EModifierFlag::MODIFIER_FLAG_CTRL;
	}
	if (::GetKeyState(VK_MENU) & 0x8000)
	{
		ModifierMask |= EModifierFlag::MODIFIER_FLAG_ALT;
	}
	if (::GetKeyState(VK_SHIFT) & 0x8000)
	{
		ModifierMask |= EModifierFlag::MODIFIER_FLAG_SHIFT;
	}
	if (::GetKeyState(VK_CAPITAL) & 1)
	{
		ModifierMask |= EModifierFlag::MODIFIER_FLAG_CAPS_LOCK;
	}
	if ((::GetKeyState(VK_LWIN) | ::GetKeyState(VK_RWIN)) & 0x8000)
	{
		ModifierMask |= EModifierFlag::MODIFIER_FLAG_SUPER;
	}
	if (::GetKeyState(VK_NUMLOCK) & 1)
	{
		ModifierMask |= EModifierFlag::MODIFIER_FLAG_NUM_LOCK;
	}

	return ModifierKeyState(ModifierMask);
}

TSharedRef<WindowsWindow> WindowsApplication::GetWindowFromHWND(HWND Window) const
{
	for (const TSharedRef<WindowsWindow>& CurrentWindow : Windows)
	{
		if (CurrentWindow->GetHandle() == Window)
		{
			return CurrentWindow;
		}
	}

	return TSharedRef<WindowsWindow>(nullptr);
}

TSharedRef<GenericWindow> WindowsApplication::GetActiveWindow() const
{
	HWND hActiveWindow = ::GetForegroundWindow();
	return GetWindowFromHWND(hActiveWindow);
}

TSharedRef<GenericWindow> WindowsApplication::GetCapture() const
{
	HWND hCapture = ::GetCapture();
	return GetWindowFromHWND(hCapture);
}

TSharedRef<GenericCursor> WindowsApplication::GetCursor() const
{
	return CurrentCursor;
}

void WindowsApplication::GetCursorPos(TSharedRef<GenericWindow> RelativeWindow, Int32& OutX, Int32& OutY) const
{
	TSharedRef<WindowsWindow> WinRelative = StaticCast<WindowsWindow>(RelativeWindow);
	HWND hRelative = WinRelative->GetHandle();

	POINT CursorPos = { };
	if (::GetCursorPos(&CursorPos))
	{
		if (::ScreenToClient(hRelative, &CursorPos))
		{
			OutX = CursorPos.x;
			OutY = CursorPos.y;
		}
	}
}

bool WindowsApplication::Tick()
{
	MSG Message = { };
	while (::PeekMessage(&Message, 0, 0, 0, PM_REMOVE))
	{
		::TranslateMessage(&Message);
		::DispatchMessage(&Message);

		if (Message.message == WM_QUIT)
		{
			return false;
		}
	}

	return true;
}

void WindowsApplication::SetCursor(TSharedRef<GenericCursor> Cursor)
{
	if (Cursor)
	{
		TSharedRef<WindowsCursor> WinCursor = StaticCast<WindowsCursor>(Cursor);
		CurrentCursor = WinCursor;

		HCURSOR hCursor = WinCursor->GetCursor();
		::SetCursor(hCursor);
	}
	else
	{
		::SetCursor(NULL);
	}
}

void WindowsApplication::SetActiveWindow(TSharedRef<GenericWindow> Window)
{
	TSharedRef<WindowsWindow> WinWindow = StaticCast<WindowsWindow>(Window);
	HWND hActiveWindow = WinWindow->GetHandle();
	if (::IsWindow(hActiveWindow))
	{
		::SetActiveWindow(hActiveWindow);
	}
}

void WindowsApplication::SetCapture(TSharedRef<GenericWindow> CaptureWindow)
{
	if (CaptureWindow)
	{
		TSharedRef<WindowsWindow> WinWindow = StaticCast<WindowsWindow>(CaptureWindow);
		HWND hCapture = WinWindow->GetHandle();
		if (::IsWindow(hCapture))
		{
			::SetCapture(hCapture);
		}
	}
	else
	{
		::ReleaseCapture();
	}
}

void WindowsApplication::SetCursorPos(TSharedRef<GenericWindow> RelativeWindow, Int32 X, Int32 Y)
{
	if (RelativeWindow)
	{
		TSharedRef<WindowsWindow> WinWindow = StaticCast<WindowsWindow>(RelativeWindow);
		HWND hRelative = WinWindow->GetHandle();
	
		POINT CursorPos = { X, Y };
		if (::ClientToScreen(hRelative, &CursorPos))
		{
			::SetCursorPos(CursorPos.x, CursorPos.y);
		}
	}
}

/*
* MessageProc
*/

LRESULT WindowsApplication::ApplicationProc(HWND hWnd, UINT uMessage, WPARAM wParam, LPARAM lParam)
{
	constexpr UInt16 SCAN_CODE_MASK		= 0x01ff;
	constexpr UInt16 BACK_BUTTON_MASK	= 0x0001;

	TSharedRef<WindowsWindow> MessageWindow = GetWindowFromHWND(hWnd);
	switch (uMessage)
	{
		case WM_DESTROY:
		{
			::PostQuitMessage(0);
			return 0;
		}

		case WM_SIZE:
		{
			if (MessageWindow)
			{
				const UInt16 Width	= LOWORD(lParam);
				const UInt16 Height = HIWORD(lParam);

				EventHandler->OnWindowResized(MessageWindow, Width, Height);
			}

			return 0;
		}

		case WM_SYSKEYUP:
		case WM_KEYUP:
		{
			const UInt32	ScanCode	= static_cast<UInt32>(HIWORD(lParam) & SCAN_CODE_MASK);
			const EKey		Key			= Input::ConvertFromScanCode(ScanCode);
			EventHandler->OnKeyReleased(Key, GetModifierKeyState());
			return 0;
		}

		case WM_SYSKEYDOWN:
		case WM_KEYDOWN:
		{
			const UInt32	ScanCode	= static_cast<UInt32>(HIWORD(lParam) & SCAN_CODE_MASK);
			const EKey		Key			= Input::ConvertFromScanCode(ScanCode);
			EventHandler->OnKeyPressed(Key, GetModifierKeyState());
			return 0;
		}

		case WM_SYSCHAR:
		case WM_CHAR:
		{
			const UInt32 Character = static_cast<UInt32>(wParam);
			EventHandler->OnCharacterInput(Character);
			return 0;
		}

		case WM_MOUSEMOVE:
		{
			const Int32 x = GET_X_LPARAM(lParam);
			const Int32 y = GET_Y_LPARAM(lParam);

			EventHandler->OnMouseMove(x, y);
			return 0;
		}

		case WM_LBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_XBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		case WM_MBUTTONDBLCLK:
		case WM_RBUTTONDBLCLK:
		case WM_XBUTTONDBLCLK:
		{
			EMouseButton Button = EMouseButton::MOUSE_BUTTON_UNKNOWN;
			if (uMessage == WM_LBUTTONDOWN || uMessage == WM_LBUTTONDBLCLK)
			{
				Button = EMouseButton::MOUSE_BUTTON_LEFT;
			}
			else if (uMessage == WM_MBUTTONDOWN || uMessage == WM_MBUTTONDBLCLK)
			{
				Button = EMouseButton::MOUSE_BUTTON_MIDDLE;
			}
			else if (uMessage == WM_RBUTTONDOWN || uMessage == WM_RBUTTONDBLCLK)
			{
				Button = EMouseButton::MOUSE_BUTTON_RIGHT;
			}
			else if (GET_XBUTTON_WPARAM(wParam) == BACK_BUTTON_MASK)
			{
				Button = EMouseButton::MOUSE_BUTTON_BACK;
			}
			else
			{
				Button = EMouseButton::MOUSE_BUTTON_FORWARD;
			}

			EventHandler->OnMouseButtonPressed(Button, GetModifierKeyState());
			return 0;
		}

		case WM_LBUTTONUP:
		case WM_MBUTTONUP:
		case WM_RBUTTONUP:
		case WM_XBUTTONUP:
		{
			EMouseButton Button = EMouseButton::MOUSE_BUTTON_UNKNOWN;
			if (uMessage == WM_LBUTTONUP)
			{
				Button = EMouseButton::MOUSE_BUTTON_LEFT;
			}
			else if (uMessage == WM_MBUTTONUP)
			{
				Button = EMouseButton::MOUSE_BUTTON_MIDDLE;
			}
			else if (uMessage == WM_RBUTTONUP)
			{
				Button = EMouseButton::MOUSE_BUTTON_RIGHT;
			}
			else if (GET_XBUTTON_WPARAM(wParam) == BACK_BUTTON_MASK)
			{
				Button = EMouseButton::MOUSE_BUTTON_BACK;
			}
			else
			{
				Button = EMouseButton::MOUSE_BUTTON_FORWARD;
			}

			EventHandler->OnMouseButtonReleased(Button, GetModifierKeyState());
			return 0;
		}

		case WM_MOUSEWHEEL:
		{
			const Float WheelDelta = static_cast<Float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<Float>(WHEEL_DELTA);
			EventHandler->OnMouseScrolled(0.0f, WheelDelta);
			return 0;
		}

		case WM_MOUSEHWHEEL:
		{
			const Float WheelDelta = static_cast<Float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<Float>(WHEEL_DELTA);
			EventHandler->OnMouseScrolled(WheelDelta, 0.0f);
			return 0;
		}

		default:
		{
			return ::DefWindowProc(hWnd, uMessage, wParam, lParam);
		}
	}

}

LRESULT WindowsApplication::MessageProc(HWND hWnd, UINT uMessage, WPARAM wParam, LPARAM lParam)
{
	return GlobalWindowsApplication->ApplicationProc(hWnd, uMessage, wParam, lParam);
}
