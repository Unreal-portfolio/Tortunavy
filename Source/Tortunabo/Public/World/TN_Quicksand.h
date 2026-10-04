#pragma once

#include "CoreMinimal.h"
#include "World/TN_SlowZoneVolume.h"
#include "TN_Quicksand.generated.h"

class ACharacter;
class UNiagaraSystem;
class USoundBase;
class UStaticMeshComponent;

/**
 * Arenas movedizas (#684, Excel_DayT): charco de arena más oscura que la del suelo. Hereda la zona lenta (salto y caída
 * de sirope, colisión de solape) y añade:
 *  - Ralentización progresiva: cuanto más rato dentro, más despacio (TNBeachCreatureRules::Quicksand::SpeedFactor).
 *    Como la zona lenta, la aplica cada máquina a la tortuga que simula (servidor y dueño).
 *  - Atrapada: tras TrapAfterSeconds seguidos dentro, el servidor la deja quieta y hundida (UTN_BeachTrapStatusComponent)
 *    hasta que se suelta machacando salto o pasa MaxTrappedSeconds; sale con un saltito hacia fuera y mareada.
 *  - Nunca muere ni pierde nada.
 *
 * No se replica: cada máquina crea la suya (como las zonas lentas del mapa de Supervivencia) o la crea el elemento
 * replicado de la carrera (ATN_BeachQuicksand). Las decisiones solo las toma la del servidor.
 */
UCLASS(Blueprintable)
class TORTUNABO_API ATN_Quicksand : public ATN_SlowZoneVolume
{
	GENERATED_BODY()

public:
	ATN_Quicksand();

	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Radio del charco (cm): ajusta la caja de solape y monta la malla. Llamar al crearla desde código. */
	void SetQuicksandRadius(float Radius);

	float GetQuicksandRadius() const { return QuicksandRadius; }

	/** Segundos seguidos dentro hasta quedar atrapada. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quicksand", meta = (ClampMin = "0.5"))
	float TrapAfterSeconds = 3.f;

	/** Tope de segundos atrapada (si no se suelta antes machacando salto). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quicksand", meta = (ClampMin = "1.0"))
	float MaxTrappedSeconds = 4.f;

	/** Fracción de la velocidad de andar al entrar y la mínima, y lo que tarda en llegar a ella. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quicksand", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float StartSpeedFactor = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quicksand", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float MinSpeedFactor = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quicksand", meta = (ClampMin = "0.1"))
	float RampSeconds = 2.5f;

	/** Cuánto se hunde la tortuga atrapada (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quicksand", meta = (ClampMin = "0.0"))
	float SinkDepth = 35.f;

	/** Saltito al salir: hacia fuera del charco y hacia arriba (cm/s), y mareo al salir (s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quicksand", meta = (ClampMin = "0.0"))
	float EscapeHopOut = 520.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quicksand", meta = (ClampMin = "0.0"))
	float EscapeHopUp = 620.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quicksand", meta = (ClampMin = "0.0"))
	float DizzySeconds = 1.2f;

	/** Sin volver a atrapar a la misma en estos segundos tras soltarla. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quicksand", meta = (ClampMin = "0.0"))
	float ImmuneSeconds = 3.f;

	/** Al quedar atrapada (glup) y al salir (plof), en cada máquina. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quicksand|Feedback")
	TObjectPtr<USoundBase> TrapSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quicksand|Feedback")
	TObjectPtr<USoundBase> EscapeSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quicksand|Feedback")
	TObjectPtr<UNiagaraSystem> EscapeVFX;

protected:
	/** Charco oscuro (malla generada por código, sin colisión). */
	UPROPERTY(VisibleAnywhere, Category = "Quicksand")
	TObjectPtr<UStaticMeshComponent> PuddleMesh;

private:
	struct FInside
	{
		float Seconds = 0.f;
		bool bWasTrapped = false;
	};

	/** Pies dentro del charco (en planta, dentro del radio y cerca de la arena). */
	bool IsInsidePuddle(const ACharacter* Turtle) const;
	/** Servidor: atrapa, suelta y aplica el escape. */
	void ServerUpdate(ACharacter* Turtle, FInside& State, double Now);
	/** En esta máquina: sonidos al cambiar de atrapada a suelta. */
	void LocalFeedback(ACharacter* Turtle, FInside& State);
	void ClearLocalCap(ACharacter* Turtle) const;
	FName ProgressiveSource() const { return FName(TEXT("Quicksand"), static_cast<int32>(GetUniqueID())); }

	float QuicksandRadius = 400.f;
	TMap<TWeakObjectPtr<ACharacter>, FInside> Inside;
	TMap<TWeakObjectPtr<ACharacter>, double> ImmuneUntil;
	TMap<TWeakObjectPtr<ACharacter>, double> TrappedAt;
};
