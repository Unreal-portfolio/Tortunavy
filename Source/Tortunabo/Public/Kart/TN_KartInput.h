// Acciones y contextos de Enhanced Input propios de los karts, creados en C++ en ejecución (sin .uasset de input), por
// encima de los del buggy (UTN_BuggyInputSet). Conductora: usar objeto (E, clic derecho, LB), mirar atrás (Q, B), mirar
// alrededor con el ratón o el stick derecho y disparar (clic izquierdo, RB) hacia donde mira si va sola (#295). Artillera: usar objeto (E,
// clic derecho, LT), tirarlo hacia atrás (Q, LB) e inclinarse (A/D, stick izquierdo), que cambia cuánto gira el kart (#295).
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TN_KartInput.generated.h"

class APlayerController;
class UInputAction;
class UInputMappingContext;

UCLASS(Transient)
class TORTUNABO_API UTN_KartInputSet : public UObject
{
	GENERATED_BODY()

public:
	/** Por encima de los contextos del buggy (UTN_BuggyInputSet::ContextPriority): el objeto se queda con su botón. */
	static constexpr int32 ContextPriority = 11;

	static UTN_KartInputSet* Create(UObject* Outer);

	static void AddContext(const APlayerController* PC, const UInputMappingContext* Context);
	static void RemoveContext(const APlayerController* PC, const UInputMappingContext* Context);

	UPROPERTY() TObjectPtr<UInputAction> UseItem;
	/** Mantenido: el objeto sale hacia atrás (y la conductora mira atrás). No se queda la tecla: el buggy también la usa. */
	UPROPERTY() TObjectPtr<UInputAction> Backward;
	/** Conductora sola: disparar el coco hacia donde mira la cámara (mantenido repite; la cadencia la pone el servidor). */
	UPROPERTY() TObjectPtr<UInputAction> Fire;
	/** Conductora: mirar alrededor con el ratón (delta por fotograma) y con el stick derecho (velocidad, -1..1). */
	UPROPERTY() TObjectPtr<UInputAction> LookMouse;
	UPROPERTY() TObjectPtr<UInputAction> LookStick;
	/** Artillera: inclinarse (-1 izquierda .. +1 derecha). */
	UPROPERTY() TObjectPtr<UInputAction> Lean;

	UPROPERTY() TObjectPtr<UInputMappingContext> DriverContext;
	UPROPERTY() TObjectPtr<UInputMappingContext> GunnerContext;
};
