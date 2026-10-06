#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/ITN_EnemyTargetInterface.h"
#include "World/Beach/TN_BeachCatapult.h"
#include "World/ProcMap/TN_ProcSearchSpot.h"
#include "TN_TutorialPractice.generated.h"

class ATN_TutorialCourse;
class UBoxComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * @brief Cangrejo de prácticas del tutorial (estación «Lanzar»): un cangrejo de playa grande (las mallas por piezas de la
 * fauna del mapa procedural, TN_ProcMapFaunaMeshes.h) que va de lado a lado sobre su peana y no hace nada a nadie.
 *
 * Es un enemigo a efectos de los objetos (ITN_EnemyTargetInterface): la bola o la tinta que le dan lo marean unos segundos
 * (se para, se le cierran las pinzas y le dan vueltas unas estrellitas) y el recorrido apunta la tarea a quien la lanzó.
 * La colisión es una caja que bloquea (la bola rebota en ella y la tortuga no lo atraviesa).
 *
 * Red: lo crea el servidor; el vaivén sale de la hora del servidor (igual en todas las máquinas, sin mover nada por red)
 * y el mareo de StunnedUntil (hora del servidor, replicada).
 */
UCLASS()
class TORTUNABO_API ATN_TutorialDummy : public AActor, public ITN_EnemyTargetInterface
{
	GENERATED_BODY()

public:
	ATN_TutorialDummy();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ── ITN_EnemyTargetInterface (servidor) ──────────────────────────────────
	virtual void ApplyStun(float Duration) override;
	virtual void ApplyBlind(float Duration) override;
	virtual bool IsStunned() const override;
	virtual bool IsBlinded() const override { return false; }

	/** Servidor: el recorrido al que avisar cuando le dan. */
	void SetCourse(ATN_TutorialCourse* InCourse);

	/** Cuánto va de lado a lado (cm, a cada lado del centro de su peana, a lo largo de su Y). */
	UPROPERTY(EditAnywhere, Category = "Tutorial")
	float SwayDistance = 130.f;

	/** Segundos de mareo por golpe. */
	UPROPERTY(EditAnywhere, Category = "Tutorial")
	float StunSeconds = 3.f;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Tutorial")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Lo que se mueve: la caja de colisión y las piezas del cangrejo. */
	UPROPERTY(VisibleAnywhere, Category = "Tutorial")
	TObjectPtr<UBoxComponent> Body;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Stars;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> Meshes;

	/** Hora del servidor hasta la que está mareado (< 0 = no lo está). */
	UPROPERTY(Replicated)
	float StunnedUntil = -1.f;

private:
	TWeakObjectPtr<ATN_TutorialCourse> Course;

	/** Por pieza: canal de animación (TNFauna::ETNFaunaBone) y pivote en el espacio del cuerpo. */
	TArray<uint8> PartBone;
	TArray<FVector> PartPivot;
	float BodyZ = 0.f;
	/** Posición del vaivén al empezar el mareo (se queda ahí mientras dura). */
	float FrozenSway = 0.f;
	bool bWasStunned = false;
	float AnimClock = 0.f;

	void BuildCrab();
	double ServerNow() const;
	float SwayAt(double Now) const;
	void Animate(float DeltaSeconds);
};

/**
 * @brief Montículo de arena de la estación «Rebuscar»: se rebusca como los decorados del mapa (mantener la tecla de
 * interactuar, aro del HUD, sonido y el objeto de un saltito) y siempre da una bola para la estación de lanzar. Se puede
 * repetir tras un respiro (si la bola se pierde por el borde, aquí hay otra). La malla la pone el recorrido.
 */
UCLASS()
class TORTUNABO_API ATN_TutorialSearchSpot : public ATN_ProcSearchSpot
{
	GENERATED_BODY()

public:
	ATN_TutorialSearchSpot();

protected:
	virtual float GetLuck() const override { return 1.f; }
	virtual float GetLootWeight(FName RowName, const FTN_InventoryItem& Row) const override;
	/** Solo el catálogo con GetLootWeight (la bola): sin los objetos del coop que sortea el rebuscable del mapa (#791). */
	virtual bool PickLoot(FTN_InventoryItem& OutItem, const APawn* Searcher) const override;
};

/**
 * @brief La catapulta de la playa (ATN_BeachCatapult) en la terraza del tutorial: la misma cuchara y las mismas reglas
 * (entrar de pie o hecha bola, aviso, disparo como bola de caparazón), pero se recarga en vez de partirse, apunta a la
 * pradera del otro lado del cañón (su +X; sin generador de playa no se gira hacia ningún mar) y solo la reciben por red
 * los que están cerca.
 */
UCLASS()
class TORTUNABO_API ATN_TutorialCatapult : public ATN_BeachCatapult
{
	GENERATED_BODY()

public:
	ATN_TutorialCatapult();

	/** Servidor, antes de FinishSpawning: su forma (semilla y tamaño, como las de la playa). */
	void SetupTutorialSpec(int32 InSeed, float InSizeScale);
};
