#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TN_Chapa.generated.h"

class ATortugaCharacter;
class USoundBase;
class UStaticMeshComponent;

/** Estado replicado de una chapa: su vuelo (una vez) y, al caer, dónde queda. Cada máquina dibuja el mismo arco desde aquí. */
USTRUCT()
struct FTNChapaFlight
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize10 Origin = FVector::ZeroVector;

	UPROPERTY()
	FVector_NetQuantize10 Velocity = FVector::ZeroVector;

	/** Hora del servidor del lanzamiento (TNItemRuntime::ServerNow). */
	UPROPERTY()
	float StartTime = 0.f;

	/** Gravedad del vuelo (cm/s²): menor que la del mundo, planea como un frisbee. */
	UPROPERTY()
	float Gravity = 500.f;

	/** Ya está en el suelo, tumbada en RestLocation: se puede recoger. */
	UPROPERTY()
	bool bResting = false;

	UPROPERTY()
	FVector_NetQuantize10 RestLocation = FVector::ZeroVector;

	UPROPERTY()
	float RestYaw = 0.f;
};

/**
 * @brief La chapa (#858, plan maestro §1 decisión 6): la moneda de la partida. Objeto del mundo, replicado, que se recoge al
 * pasar por encima y se apila en el contador de chapas del inventario (UTN_InventoryComponent::GetChapaCount), sin ocupar
 * ninguno de los dos huecos. No pasa al perfil.
 *
 * Se lanza en vertical, como un frisbee: un disco de canto que gira sobre su eje, con un arco parabólico predecible (planea:
 * gravedad propia) hacia el centro de la pantalla. Con las aletas vacías, la tecla de soltar (X / Triángulo) lanza una;
 * apuntando al suelo, se tira. Cae y se queda tumbada; cualquier tortuga la recoge (quien la lanzó, pasado un momento).
 *
 * Red, como los objetos lanzados del coop: sin movimiento replicado. El servidor la crea con el vuelo en el primer paquete y
 * cada máquina pone la chapa en el mismo arco por la hora del servidor. Lo que pasa lo decide solo el servidor: si cruza la
 * ranura de una máquina expendedora (ATN_VendingMachine) suma su valor al crédito de quien la lanzó y desaparece; si choca,
 * cae al suelo y replica dónde queda.
 *
 * Arte: la malla es un cilindro aplastado del motor con color (falta el arte de la chapa).
 */
UCLASS()
class TORTUNABO_API ATN_Chapa : public AActor
{
	GENERATED_BODY()

public:
	ATN_Chapa();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Servidor: Thrower lanza una de sus chapas hacia el centro de su pantalla. Null (y no gasta nada) si no tiene chapas o no
	 * puede lanzar ahora.
	 */
	static ATN_Chapa* ServerThrowFrom(ATortugaCharacter* Thrower);

	/**
	 * Servidor: suelta Count chapas en Where, que saltan un poco hacia los lados y caen al suelo (cajas, airdrop, premios).
	 * Devuelve cuántas ha creado.
	 */
	UFUNCTION(BlueprintCallable, Category = "Economía", meta = (WorldContext = "WorldContextObject"))
	static int32 SpawnChapas(UObject* WorldContextObject, FVector Where, int32 Count);

	/** Servidor: una chapa que sale de Origin con Velocity; Thrower (puede ser nulo) es quien cobra si entra por una ranura. */
	static ATN_Chapa* SpawnFlying(UWorld* World, const FVector& Origin, const FVector& Velocity, ATortugaCharacter* Thrower);

	/**
	 * Servidor: avanza el vuelo de FromSeconds a ToSeconds desde el lanzamiento. Si cruza la ranura de una máquina, cobra y
	 * desaparece; si choca con el escenario, cae al suelo. Lo llama el Tick (y los tests, sin esperar).
	 */
	void ServerStepFlight(double FromSeconds, double ToSeconds);

	/** Servidor: Turtle la recoge si puede (viva, de pie, con sitio; quien la lanzó, pasado ThrowerPickupDelaySeconds). */
	bool ServerTryCollect(ATortugaCharacter* Turtle);

	/** Servidor: deja la chapa tumbada en el suelo bajo Where. */
	void ServerRestAt(const FVector& Where);

