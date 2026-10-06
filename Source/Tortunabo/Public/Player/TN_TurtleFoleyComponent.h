#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "UObject/ObjectKey.h"
#include "TN_TurtleFoleyComponent.generated.h"

class UPrimitiveComponent;
class UTN_StormCoughComponent;

namespace TNTurtleFoley
{
	// Parámetros atómicos y anillo de pasos compartidos con el generador del hilo de audio (Private/Player/TN_TurtleFoleyDSP.h).
	struct FSharedParams;
	// Estado de la tortuga leído en cada fotograma (Private/Player/TN_TurtleFoleyComponent.cpp).
	struct FTurtleState;
}

/**
 * Ruidos del cuerpo de la tortuga, sintetizados en tiempo real sin archivos de audio: pasos al andar, pasos más rápidos
 * y fuertes al correr, el roce del impulso al saltar, el golpe al aterrizar y el jadeo cuando se cansa. Cartoon pero
 * creíble: patas blandas (almohadilla y dedos), nada de botas.
 *
 * Pasos sincronizados con la zancada de la animación: se leen los huesos de los pies (LeftFoot y RightFoot en el espacio
 * de la malla) y suena una pisada cuando un pie que se había levantado vuelve al suelo. Si no hay huesos o no dan pasos
 * (malla sin animar), un reloj al ritmo de la animación (el de UTN_TurtleAnimInstance) hace de reserva. El timbre depende
 * de la superficie bajo la pata, mezclada por pesos: en el mapa procedural, el bioma del terreno (arena en playa y
 * desierto, tierra y hojas en la selva, roca en acantilados y volcán, fango en el manglar), la pendiente (roca), las
 * estructuras (tablones de madera o piedra) y el agua poco profunda por debajo del nivel del mar; en el resto, los
 * nombres de la malla, el material y el actor pisados (arena, madera, tierra, agua; roca por defecto).
 *
 * Jadeo: con la estamina por debajo de PantBelowStamina, respiraciones por la boca cada vez más fuertes y seguidas; al
 * agotarse, «hah-hah» rápido con algo de voz; al recuperarse se calma poco a poco y, si ha jadeado fuerte, suspira. En
 * la tormenta manda la tos (UTN_StormCoughComponent): mientras tose, el jadeo calla. La voz es la misma que la de la tos.
 *
 * Panzazo: «plaf» de tripa al caer, arrastre continuo mientras se desliza sobre la tripa (con el timbre de la superficie
 * y la fuerza de la velocidad) y «tonc» hueco del caparazón si choca contra algo arrastrándose.
 *
 * Nado y caparazón: una brazada (el agua que empujan las aletas) en cada ciclo de la brazada de la animación, más fuerte
 * cuanto más rápido nada, y el roce con «tonc» hueco al meterse o salir del caparazón (PlayShell).
 *
 * Estado local por máquina, sin RPC: cada máquina con audio da este componente a cada tortuga (ATortugaCharacter::
 * BeginPlay) y lee cada fotograma su estado replicado (velocidad, en el suelo o en el aire, sprint, estamina, derribo,
 * caparazón...). Fuente 3D mono en la raíz de la tortuga; la del jugador local suena algo más alta. Nada en servidor
 * dedicado. Si el Blueprint tiene asignado el FootstepSound de siempre, los pasos sintetizados callan (no se doblan).
 *
 * El sonido lo genera un ISoundGenerator en el hilo de render de audio (TNTurtleFoley::FEngine, en TN_TurtleFoleyDSP.h):
 * sin UObjects, asignaciones ni bloqueos allí. Este componente solo deja pisadas en un anillo y objetivos en parámetros
 * atómicos; el sintetizador arranca al hacer falta y se para en silencio o lejos del oyente (sin gastar CPU).
 *
 * Consola: TN.Voice.Volume (volumen), TN.Voice.Surface (forzar superficie), TN.Voice.Debug (estado en pantalla),
 * TN.Voice.Steps <0|1|2>, TN.Voice.Pant <0|1|2>, TN.Voice.Drag <0|1|2> y TN.Voice.Swim <0|1|2> (forzar pasos, jadeo,
 * arrastre o brazadas en la tortuga local) y TN.Voice.Shell <0|1>. Ver Docs/Sonido_Tortuga.md.
 */
