#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TN_VRScreenWidget.generated.h"

class SWidget;
class UCanvasPanel;
class UNativeWidgetHost;

/**
 * La pantalla del modo VR: un lienzo de 1920 × 1080 que pinta ATN_VRRig en un panel del mundo (UWidgetComponent). Todo lo
 * que el juego pone en pantalla (TNVR::AddToScreen: HUD, menús, ruedas, carteles...) va aquí, a toda la superficie y con su
 * ZOrder, igual que en el viewport. Se quita con RemoveFromParent de siempre.
 */
UCLASS(NotBlueprintable)
class TORTUNABO_API UTN_VRScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static constexpr float ScreenWidth = 1920.f;
	static constexpr float ScreenHeight = 1080.f;

	/**
	 * Pone Widget en el lienzo (lo quita antes del viewport o de otro panel). bPlayerScreen: lo puso TNVR::AddToScreen (el
	 * widget de un jugador, no uno de toda la pantalla): al soltar el panel con la VR apagada vuelve a su trozo de la pantalla
	 * partida (#639).
	 */
	bool Host(UUserWidget* Widget, int32 ZOrder, bool bPlayerScreen = false);

	/**
	 * Al soltar un widget alojado con la VR apagada: ¿vuelve al trozo de pantalla de su jugador (AddToPlayerScreen) en lugar de
	 * a toda la pantalla (AddToViewport)? Lo mismo que decide TNVR::AddToScreen sin VR: lo puso como widget de un jugador, tiene
	 * jugador local y la partida es la local (#639).
	 */
	static bool ReturnsToPlayerScreen(bool bAddedToPlayerScreen, bool bHasOwningPlayer, bool bLocalGame)
	{
		return bAddedToPlayerScreen && bHasOwningPlayer && bLocalGame;
	}

	/** ¿Sigue Widget en este lienzo? */
	bool IsHosting(const UUserWidget* Widget) const;

	/** Un widget Slate suelto (el «¡ADELANTE!» de la ronda). */
	bool HostSlate(const TSharedRef<SWidget>& Widget, int32 ZOrder);
	bool UnhostSlate(const TSharedRef<SWidget>& Widget);

	/**
	 * Suelta todo lo alojado. Con bToViewport, lo devuelve a la pantalla con su ZOrder (al apagar el modo VR a mitad de
	 * partida): al trozo de su jugador si lo era (ReturnsToPlayerScreen) y al viewport si no; si no, solo lo quita (el mundo
	 * se va: sus dueños lo vuelven a poner en el mundo siguiente).
	 */
	void ReleaseAll(bool bToViewport);

	/** Cuántos widgets alojados se ven ahora. */
	int32 CountVisible() const;

protected:
	virtual void NativeOnInitialized() override;

private:
	void BuildTree();
	void Prune();

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Canvas;

	struct FHostedWidget
	{
		TWeakObjectPtr<UUserWidget> Widget;
		int32 ZOrder = 0;
		/** Lo puso TNVR::AddToScreen (el widget de un jugador) y no AddToFullScreen. */
		bool bPlayerScreen = false;
	};
	TArray<FHostedWidget> Hosted;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UNativeWidgetHost>> SlateHosts;

	TArray<TWeakPtr<SWidget>> SlateContents;
};
