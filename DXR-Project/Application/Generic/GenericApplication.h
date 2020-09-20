#pragma once
#include "Application/InputCodes.h"

#include "Application/Events/ApplicationEventHandler.h"

#include "GenericWindow.h"
#include "GenericCursor.h"

/*
* ModifierKeyState
*/
struct ModifierKeyState
{
public:
	inline ModifierKeyState(Uint32 InModifierMask)
		: ModifierMask(InModifierMask)
	{
	}

	FORCEINLINE bool IsCtrlDown() const
	{
		return (ModifierMask & MODIFIER_FLAG_CTRL);
	}

	FORCEINLINE bool IsAltDown() const
	{
		return (ModifierMask & MODIFIER_FLAG_ALT);
	}

	FORCEINLINE bool IsShiftDown() const
	{
		return (ModifierMask & MODIFIER_FLAG_SHIFT);
	}

	FORCEINLINE bool IsCapsLockDown() const
	{
		return (ModifierMask & MODIFIER_FLAG_CAPS_LOCK);
	}

	FORCEINLINE bool IsSuperKeyDown() const
	{
		return (ModifierMask & MODIFIER_FLAG_SUPER);
	}

	FORCEINLINE bool IsNumPadDown() const
	{
		return (ModifierMask & MODIFIER_FLAG_NUM_LOCK);
	}

private:
	Uint32 ModifierMask = 0;
};

/*
* GenericApplication
*/
class GenericApplication
{
public:
	GenericApplication() = default;
	virtual ~GenericApplication() = default;

	virtual bool Initialize() = 0;
	virtual bool Tick() = 0;

	virtual TSharedPtr<GenericWindow> MakeWindow() = 0;
	virtual TSharedPtr<GenericCursor> MakeCursor() = 0;

	virtual void SetCursor(TSharedPtr<GenericCursor> Cursor) = 0;
	virtual TSharedPtr<GenericCursor> GetCursor() const = 0;

	virtual void SetActiveWindow(TSharedPtr<GenericWindow> Window) = 0;
	
	virtual void SetCapture(TSharedPtr<GenericWindow> Window)
	{
		UNREFERENCED_VARIABLE(Window);
	}

	virtual ModifierKeyState GetModifierKeyState() const = 0;
	virtual TSharedPtr<GenericWindow> GetActiveWindow() const = 0;

	// Some platforms does not have the concept of mouse capture, therefor return nullptr as standard
	virtual TSharedPtr<GenericWindow> GetCapture() const
	{
		return TSharedPtr<GenericWindow>();
	}

	virtual void SetCursorPos(TSharedPtr<GenericWindow> RelativeWindow, Int32 X, Int32 Y) = 0;
	virtual void GetCursorPos(TSharedPtr<GenericWindow> RelativeWindow, Int32& OutX, Int32& OutY) const = 0;

	FORCEINLINE void SetEventHandler(TSharedPtr<ApplicationEventHandler> InEventHandler)
	{
		EventHandler = InEventHandler;
	}

	FORCEINLINE TSharedPtr<ApplicationEventHandler> GetEventHandler() const
	{
		return EventHandler;
	}

	static GenericApplication* Make()
	{
		return nullptr;
	}

protected:
	TSharedPtr<ApplicationEventHandler> EventHandler;
};

extern GenericApplication* GlobalPlatformApplication;