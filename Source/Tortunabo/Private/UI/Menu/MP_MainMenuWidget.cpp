#include "UI/Menu/MP_MainMenuWidget.h"
#include "UI/Menu/TN_RoomMenuWidget.h"
#include "UI/Credits/TN_CreditsWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Settings/TN_GameSettingsSubsystem.h"
#include "VR/TN_VRMode.h"

namespace TNMainMenuDetail
{
	/** Primer texto dentro del botón (el rótulo del Blueprint, esté directamente o dentro de otros paneles). */
	UTextBlock* FindLabel(UWidget* Widget)
	{
		if (UTextBlock* Text = Cast<UTextBlock>(Widget))
		{
			return Text;
		}
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
			{
				if (UTextBlock* Found = FindLabel(Panel->GetChildAt(ChildIndex)))
				{
					return Found;
				}
			}
		}
		return nullptr;
	}

	/** Cambia solo el texto: fuente, color y tamaño siguen siendo los del Blueprint. */
	void SetLabel(UButton* Button, const FText& Label)
	{
		if (UTextBlock* Text = FindLabel(Button))
		{
			Text->SetText(Label);
		}
	}

	/** Pantallas de salas: por encima de este menú. */
	constexpr int32 RoomMenuZOrder = 10;

	/** Margen máximo encima y debajo de cada botón. El Blueprint pone 50, pensado para tres botones: con «Ajustes» y
	 *  «Créditos» son cinco en la misma caja y a cada uno le quedaban unos 40 px para un texto de 50 pt. */
	constexpr float MaxButtonGapY = 10.f;

	/** Colocación de un botón dentro de su caja (vertical u horizontal): margen, tamaño y alineación. */
	struct FBoxSlotLayout
	{
		bool bValid = false;
		FMargin Padding;
		FSlateChildSize Size;
		EHorizontalAlignment Horizontal = HAlign_Fill;
		EVerticalAlignment Vertical = VAlign_Fill;
	};

	FBoxSlotLayout ReadLayout(UPanelSlot* Slot)
	{
		FBoxSlotLayout Layout;
		if (const UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(Slot))
		{
			Layout.bValid = true;
			Layout.Padding = VSlot->GetPadding();
			Layout.Size = VSlot->GetSize();
			Layout.Horizontal = VSlot->GetHorizontalAlignment();
			Layout.Vertical = VSlot->GetVerticalAlignment();
		}
		else if (const UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(Slot))
		{
			Layout.bValid = true;
			Layout.Padding = HSlot->GetPadding();
			Layout.Size = HSlot->GetSize();
			Layout.Horizontal = HSlot->GetHorizontalAlignment();
			Layout.Vertical = HSlot->GetVerticalAlignment();
		}
		return Layout;
	}

	void ApplyLayout(UPanelSlot* Slot, const FBoxSlotLayout& Layout)
	{
		if (!Layout.bValid)
		{
			return;
		}
		if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(Slot))
		{
			VSlot->SetPadding(Layout.Padding);
			VSlot->SetSize(Layout.Size);
			VSlot->SetHorizontalAlignment(Layout.Horizontal);
			VSlot->SetVerticalAlignment(Layout.Vertical);
		}
		else if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(Slot))
		{
			HSlot->SetPadding(Layout.Padding);
			HSlot->SetSize(Layout.Size);
			HSlot->SetHorizontalAlignment(Layout.Horizontal);
			HSlot->SetVerticalAlignment(Layout.Vertical);
		}
	}

	/** Un widget de la caja de botones y cómo estaba colocado (para volver a ponerlo tras el botón nuevo). */
	struct FTailEntry
	{
		TWeakObjectPtr<UWidget> Widget;
		FBoxSlotLayout Layout;
	};
}

