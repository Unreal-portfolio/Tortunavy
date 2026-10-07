#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Lobby/TN_MysteryBox.h"
#include "TN_MysteryBoxPanel.generated.h"

class UBorder;
class UImage;
class UMaterialInstanceDynamic;
class UTextBlock;
class UTN_ShopButton;

/** Nombre y color de cada rareza (tienda y caja sorpresa, #873). */
namespace TNSkinRarityText
{
	TORTUNABO_API FText Name(ETNSkinRarity Rarity);
	TORTUNABO_API FLinearColor Color(ETNSkinRarity Rarity);
}

/**
 * Caja sorpresa de la tienda (#873), encima del catálogo: la carta gira y va pasando skins al azar cada vez más despacio
 * hasta que se para en la que ha salido y la revela con un salto, su rareza en color y si es nueva o repetida (con los
 * puntos devueltos). La compra ya está hecha al empezar la animación: cerrar a medias no cambia nada. Intro la salta o,
 * al acabar, abre otra; Escape vuelve al catálogo. Todo en código (UTN_ShopWidget la crea), sin eventos de Blueprint.
 */
UCLASS()
class TORTUNABO_API UTN_MysteryBoxPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Qué hacer al pulsar «Abrir otra» y «Volver». */
	void SetActions(TFunction<void()> InOnAgain, TFunction<void()> InOnClose);

	/** Empieza la animación con el resultado ya decidido y lo que puede salir (para las vueltas). */
	void Play(const FTN_MysteryBoxResult& InResult, TArray<TNMysteryBox::FCandidate> InCandidates);

	/** Precio de la caja y si llega el saldo para otra (rótulo y estado del botón «Abrir otra»). */
	void SetAgainState(int32 Price, bool bCanAfford);

	/** Teclas mientras está a la vista; true si la usa. */
	bool HandleKey(const FKey& Key);

	bool IsSpinning() const { return bSpinning; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UBorder> Card;

	UPROPERTY(Transient)
	TObjectPtr<UImage> ThumbImage;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ThumbMID;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> RarityText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ResultText;

	UPROPERTY(Transient)
	TObjectPtr<UTN_ShopButton> AgainButton;

	UPROPERTY(Transient)
	TObjectPtr<UTN_ShopButton> CloseButton;

	TFunction<void()> OnAgain;
	TFunction<void()> OnClose;
	FTN_MysteryBoxResult Result;
	TArray<TNMysteryBox::FCandidate> Candidates;
	FRandomStream SpinStream;
	bool bSpinning = false;
	bool bCanOpenAgain = false;
	float SpinElapsed = 0.f;
	float NextFlipAt = 0.f;
	float RevealElapsed = -1.f;

	void BuildTree();
	void ShowItem(const TNMysteryBox::FCandidate& Item);
	void Reveal();
	void Close();
};
