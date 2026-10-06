#include "Menu/MP_MenuPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "UI/TN_ScreenHost.h"

AMP_MenuPlayerController::AMP_MenuPlayerController()
{
	bShowMouseCursor = true;
}

void AMP_MenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	if (MainMenuWidgetClass)
	{
		MainMenuWidget = CreateWidget<UUserWidget>(this, MainMenuWidgetClass);
		if (MainMenuWidget)
		{
			TNScreen::AddToScreen(MainMenuWidget);
		}
	}
}

void AMP_MenuPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MainMenuWidget)
	{
		MainMenuWidget->RemoveFromParent();
		MainMenuWidget = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