UCLASS(ClassGroup = (Audio), meta = (BlueprintSpawnableComponent))
class TORTUNABO_API UTN_TurtleFoleyComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UTN_TurtleFoleyComponent(const FObjectInitializer& ObjectInitializer);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * El componente de InOwner; si no tiene, se lo crea (adjunto a su raíz y registrado). Null en servidor dedicado, sin
	 * audio, fuera de un mundo de juego o con el actor destruyéndose.
	 */
	static UTN_TurtleFoleyComponent* FindOrAddTo(AActor* InOwner);

	/** Pruebas (TN.Voice.Pant): 0 = sin forzar (manda la estamina), 1 = jadeo suave, 2 = agotada. */
	void SetDebugPant(int32 InLevel);

	/** Pruebas (TN.Voice.Steps): 0 = sin forzar, 1 = pasos andando en el sitio, 2 = corriendo en el sitio. */
	void SetDebugSteps(int32 InLevel);

	/** Pruebas (TN.Voice.Drag): 0 = sin forzar (manda el panzazo), 1 = arrastre lento en el sitio, 2 = rápido. */
	void SetDebugDrag(int32 InLevel);

	/** Jadeo enviado por última vez (0 = respira normal; 1 = agotada). */
	float GetPantLevel() const { return PantSent; }

	/**
	 * «Toc» hueco y corto de guardar algo en el caparazón (bIntoShell) o de sacarlo (más agudo). Lo pide
	 * UTN_InventoryComponent en cada máquina cuando la aleta llega a la espalda (cosmético, sin red).
	 */
	void PlayStash(bool bIntoShell);

	/**
	 * Meterse en el caparazón (bEntering) o salir: roce de la piel y «tonc» hueco de la concha. Lo pide UTN_ShellComponent
	 * en cada máquina al cambiar de estado (cosmético, sin red) si bSynthShellSounds; si no, suenan sus assets.
	 */
	void PlayShell(bool bEntering);

	/** Pruebas (TN.Voice.Swim): 0 = sin forzar (manda el nado), 1 = brazadas flotando en el sitio, 2 = a toda velocidad. */
	void SetDebugSwim(int32 InLevel);

	/** Volumen general (pasos y jadeo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurtleFoley", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float Loudness = 1.f;

	/** Volumen de los pasos, aterrizajes e impulsos (y de las brazadas y el caparazón). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurtleFoley", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float StepLoudness = 1.f;

	/** Volumen del jadeo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurtleFoley", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float BreathLoudness = 1.f;

	/** Multiplica el volumen si es la tortuga del jugador local (se oye a sí misma algo más alta). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurtleFoley", meta = (ClampMin = "1.0", ClampMax = "4.0"))
	float LocalPlayerBoost = 1.3f;

	/** Fracción de estamina por debajo de la cual empieza a jadear (a más baja, más fuerte; agotada, a tope). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurtleFoley", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float PantBelowStamina = 0.45f;

	/** Segundos que tarda el jadeo en calmarse al recuperar la estamina (constante de tiempo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurtleFoley", meta = (ClampMin = "0.1"))
	float PantCalmSeconds = 2.5f;

	/** Velocidad mínima (cm/s) para que suenen pasos. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurtleFoley", meta = (ClampMin = "0.0"))
	float MinStepSpeed = 60.f;

	/** Volumen del arrastre sobre la tripa (panzazo), del «plaf» al caer y del choque arrastrándose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurtleFoley|Panzazo", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float DragLoudness = 1.f;

	/** Velocidad mínima (cm/s) para que suene el arrastre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurtleFoley|Panzazo", meta = (ClampMin = "0.0"))
	float MinDragSpeed = 25.f;

	/** Velocidad (cm/s) a la que el arrastre suena a tope. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurtleFoley|Panzazo", meta = (ClampMin = "100.0"))
	float DragFullSpeed = 650.f;

	/** Radio con volumen pleno (cm). Se aplica en el siguiente arranque. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurtleFoley|3D", meta = (ClampMin = "0.0"))
	float InnerRadius = 300.f;

	/** Distancia (cm) desde el radio interior en la que se apaga. Se aplica en el siguiente arranque. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TurtleFoley|3D", meta = (ClampMin = "100.0"))
	float FalloffDistance = 2400.f;

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

private:
	/** Superficies de los pesos (arena, tierra, roca, madera, agua); coincide con TNTurtleFoley::Surface::Num. */
	static constexpr int32 NumSurfaces = 5;

	/** Mono, 3D y atenuación hecha en código (Start la copia al componente de audio en cada arranque). */
	void ConfigureAttenuation();

	/** Arranca el sintetizador (con la atenuación al día). */
	void StartSynth();

	/** Lee el estado de la tortuga en este fotograma. */
	void ReadFrame(TNTurtleFoley::FTurtleState& Out, double Now);

	/** Pisadas por los huesos de los pies o, si no dan, por el reloj de reserva; y las de prueba. */
	void UpdateSteps(float DeltaTime, double Now, const TNTurtleFoley::FTurtleState& Frame);

	/** Impulso al saltar y golpe al aterrizar. */
	void UpdateJumpAndLanding(float DeltaTime, double Now, const TNTurtleFoley::FTurtleState& Frame);

	/** Cansancio suavizado a partir de la estamina (o de la prueba). */
	void UpdatePant(float DeltaTime, double Now, const TNTurtleFoley::FTurtleState& Frame);

	/** Arrastre sobre la tripa (fuerza por la velocidad, superficie) y choques arrastrándose. */
	void UpdateDrag(float DeltaTime, double Now, const TNTurtleFoley::FTurtleState& Frame);

	/** Brazadas al nadar, al ritmo de la brazada de la animación (o de un reloj propio si no se evalúa). */
	void UpdateSwim(float DeltaTime, double Now, const TNTurtleFoley::FTurtleState& Frame);

	/** Manda una pisada al hilo de audio (arranca el sintetizador si hace falta). */
	void EmitStep(uint8 Kind, uint8 Foot, float Force, float Pace, double Now, const TNTurtleFoley::FTurtleState& Frame);

	/**
	 * Pesos de superficie bajo un punto del pie (TNTurtleSurface::Probe, con caché corta: los dos pies de una zancada
	 * pisan casi lo mismo).
	 */
	void ResolveSurface(const FVector& FootLocation, double Now, float OutWeights[NumSurfaces]);

	/** Índices de los huesos de los pies en la malla actual; false si no los tiene. */
	bool ResolveFootBones();

	/** Olvida el seguimiento de los pies (al parar, saltar o dejar de oírse). */
	void ResetFeet();

	/** TN.Voice.Debug: una línea en pantalla por tortuga que se oye. */
	void ShowDebug(const TNTurtleFoley::FTurtleState& Frame) const;

	/** Si es la tortuga que controla esta máquina. */
	bool IsLocalTurtle() const;

	/** Distancia (cm) al oyente más cercano; 0 si no hay dispositivo de audio. */
	float GetListenerDistance() const;

	/** Parámetros compartidos con el generador del hilo de audio (los dos los mantienen vivos). */
	TSharedPtr<TNTurtleFoley::FSharedParams, ESPMode::ThreadSafe> SharedParams;

	/** Tos de la tormenta de esta tortuga (la pone ATN_PathStorm; se busca cada medio segundo mientras falte). */
	TWeakObjectPtr<UTN_StormCoughComponent> Cough;
	double NextCoughLookup = 0.0;

	/** Superficie por nombres de cada componente ya visto (índice de preajuste). */
	TMap<TObjectKey<UPrimitiveComponent>, uint8> NameSurfaceCache;

	/** Última consulta de superficie: dónde, cuándo y qué salió. */
	float LastSurface[NumSurfaces] = { 0.f, 0.f, 1.f, 0.f, 0.f };
	FVector LastProbeLocation = FVector::ZeroVector;
	double LastProbeTime = -10.0;

	// ── Pies ────────────────────────────────────────────────────────────────
	/** Malla para la que valen los índices de los huesos. */
	TWeakObjectPtr<const UObject> BonesFor;
	int32 FootBone[2] = { INDEX_NONE, INDEX_NONE };
	/** Altura del suelo según los pies (el más bajo, con subida lenta) en el espacio de la malla (cm). */
	float FloorZ = 0.f;
	bool bFloorValid = false;
	/** Pie levantado: la siguiente vez que llegue al suelo (o deje de bajar cerca de él), suena. */
	bool bFootArmed[2] = { false, false };
	/** De cada pie en esta zancada: altura máxima, altura del fotograma anterior y bajada más rápida (cm, cm/s). */
	float FootPeak[2] = { 0.f, 0.f };
	float FootPrevH[2] = { 0.f, 0.f };
	float FootMaxDescent[2] = { 0.f, 0.f };
	double FootLastStep[2] = { -10.0, -10.0 };
	double LastStepTime = -10.0;
	double LastBoneStepTime = -10.0;
	/** Desde cuándo anda sin parar (-1 = parada). */
	double MovingSince = -1.0;
	/** Largo del paso andando (cm), aprendido de los pasos de los huesos; da el ritmo del reloj de reserva. */
	float WalkStepLength = 64.f;
	float FallbackTimer = 0.f;
	uint8 NextFoot = 0;
	/** De dónde salió el último paso (para TN.Voice.Debug). */
	bool bLastStepFromBones = false;

	// ── Saltos y caídas ─────────────────────────────────────────────────────
	bool bWasFalling = false;
	bool bWasGrounded = false;
	float FallPeakSpeed = 0.f;
	float AirTime = 0.f;

	// ── Jadeo ───────────────────────────────────────────────────────────────
	float Fatigue = 0.f;
	float PantSent = 0.f;
	bool bPantHushed = false;

	// ── Arrastre del panzazo ────────────────────────────────────────────────
	/** Fuerza (0 = callado) y viveza enviadas, y pesos de la superficie bajo la tripa. */
	float DragSent = 0.f;
	float DragPaceSent = 0.f;
	float DragSurface[NumSurfaces] = { 0.f, 0.f, 1.f, 0.f, 0.f };
	/** Velocidad del fotograma anterior sobre la tripa (un cambio brusco es un choque). */
	FVector2D PrevBellyVelocity = FVector2D::ZeroVector;
	bool bWasBellyGround = false;
	double LastBumpTime = -10.0;

	// ── Nado ────────────────────────────────────────────────────────────────
	/** Fase de la brazada del fotograma anterior (-1 = sin nadar), reloj propio y última fase leída de la animación. */
	float PrevSwimPhase = -1.f;
	float SwimClock = 0.f;
	float LastAnimSwimPhase = -1.f;

	// ── Pruebas ─────────────────────────────────────────────────────────────
	int32 DebugPant = 0;
	int32 DebugSwim = 0;
	int32 DebugSteps = 0;
	int32 DebugDrag = 0;
	float DebugStepTimer = 0.f;

	/** Último momento con algo que decir (paso o jadeo): el sintetizador se para tras un rato sin nada. */
	double LastActivityTime = -10.0;
	/** Arranques del sintetizador: cambia el azar de cada arranque (la voz sale siempre de la tortuga). */
	uint32 StartCount = 0;
	/** Tick lento (lejos del oyente). */
	bool bSlowTick = false;
};
