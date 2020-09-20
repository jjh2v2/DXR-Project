#include "Application.h"
#include "Input.h"

#include "Events/EventQueue.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"
#include "Events/WindowEvent.h"

#include "Platform/PlatformApplication.h"

// TODO: Mayebe should handle this in a different way
#include "EngineLoop.h"

#include "Rendering/Renderer.h"
#include "Rendering/DebugUI.h"
#include "Rendering/TextureFactory.h"

#include "Scene/Scene.h"
#include "Scene/PointLight.h"
#include "Scene/DirectionalLight.h"
#include "Scene/Components/MeshComponent.h"

TSharedPtr<Application> Application::Instance = nullptr;

Application::Application()
{
}

Application::~Application()
{
}

void Application::Release()
{
	DebugUI::Release();

	SAFEDELETE(CurrentScene);
	SAFEDELETE(PlatformApplication);
}

void Application::Tick()
{
	// Tick OS
	if (!PlatformApplication->Tick())
	{
		EngineLoop::Exit();
	}
	
	// Run app
	const Float32 Delta = static_cast<Float32>(EngineLoop::GetDeltaTime().AsSeconds());
	const Float32 RotationSpeed = 45.0f;

	Float32 Speed = 1.0f;
	if (Input::IsKeyDown(EKey::KEY_LEFT_SHIFT))
	{
		Speed = 4.0f;
	}

	if (Input::IsKeyDown(EKey::KEY_RIGHT))
	{
		CurrentCamera->Rotate(0.0f, XMConvertToRadians(RotationSpeed * Delta), 0.0f);
	}
	else if (Input::IsKeyDown(EKey::KEY_LEFT))
	{
		CurrentCamera->Rotate(0.0f, XMConvertToRadians(-RotationSpeed * Delta), 0.0f);
	}

	if (Input::IsKeyDown(EKey::KEY_UP))
	{
		CurrentCamera->Rotate(XMConvertToRadians(-RotationSpeed * Delta), 0.0f, 0.0f);
	}
	else if (Input::IsKeyDown(EKey::KEY_DOWN))
	{
		CurrentCamera->Rotate(XMConvertToRadians(RotationSpeed * Delta), 0.0f, 0.0f);
	}

	if (Input::IsKeyDown(EKey::KEY_W))
	{
		CurrentCamera->Move(0.0f, 0.0f, Speed * Delta);
	}
	else if (Input::IsKeyDown(EKey::KEY_S))
	{
		CurrentCamera->Move(0.0f, 0.0f, -Speed * Delta);
	}

	if (Input::IsKeyDown(EKey::KEY_A))
	{
		CurrentCamera->Move(Speed * Delta, 0.0f, 0.0f);
	}
	else if (Input::IsKeyDown(EKey::KEY_D))
	{
		CurrentCamera->Move(-Speed * Delta, 0.0f, 0.0f);
	}

	if (Input::IsKeyDown(EKey::KEY_Q))
	{
		CurrentCamera->Move(0.0f, Speed * Delta, 0.0f);
	}
	else if (Input::IsKeyDown(EKey::KEY_E))
	{
		CurrentCamera->Move(0.0f, -Speed * Delta, 0.0f);
	}

	CurrentCamera->UpdateMatrices();
}

void Application::SetCursor(TSharedPtr<GenericCursor> Cursor)
{
	PlatformApplication->SetCursor(Cursor);
}

void Application::SetActiveWindow(TSharedPtr<GenericWindow> Window)
{
	PlatformApplication->SetActiveWindow(Window);
}

void Application::SetCapture(TSharedPtr<GenericWindow> Window)
{
	PlatformApplication->SetCapture(Window);
}

void Application::SetCursorPos(TSharedPtr<GenericWindow> RelativeWindow, Int32 X, Int32 Y)
{
	PlatformApplication->SetCursorPos(RelativeWindow, X, Y);
}

ModifierKeyState Application::GetModifierKeyState() const
{
	return PlatformApplication->GetModifierKeyState();
}

TSharedPtr<GenericWindow> Application::GetWindow() const
{
	return Window;
}

TSharedPtr<GenericWindow> Application::GetActiveWindow() const
{
	return PlatformApplication->GetActiveWindow();
}

TSharedPtr<GenericWindow> Application::GetCapture() const
{
	return PlatformApplication->GetCapture();
}

