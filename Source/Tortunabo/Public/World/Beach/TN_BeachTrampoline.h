#pragma once

#include "CoreMinimal.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachTrapCommon.h"
#include "World/Beach/TN_BeachTrampolineRules.h"
#include "TN_BeachTrampoline.generated.h"

class AActor;
class ACharacter;
class APawn;
class UPrimitiveComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTN_PlaygroundSynthComponent;

/**
 * Trampolín de la playa, con la idea de la medusa cama elástica del lobby (ATN_JellyfishTrampoline): rebota todo el
 * cuerpo (cima, costados y borde, también de lado desde la arena) con un «boing» sintetizado. Cuatro variantes según la
 * semilla (todas a escala TNBeach::Scale y dentro de su huella de 7 m):
 *
 * - Medusa gorda varada (campana de ~10 m, cima a ~3 m) con su trébol, motas, una cara simpática mirando a la salida y
 *   brazos orales tendidos en la arena.
 * - Colchoneta hinchable de cinco tubos a rayas con su almohada (1,4 m; la almohada, 1,9 m).
 * - Flotador de donut (14 m, 4,2 m de alto) con glaseado y virutas; se puede caer en el agujero.
 * - Sombrero de paja tenso: ala ancha y baja (se pisa y rebota) y copa con cinta y lazo.
 *
 * Rebote: hacia arriba BaseUp (por la variante), más FallGain por cada cm/s de caída por encima de 300 (caer de más alto
 * rebota más), hasta MaxUp; y hacia el mar: se conserva KeepHorizontal de la velocidad horizontal y se suma ForwardPush
 * (tope MaxHorizontal). Sirve para subir a castillos, dunas y plataformas o saltar hoyos y alambre. La malla se deforma:
 * se aplasta entera (squash & stretch) y se hunde donde cae la tortuga (abolladura que vibra y se recupera).
 *
 * Potenciado (Spec.Flags & TNBeach::FlagBoosted, el de la cima de las fortalezas): rebota BoostedUpScale veces más alto
 * y empuja BoostedForwardPush hacia el mar (tope BoostedMaxHorizontal): unas 2-2,4 veces más lejos que uno normal en
 * llano. Aro dorado en la arena, cuatro palos con guirnaldas de banderines y la bandera de Tortunavy, destellos dorados,
 * un boing más grave con barrido y la fanfarria.
 *
 * Cartel (TNBeachSignKit): tabla de madera clavada en la arena por el lado por el que se llega (-X del marco), a un lado
 * y por fuera del cuerpo (de cara a quien llega), con una flecha que baja y rebota hacia arriba pintada y el rótulo
 * «¡BOING!» (dorada y con estrella en el potenciado). Rebota y brilla al acercarse la tortuga local.
 *
 * Red (#21): el rebote de una tortuga lo decide su movimiento (UTN_TurtleMovementComponent) al empezar cada paso en que
 * su cápsula toca el sensor, con las reglas puras de TN_BeachTrampolineRules.h. Así cae en el mismo paso en el servidor y
 * en el cliente dueño, también al repetir pasos tras una corrección, y la predicción cuadra (antes llegaba por el golpe o
 * el solape, un paso después o fuera del paso, con una espera según la hora del mundo de cada máquina). El resto ve la
 * deformación y oye el boing por un multicast no fiable. Los caparazones con física rebotan también (los lanza el
 * servidor). Lo potenciado sale de Spec (replicado): servidor y cliente dueño aplican el mismo impulso.
 */
UCLASS()
class TORTUNABO_API ATN_BeachTrampoline : public ATN_BeachElement
{
	GENERATED_BODY()

public:
	ATN_BeachTrampoline();

	virtual void Tick(float DeltaSeconds) override;

	/** Velocidad vertical del rebote sin caída (cm/s), antes del ajuste de la variante. */
	UPROPERTY(EditAnywhere, Category = "Trampolín", meta = (ClampMin = "300.0"))
	float BaseUp = 1250.f;

