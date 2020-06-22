#include "WindowsApplication.h"
#include "WindowsWindow.h"
#include "WindowsCursor.h"

#include "Application/EventHandler.h"
#include "Application/InputManager.h"

/*
* Static
*/

WindowsApplication* GlobalWindowsApplication = nullptr;

WindowsApplication* WindowsApplication::Create(HINSTANCE hInstance)
{
	GlobalWindowsApplication = new WindowsApplication(hInstance);
	if (GlobalWindowsApplication->Create())
	{
		return GlobalWindowsApplication;
	}
	else
	{
		return nullptr;
	}
}

/*
* Members
*/

WindowsApplication::WindowsApplication(HINSTANCE hInstance)
	: hInstance(hInstance)
{
}

WindowsApplication::~WindowsApplication()
{
}

bool WindowsApplication::Create()
{
	if (!RegisterWindowClass())
	{
		return false;
	}

	return true;
}

void WindowsApplication::AddWindow(std::shared_ptr<WindowsWindow>& InWindow)
{
	Windows.emplace_back(InWindow);
}

bool WindowsApplication::RegisterWindowClass()
{
	WNDCLASS WindowClass = { };
	WindowClass.hInstance		= hInstance;
	WindowClass.lpszClassName	= "WinClass";
	WindowClass.hbrBackground	= static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
	WindowClass.hCursor			= ::LoadCursor(NULL, IDC_ARROW);
	WindowClass.lpfnWndProc		= WindowsApplication::MessageProc;

	ATOM ClassAtom = ::RegisterClass(&WindowClass);
	if (ClassAtom == 0)
	{
		::OutputDebugString("[WindowsApplication]: Failed to register WindowClass\n");
		return false;
	}
	else
	{
		return true;
	}
}

std::shared_ptr<WindowsWindow> WindowsApplication::CreateWindow(Uint16 InWidth, Uint16 InHeight)
{
	std::shared_ptr<WindowsWindow> NewWindow = std::shared_ptr<WindowsWindow>(new WindowsWindow());
	if (NewWindow->Initialize(this, InWidth, InHeight))
	{
		AddWindow(NewWindow);
		return NewWindow;
	}
	else
	{
		return std::shared_ptr<WindowsWindow>(nullptr);
	}
}

ModifierKeyState WindowsApplication::GetModifierKeyState() const
{
	Uint32 ModifierMask = 0;
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

std::shared_ptr<WindowsWindow> WindowsApplication::GetWindowFromHWND(HWND hWindow) const
{
	for (const std::shared_ptr<WindowsWindow>& CurrentWindow : Windows)
	{
		if (CurrentWindow->GetHandle() == hWindow)
		{
			return CurrentWindow;
		}
	}

	return std::shared_ptr<WindowsWindow>(nullptr);
}

std::shared_ptr<WindowsWindow> WindowsApplication::GetActiveWindow() const
{
	HWND hActiveWindow = ::GetForegroundWindow();
	return GetWindowFromHWND(hActiveWindow);
}

std::shared_ptr<WindowsWindow> WindowsApplication::GetCapture() const
{
	HWND hCapture = ::GetCapture();
	return GetWindowFromHWND(hCapture);
}

void WindowsApplication::GetCursorPos(std::shared_ptr<WindowsWindow>& InRelativeWindow, Int32& OutX, Int32& OutY) const
{
	HWND hRelative = InRelativeWindow->GetHandle();

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
	MSG msg = { };
	while (::PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
	{
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);

		if (msg.message == WM_QUIT)
		{
			return false;
		}
	}

	return true;
}

void WindowsApplication::SetCursor(std::shared_ptr<WindowsCursor> InCursor)
{
	if (InCursor)
	{
		HCURSOR hCursor = InCursor->GetCursor();
		::SetCursor(hCursor);
	}
	else
	{
		::SetCursor(NULL);
	}
}

void WindowsApplication::SetActiveWindow(std::shared_ptr<WindowsWindow>& InActiveWindow)
{
	HWND hActiveWindow = InActiveWindow->GetHandle();
	if (::IsWindow(hActiveWindow))
	{
		::SetActiveWindow(hActiveWindow);
	}
}

void WindowsApplication::SetCapture(std::shared_ptr<WindowsWindow> InCaptureWindow)
{
	if (InCaptureWindow)
	{
		HWND hCapture = InCaptureWindow->GetHandle();
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

void WindowsApplication::SetCursorPos(std::shared_ptr<WindowsWindow>& InRelativeWindow, Int32 InX, Int32 InY)
{
	HWND hRelative = InRelativeWindow->GetHandle();
	
	POINT CursorPos = { InX, InY };
	if (::ClientToScreen(hRelative, &CursorPos))
	{
		::SetCursorPos(CursorPos.x, CursorPos.y);
	}
}

/*
* MessageProc
*/

LRESULT WindowsApplication::ApplicationProc(HWND hWnd, UINT uMessage, WPARAM wParam, LPARAM lParam)
{
	constexpr Uint16 SCAN_CODE_MASK		= 0x01ff;
	constexpr Uint16 BACK_BUTTON_MASK	= 0x0001;

	std::shared_ptr<WindowsWindow> MessageWindow = GetWindowFromHWND(hWnd);
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
				const Uint16 Width	= LOWORD(lParam);
				const Uint16 Height = HIWORD(lParam);

				MessageEventHandler->OnWindowResized(MessageWindow, Width, Height);
			}

			return 0;
		}

		case WM_SYSKEYUP:
		case WM_KEYUP:
		{
			const Uint32	ScanCode	= static_cast<Uint32>(HIWORD(lParam) & SCAN_CODE_MASK);
			const EKey		Key			= InputManager::Get().ConvertFromKeyCode(ScanCode);
			MessageEventHandler->OnKeyReleased(Key, GetModifierKeyState());
			return 0;
		}

		case WM_SYSKEYDOWN:
		case WM_KEYDOWN:
		{
			const Uint32	ScanCode	= static_cast<Uint32>(HIWORD(lParam) & SCAN_CODE_MASK);
			const EKey		Key			= InputManager::Get().ConvertFromKeyCode(ScanCode);
			MessageEventHandler->OnKeyPressed(Key, GetModifierKeyState());
			return 0;
		}

		case WM_SYSCHAR:
		case WM_CHAR:
		{
			const Uint32 Character = static_cast<Uint32>(wParam);
			MessageEventHandler->OnCharacterInput(Character);
			return 0;
		}

		case WM_MOUSEMOVE:
		{
			const Int32 x = GET_X_LPARAM(lParam);
			const Int32 y = GET_Y_LPARAM(lParam);

			MessageEventHandler->OnMouseMove(x, y);
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

			MessageEventHandler->OnMouseButtonPressed(Button, GetModifierKeyState());
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

			MessageEventHandler->OnMouseButtonReleased(Button, GetModifierKeyState());
			return 0;
		}

		case WM_MOUSEWHEEL:
		{
			const Float32 WheelDelta = static_cast<Float32>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<Float32>(WHEEL_DELTA);
			MessageEventHandler->OnMouseScrolled(0.0f, WheelDelta);
			return 0;
		}

		case WM_MOUSEHWHEEL:
		{
			const Float32 WheelDelta = static_cast<Float32>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<Float32>(WHEEL_DELTA);
			MessageEventHandler->OnMouseScrolled(WheelDelta, 0.0f);
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
