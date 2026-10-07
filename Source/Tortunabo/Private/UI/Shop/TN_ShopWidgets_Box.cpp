// Caja sorpresa y aviso de saldo de la tienda (#873): el botón, el panel con la animación (UTN_MysteryBoxPanel) y el aviso
// de que faltan puntos. La compra la hace el PlayerController (RequestOpenMysteryBox) con el perfil de este jugador.

#include "UI/Shop/TN_ShopWidgets.h"
#include "UI/Shop/TN_MysteryBoxPanel.h"
#include "TN_ShopArt.h"
#include "../HUD/TN_HUDArt.h"
#include "Components/TextBlock.h"
#include "Core/TN_PointsEconomy.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Player/MP_GamePlayerController.h"

namespace TNShopBox
{
	int32 BoxPrice()
	{
		return FMath::Max(0, UTN_PointsEconomy::Get().MysteryBox.BoxPrice);
	}
}

UWidget* UTN_ShopWidget::MakeBoxButton()
{
	BoxButton = CreateWidget<UTN_ShopButton>(this, UTN_ShopButton::StaticClass());
	BoxButton->Setup(FText::GetEmpty(), TNShopArt::Pill(0xC08BFF, 0x7A3FC9), TNHUDArt::Cream, 22, FVector2D(300.f, 66.f),
		[this]() { OpenMysteryBox(); });
	return BoxButton;
}

bool UTN_ShopWidget::IsBoxOpen() const
{
	return BoxPanel && BoxPanel->GetVisibility() != ESlateVisibility::Collapsed;
}

void UTN_ShopWidget::RefreshBoxAndNotice()
{
	const UMP_GameInstance* GI = GetTNGI();
	const APlayerController* PC = GetOwningPlayer();
	const int32 Balance = GI ? GI->GetShopPointsFor(PC) : 0;
	const int32 Price = TNShopBox::BoxPrice();
	const bool bBoxAffordable = GI && TNMysteryBox::CanAfford(Balance, Price) && GI->GetMysteryBoxCandidates().Num() > 0;
	if (BoxButton)
	{
		BoxButton->SetLabel(FText::Format(NSLOCTEXT("Tortunabo", "ShopBoxButton", "CAJA SORPRESA · {0}"), FText::AsNumber(Price)));
		BoxButton->SetDisabled(!bBoxAffordable);
	}
	if (!NoticeText) { return; }

	// Lo que falta para lo que se mira; si eso se puede pagar, lo que falta para la caja.
	int32 Missing = 0;
	if (GI && Items.IsValidIndex(Selected) && !GI->IsCosmeticUnlockedFor(PC, Items[Selected].Category, Items[Selected].Id))
	{
		Missing = GI->GetCosmeticPrice(Items[Selected].Category, Items[Selected].Id) - Balance;
	}
	FText Notice;
	if (Missing > 0)
	{
		Notice = FText::Format(NSLOCTEXT("Tortunabo", "ShopMissingPoints", "Te {0}|plural(one=falta,other=faltan) {0} {0}|plural(one=punto,other=puntos) para comprarlo."), Missing);
	}
	else if (!bBoxAffordable && Price > Balance)
	{
		Notice = FText::Format(NSLOCTEXT("Tortunabo", "ShopBoxMissingPoints", "La caja sorpresa cuesta {0}: te {1}|plural(one=falta,other=faltan) {1} {1}|plural(one=punto,other=puntos)."),
			FText::AsNumber(Price), Price - Balance);
	}
	NoticeText->SetText(Notice);
	NoticeText->SetVisibility(Notice.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UTN_ShopWidget::OpenMysteryBox()
{
	AMP_GamePlayerController* PC = GetTNPC();
	UMP_GameInstance* GI = GetTNGI();
	if (!PC || !GI || !BoxPanel) { return; }
	const FTN_MysteryBoxResult Result = PC->RequestOpenMysteryBox();
	if (!Result.bOpened)
	{
		Say(NSLOCTEXT("Tortunabo", "ShopBoxNoPoints", "Para una caja sorpresa te faltan puntos. ¡Juega otra partida y vuelve!"));
		RefreshBoxAndNotice();
		return;
	}
	BoxPanel->SetVisibility(ESlateVisibility::Visible);
	BoxPanel->Play(Result, GI->GetMysteryBoxCandidates());
	BoxPanel->SetAgainState(TNShopBox::BoxPrice(), TNMysteryBox::CanAfford(Result.BalanceAfter, TNShopBox::BoxPrice()));
	Say(NSLOCTEXT("Tortunabo", "ShopBoxOpened", "¡Allá va la caja! A ver qué te trae la marea..."));
	RefreshWallet();
	RefreshCards();
	RefreshBuyButton();
	RefreshBoxAndNotice();
}

void UTN_ShopWidget::CloseMysteryBox()
{
	if (BoxPanel) { BoxPanel->SetVisibility(ESlateVisibility::Collapsed); }
	SetKeyboardFocus();
	RefreshWallet();
	RefreshCards();
	RefreshBuyButton();
	RefreshBoxAndNotice();
}
