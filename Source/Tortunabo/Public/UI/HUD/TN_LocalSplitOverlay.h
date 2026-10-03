#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Multiplayer/TN_LocalPlayRules.h"
#include "TN_LocalSplitOverlay.generated.h"

class UBorder;
class UCanvasPanel;
class UProgressBar;
class UTextBlock;
class UVerticalBox;

/** Lo que enseña UTN_LocalSplitOverlay en la vista de un jugador. */
struct FTNSplitOverlayView
{
	TNLocalPlay::FViewRect Rect;
	int32 PlayerNumber = 1;
	/** Cuánto lleva manteniendo B para salir (0..1); negativo si no. */
	float LeaveProgress = -1.f;
	/** Su número en grande (unos segundos al cambiar el reparto). */
	bool bShowTag = false;
	/** Invitado en el lobby: el número dice también cómo salir. */
	bool bCanLeave = false;
};

/** Estado de la capa de la pantalla partida (lo rellena UTN_LocalPlaySubsystem cada fotograma). */
struct FTNSplitOverlayState
{
	TArray<FTNSplitOverlayView> Views;
	/** Con tres jugadores: el cuadrante libre. */
	bool bHasEmptyRect = false;
	TNLocalPlay::FViewRect EmptyRect;
	/** En el lobby y con sitio: cómo se une otro mando. */
	bool bCanJoin = false;
};

/**
 * @brief Capa de la pantalla partida del modo local (#311), encima de las vistas y debajo del menú de pausa y de la carga:
 * tapa el cuadrante libre con tres jugadores («Pulsa Start en otro mando para unirte» en el lobby), avisa de cómo se une otro
 * mando, enseña el número de cada jugador en su vista al cambiar el reparto y quién está saliendo con B. No quita clics.
 */
UCLASS()
class TORTUNABO_API UTN_LocalSplitOverlay : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Pone lo que hay que enseñar (solo cambia lo que cambia). */
	void Refresh(const FTNSplitOverlayState& State);

protected:
	virtual void NativeOnInitialized() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Root;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> EmptyCover;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> EmptyTitle;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> EmptyText;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> JoinPill;

	/** Por vista (hasta cuatro): su zona, la etiqueta con el número y el aviso de salir con su barra. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCanvasPanel>> ViewAreas;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> ViewTags;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> ViewTagTexts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> LeavePanels;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProgressBar>> LeaveBars;

	/** Lo último que se puso (para no tocar nada si no cambia). */
	TArray<TNLocalPlay::FViewRect> ShownRects;
	TArray<FString> ShownTags;
	int32 ShownEmpty = -1;
	int32 ShownJoin = -1;

	void Build();
	/** Pone el hueco Widget del lienzo en Rect (fracciones de la pantalla). */
	static void PlaceInRect(UWidget* Widget, const TNLocalPlay::FViewRect& Rect);
};
