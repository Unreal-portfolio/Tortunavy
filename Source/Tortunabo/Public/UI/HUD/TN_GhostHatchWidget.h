#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "TN_GhostHatchWidget.generated.h"

class APlayerController;
class UCanvasPanel;
class UTN_EggSynthComponent;

/** Grieta de la cáscara oscura: polilínea desde la unión hacia dentro de una mitad (0-1 de la pantalla). */
struct FTNGhostHatchCrack
{
	TArray<FVector2f> Points;
	bool bTop = true;
	/** Golpe (1-3) con el que aparece. */
	int32 Knock = 1;
};

/** Trozo de cáscara que sale despedido al abrirse. */
struct FTNGhostHatchShard
{
	FVector2f Origin = FVector2f::ZeroVector;
	FVector2f Velocity = FVector2f::ZeroVector;
	float Spin = 0.f;
	float Size = 30.f;
	int32 Shape = 0;
};

/**
 * Transición de pantalla del que vuelve a la vida (TNGhost::ReviveIntoEgg, Docs/Fantasma_Espectador.md), solo en la
 * suya: mientras su fantasma se mete en el huevo la pantalla se pone negra; ¡pum! y una cáscara de huevo oscura tapa la
 * pantalla entera; con cada «pum» del huevo se resquebraja con líneas de luz por el medio; al eclosionar (y en cuanto ya
 * tiene su tortuga) las dos mitades salen despedidas entre trozos de cáscara y un fogonazo, y se ve a su tortuga saliendo
 * del huevo. Pintado en código (como la pantalla de carga del huevo, de la que reutiliza el blanco, los trozos de
 * cáscara y los sonidos sintetizados: crujidos, «¡pum!» y el soplido de las mitades).
 *
 * Modo carrera (ShowCurtain; Docs/Modo_Carrera.md, «Llegada al agua» y «Entre ronda y ronda»): la misma cáscara, pero sus
 * dos mitades entran deprisa desde arriba y desde abajo de la pantalla (se ve la partida por la rendija hasta que se
 * juntan con un «¡clac!»), se queda cerrada con la unión brillando mientras encima se enseña el puesto o el título de la
 * ronda, cada Knock es un «pum» desde dentro con su grieta de luz y Open la rompe (o la funde). La maneja
 * UTN_RaceScreensSubsystem.
 *
 * Con gafas (#646) el widget va en el panel de la interfaz (unos 80°) y, además, tapa el resto de la vista con una esfera
 * oscura alrededor de la cabeza (TNVR::SetViewCover) que se aclara al abrirse la cáscara.
 */
UCLASS()
class TORTUNABO_API UTN_GhostHatchWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Capa en el viewport: encima del HUD, las ruedas y las pantallas de la carrera; debajo del menú de pausa y de la carga. */
	static constexpr int32 ViewportZOrder = 50;

	/**
	 * Enseña la transición en la pantalla del jugador local PC. SecondsToDark: hasta que el fantasma entra en el huevo
	 * (todo negro y ¡pum!); SecondsToHatch: hasta que eclosiona (se abre en cuanto además ya tiene su tortuga).
	 */
	static void ShowFor(APlayerController* PC, float SecondsToDark, float SecondsToHatch);

	/**
	 * Modo carrera: la cáscara se cierra desde arriba y desde abajo en CloseSeconds y espera cerrada a Open; si nadie la
	 * abre en MaxHoldSeconds (desde que se cierra), se rompe sola. Null si PC no es un jugador local.
	 */
	static UTN_GhostHatchWidget* ShowCurtain(APlayerController* PC, float CloseSeconds, float MaxHoldSeconds = 30.f);

	/** Modo carrera: un «pum» desde dentro (tiembla, suena y, en los tres primeros, se abre una grieta de luz más). */
	void Knock();

	/**
	 * Se abre ya. Con bBurst, como al eclosionar: fogonazo y las mitades salen despedidas entre trozos de cáscara; si no, se
	 * funde en un momento (para devolver la vista sin fiesta). Se quita sola al acabar.
	 */
	void Open(bool bBurst);

	/** Las dos mitades tapan la pantalla entera (ya juntas y sin empezar a abrirse). */
	bool IsClosed() const;

	/** Ya se está abriendo o fundiendo (o se ha quitado). */
	bool IsOpening() const { return bFinished || OpenAt >= 0.f || FadeOutAt >= 0.f; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void Begin(float SecondsToDark, float SecondsToHatch);
	void BeginCurtain(float CloseSeconds, float MaxHoldSeconds);
	/** Unión, grietas, trozos y motas (una semilla por transición), pinceles y los sonidos del huevo. */
	void BuildShell();
	void TickCurtain(float Now);
	/** Con gafas (#646): el panel de la interfaz no abarca toda la vista; la esfera del rig (TNVR::SetViewCover) completa la cáscara. */
	void UpdateVRCover(float Now) const;
	void Finish();
	float Elapsed() const;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Root;

	UPROPERTY(Transient)
	TObjectPtr<UTN_EggSynthComponent> Synth;

	/** Reloj de la aplicación (FPlatformTime) al empezar, y los tiempos desde ahí. */
	double StartTime = 0.0;
	float DarkAt = 1.f;
	float HatchAt = 2.f;
	/** Cuándo se abrió (< 0: todavía no) y los golpes que ya han sonado. */
	float OpenAt = -1.f;
	int32 KnocksDone = 0;
	TArray<float> KnockTimes;
	bool bSlamDone = false;
	bool bFinished = false;
	/** Modo carrera: las mitades entran desde fuera (DarkAt es cuando se juntan) y la abre quien la puso (o HatchAt). */
	bool bCurtain = false;
	/** Cuándo empezó a fundirse (< 0: no se funde). */
	float FadeOutAt = -1.f;

	/** Línea de unión (0-1 de la pantalla, en zigzag), grietas y trozos (con una semilla por transición). */
	TArray<FVector2f> Seam;
	TArray<FTNGhostHatchCrack> Cracks;
	TArray<FTNGhostHatchShard> Shards;
	TArray<FVector2f> Speckles;

	FSlateBrush WhiteBrush;
	FSlateBrush ShardBrushes[3];
	FSlateFontInfo PumFont;
};
