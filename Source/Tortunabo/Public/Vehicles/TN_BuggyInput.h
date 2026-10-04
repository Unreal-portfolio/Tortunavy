// Acciones y contextos de Enhanced Input del buggy, creados en C++ en tiempo de ejecución (sin .uasset de input).
// Controles de Docs/Rally_MVP.md: teclado y ratón, y mando; con gafas, los Touch (Docs/Modo_VR.md, «Vehículos»).
#pragma once

#include "CoreMinimal.h"
#include "InputModifiers.h"
#include "UObject/Object.h"
#include "TN_BuggyInput.generated.h"

class APlayerController;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;
struct FKey;

/**
 * Medio eje de un stick como botón (mandos Touch, que no dan las direcciones del stick como botones con OpenXR): deja el
 * lado positivo del eje (o el negativo, ya en positivo) y quita el otro, para que un disparador «Down» solo salte hacia ese
 * lado.
 */
UCLASS(NotBlueprintable, meta = (DisplayName = "Medio eje (Tortunavy)"))
class TORTUNABO_API UTN_InputModifierHalfAxis : public UInputModifier
{
	GENERATED_BODY()

public:
	/** Si deja el lado negativo (stick hacia abajo o a la izquierda). */
	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bNegative = false;

protected:
	virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue,
		float DeltaTime) override;
};

UCLASS(Transient)
class TORTUNABO_API UTN_BuggyInputSet : public UObject
{
	GENERATED_BODY()

public:
	/** Prioridad de los contextos del buggy: por encima de los del personaje, que pueden seguir puestos. */
	static constexpr int32 ContextPriority = 10;

	/** Crea las acciones y los dos contextos (conductora y artillera) con Outer como dueño. */
	static UTN_BuggyInputSet* Create(UObject* Outer);

	/** Pone Context en el subsistema de Enhanced Input del jugador local de PC (no hace nada si no es local). */
	static void AddContext(const APlayerController* PC, const UInputMappingContext* Context);
	static void RemoveContext(const APlayerController* PC, const UInputMappingContext* Context);

	/** Sentido de CycleAmmo: +1 (rueda arriba, cruceta derecha), -1 (rueda abajo, cruceta izquierda) o 0. */
	static int32 CycleDirection(const FInputActionValue& Value);

	// ── Mandos Touch (con gafas; sin ellas esas teclas no llegan nunca) ─────────

	/** Gatillo de los Touch como botón: a partir del 55 % (TNVRMath::AnalogPressThreshold). */
	static void MapTouchTrigger(UInputMappingContext* Context, const UInputAction* Action, const FKey& AxisKey);
	/** Un eje de los Touch tal cual (con zona muerta). */
	static void MapTouchAxis(UInputMappingContext* Context, const UInputAction* Action, const FKey& AxisKey, bool bSwizzleToY = false);
	/** Un stick de los Touch empujado hacia un lado (bNegative: abajo o a la izquierda) como botón, a partir del 60 %. */
	static void MapTouchStickDirection(UInputMappingContext* Context, const UInputAction* Action, const FKey& AxisKey, bool bNegative);
	/** Un botón de los Touch (A, B, X, Y). */
	static void MapTouchButton(UInputMappingContext* Context, const UInputAction* Action, const FKey& Key);

	// Conductora
	UPROPERTY() TObjectPtr<UInputAction> Throttle;
	UPROPERTY() TObjectPtr<UInputAction> Brake;
	UPROPERTY() TObjectPtr<UInputAction> Steer;
	/** Freno de mano (Shift izquierdo · X). */
	UPROPERTY() TObjectPtr<UInputAction> Handbrake;
	/** Turbo (Espacio · A): mientras se mantiene y haya carga. */
	UPROPERTY() TObjectPtr<UInputAction> Boost;
	/** Atrás (Q · B): mientras se mantiene, la conductora sola dispara hacia atrás. */
	UPROPERTY() TObjectPtr<UInputAction> FireBack;

	// Las dos
	UPROPERTY() TObjectPtr<UInputAction> SelfRight;
	UPROPERTY() TObjectPtr<UInputAction> FireCoco;
	UPROPERTY() TObjectPtr<UInputAction> FireSpecial;
	/** Cambio de munición (rueda del ratón · cruceta izquierda y derecha): Axis1D, el signo da el sentido. */
	UPROPERTY() TObjectPtr<UInputAction> CycleAmmo;

	// Artillera
	/** Apuntar con el ratón (delta por frame). */
	UPROPERTY() TObjectPtr<UInputAction> AimMouse;
	/** Apuntar con el stick derecho (velocidad, -1..1 por eje). */
	UPROPERTY() TObjectPtr<UInputAction> AimStick;
	/** Cantar la próxima nota de copiloto a la conductora (F · A, #330). */
	UPROPERTY() TObjectPtr<UInputAction> CallNote;
	/** Aviso rápido (#330): Axis1D, +1 «¡Turbo ya!» (1 · cruceta arriba), -1 «¡Frena!» (2 · B). */
	UPROPERTY() TObjectPtr<UInputAction> QuickCall;

	UPROPERTY() TObjectPtr<UInputMappingContext> DriverContext;
	UPROPERTY() TObjectPtr<UInputMappingContext> GunnerContext;
};
