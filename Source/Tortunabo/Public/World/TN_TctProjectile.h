#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_TctProjectile.generated.h"

class ATortugaCharacter;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

/** El lanzamiento de un proyectil de TcT: replicado una vez; cada máquina simula el vuelo desde aquí. */
USTRUCT()
struct FTNTctShotData
{
	GENERATED_BODY()

	/** ETNTctItem (BeachBall, Anchor, JellyDart, Cocobomba o Alga). */
	UPROPERTY()
	uint8 Kind = 0;

	UPROPERTY()
	FVector_NetQuantize Origin = FVector::ZeroVector;

	UPROPERTY()
	FVector_NetQuantize Velocity = FVector::ZeroVector;
};

/**
 * Proyectil de los objetos de Todos contra Todos (#651): el balón de playa, el ancla y el dardo de medusa.
 *
 * Red, como la bola (ATN_TctProjectile no replica el movimiento): el servidor lo crea con el lanzamiento (Shot, replicado en
 * el primer paquete) y cada máquina simula el mismo vuelo con su UProjectileMovementComponent. Los golpes los decide solo el
 * servidor, barriendo en cada fotograma el tramo recorrido contra las tortugas (la colisión del proyectil solo choca con el
 * escenario), y los manda con un multicast:
 *  - Balón: rebota en el escenario y empuja a quien toca (LaunchFromServer, sin corrección); al empujar, rebota hacia atrás
 *    y el servidor manda su posición y velocidad nuevas a todas (MulticastResync). Puede empujar varias veces.
 *  - Ancla: parábola pesada; al tocar a una tortuga o el suelo, derriba a las del círculo y las lastra (UTN_TctItemComponent).
 *  - Dardo: rápido y casi recto; marea a la primera tortuga que toca (MulticastApplyMareoEffect). Se clava en el escenario.
 *  - Cocobomba (#714): parábola con poco rebote; no golpea al tocar: a los CocoFuseSeconds explota donde esté y lanza a todas
 *    las del radio (TNTctItemRules::CocoBlast; el caparazón protege), también a quien la lanzó. Se ve el fogonazo en todas.
 *  - Alga (#714): lanzamiento corto; al caer deja un charco de alga (ATN_TctAlgaPuddle) en el suelo.
 * Quien lo lanza no se golpea a sí misma en los primeros instantes. Se acaba solo (vida máxima) o al caer al agua de la arena.
 */
UCLASS()
class TORTUNABO_API ATN_TctProjectile : public AActor
{
	GENERATED_BODY()

public:
	ATN_TctProjectile();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Servidor: Thrower lanza un proyectil Kind (ETNTctItem) hacia Direction desde delante de ella. false si no se ha podido
	 * crear.
	 */
	static bool ServerLaunch(ATortugaCharacter* Thrower, uint8 Kind, const FVector& Direction);

	uint8 GetKind() const { return Shot.Kind; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Tct")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Tct")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Tct")
	TObjectPtr<UProjectileMovementComponent> Movement;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Shot)
	FTNTctShotData Shot;

	UFUNCTION()
	void OnRep_Shot();

	/** Pone tamaño, malla y física del tipo y arranca el vuelo (una vez por máquina). */
	void ApplyShot();

	/** Servidor: el balón ha empujado a alguien: posición y velocidad nuevas para todas. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastResync(FVector_NetQuantize Location, FVector_NetQuantize Velocity);

	/** Servidor: el ancla o el dardo ha dado: se queda quieto en Location (y se va enseguida). */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastImpact(FVector_NetQuantize Location);

	UFUNCTION()
	void OnStop(const FHitResult& ImpactResult);

	UFUNCTION()
	void OnBounce(const FHitResult& ImpactResult, const FVector& ImpactVelocity);

	/** Servidor: tortugas en el tramo From→To y su golpe. */
	void ServerSweep(const FVector& From, const FVector& To);
	void ServerHitTurtle(ATortugaCharacter* Victim);
	/** Servidor: el ancla cae en Center. */
	void ServerAnchorSplash(const FVector& Center);
	/** Servidor: la cocobomba explota en Center. */
	void ServerCocoBlast(const FVector& Center);
	/** Servidor: el alga cae en Where y deja su charco. */
	void ServerDropPuddle(const FVector& Where);
	/** Máquinas con pantalla: el fogonazo de la cocobomba (se hincha y se apaga). */
	void ShowBlast();
	void TickBlast(float DeltaSeconds);
	/** Servidor: acaba el ancla o el dardo en Location. */
	void ServerFinish(const FVector& Location);
	/** Cualquier máquina: se para (clavado o caído) en Location. */
	void StopAt(const FVector& Location);

	/** Fogonazo de la cocobomba (solo visual). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BlastMesh;
	float BlastAge = -1.f;

	bool bShotApplied = false;
	bool bFinished = false;
	FVector PrevLocation = FVector::ZeroVector;
	float Age = 0.f;
	/** Servidor: hora del mundo del último reenvío tras un rebote (#708). */
	double LastBounceSyncTime = -1.0;
	/** Servidor: hasta cuándo (Age) no vuelve a golpear a cada tortuga. */
	TMap<TWeakObjectPtr<AActor>, float> RehitUntil;
};
