#include "Application.h"
#include "InputManager.h"

#include "Rendering/Renderer.h"
#include "Rendering/GuiContext.h"

std::shared_ptr<Application> Application::Instance = nullptr;

Application::Application()
{
}

Application::~Application()
{
	SAFEDELETE(PlatformApplication);
}

bool Application::Tick()
{
	return PlatformApplication->Tick();
}

void Application::SetCursor(std::shared_ptr<WindowsCursor> Cursor)
{
	PlatformApplication->SetCursor(Cursor);
}

void Application::SetActiveWindow(std::shared_ptr<WindowsWindow>& ActiveWindow)
{
	PlatformApplication->SetActiveWindow(ActiveWindow);
}

void Application::SetCapture(std::shared_ptr<WindowsWindow> Capture)
{
	PlatformApplication->SetCapture(Capture);
}

void Application::SetCursorPos(std::shared_ptr<WindowsWindow>& RelativeWindow, Int32 X, Int32 Y)
{
	PlatformApplication->SetCursorPos(RelativeWindow, X, Y);
}

ModifierKeyState Application::GetModifierKeyState() const
{
	return PlatformApplication->GetModifierKeyState();
}

std::shared_ptr<WindowsWindow> Application::GetWindow() const
{
	return Window;
}

std::shared_ptr<WindowsWindow> Application::GetActiveWindow() const
{
	return PlatformApplication->GetActiveWindow();
}

std::shared_ptr<WindowsWindow> Application::GetCapture() const
{
	return PlatformApplication->GetCapture();
}

void Application::GetCursorPos(std::shared_ptr<WindowsWindow>& RelativeWindow, Int32& OutX, Int32& OutY) const
{
	PlatformApplication->GetCursorPos(RelativeWindow, OutX, OutY);
}

Application* Application::Make()
{
	Instance = std::shared_ptr<Application>(new Application());
	if (Instance->Initialize())
	{
		return Instance.get();
	}
	else
	{
		return nullptr;
	}
}

Application* Application::Get()
{
	return Instance.get();
}

void Application::OnWindowResized(std::shared_ptr<WindowsWindow>& InWindow, Uint16 Width, Uint16 Height)
{
	UNREFERENCED_PARAMETER(InWindow);

	if (Renderer::Get())
	{
		Renderer::Get()->OnResize(Width, Height);
	}
}

void Application::OnKeyReleased(EKey KeyCode, const ModifierKeyState& ModierKeyState)
{
	UNREFERENCED_PARAMETER(ModierKeyState);

	InputManager::Get().RegisterKeyUp(KeyCode);

	if (GuiContext::Get())
	{
		GuiContext::Get()->OnKeyReleased(KeyCode);
	}
}

void Application::OnKeyPressed(EKey KeyCode, const ModifierKeyState& ModierKeyState)
{
	UNREFERENCED_PARAMETER(ModierKeyState);

	InputManager::Get().RegisterKeyDown(KeyCode);

	if (Renderer::Get())
	{
		Renderer::Get()->OnKeyPressed(KeyCode);
	}

	if (GuiContext::Get())
	{
		GuiContext::Get()->OnKeyPressed(KeyCode);
	}
}

void Application::OnMouseMove(Int32 X, Int32 Y)
{
	if (Renderer::Get())
	{
		Renderer::Get()->OnMouseMove(X, Y);
	}
}

void Application::OnMouseButtonReleased(EMouseButton Button, const ModifierKeyState& ModierKeyState)
{
	UNREFERENCED_PARAMETER(ModierKeyState);

	std::shared_ptr<WindowsWindow> CaptureWindow = GetCapture();
	if (CaptureWindow)
	{
		SetCapture(nullptr);
	}

	if (GuiContext::Get())
	{
		GuiContext::Get()->OnMouseButtonReleased(Button);
	}
}

void Application::OnMouseButtonPressed(EMouseButton Button, const ModifierKeyState& ModierKeyState)
{
	UNREFERENCED_PARAMETER(ModierKeyState);

	std::shared_ptr<WindowsWindow> CaptureWindow = GetCapture();
	if (!CaptureWindow)
	{
		std::shared_ptr<WindowsWindow> ActiveWindow = GetActiveWindow();
		SetCapture(ActiveWindow);
	}

	if (GuiContext::Get())
	{
		GuiContext::Get()->OnMouseButtonPressed(Button);
	}
}

void Application::OnMouseScrolled(Float32 HorizontalDelta, Float32 VerticalDelta)
{
	if (GuiContext::Get())
	{
		GuiContext::Get()->OnMouseScrolled(HorizontalDelta, VerticalDelta);
	}
}

void Application::OnCharacterInput(Uint32 Character)
{
	if (GuiContext::Get())
	{
		GuiContext::Get()->OnCharacterInput(Character);
	}
}

bool Application::Initialize()
{
	// Application
	HINSTANCE InstanceHandle = static_cast<HINSTANCE>(GetModuleHandle(NULL));
	PlatformApplication = WindowsApplication::Make(InstanceHandle);
	if (PlatformApplication)
	{
		PlatformApplication->SetEventHandler(std::shared_ptr<EventHandler>(Instance));
	}
	else
	{
		return false;
	}

	// Window
	WindowProperties WindowProperties;
	WindowProperties.Title	= "DXR";
	WindowProperties.Width	= 1280;
	WindowProperties.Height = 720;
	WindowProperties.Style	=	WINDOW_STYLE_FLAG_TITLED | WINDOW_STYLE_FLAG_CLOSABLE | 
								WINDOW_STYLE_FLAG_MINIMIZABLE | WINDOW_STYLE_FLAG_MAXIMIZABLE |
								WINDOW_STYLE_FLAG_RESIZEABLE;

	Window = PlatformApplication->MakeWindow(WindowProperties);
	if (Window)
	{
		Window->Show();
	}
	else
	{
		return false;
	}

	return true;
}
