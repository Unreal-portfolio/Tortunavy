#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_ShellBody.generated.h"

class ATortugaCharacter;
class UBoxComponent;
class UPhysicalMaterial;
class UPrimitiveComponent;

/**
 * Caparazón con física propia: mientras la tortuga está metida en su caparazón (y nadie la lleva) su cuerpo es esta
 * caja que simula física (rueda, resbala, rebota, la empujan los demás y cae por las cuestas). La tortuga no se
 * controla: su cápsula y su malla siguen a la caja en todas las máquinas.
 *
 * - Caja de 55 x 46 x 42 cm (largo cola-cabeza, ancho y alto tripa-lomo), sacada del tronco de la malla, con perfil
 *   PhysicsActor, 38 kg, amortiguación suave, CCD, colisión con aristas suavizadas (no tropieza en las costuras de las
 *   teselas del terreno), giro máximo de 900 °/s, salida lenta de lo que solape al nacer y un material resbaladizo
 *   (fricción 0,25, rebote 0,2).
 * - Replicada con su movimiento (física replicada, en interpolación predictiva: los clientes corrigen con velocidad hacia
 *   el estado extrapolado del servidor; TN.Shell.PhysicsRep 0 vuelve a la de siempre) y con la relevancia de su tortuga
 *   (es su dueña). Cada máquina pone la cápsula de la tortuga de pie sobre la caja y la malla tumbada sobre la tripa con
 *   la transformación de la caja (Tick en TG_PostPhysics, a través de UTN_ShellComponent::FollowBody).
 * - En el servidor: si tiene que salir al pararse (lanzamiento, caída larga, escape), cuando se queda quieta saca a la
 *   tortuga del caparazón; si cae al agua, sale y nada; si la destruye el mundo (caída fuera), avisa al componente.
 *
 * La crean y la destruyen UTN_ShellComponent::StartBody / StopBody.
 */
UCLASS(NotBlueprintable)
class TORTUNABO_API ATN_ShellBody : public AActor
{
	GENERATED_BODY()

public:
	/** Amortiguación lineal de la caja (1/s): frena un poco en el aire; los lanzamientos al punto de mira la compensan. */
	static constexpr float BoxLinearDamping = 0.25f;

	ATN_ShellBody();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Servidor, justo tras el spawn: la tortuga a la que pertenece y si sale del caparazón al pararse. */
	void InitBody(ATortugaCharacter* InTurtle, bool bInExitOnRest);

	ATortugaCharacter* GetTurtle() const { return Turtle; }
	UBoxComponent* GetBox() const { return Box; }

	/** La destruye el componente de caparazón (no hay que avisarle en EndPlay). */
	void MarkReleased() { bReleased = true; }

	/**
	 * Servidor: la caja atraviesa todo (no choca con nada, sigue cayendo con la gravedad) o vuelve a chocar como siempre. Se
	 * replica: en cada máquina la caja deja de chocar a la vez, así la réplica de la física no la pelea contra una pared que
	 * en el servidor ha cruzado. Mientras atraviesa, el servidor no la saca del agua ni de debajo del terreno. Lo usa la
	 * patada de la tormenta cuando no hay arco libre hasta su sitio (ATN_BeachStorm).
	 */
	void SetPassThrough(bool bOn);

	bool IsPassThrough() const { return bPassThrough; }

	/** Semiejes de la caja (cm). */
	static FVector BoxHalfExtent() { return FVector(27.5, 23.0, 21.0); }

	/**
	 * Transformación en el mundo de la malla de la tortuga (tumbada sobre la tripa, la cabeza hacia +X de la caja) para
	 * una caja en BoxWorld. MeshScale es la escala de la malla (2,5 de serie).
	 */
	static FTransform MeshWorldTransform(const FTransform& BoxWorld, const FVector& MeshScale);

	/** true si el centro del componente está dentro de un volumen de agua (APhysicsVolume con bWaterVolume). */
	static bool IsInWater(const UPrimitiveComponent& Component);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shell")
	TObjectPtr<UBoxComponent> Box;

private:
	UPROPERTY(Replicated)
	TObjectPtr<ATortugaCharacter> Turtle;

	/** Material físico resbaladizo propio (se crea en BeginPlay en cada máquina). */
	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> Slippery;

	/** Atraviesa todo (SetPassThrough). */
	UPROPERTY(ReplicatedUsing = OnRep_PassThrough)
	bool bPassThrough = false;

	UFUNCTION()
	void OnRep_PassThrough();

	/** Pone en la caja de esta máquina la colisión que toca: ninguna mientras atraviesa, la de siempre después. */
	void ApplyPassThrough();

	/** Servidor: parada (si toca salir al pararse), agua y límite de tiempo. */
	void ServerChecks(float DeltaSeconds);


	/**
	 * Instrumento TN.Shell.Debug (todas las máquinas): torbellino, caja bajo el terreno y saltos de velocidad, con los
	 * últimos choques de la caja (quién la empuja, su normal, su impulso y la velocidad del otro) y, en los clientes, el
	 * desfase con el último estado del servidor. Solo escribe cuando hay una anomalía (TNShellLogic::ClassifyShellMotion).
	 */
	void TickDebugWatch(float DeltaSeconds);

	UFUNCTION()
	void HandleDebugHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse,
		const FHitResult& Hit);

	/** Los choques recientes, del más nuevo al más viejo, para el registro. */
	FString DescribeDebugContacts(float Now) const;

	/** Último choque con cada componente (los más recientes, sin repetir componente). */
	struct FDebugContact
	{
		TWeakObjectPtr<const UPrimitiveComponent> Component;
		FString Name;
		FVector Normal = FVector::ZeroVector;
		FVector OtherVelocity = FVector::ZeroVector;
		float Impulse = 0.f;
		float Time = 0.f;
		bool bOtherSimulating = false;
	};
	TArray<FDebugContact> DebugContacts;
	FVector DebugPrevVelocity = FVector::ZeroVector;
	float DebugSpinSeconds = 0.f;
	float DebugNextLogTime = 0.f;
	bool bDebugBound = false;

	bool bExitOnRest = false;
	bool bReleased = false;
	float Age = 0.f;
	float RestTime = 0.f;
	float WaterCheckTimer = 0.f;
};