void Application::GetCursorPos(TSharedPtr<GenericWindow> RelativeWindow, Int32& OutX, Int32& OutY) const
{
	PlatformApplication->GetCursorPos(RelativeWindow, OutX, OutY);
}

Application* Application::Make()
{
	Instance = TSharedPtr<Application>(new Application());
	if (Instance)
	{
		return Instance.Get();
	}
	else
	{
		return nullptr;
	}
}

Application& Application::Get()
{
	VALIDATE(Instance != nullptr);
	return (*Instance.Get());
}

void Application::OnWindowResized(TSharedPtr<GenericWindow> InWindow, Uint16 Width, Uint16 Height)
{
	WindowResizeEvent Event(InWindow, Width, Height);
	EventQueue::SendEvent(Event);

	if (Renderer::Get())
	{
		Renderer::Get()->OnResize(Width, Height);
	}
}

void Application::OnKeyReleased(EKey KeyCode, const ModifierKeyState& ModierKeyState)
{
	Input::RegisterKeyUp(KeyCode);

	KeyReleasedEvent Event(KeyCode, ModierKeyState);
	EventQueue::SendEvent(Event);
}

void Application::OnKeyPressed(EKey KeyCode, const ModifierKeyState& ModierKeyState)
{
	Input::RegisterKeyDown(KeyCode);

	KeyPressedEvent Event(KeyCode, ModierKeyState);
	EventQueue::SendEvent(Event);
}

void Application::OnMouseMove(Int32 X, Int32 Y)
{
	MouseMovedEvent Event(X, Y);
	EventQueue::SendEvent(Event);
}

void Application::OnMouseButtonReleased(EMouseButton Button, const ModifierKeyState& ModierKeyState)
{
	TSharedPtr<GenericWindow> CaptureWindow = GetCapture();
	if (CaptureWindow)
	{
		SetCapture(nullptr);
	}

	MouseReleasedEvent Event(Button, ModierKeyState);
	EventQueue::SendEvent(Event);
}

void Application::OnMouseButtonPressed(EMouseButton Button, const ModifierKeyState& ModierKeyState)
{
	TSharedPtr<GenericWindow> CaptureWindow = GetCapture();
	if (!CaptureWindow)
	{
		TSharedPtr<GenericWindow> ActiveWindow = GetActiveWindow();
		SetCapture(ActiveWindow);
	}

	MousePressedEvent Event(Button, ModierKeyState);
	EventQueue::SendEvent(Event);
}

void Application::OnMouseScrolled(Float32 HorizontalDelta, Float32 VerticalDelta)
{
	MouseScrolledEvent Event(HorizontalDelta, VerticalDelta);
	EventQueue::SendEvent(Event);
}

void Application::OnCharacterInput(Uint32 Character)
{
	KeyTypedEvent Event(Character);
	EventQueue::SendEvent(Event);
}