void UMP_MainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (HostButton)
	{
		HostButton->OnClicked.AddUniqueDynamic(this, &UMP_MainMenuWidget::OnHostClicked);
	}

	if (FindButton)
	{
		FindButton->OnClicked.AddUniqueDynamic(this, &UMP_MainMenuWidget::OnFindClicked);
	}

	if (QuitButton)
	{
		QuitButton->OnClicked.AddUniqueDynamic(this, &UMP_MainMenuWidget::OnQuitClicked);
	}

	if (StatusText)
	{
		StatusText->SetAutoWrapText(true);
	}

	UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance());
	if (GI)
	{
		GI->OnStatusChanged.AddUniqueDynamic(this, &UMP_MainMenuWidget::OnGameInstanceStatusChanged);
	}

	BuildSettingsButton();
	// Primero, cómo jugar: Local u Online (con un aviso de las salas pendiente, directamente en Online).
	ShowModePage(false);

	// Pantallas de salas: su propio widget a pantalla completa, montado y en pantalla antes de enseñar nada.
	if (!RoomMenu)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			RoomMenu = CreateWidget<UTN_RoomMenuWidget>(PC, UTN_RoomMenuWidget::StaticClass());
		}
		if (RoomMenu)
		{
			TWeakObjectPtr<UMP_MainMenuWidget> WeakThis(this);
			RoomMenu->OnOpenChanged = [WeakThis](bool bOpen)
			{
				if (UMP_MainMenuWidget* Menu = WeakThis.Get()) { Menu->HandleRoomsOpenChanged(bOpen); }
			};
			TNVR::AddToScreen(RoomMenu, TNMainMenuDetail::RoomMenuZOrder);
		}
	}

	// Aviso que dejó la GameInstance al volver aquí (expulsado, sala cerrada o llena, el anfitrión se fue...).
	if (GI && RoomMenu)
	{
		const FTNMenuNotice Notice = GI->ConsumeMenuNotice();
		if (!Notice.Text.IsEmpty())
		{
			ShowModePage(true);
			if (Notice.bOpenJoin)
			{
				OpenRooms(ETNRoomMenuPage::Join);
			}
			RoomMenu->ShowNotice(Notice.Text, Notice.bError, 9.f);
		}
	}

	// «Créditos», entre «Ajustes» y «Salir» (Docs/Creditos.md).
	if (!CreditsButton)
	{
		CreditsButton = UTN_CreditsWidget::AddMenuButton(WidgetTree, FindButton, QuitButton);
		if (CreditsButton) { CreditsButton->OnClicked.AddUniqueDynamic(this, &UMP_MainMenuWidget::OnCreditsClicked); }
	}
	FitButtonGaps();

	// Con el mando, el foco empieza en «Crear partida».
	if (HostButton && !(RoomMenu && RoomMenu->IsOpen()))
	{
		HostButton->SetKeyboardFocus();
	}
}

void UMP_MainMenuWidget::NativeDestruct()
{
	if (UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance()))
	{
		GI->OnStatusChanged.RemoveDynamic(this, &UMP_MainMenuWidget::OnGameInstanceStatusChanged);
	}
	// Ajustes abiertos al irse este menú (un viaje): se cierran con él.
	if (bSettingsOpen)
	{
		bSettingsOpen = false;
		if (UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(this)) { Settings->ClosePauseMenu(); }
	}
	if (RoomMenu)
	{
		RoomMenu->OnOpenChanged = nullptr;
		RoomMenu->RemoveFromParent();
		RoomMenu = nullptr;
	}
	Super::NativeDestruct();
}

void UMP_MainMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bSettingsOpen)
	{
		const UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(this);
		if (!Settings || !Settings->IsPauseMenuOpen())
		{
			HandleSettingsClosed();
		}
	}
}

void UMP_MainMenuWidget::BuildSettingsButton()
{
	using namespace TNMainMenuDetail;
	if (SettingsButton || !FindButton || !QuitButton || !WidgetTree)
	{
		return;
	}
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SettingsButton"));
	if (!Button)
	{
		return;
	}
	// Mismo aspecto que «Unirse»: el estilo del botón y el del rótulo.
	Button->SetStyle(FindButton->GetStyle());
	Button->SetColorAndOpacity(FindButton->GetColorAndOpacity());
	Button->SetBackgroundColor(FindButton->GetBackgroundColor());
	Button->SetClickMethod(FindButton->GetClickMethod());
	Button->SetTouchMethod(FindButton->GetTouchMethod());
	Button->SetPressMethod(FindButton->GetPressMethod());
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	if (const UTextBlock* Source = FindLabel(FindButton))
	{
		Label->SetFont(Source->GetFont());
		Label->SetColorAndOpacity(Source->GetColorAndOpacity());
		Label->SetShadowOffset(Source->GetShadowOffset());
		Label->SetShadowColorAndOpacity(Source->GetShadowColorAndOpacity());
		Label->SetMinDesiredWidth(Source->GetMinDesiredWidth());
	}
	Label->SetText(NSLOCTEXT("TNRooms", "MenuSettings", "Ajustes"));
	Button->SetContent(Label);
	Button->OnClicked.AddUniqueDynamic(this, &UMP_MainMenuWidget::OnSettingsClicked);
	SettingsButton = Button;

	// Entre «Unirse» y «Salir», con la colocación de «Unirse». UMG solo sabe insertar en su lista, no en la caja de Slate: lo
	// que va desde «Salir» en adelante se quita y se vuelve a poner detrás del botón nuevo.
	UPanelWidget* Parent = QuitButton->GetParent();
	if (Parent && FindButton->GetParent() == Parent && (Parent->IsA<UVerticalBox>() || Parent->IsA<UHorizontalBox>()))
	{
		const FBoxSlotLayout FindLayout = ReadLayout(FindButton->Slot);
		TArray<FTailEntry> Tail;
		for (int32 ChildIndex = Parent->GetChildIndex(QuitButton); ChildIndex >= 0 && ChildIndex < Parent->GetChildrenCount(); ++ChildIndex)
		{
			UWidget* Child = Parent->GetChildAt(ChildIndex);
			FTailEntry Entry;
			Entry.Widget = Child;
			Entry.Layout = ReadLayout(Child ? Child->Slot.Get() : nullptr);
			Tail.Add(Entry);
		}
		for (const FTailEntry& Entry : Tail)
		{
			if (UWidget* Child = Entry.Widget.Get()) { Parent->RemoveChild(Child); }
		}
		ApplyLayout(Parent->AddChild(Button), FindLayout);
		for (const FTailEntry& Entry : Tail)
		{
			if (UWidget* Child = Entry.Widget.Get()) { ApplyLayout(Parent->AddChild(Child), Entry.Layout); }
		}
	}
	else if (UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget))
	{
		// El Blueprint no tiene los botones en una caja: el botón nuevo va abajo, en el centro.
		if (UCanvasPanelSlot* CanvasSlot = Root->AddChildToCanvas(Button))
		{
			CanvasSlot->SetAnchors(FAnchors(0.5f, 1.f));
			CanvasSlot->SetAlignment(FVector2D(0.5f, 1.f));
			CanvasSlot->SetPosition(FVector2D(0.0, -90.0));
			CanvasSlot->SetAutoSize(true);
		}
	}
}