	/** Velocidad vertical extra por cada cm/s de caída por encima de 300. */
	UPROPERTY(EditAnywhere, Category = "Trampolín", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float FallGain = 0.55f;

	/** Tope de la velocidad vertical del rebote. */
	UPROPERTY(EditAnywhere, Category = "Trampolín", meta = (ClampMin = "300.0"))
	float MaxUp = 2000.f;

	/** Empujón hacia el mar (cm/s) que se suma a la horizontal conservada. */
	UPROPERTY(EditAnywhere, Category = "Trampolín", meta = (ClampMin = "0.0"))
	float ForwardPush = 320.f;

	/** Fracción de la velocidad horizontal que se conserva. */
	UPROPERTY(EditAnywhere, Category = "Trampolín", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float KeepHorizontal = 0.75f;

	/** Tope de la velocidad horizontal tras el rebote. */
	UPROPERTY(EditAnywhere, Category = "Trampolín", meta = (ClampMin = "0.0"))
	float MaxHorizontal = 1100.f;

	/**
	 * Segundos mínimos entre dos boings y deformaciones por la misma tortuga y, con 0,2 s más, entre dos rebotes del mismo
	 * caparazón con física. El rebote de una tortuga no espera: lo decide su paso del movimiento (TNTrampolineRules).
	 */
	UPROPERTY(EditAnywhere, Category = "Trampolín", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float BounceCooldown = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Trampolín", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float BoingVolume = 1.f;

	/** Potenciado: por cuánto se multiplica la velocidad vertical del rebote. */
	UPROPERTY(EditAnywhere, Category = "Trampolín|Potenciado", meta = (ClampMin = "1.0", ClampMax = "2.0"))
	float BoostedUpScale = 1.25f;

	/** Potenciado: empujón hacia el mar (cm/s) que se suma a la horizontal conservada. */
	UPROPERTY(EditAnywhere, Category = "Trampolín|Potenciado", meta = (ClampMin = "0.0"))
	float BoostedForwardPush = 900.f;

	/** Potenciado: tope de la velocidad horizontal tras el rebote. */
	UPROPERTY(EditAnywhere, Category = "Trampolín|Potenciado", meta = (ClampMin = "0.0"))
	float BoostedMaxHorizontal = 1500.f;

	/** Potenciado: tope de la velocidad vertical del rebote. */
	UPROPERTY(EditAnywhere, Category = "Trampolín|Potenciado", meta = (ClampMin = "300.0"))
	float BoostedMaxUp = 2400.f;

	/** Potenciado (Spec.Flags & TNBeach::FlagBoosted). */
	bool IsBoosted() const { return bBoosted; }

	/** true si Component es el sensor de rebote de este trampolín. */
	bool IsBounceSensor(const UPrimitiveComponent* Component) const;

	/**
	 * Rebote de una tortuga cuya cápsula toca el sensor, con la velocidad con la que empieza su paso del movimiento (lo pide
	 * UTN_TurtleMovementComponent, en el servidor y en el cliente dueño). false si aún sube (TNTrampolineRules::CanBounce);
	 * si no, la velocidad con la que sale y la fuerza del efecto.
	 */
	bool ComputeTurtleBounce(const FVector& Velocity, FVector& OutLaunch, float& OutStrength) const;

	/** Rebote nuevo (no al repetir pasos tras una corrección): deformación y boing (no más de uno por BounceCooldown). */
	void NotifyTurtleBounced(ACharacter* Turtle, float Strength);

	/**
	 * Distancia (cm) que le queda a la cápsula de Character para tocar un trampolín si cae en vertical, o
	 * TNTrampolineRules::NoTrampolineBelow si lo primero que hay debajo (a menos de MaxDrop, sin contar personajes) no es
	 * un trampolín. Lo pide la caída larga para no meterla en el caparazón (TNTrampolineRules::HoldsAutoShell).
	 */
	static double DropOntoTrampoline(const ACharacter& Character, double MaxDrop);

protected:
	virtual void BeginPlay() override;
	virtual void ApplySpec() override;

	/** Deformación y boing en todas las máquinas (el cliente dueño ya los ha hecho al predecir su rebote). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBounceFX(APawn* Bouncer, float Strength);

	/** Marco orientado hacia el mar. */
	UPROPERTY(VisibleAnywhere, Category = "Trampolín")
	TObjectPtr<USceneComponent> Frame;

	/** Pivote del aplastamiento (en la arena, en el centro). */
	UPROPERTY(VisibleAnywhere, Category = "Trampolín")
	TObjectPtr<USceneComponent> BodyPivot;

	/** Cuerpo que se deforma (malla procedural; sin colisión). */
	UPROPERTY(VisibleAnywhere, Category = "Trampolín")
	TObjectPtr<UProceduralMeshComponent> BodyMesh;

	/** Lo que no rebota: brazos de la medusa, arena. */
	UPROPERTY(VisibleAnywhere, Category = "Trampolín")
	TObjectPtr<UStaticMeshComponent> DecorMesh;

	/** Colisión del cuerpo (no se deforma; no se sube andando). */
	UPROPERTY(VisibleAnywhere, Category = "Trampolín")
	TObjectPtr<UProceduralMeshComponent> BodyCollision;

	/** Sensor: la misma forma algo más grande, solo solapa con personajes. */
	UPROPERTY(VisibleAnywhere, Category = "Trampolín")
	TObjectPtr<UProceduralMeshComponent> BounceSensor;

	/** Pie del cartel (en el marco, por el lado por el que se llega). */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Trampolín")
	TObjectPtr<USceneComponent> SignPivot;

	/** Postes, tabla e icono pintado. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Trampolín")
	TObjectPtr<UStaticMeshComponent> SignMesh;

	/** Rótulo «¡BOING!». */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Trampolín")
	TObjectPtr<UTextRenderComponent> SignText;

	UPROPERTY(Transient)
	TObjectPtr<UTN_PlaygroundSynthComponent> Toy;

private:
	/** Servidor: caparazones con física que tocan el cuerpo. */
	void BounceShells(double Now);

	/** Punto (espacio del marco) a menos de Margin del cuerpo. */
	bool IsNearBody(const FVector& Local, double Margin) const;

	void SpreadBounceFX(APawn* Bouncer, float Strength);
	void PlayBounceFX(const FVector& WorldAt, float Strength);
	void AnimateBody(float DeltaSeconds);

	/** Empujón hacia el mar, tope horizontal y tope vertical con los que rebota (los potenciados, más). */
	float EffectivePush() const { return bBoosted ? BoostedForwardPush : ForwardPush; }
	float EffectiveMaxHorizontal() const { return bBoosted ? BoostedMaxHorizontal : MaxHorizontal; }
	float EffectiveMaxUp() const { return FMath::Max(bBoosted ? FMath::Max(BoostedMaxUp, MaxUp) : MaxUp, BaseUp); }

	/** Ajustes del rebote de una tortuga en este trampolín (variante y potenciado incluidos). */
	TNTrampolineRules::FBounceTuning TurtleTuning() const;

	/** Coloca el cartel por fuera del cuerpo (en ApplySpec, con las medidas de la variante ya puestas). */
	void PlaceSign(double Fit, uint32 Seed);

	int32 Variant = 0;
	bool bBoosted = false;
	/** Cartel: giro de su tabla y estado de su animación. */
	double SignYawDeg = 0.0;
	float SignAge = 10.f;
	bool bSignNear = false;
	float SignGlow = 0.f;
	float SignGlowApplied = -1.f;
	/** El cartel se está moviendo (solo entonces se toca su transformada). */
	bool bSignMoving = false;
	/** Última fanfarria en esta máquina (tiempo del mundo): no más de una cada pocos segundos. */
	double LastFanfareAt = -100.0;
	FTNTrapBurst Sparkle;
	/** Medidas de la variante (cm, espacio del marco). */
	double BodyR = 500.0;
	double TopZ = 300.0;
	double RimZ = 40.0;
	double InnerR = 0.0;
	double HalfLength = 0.0;
	double HalfWidth = 0.0;
	double CrownR = 0.0;
	float UpScale = 1.f;
	float BoingPitch = 1.f;

	TArray<FVector> RestVerts;
	TArray<FVector> RestNormals;
	TArray<FVector> WorkVerts;
	bool bBodyDirty = false;
	FVector DentLocal = FVector::ZeroVector;
	float DentAge = 10.f;
	float DentAmp = 0.f;
	float SquashAge = 10.f;
	float SquashAmp = 0.f;
	double AnimClock = 0.0;
	float BreathPhase = 0.f;

	/** Último rebote nuevo de cada tortuga (tiempo del mundo), en cada máquina: el efecto y el vuelo sin caparazón automático. */
	TMap<TWeakObjectPtr<ACharacter>, double> LastBounceTime;

	/** Servidor: último rebote de cada caparazón con física. */
	TMap<TWeakObjectPtr<AActor>, double> LastShellBounce;
};
