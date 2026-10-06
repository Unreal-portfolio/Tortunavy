#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Game/TN_TctRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "TN_TctFloodWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UTextBlock;
class UWidget;

/** Lo que enseña el HUD del agua de Todos contra Todos en un fotograma (lo rellena UTN_TctHudSubsystem). */
struct FTNTctFloodView
{
	/** La próxima subida y si el agua sube ahora (ATN_TctGameState::GetNextRise). */
	FTNTctNextRise Next;
	/** Veneno de la tortuga propia (0-1). */
	float Poison = 0.f;
};

/**
 * @brief HUD del agua venenosa de Todos contra Todos (#831): arriba a la derecha, siempre, cuándo es la próxima subida y en
 * qué tramo va; en los TNTctPoisonDefaults::WarnSeconds antes de cada subida, una cinta con la cuenta atrás («¡El agua sube en
 * 3!») que explica la marca verde del mapa; y abajo, el veneno de la tortuga propia mientras lo tiene. No coge el ratón.
 */
UCLASS()
class TORTUNABO_API UTN_TctFloodWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetView(const FTNTctFloodView& InView);

	/** Se va con un fundido. */
	void Dismiss();
	bool IsDismissing() const { return DismissAt >= 0.f; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildTree();

	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Canvas;
	UPROPERTY(Transient) TObjectPtr<UWidget> Pill;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PillLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PillTime;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PillTier;
	UPROPERTY(Transient) TObjectPtr<UWidget> Warning;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WarningText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WarningSubText;
	UPROPERTY(Transient) TObjectPtr<UWidget> PoisonBox;
	UPROPERTY(Transient) TObjectPtr<UBorder> PoisonFill;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PoisonText;

	FTNTctFloodView View;
	float Time = 0.f;
	/** Segundo entero que enseña la pastilla y la cinta (para no reescribirlos) y cuándo cambió (rebote). */
	int32 ShownSeconds = -1;
	int32 ShownWarnSeconds = -1;
	bool bShownRising = false;
	bool bShownFinal = false;
	float PopAt = -10.f;
	float DismissAt = -1.f;
};

/**
 * Pone y quita UTN_TctFloodWidget en la máquina con pantalla: mientras la ronda de Todos contra Todos está en juego (fase
 * Racing de ATN_TctGameState). Sin RPC: cada fotograma mira lo que ya se replica (el plan del agua y el veneno de la tortuga).
 */
UCLASS()
class TORTUNABO_API UTN_TctHudSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTN_TctFloodWidget> Widget;

	/** Capa: por encima del HUD, debajo del reloj de la ronda (14) y de la cuenta atrás (15). */
	static constexpr int32 ZOrder = 13;
};
