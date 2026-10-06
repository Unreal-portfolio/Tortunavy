#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/ProcMap/TN_ProcMapAmbientFX.h"
#include "TN_RaceWhirlpool.generated.h"

class ATortugaCharacter;
class UStaticMeshComponent;
class UTN_RaceItemSynthComponent;

/**
 * La tortuga que tiene atrapada el remolino (replicado una vez al atraparla y otra al soltarla). Cada máquina saca de aquí,
 * con el reloj del servidor, dónde va y cómo gira: nada de posiciones por fotograma en la red.
 */
USTRUCT()
struct FTNWhirlpoolCatch
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<ATortugaCharacter> Victim = nullptr;

	/** Dónde entró, en planta y respecto al centro (cm). */
	UPROPERTY()
	FVector_NetQuantize10 Entry = FVector_NetQuantize10(0.0, 0.0, 0.0);

	/** Su giro al entrar (grados). */
	UPROPERTY()
	float EntryYaw = 0.f;

	/** Hora del servidor en que la atrapó y en que la soltó (0 mientras la tiene). */
	UPROPERTY()
	float StartTime = 0.f;

	UPROPERTY()
	float ReleaseTime = 0.f;

	/** La tiene ahora. */
	UPROPERTY()
	bool bActive = false;

	/** Sube con cada tortuga atrapada. */
	UPROPERTY()
	uint8 Serial = 0;
};

/**
 * Remolino (objeto de carrera ETNRaceItem::Remolino, #786): trampa de agua que la tortuga deja detrás y dura 12 s. A la que
 * entra la hace girar y la atrae al centro durante 1,5 s (TNRaceItemRules::WhirlOffset y WhirlSpinYaw) y después la suelta,
 * lanzada hacia fuera y mareada 1 s (ralentizada con UTN_BeachTrapStatusComponent::ServerSlow y con los pajaritos). Quien lo
 * suelta no cae en él los 2 primeros segundos, y la que acaba de salir no vuelve a caer en el mismo en 3 s. El protector
 * solar lo atraviesa. Atrapa a una a la vez.
 *
 * Deriva de ATN_BeachEnemy para reutilizar la sujeción de tortugas (BeginHoldTurtle, PlaceHeldTurtle, EndHoldTurtle, sin
 * correcciones al dueño y con los abortos de la red de seguridad, los rescates y los gusanos), como el pelícano taxi. No es
 * un enemigo de verdad: no se marea ni se le puede dar con nada.
 */
UCLASS()
class TORTUNABO_API ATN_RaceWhirlpool : public ATN_BeachEnemy
{
	GENERATED_BODY()

public:
	ATN_RaceWhirlpool();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** Servidor: Turtle deja un remolino detrás (ya validado que puede usar el objeto). false si hay demasiados o no hay suelo. */
	static bool ServerDrop(ATortugaCharacter* Turtle);

	/** No se marea con nada (ni con lo lanzado ni con el silbato). */
	virtual bool AcceptsHitStun() const override { return false; }

	/** No tiene cuerpo al que dar. */
	virtual bool GetHitCapsule(FVector& OutA, FVector& OutB, float& OutRadius) const override { return false; }

	/** Servidor: se acaba ya (suelta a la que tenga). Lo usan el final de su vida, la ronda nueva y TN.Race.ItemClear. */
	void ServerEnd();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void ServerTick(float DeltaSeconds) override;
	virtual void VisualTick(float DeltaSeconds) override;
	virtual void OnHoldAborted(ATortugaCharacter* Turtle) override;
	virtual double GetMaxHoldSeconds() const override;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Catch)
	FTNWhirlpoolCatch Catch;

	/** Quien lo soltó y la hora del servidor en que nació y en que se acaba (0 = aún no). */
	UPROPERTY(Replicated)
	TObjectPtr<ATortugaCharacter> DroppedBy = nullptr;

	UPROPERTY(Replicated)
	float BornTime = 0.f;

	UPROPERTY(Replicated)
	float EndTime = 0.f;

	UFUNCTION()
	void OnRep_Catch();

	/** Segundos desde que atrapó a la de ahora (reloj del servidor). */
	float CatchAge() const;

	/** Todas las máquinas: sujeta a la atrapada y la coloca en la espiral, o la suelta a su hora. */
	void SyncHold();

	/** La cápsula de la atrapada de pie en el centro (justo antes de soltarla). */
	void PutVictimAtCenter(ATortugaCharacter* Victim) const;

	/** Servidor: busca a quién atrapar. */
	void ServerTryCatch();

	/** Servidor: la suelta a su hora: lanzada hacia fuera y mareada. */
	void ServerRelease();

	/** Servidor: deja de tenerla sin lanzarla (se la quitan o ya no está). */
	void ServerDropCatch(const TCHAR* Reason);

	/** Servidor: cuándo salió cada tortuga de este remolino (para no volver a atraparla enseguida). */
	TMap<TWeakObjectPtr<ATortugaCharacter>, double> ReleasedAt;

	/** Serie de la atrapada que esta máquina ya ha soltado (no la vuelve a sujetar). */
	uint8 LocalDoneSerial = 0;

	/** Ronda del generador en que nació (se acaba con la ronda). */
	int32 SpawnRound = -1;
	bool bServerEnding = false;

	// ── Visual ──

	void BuildVisuals();

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> WaterMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> FoamMesh;

	UPROPERTY(Transient)
	TObjectPtr<UTN_RaceItemSynthComponent> Sfx;

	TNAmbientFX::FEmitter Splash;
	bool bVisualsBuilt = false;
	float SpinAngle = 0.f;
	float GurgleTimer = 0.f;
	/** Pajaritos del mareo puestos en esta máquina a la última soltada (serie). */
	uint8 DizzyShownSerial = 0;
	bool bDizzyOn = false;
	TWeakObjectPtr<ATortugaCharacter> DizzyVictim;
	uint8 CaughtSoundSerial = 0;
};