void UMP_MainMenuWidget::FitButtonGaps()
{
	const UPanelWidget* Box = QuitButton ? QuitButton->GetParent() : nullptr;
	if (!Box)
	{
		return;
	}
	for (int32 ChildIndex = 0; ChildIndex < Box->GetChildrenCount(); ++ChildIndex)
	{
		const UWidget* Child = Box->GetChildAt(ChildIndex);
		if (UVerticalBoxSlot* VSlot = Child ? Cast<UVerticalBoxSlot>(Child->Slot) : nullptr)
		{
			FMargin Gap = VSlot->GetPadding();
			Gap.Top = FMath::Min(Gap.Top, TNMainMenuDetail::MaxButtonGapY);
			Gap.Bottom = FMath::Min(Gap.Bottom, TNMainMenuDetail::MaxButtonGapY);
			VSlot->SetPadding(Gap);
		}
	}
}

void UMP_MainMenuWidget::OnSettingsClicked()
{
	UTN_GameSettingsSubsystem* Settings = UTN_GameSettingsSubsystem::Get(this);
	APlayerController* PC = GetOwningPlayer();
	if (!Settings || !PC || bSettingsOpen)
	{
		return;
	}
	Settings->OpenMainMenuSettings(PC);
	if (!Settings->IsPauseMenuOpen())
	{
		return;
	}
	RoomsOpener = SettingsButton.Get();
	bSettingsOpen = true;
	// A la vista tras el velo de los ajustes, pero sin clics ni foco: el mando no se escapa a estos botones.
	if (GetVisibility() != ESlateVisibility::HitTestInvisible)
	{
		VisibilityBeforeSettings = GetVisibility();
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMP_MainMenuWidget::OnCreditsClicked()
{
	UTN_CreditsWidget::OpenOver(GetOwningPlayer(), this, CreditsButton);
}

void UMP_MainMenuWidget::HandleSettingsClosed()
{
	bSettingsOpen = false;
	SetVisibility(VisibilityBeforeSettings);
	// Por si se ha cambiado el idioma: el saludo se vuelve a leer en el idioma nuevo.
	SetStatus(BuildIdleStatus());
	UButton* Back = RoomsOpener.Get();
	if (!Back) { Back = HostButton.Get(); }
	if (Back) { Back->SetKeyboardFocus(); }
}

void UMP_MainMenuWidget::OnHostClicked()
{
	if (!bOnlinePage)
	{
		// «Local»: hasta cuatro en este PC; el jugador 1 va derecho al lobby y los mandos se unen allí con Start.
		if (UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance()))
		{
			SetStatus(NSLOCTEXT("TNLocal", "MenuLocalGo", "Partida local: en el lobby, cada mando se une con Start (hasta 4).").ToString());
			GI->StartLocalGame();
		}
		return;
	}
	RoomsOpener = HostButton.Get();
	OpenRooms(ETNRoomMenuPage::Create);
}

void UMP_MainMenuWidget::OnFindClicked()
{
	if (!bOnlinePage)
	{
		ShowModePage(true);
		return;
	}
	RoomsOpener = FindButton.Get();
	OpenRooms(ETNRoomMenuPage::Join);
}

void UMP_MainMenuWidget::OnQuitClicked()
{
	if (bOnlinePage)
	{
		// «Volver»: otra vez a elegir Local u Online.
		ShowModePage(false);
		return;
	}
	UKismetSystemLibrary::QuitGame(GetWorld(), GetOwningPlayer(), EQuitPreference::Quit, true);
}

void UMP_MainMenuWidget::ShowModePage(bool bOnline)
{
	bOnlinePage = bOnline;
	if (bOnline)
	{
		TNMainMenuDetail::SetLabel(HostButton, NSLOCTEXT("TNRooms", "MenuCreate", "Crear partida"));
		TNMainMenuDetail::SetLabel(FindButton, NSLOCTEXT("TNRooms", "MenuJoin", "Unirse"));
		TNMainMenuDetail::SetLabel(QuitButton, NSLOCTEXT("TNLocal", "MenuBack", "Volver"));
	}
	else
	{
		TNMainMenuDetail::SetLabel(HostButton, NSLOCTEXT("TNLocal", "MenuLocal", "Local"));
		TNMainMenuDetail::SetLabel(FindButton, NSLOCTEXT("TNLocal", "MenuOnline", "Online"));
		TNMainMenuDetail::SetLabel(QuitButton, NSLOCTEXT("TNRooms", "MenuQuit", "Salir"));
	}
	SetStatus(BuildIdleStatus());
	if (HostButton && !(RoomMenu && RoomMenu->IsOpen()) && !bSettingsOpen)
	{
		HostButton->SetKeyboardFocus();
	}
}

FReply UMP_MainMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// En la página Online, Escape o B vuelven a elegir Local u Online (como «Volver»).
	const FKey Key = InKeyEvent.GetKey();
	if (bOnlinePage && !InKeyEvent.IsRepeat() && (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::BackSpace)
		&& !(RoomMenu && RoomMenu->IsOpen()) && !bSettingsOpen)
	{
		ShowModePage(false);
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UMP_MainMenuWidget::OpenRooms(ETNRoomMenuPage Page)
{
	if (RoomMenu)
	{
		RoomMenu->Open(Page);
		return;
	}
	// Sin pantalla de salas (no debería pasar): lo de siempre, crear una pública o unirse a la primera.
	if (UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance()))
	{
		if (Page == ETNRoomMenuPage::Create)
		{
			GI->HostSessionWithMode(GI->SelectedProcMode);
		}
		else
		{
			GI->FindAndJoinSession();
		}
	}
}

void UMP_MainMenuWidget::HandleRoomsOpenChanged(bool bOpen)
{
	if (bOpen)
	{
		// Escondido mientras tanto: así el mando no se escapa a estos botones y no se ven dos menús.
		if (GetVisibility() != ESlateVisibility::Collapsed)
		{
			VisibilityBeforeRooms = GetVisibility();
		}
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	SetVisibility(VisibilityBeforeRooms);
	SetStatus(BuildIdleStatus());
	UButton* Back = RoomsOpener.Get();
	if (!Back) { Back = HostButton.Get(); }
	if (Back) { Back->SetKeyboardFocus(); }
}

void UMP_MainMenuWidget::OnGameInstanceStatusChanged(const FString& StatusMessage)
{
	// El delegate trae el registro entero: en pantalla solo va el último mensaje, para que no se acumulen los de antes.
	int32 LastBreak = INDEX_NONE;
	SetStatus(StatusMessage.FindLastChar(TEXT('\n'), LastBreak) ? StatusMessage.Mid(LastBreak + 1) : StatusMessage);
}

void UMP_MainMenuWidget::SetStatus(const FString& Message)
{
	if (StatusText)
	{
		// Los mensajes de la GameInstance llegan ya traducidos y como FString: se enseñan tal cual.
		StatusText->SetText(FText::AsCultureInvariant(Message));
	}
}

FString UMP_MainMenuWidget::BuildIdleStatus() const
{
	const UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance());
	FString Existing = GI ? GI->BuildStatusLog() : FString();
	int32 LastBreak = INDEX_NONE;
	if (Existing.FindLastChar(TEXT('\n'), LastBreak))
	{
		Existing = Existing.Mid(LastBreak + 1);
	}
	if (!bOnlinePage)
	{
		// Primera página: qué es cada cosa (el estado de las salas, en la de Online).
		return NSLOCTEXT("TNLocal", "MenuChoose", "Local: hasta 4 en este PC, a pantalla partida y sin conexión. Online: salas por Steam, hasta 8.").ToString();
	}
	return Existing.IsEmpty() ? NSLOCTEXT("TNRooms", "MenuIdle", "Listo. Crea una partida o únete a una.").ToString() : Existing;
}
