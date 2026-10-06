#include "UI/TN_ScreenHost.h"
#include "Multiplayer/TN_LocalPlaySubsystem.h"
#include "Blueprint/UserWidget.h"

void TNScreen::AddToScreen(UUserWidget* Widget, int32 ZOrder)
{
	if (!Widget)
	{
		return;
	}
	// Partida local (#311): el widget de un jugador, a su trozo de la pantalla partida (con uno solo, la pantalla entera).
	if (Widget->GetOwningLocalPlayer() && UTN_LocalPlaySubsystem::IsLocalGame(Widget) && Widget->AddToPlayerScreen(ZOrder))
	{
		return;
	}
	Widget->AddToViewport(ZOrder);
}

void TNScreen::AddToFullScreen(UUserWidget* Widget, int32 ZOrder)
{
	if (Widget)
	{
		Widget->AddToViewport(ZOrder);
	}
}

bool TNScreen::IsOnScreen(const UUserWidget* Widget)
{
	return Widget && Widget->IsInViewport();
}