	bool IsResting() const { return Flight.bResting; }
	bool IsConsumed() const { return bConsumed; }
	ATortugaCharacter* GetThrower() const { return Thrower.Get(); }
	int32 GetValue() const { return Value; }
	const FTNChapaFlight& GetFlight() const { return Flight; }

	/** Velocidad de salida (cm/s) y gravedad del vuelo (cm/s²) con las que se lanzan las chapas. */
	float GetThrowSpeed() const { return ThrowSpeed; }
	float GetFlightGravity() const { return FlightGravity; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Chapa")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Velocidad de salida al lanzarla (cm/s). */
	UPROPERTY(EditDefaultsOnly, Category = "Chapa|Vuelo", meta = (ClampMin = "200.0"))
	float ThrowSpeed = 1300.f;

	/** Gravedad del vuelo (cm/s²): menos que la del mundo, para que planee. */
	UPROPERTY(EditDefaultsOnly, Category = "Chapa|Vuelo", meta = (ClampMin = "50.0"))
	float FlightGravity = 500.f;

	/** Vueltas sobre su eje en el aire (grados por segundo). */
	UPROPERTY(EditDefaultsOnly, Category = "Chapa|Vuelo")
	float SpinDegPerSecond = 900.f;

	/** Vuelo más largo (s): pasado esto, cae al suelo de debajo aunque no haya chocado. */
	UPROPERTY(EditDefaultsOnly, Category = "Chapa|Vuelo", meta = (ClampMin = "0.5"))
	float MaxFlightSeconds = 4.f;

	/** Radio (cm) de la chapa al chocar. */
	UPROPERTY(EditDefaultsOnly, Category = "Chapa|Vuelo", meta = (ClampMin = "1.0"))
	float CollisionRadius = 6.f;

	/** Distancia (cm, en planta) a la que una tortuga la recoge al pasar. */
	UPROPERTY(EditDefaultsOnly, Category = "Chapa|Recoger", meta = (ClampMin = "10.0"))
	float CollectRadius = 90.f;

	/** Segundos desde que cae hasta que quien la lanzó la puede volver a coger (los demás, al momento). */
	UPROPERTY(EditDefaultsOnly, Category = "Chapa|Recoger", meta = (ClampMin = "0.0"))
	float ThrowerPickupDelaySeconds = 1.5f;

	/** Sonido al lanzarla y al recogerla. Nulo = el sintetizado de los objetos. */
	UPROPERTY(EditDefaultsOnly, Category = "Chapa|Audio")
	TObjectPtr<USoundBase> ThrowSound;

	UPROPERTY(EditDefaultsOnly, Category = "Chapa|Audio")
	TObjectPtr<USoundBase> CollectSound;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Flight)
	FTNChapaFlight Flight;

	UFUNCTION()
	void OnRep_Flight();

	/** Servidor: se ha metido por una ranura. Se esconde en todas las máquinas y se va. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastConsumed();

	/** Pone la chapa donde toca ahora (en el arco o tumbada). */
	void ApplyFlightPose(double Seconds);

	/** Segundos desde el lanzamiento según la hora del servidor. */
	double FlightSeconds() const;

	/** Servidor: el primer choque con el escenario de A a B. */
	bool SweepWorld(const FVector& A, const FVector& B, FHitResult& OutHit) const;

	/** El suelo bajo Where (o Where, si no hay suelo a mano). */
	FVector GroundUnder(const FVector& Where) const;

	/** Servidor: busca tortugas que pasen por encima. */
	void ServerScanCollectors();

	void PlayChapaSound(ATortugaCharacter* Turtle, USoundBase* Sound, bool bThrow) const;

	/** Servidor: quien la lanzó (cobra el crédito si entra por una ranura). */
	TWeakObjectPtr<ATortugaCharacter> Thrower;

	/** Crédito que da (UTN_EconomySettings::ChapaValue al crearla). */
	int32 Value = 1;

	/** Servidor: hasta dónde se ha simulado el vuelo (s) y hora del servidor a la que cayó. */
	double SteppedSeconds = 0.0;
	double RestServerTime = 0.0;
	float CollectScanClock = 0.f;

	bool bConsumed = false;
	/** Cliente: el arco ha chocado aquí y espera a que el servidor diga dónde queda. */
	bool bClientFrozen = false;
	FVector LastClientPoint = FVector::ZeroVector;
};