bool Application::Initialize()
{
	// Application
	HINSTANCE InstanceHandle = static_cast<HINSTANCE>(GetModuleHandle(NULL));
	PlatformApplication = PlatformApplication::Make(InstanceHandle);
	if (PlatformApplication)
	{
		PlatformApplication->SetEventHandler(TSharedPtr<ApplicationEventHandler>(Instance));
	}
	else
	{
		return false;
	}

	// Window
	Uint32 Style =	
		WINDOW_STYLE_FLAG_TITLED		| 
		WINDOW_STYLE_FLAG_CLOSABLE		| 
		WINDOW_STYLE_FLAG_MINIMIZABLE	| 
		WINDOW_STYLE_FLAG_MAXIMIZABLE	|
		WINDOW_STYLE_FLAG_RESIZEABLE;

	WindowInitializer WinInitializer("DXR", 1920, 1080, Style);
	Window = PlatformApplication->MakeWindow();
	if (Window->Initialize(WinInitializer))
	{
		Window->Show(false);
	}
	else
	{
		return false;
	}

	// Renderer
	Renderer* Renderer = Renderer::Make(GetWindow());
	if (!Renderer)
	{
		::MessageBox(0, "FAILED to create Renderer", "ERROR", MB_ICONERROR);
		return false;
	}

	// ImGui
	if (!DebugUI::Initialize())
	{
		::MessageBox(0, "FAILED to create ImGuiContext", "ERROR", MB_ICONERROR);
		return false;
	}

	// Initialize Scene
	constexpr Float32	SphereOffset	= 1.25f;
	constexpr Uint32	SphereCountX	= 8;
	constexpr Float32	StartPositionX	= (-static_cast<Float32>(SphereCountX) * SphereOffset) / 2.0f;
	constexpr Uint32	SphereCountY	= 8;
	constexpr Float32	StartPositionY	= (-static_cast<Float32>(SphereCountY) * SphereOffset) / 2.0f;
	constexpr Float32	MetallicDelta	= 1.0f / SphereCountY;
	constexpr Float32	RoughnessDelta	= 1.0f / SphereCountX;
	
	Actor* NewActor = nullptr;
	MeshComponent* NewComponent = nullptr;
	CurrentScene = Scene::LoadFromFile("../Assets/Scenes/Sponza/Sponza.obj");

	// Create Spheres
	MeshData SphereMeshData = MeshFactory::CreateSphere(3);
	TSharedPtr<Mesh> SphereMesh = Mesh::Make(SphereMeshData);

	// Create standard textures
	Byte Pixels[] = { 255, 255, 255, 255 };
	TSharedPtr<D3D12Texture> BaseTexture = TSharedPtr<D3D12Texture>(TextureFactory::LoadFromMemory(Pixels, 1, 1, 0, DXGI_FORMAT_R8G8B8A8_UNORM));
	if (!BaseTexture)
	{
		return false;
	}
	else
	{
		BaseTexture->SetDebugName("BaseTexture");
	}

	Pixels[0] = 127;
	Pixels[1] = 127;
	Pixels[2] = 255;

	TSharedPtr<D3D12Texture> BaseNormal = TSharedPtr<D3D12Texture>(TextureFactory::LoadFromMemory(Pixels, 1, 1, 0, DXGI_FORMAT_R8G8B8A8_UNORM));
	if (!BaseNormal)
	{
		return false;
	}
	else
	{
		BaseNormal->SetDebugName("BaseNormal");
	}

	Pixels[0] = 255;
	Pixels[1] = 255;
	Pixels[2] = 255;

	TSharedPtr<D3D12Texture> WhiteTexture = TSharedPtr<D3D12Texture>(TextureFactory::LoadFromMemory(Pixels, 1, 1, 0, DXGI_FORMAT_R8G8B8A8_UNORM));
	if (!WhiteTexture)
	{
		return false;
	}
	else
	{
		WhiteTexture->SetDebugName("WhiteTexture");
	}

	MaterialProperties MatProperties;
	Uint32 SphereIndex = 0;
	for (Uint32 y = 0; y < SphereCountY; y++)
	{
		for (Uint32 x = 0; x < SphereCountX; x++)
		{
			NewActor = new Actor();
			NewActor->GetTransform().SetPosition(StartPositionX + (x * SphereOffset), 8.0f + StartPositionY + (y * SphereOffset), 0.0f);

			NewActor->SetDebugName("Sphere[" + std::to_string(SphereIndex) + "]");
			SphereIndex++;

			CurrentScene->AddActor(NewActor);

			NewComponent = new MeshComponent(NewActor);
			NewComponent->Mesh		= SphereMesh;
			NewComponent->Material	= MakeShared<Material>(MatProperties);
			
			NewComponent->Material->AlbedoMap		= BaseTexture;
			NewComponent->Material->NormalMap		= BaseNormal;
			NewComponent->Material->RoughnessMap	= WhiteTexture;
			NewComponent->Material->HeightMap		= WhiteTexture;
			NewComponent->Material->AOMap			= WhiteTexture;
			NewComponent->Material->MetallicMap		= WhiteTexture;
			NewComponent->Material->Initialize();

			NewActor->AddComponent(NewComponent);

			MatProperties.Roughness += RoughnessDelta;
		}

		MatProperties.Roughness	= 0.05f;
		MatProperties.Metallic	+= MetallicDelta;
	}

	// Create Other Meshes
	MeshData CubeMeshData = MeshFactory::CreateCube();
	
	NewActor = new Actor();
	CurrentScene->AddActor(NewActor);

	NewActor->SetDebugName("Cube");
	NewActor->GetTransform().SetPosition(0.0f, 2.0f, -2.0f);

	MatProperties.AO		= 1.0f;
	MatProperties.Metallic	= 1.0f;
	MatProperties.Roughness = 1.0f;

	NewComponent = new MeshComponent(NewActor);
	NewComponent->Mesh		= Mesh::Make(CubeMeshData);
	NewComponent->Material	= MakeShared<Material>(MatProperties);

	TSharedPtr<D3D12Texture> AlbedoMap = TSharedPtr<D3D12Texture>(TextureFactory::LoadFromFile("../Assets/Textures/Gate_Albedo.png", TEXTURE_FACTORY_FLAGS_GENERATE_MIPS, DXGI_FORMAT_R8G8B8A8_UNORM));
	if (!AlbedoMap)
	{
		return false;
	}
	else
	{
		AlbedoMap->SetDebugName("AlbedoMap");
	}

	TSharedPtr<D3D12Texture> NormalMap = TSharedPtr<D3D12Texture>(TextureFactory::LoadFromFile("../Assets/Textures/Gate_Normal.png", TEXTURE_FACTORY_FLAGS_GENERATE_MIPS, DXGI_FORMAT_R8G8B8A8_UNORM));
	if (!NormalMap)
	{
		return false;
	}
	else
	{
		NormalMap->SetDebugName("NormalMap");
	}

	TSharedPtr<D3D12Texture> AOMap = TSharedPtr<D3D12Texture>(TextureFactory::LoadFromFile("../Assets/Textures/Gate_AO.png", TEXTURE_FACTORY_FLAGS_GENERATE_MIPS, DXGI_FORMAT_R8G8B8A8_UNORM));
	if (!AOMap)
	{
		return false;
	}
	else
	{
		AOMap->SetDebugName("AOMap");
	}

	TSharedPtr<D3D12Texture> RoughnessMap = TSharedPtr<D3D12Texture>(TextureFactory::LoadFromFile("../Assets/Textures/Gate_Roughness.png", TEXTURE_FACTORY_FLAGS_GENERATE_MIPS, DXGI_FORMAT_R8G8B8A8_UNORM));
	if (!RoughnessMap)
	{
		return false;
	}
	else
	{
		RoughnessMap->SetDebugName("RoughnessMap");
	}

	TSharedPtr<D3D12Texture> HeightMap = TSharedPtr<D3D12Texture>(TextureFactory::LoadFromFile("../Assets/Textures/Gate_Height.png", TEXTURE_FACTORY_FLAGS_GENERATE_MIPS, DXGI_FORMAT_R8G8B8A8_UNORM));
	if (!HeightMap)
	{
		return false;
	}
	else
	{
		HeightMap->SetDebugName("HeightMap");
	}

	TSharedPtr<D3D12Texture> MetallicMap = TSharedPtr<D3D12Texture>(TextureFactory::LoadFromFile("../Assets/Textures/Gate_Metallic.png", TEXTURE_FACTORY_FLAGS_GENERATE_MIPS, DXGI_FORMAT_R8G8B8A8_UNORM));
	if (!MetallicMap)
	{
		return false;
	}
	else
	{
		MetallicMap->SetDebugName("MetallicMap");
	}

	NewComponent->Material->AlbedoMap		= AlbedoMap;
	NewComponent->Material->NormalMap		= NormalMap;
	NewComponent->Material->RoughnessMap	= RoughnessMap;
	NewComponent->Material->HeightMap		= HeightMap;
	NewComponent->Material->AOMap			= AOMap;
	NewComponent->Material->MetallicMap		= MetallicMap;
	NewComponent->Material->Initialize();
	NewActor->AddComponent(NewComponent);

	CurrentCamera = new Camera();
	CurrentScene->AddCamera(CurrentCamera);

	// Add PointLight- Source
	PointLight* Light0 = new PointLight();
	Light0->SetPosition(14.0f, 1.0f, -0.5f);
	Light0->SetColor(1.0f, 1.0f, 1.0f);
	Light0->SetShadowBias(0.0005f);
	Light0->SetMaxShadowBias(0.009f);
	Light0->SetShadowFarPlane(50.0f);
	Light0->SetIntensity(100.0f);
	CurrentScene->AddLight(Light0);

	// Add DirectionalLight- Source
	DirectionalLight* Light1 = new DirectionalLight();
	Light1->SetDirection(0.0f, -1.0f, 0.0f);
	Light1->SetShadowMapPosition(0.0f, 40.0f, 0.0f);
	Light1->SetShadowBias(0.0008f);
	Light1->SetMaxShadowBias(0.01f);
	Light1->SetShadowFarPlane(60.0f);
	Light1->SetColor(1.0f, 1.0f, 1.0f);
	Light1->SetIntensity(10.0f);
	CurrentScene->AddLight(Light1);

	Scene::SetCurrentScene(CurrentScene);
	return true;
}
