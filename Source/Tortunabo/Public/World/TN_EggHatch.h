#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TN_EggHatch.generated.h"

class ACharacter;
class UInstancedStaticMeshComponent;
class UTN_SearchSynthComponent;
class UWorld;

/**
 * Salida con huevos, pieza común de la carrera (ATN_BeachRaceGenerator, TN_BeachRaceGenerator_Start.cpp) y del
 * cooperativo (ATN_ProcStartStructure). Al romperse la tapa, la tortuga se ve TNEggHatch::PauseSeconds (1 s) en su huevo
 * antes de salir lanzada:
 *  - se agacha un instante y se pone de pie de un estirón, con su rebote;
 *  - se sacude la cáscara (giros rápidos del cuerpo que se apagan) y saltan trocitos de cáscara con un crujido;
 *  - gira hacia el frente (la tortuga y, en la máquina de su jugador, la cámara) y se encoge un poco para el salto;
 *  - al acabar la pausa, el lanzamiento de siempre de cada salida.
 *
 * Igual en todas las máquinas y con el reloj del servidor: la pose sale de la hora del servidor a la que se rompió la
 * tapa, y el lanzamiento lo dan a la vez el servidor y el cliente dueño (como antes, pero 1 s después). Mientras dura la
 * pausa, los dos la tienen quieta en el huevo (MOVE_None). Lo visual es local de cada máquina (nada en servidor
 * dedicado): la malla de la tortuga se estira, se aplasta y gira sobre su transformación de siempre, que se le devuelve
 * al acabar.
 *
 * Quien lo usa solo llama a TNEggHatch::Begin por cada tortuga de su huevo al romperse la tapa, en cada máquina: no
 * depende de nada de la carrera ni del cooperativo (se puede llevar tal cual a la rama del cooperativo).
 */
namespace TNEggHatch
{
	/** Segundos que se ve la tortuga en su huevo roto antes del lanzamiento. */
	constexpr float PauseSeconds = 1.f;

	/** Hora del servidor (AGameStateBase::GetServerWorldTimeSeconds; en el servidor, o sin estado de juego, la del mundo). */
	TORTUNABO_API double ServerNow(const UWorld* World);

	/**
	 * Cada máquina, al romperse la tapa del huevo de Turtle: la pausa en el huevo y, si esta máquina mueve a la tortuga
	 * (el servidor, o el cliente que la controla), el lanzamiento con LaunchVelocity a la hora del servidor
	 * LaunchServerTime. HatchServerTime es la hora del servidor a la que se rompió la tapa (la pose empieza ahí; si llega
	 * tarde, va por donde toque y, pasada la hora del lanzamiento, lanza enseguida). FacingYaw: hacia dónde mira al final
	 * (grados, mundo). AccentHex: color de los trocitos de cáscara (el acento del huevo, 0xRRGGBB). Si la tortuga ya
	 * tenía una pausa a medias, la sustituye.
	 */
	TORTUNABO_API void Begin(ACharacter* Turtle, double HatchServerTime, double LaunchServerTime, const FVector& LaunchVelocity,
		float FacingYaw, uint32 AccentHex);

	/**
	 * Quita la pausa de Turtle si la tiene: le devuelve la pose y, con bRelease, la suelta si la tenía quieta (sin
	 * lanzarla; sin bRelease se queda como esté, para quien la vaya a sujetar o colocar él mismo).
	 */
	TORTUNABO_API void Cancel(ACharacter* Turtle, bool bRelease = true);

	/** true mientras Turtle está en la pausa del huevo, antes del lanzamiento. */
	TORTUNABO_API bool IsHatching(const ACharacter* Turtle);

	/**
	 * true mientras Turtle sale del huevo sin control propio: en la pausa o en el vuelo del lanzamiento, hasta que toca el
	 * suelo (en el servidor y en el cliente que la controla). La estamina no se gasta en ese tramo (#520).
	 */
	TORTUNABO_API bool IsLeavingEgg(const ACharacter* Turtle);
}

/**
 * La pausa en el huevo de una tortuga (TNEggHatch::Begin la crea en ella, en cada máquina; no se replica). Se borra sola
 * al acabar: tras el lanzamiento y cuando ya no quedan trocitos de cáscara por el aire.
 */
