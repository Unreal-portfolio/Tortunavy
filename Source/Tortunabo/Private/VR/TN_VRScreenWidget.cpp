#include "VR/TN_VRScreenWidget.h"
#include "Multiplayer/TN_LocalPlaySubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/NativeWidgetHost.h"
#include "Widgets/SNullWidget.h"

namespace TNVRScreenDetail
{
	/** A toda la superficie del lienzo, como un widget de pantalla completa en el viewport. */
	void FillSlot(UCanvasPanelSlot* CanvasSlot, int32 ZOrder)
	{
		if (!CanvasSlot)
		{
			return;
		}
		CanvasSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		CanvasSlot->SetOffsets(FMargin(0.f));
		CanvasSlot->SetAlignment(FVector2D::ZeroVector);
		CanvasSlot->SetZOrder(ZOrder);
	}
}

void UTN_VRScreenWidget::NativeOnInitialized()
{
	BuildTree();
	Super::NativeOnInitialized();
	// El lienzo no se pulsa: solo lo que lleva dentro.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UTN_VRScreenWidget::BuildTree()
{
	if (!WidgetTree || Canvas)
	{
		return;
	}
	Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
	WidgetTree->RootWidget = Canvas;
}

bool UTN_VRScreenWidget::Host(UUserWidget* Widget, int32 ZOrder, bool bPlayerScreen)
{
	BuildTree();
	if (!Widget || !Canvas || Widget == this)
	{
		return false;
	}
	Prune();
	if (Widget->GetParent() == Canvas)
	{
		// Ya está: solo cambia el orden.
		TNVRScreenDetail::FillSlot(Cast<UCanvasPanelSlot>(Widget->Slot), ZOrder);
		for (FHostedWidget& Entry : Hosted)
		{
			if (Entry.Widget.Get() == Widget)
			{
				Entry.ZOrder = ZOrder;
				Entry.bPlayerScreen = bPlayerScreen;
			}
		}
		return true;
	}
	if (Widget->IsInViewport() || Widget->GetParent())
	{
		Widget->RemoveFromParent();
	}
	UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(Widget);
	if (!CanvasSlot)
	{
		return false;
	}
	TNVRScreenDetail::FillSlot(CanvasSlot, ZOrder);
	FHostedWidget Entry;
	Entry.Widget = Widget;
	Entry.ZOrder = ZOrder;
	Entry.bPlayerScreen = bPlayerScreen;
	Hosted.Add(Entry);
	return true;
}

bool UTN_VRScreenWidget::IsHosting(const UUserWidget* Widget) const
{
	if (!Widget || !Canvas || Widget->GetParent() != Canvas)
	{
		return false;
	}
	return Hosted.ContainsByPredicate([Widget](const FHostedWidget& Entry) { return Entry.Widget.Get() == Widget; });
}

bool UTN_VRScreenWidget::HostSlate(const TSharedRef<SWidget>& Widget, int32 ZOrder)
{
	BuildTree();
	if (!Canvas || !WidgetTree)
	{
		return false;
	}
	UnhostSlate(Widget);
	UNativeWidgetHost* HostWidget = WidgetTree->ConstructWidget<UNativeWidgetHost>(UNativeWidgetHost::StaticClass());
	if (!HostWidget)
	{
		return false;
	}
	HostWidget->SetContent(Widget);
	TNVRScreenDetail::FillSlot(Canvas->AddChildToCanvas(HostWidget), ZOrder);
	SlateHosts.Add(HostWidget);
	SlateContents.Add(Widget);
	return true;
}

bool UTN_VRScreenWidget::UnhostSlate(const TSharedRef<SWidget>& Widget)
{
	for (int32 i = SlateContents.Num() - 1; i >= 0; --i)
	{
		const TSharedPtr<SWidget> Content = SlateContents[i].Pin();
		if (Content.IsValid() && Content != Widget)
		{
			continue;
		}
		if (SlateHosts.IsValidIndex(i) && SlateHosts[i])
		{
			SlateHosts[i]->SetContent(SNullWidget::NullWidget);
			SlateHosts[i]->RemoveFromParent();
		}
		if (SlateHosts.IsValidIndex(i)) { SlateHosts.RemoveAt(i); }
		SlateContents.RemoveAt(i);
		if (Content.IsValid())
		{
			return true;
		}
	}
	return false;
}

void UTN_VRScreenWidget::ReleaseAll(bool bToViewport)
{
	Prune();
	TArray<FHostedWidget> Released = Hosted;
	Hosted.Reset();
	for (const FHostedWidget& Entry : Released)
	{
		UUserWidget* Widget = Entry.Widget.Get();
		if (!Widget || Widget->GetParent() != Canvas)
		{
			continue;
		}
		Widget->RemoveFromParent();
		if (bToViewport)
		{
			// Partida local (#639): lo de un jugador vuelve a su trozo de la pantalla partida, no a toda la pantalla.
			if (ReturnsToPlayerScreen(Entry.bPlayerScreen, Widget->GetOwningLocalPlayer() != nullptr, UTN_LocalPlaySubsystem::IsLocalGame(Widget))
				&& Widget->AddToPlayerScreen(Entry.ZOrder))
			{
				continue;
			}
			Widget->AddToViewport(Entry.ZOrder);
		}
	}
	for (UNativeWidgetHost* HostWidget : SlateHosts)
	{
		if (HostWidget)
		{
			HostWidget->SetContent(SNullWidget::NullWidget);
			HostWidget->RemoveFromParent();
		}
	}
	SlateHosts.Reset();
	SlateContents.Reset();
}

int32 UTN_VRScreenWidget::CountVisible() const
{
	int32 Count = 0;
	for (const FHostedWidget& Entry : Hosted)
	{
		const UUserWidget* Widget = Entry.Widget.Get();
		if (Widget && Widget->GetParent() == Canvas && Widget->IsVisible())
		{
			++Count;
		}
	}
	return Count;
}

void UTN_VRScreenWidget::Prune()
{
	// Lo que ya se quitó con RemoveFromParent (o se destruyó) deja de contar.
	Hosted.RemoveAll([this](const FHostedWidget& Entry)
	{
		const UUserWidget* Widget = Entry.Widget.Get();
		return !Widget || Widget->GetParent() != Canvas;
	});
}