UCLASS(NotBlueprintable, Transient, ClassGroup = (Tortunabo))
class TORTUNABO_API UTN_EggHatchComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTN_EggHatchComponent();

	/** Arranca la pausa (ver TNEggHatch::Begin). */
	void StartHatch(double InHatchServerTime, double InLaunchServerTime, const FVector& InLaunchVelocity, float InFacingYaw, uint32 InAccentHex);

	/** Devuelve la pose y, con bRelease, suelta a la tortuga si la tenía quieta (sin lanzarla); luego se borra. */
	void StopHatch(bool bRelease);

	/** Aún no ha llegado la hora del lanzamiento. */
	bool IsWaiting() const { return !bStopped && !bPauseOver; }

	/** En la pausa o en el vuelo del lanzamiento, aún sin tocar el suelo. */
	bool IsLeaving() const { return !bStopped && (!bPauseOver || bInLaunchFlight); }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Trocito de cáscara que salta (se mueve a mano; instancias de BitsMesh). */
	struct FShellBit
	{
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FVector SpinAxis = FVector::UpVector;
		float SpinSpeed = 0.f;
		float Angle = 0.f;
		float Size = 1.f;
		float Age = 0.f;
		float Life = 1.f;
		/** Suelo (los pies de la tortuga al saltar): ahí se queda tumbado. */
		double FloorZ = 0.0;
		bool bGrounded = false;
		bool bAlive = false;
	};

	/** Un paso de la pausa (lo da cada fotograma y, al empezar, StartHatch). */
	void Step(float DeltaTime);
	/** Hay algo que ver en esta máquina (con pantalla y la tortuga a la vista). */
	bool IsVisual(const ACharacter* Turtle) const;
	/** La tortuga puede ponerse de pie y sacudirse (sin caparazón, ni tumbada, ni ragdoll, ni oculta; con pantalla). */
	bool CanPose(const ACharacter* Turtle) const;
	/** Pose a T segundos de romperse la tapa y a ToLaunch del lanzamiento. */
	void ApplyPose(ACharacter* Turtle, double T, double ToLaunch);
	/** Devuelve la malla a su transformación de siempre (la de antes de la pausa). */
	void RestorePose();
	/** Quieta en el huevo (el servidor y el cliente dueño). */
	void Hold(ACharacter* Turtle);
	/** Gira la tortuga (y la cámara de su jugador, en su máquina) hacia el frente. */
	void TurnToFront(ACharacter* Turtle, double T);
	void Launch(ACharacter* Turtle);
	/** Trocitos de cáscara en los momentos de la pose (al romperse, al empezar a sacudirse y a media sacudida). */
	void EmitDueBits(ACharacter* Turtle, double T);
	void EmitBits(ACharacter* Turtle, int32 Count, float Speed);
	/** Mueve los trocitos; false cuando ya no queda ninguno. */
	bool TickBits(float DeltaTime);

	/** Trocitos de cáscara (creados con la primera tanda; solo en máquinas con pantalla). */
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> BitsMesh;

	/** Crujido de la cáscara (el sonido de rebuscar, más agudo). */
	UPROPERTY(Transient)
	TObjectPtr<UTN_SearchSynthComponent> Synth;

	TArray<FShellBit> Bits;
	TArray<FTransform> BitXf;

	/** Horas del servidor de la rotura y del lanzamiento. */
	double HatchTime = 0.0;
	double LaunchTime = 0.0;
	FVector LaunchVelocity = FVector::ZeroVector;
	float FacingYaw = 0.f;
	/** Giro de la tortuga y de la cámara de su jugador al empezar. */
	float StartYaw = 0.f;
	float StartViewYaw = 0.f;
	uint32 AccentHex = 0xFFF3DC;

	/** Transformación de la malla antes de la pausa. */
	FVector BaseMeshLocation = FVector::ZeroVector;
	FQuat BaseMeshRotation = FQuat::Identity;
	FVector BaseMeshScale = FVector::OneVector;

	/** Esta máquina mueve a la tortuga (servidor o cliente dueño): la sujeta y la lanza. */
	bool bMoves = false;
	/** La malla está en la pose de la pausa (hay que devolvérsela). */
	bool bPoseOn = false;
	/** La tiene quieta (MOVE_None) esta máquina. */
	bool bHolding = false;
	/** Se puede sujetar: sin caparazón con cuerpo físico al empezar. */
	bool bHoldable = true;
	bool bLaunched = false;
	/** Lanzada por esta máquina y aún en el aire: hasta que toca el suelo, el componente no se borra (#520). */
	bool bInLaunchFlight = false;
	bool bPauseOver = false;
	bool bStopped = false;
	/** Tandas de trocitos ya soltadas (bit por tanda). */
	uint8 BitBursts = 0;
};
