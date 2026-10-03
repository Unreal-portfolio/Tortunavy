#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstanceProxy.h"
#include "Player/TN_ProcAnimInstance.h"
#include "TN_TurtleAnimInstance.generated.h"

class AActor;
class UAnimSequence;

/**
 * Poses de celebración del podio del modo carrera (ATN_RacePodiumStage), accesibles desde fuera del personaje
 * (UTN_TurtleAnimInstance::SetCelebration). Bucles cortos, como un GIF.
 */
UENUM(BlueprintType)
enum class ETNTurtleCelebration : uint8
{
	None          UMETA(DisplayName = "Ninguna"),
	/** Levanta la concha como un trofeo con las dos manos, dando saltitos. */
	Trophy        UMETA(DisplayName = "Trofeo"),
	/** Hombros caídos, cabeza gacha, un suspiro y niega despacio con la cabeza. */
	Disappointed  UMETA(DisplayName = "Decepcionada"),
	/** Sentada en el suelo, enfadadísima: patalea, aporrea el suelo con los puños y sacude la cabeza. */
	Tantrum       UMETA(DisplayName = "Pataleta")
};

/**
 * Estado de la tortuga que el hilo de juego pasa cada fotograma a la evaluación de la pose (que puede correr en otro
 * hilo). Pesos de 0 a 1 ya suavizados; tiempos en segundos.
 */
struct FTNTurtleAnimFrame
{
	float Clock = 0.f;
	/** Locomoción: tiempo de los clips de espera y andar, ciclos de la carrera y mezcla (andar sobre la espera, correr encima). */
	float IdleTime = 0.f;
	float WalkTime = 0.f;
	float RunTime = 0.f;
	float WalkW = 0.f;
	float RunW = 0.f;
	/** Cuánto de sprint lleva la carrera (braceo e inclinación mayores). */
	float SprintW = 0.f;
	/** Amplitud (grados) del paso de la carrera: la justa para que el pie apoyado no patine a esa velocidad. */
	float RunStride = 40.f;
	/** Inclinación en las curvas y hacia delante al correr (grados). */
	float LeanRoll = 0.f;
	float LeanPitch = 0.f;
	/** Poses de estado. */
	float AirW = 0.f;
	float Falling = 0.f;
	float DiveW = 0.f;
	/** Panzazo sobre la tripa en el suelo (arrastre o reptar), su velocidad (0..1, a 7 m/s) y los golpes (caer, chocar). */
	float SlideW = 0.f;
	float SlideSpeed = 0.f;
	float SlideImpact = 0.f;
	/** Levantarse de la tripa: empujón de brazos y rodillas (sube y baja). */
	float BellyGetUpW = 0.f;
	float SwimW = 0.f;
	float ShellW = 0.f;
	float CarryW = 0.f;
	float CarriedW = 0.f;
	float DownW = 0.f;
	float TiredW = 0.f;
	/** El caparazón tiene cuerpo físico (la malla va tumbada sobre la caja): sin bajar el cuerpo al meterse. */
	bool bShellBody = false;
	/**
	 * Lanzamiento como un saque de banda: fase (de -1 a 0 toma impulso con las aletas detrás de la cabeza y vuelve a
	 * subirlas; de 0 a 1 suelta y acompaña hacia delante y abajo), peso y si es con las dos aletas (o solo la derecha).
	 */
	float ThrowU = -2.f;
	float ThrowW = 0.f;
	bool bThrowBoth = false;
	/**
	 * Lo que lleva en las aletas (UTN_InventoryComponent): cómo (0 = nada, 1 = en la aleta derecha, 2 = abrazado con las
	 * dos, 3 = por un extremo; como ETNItemHold), su peso, cuánto abre las aletas abrazando (0..1) y la aleta derecha
	 * hacia la espalda al guardar o sacar algo del caparazón.
	 */
	uint8 HoldStyle = 0;
	float HoldW = 0.f;
	float HoldOpen = 0.5f;
	float StashW = 0.f;
	/** Emote (índice del catálogo 0-9 o -1), su tiempo y su peso (entra y sale suave). */
	int32 Emote = -1;
	float EmoteTime = 0.f;
	float EmoteW = 0.f;
	/** Emote anterior mientras se funde con el nuevo (cambio de un emote a otro sin cortes; -1 = ninguno). */
	int32 PrevEmote = -1;
	float PrevEmoteTime = 0.f;
	float PrevEmoteW = 0.f;
	/** Levantarse del derribo: peso de la pose del suelo (1 → 0) y del empujón de brazos y rodillas (sube y baja). */
	float GetUpW = 0.f;
	float GetUpFlex = 0.f;
	/** Celebración del podio (modo carrera): pose, segundos que lleva (bucle) y peso (entra y sale suave). */
	ETNTurtleCelebration Celebration = ETNTurtleCelebration::None;
	float CelebrationTime = 0.f;
	float CelebrationW = 0.f;
	/**
	 * Zambullida de cabeza desde el acantilado de la meta (modo carrera): peso, segundos en el aire y giro del cuerpo
	 * hacia delante (grados: 90 = tumbada en horizontal, 180 = cabeza abajo en vertical).
	 */
	float CliffDiveW = 0.f;
	float CliffDiveTime = 0.f;
	float CliffDivePitch = 0.f;
	/**
	 * VR (Docs/Modo_VR.md): las manos del cuerpo van a los mandos (IK de brazo y antebrazo). Peso de cada brazo (0 bailando,
	 * en el caparazón, tumbada o llevando a otra tortuga) y dónde tiene que llegar cada mano, en el espacio de la malla.
	 */
	float VRArmLW = 0.f;
	float VRArmRW = 0.f;
	FVector VRHandL = FVector::ZeroVector;
	FVector VRHandR = FVector::ZeroVector;
};

/** Evaluación en C++ de la pose de la tortuga (clips de Mixamo y poses procedurales encima). */
struct FTNTurtleAnimProxy : public FAnimInstanceProxy
{
	FTNTurtleAnimProxy() = default;
	explicit FTNTurtleAnimProxy(UAnimInstance* InInstance) : FAnimInstanceProxy(InInstance) {}

	virtual bool Evaluate(FPoseContext& Output) override;

	FTNTurtleAnimFrame Frame;
	const UAnimSequence* IdleClip = nullptr;
	const UAnimSequence* WalkClip = nullptr;
	const UAnimSequence* CheerClip = nullptr;
	/** Pose en la que quedó el ragdoll, en locales e indexada por hueso de la malla (vacía si no se está levantando). */
	TArray<FTransform> GetUpPose;
};

/**
 * Animación de la tortuga del jugador sobre el esqueleto Mixamo de TotugaDemo_Rig, sin AnimBP: espera y andar con los
 * clips (mezclados por velocidad, como ABS_Walk), la carrera del sprint hecha en código y, encima, poses para el
 * salto, el panzazo (en el aire y arrastrándose sobre la tripa, con el empujón para levantarse), el nado, el caparazón
 * (se esconden cabeza y patas), llevar y ser llevado, el lanzamiento (saque de banda con las dos aletas o golpe de una),
 * los brazos que sujetan lo que lleva en las aletas (en una, abrazado o por un extremo) y la aleta que va a la espalda a
 * guardarlo o sacarlo del caparazón, el tumbado, el cansancio y los emotes del catálogo.
 * Hereda de UTN_ProcAnimInstance: los ajustes por hueso que escriben los sistemas viejos se siguen aplicando al final.
 *
 * Las poses se escriben como giros en el espacio de la malla (mira a +Y, arriba +Z, su izquierda +X) sobre la
 * postura de referencia (en T) y se mezclan con la de los clips.
 */
UCLASS(Transient)
class TORTUNABO_API UTN_TurtleAnimInstance : public UTN_ProcAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	/**
	 * Empieza la animación de levantarse desde la pose del ragdoll (LocalPose: transformaciones locales indexadas por
	 * hueso de la malla, con la malla ya devuelta a la cápsula): la tortuga gira desde el suelo hasta ponerse de pie en
	 * Seconds, con un empujón de brazos contra el suelo y las rodillas dobladas a mitad de camino.
	 */
	void BeginGetUp(const TArray<FTransform>& LocalPose, float Seconds);

	/**
	 * Saca ya la pose del caparazón (cabeza y patas fuera), sin el paso suave de siempre, también en la evaluación. Lo usa el
	 * derribo justo antes de pausar las animaciones del ragdoll (#251): la escala de los huesos del ragdoll sale de la
	 * animación y, con la pausa, la cabeza y las patas se quedaban medio metidas en la concha hasta levantarse. true si
	 * estaba algo metida (entonces hay que volver a evaluar la pose).
	 */
	bool SnapOutOfShellPose();

	/**
	 * Pose de celebración del podio (modo carrera) encima de todo lo demás: Trofeo, Decepcionada o Pataleta (ver
	 * ETNTurtleCelebration); None la quita. Al cambiar de una a otra, la anterior sale antes de que entre la nueva. Sirve
	 * para cualquier malla de tortuga con esta animación, con personaje o sin él (el podio, ATN_RacePodiumStage).
	 */
	UFUNCTION(BlueprintCallable, Category = "Tortuga|Animación")
	void SetCelebration(ETNTurtleCelebration InCelebration);

	UFUNCTION(BlueprintPure, Category = "Tortuga|Animación")
	ETNTurtleCelebration GetCelebration() const { return WantedCelebration; }

	/** Segundos que lleva la celebración en curso (para acompasar la cara y los efectos con su bucle). */
	float GetCelebrationTime() const { return Frame.CelebrationTime; }

	/** true mientras se zambulle de cabeza desde el acantilado de la meta (modo carrera). */
	bool IsCliffDiving() const { return bCliffDive; }

	/**
	 * Golpe de brazo de lanzar: con la aleta derecha (un objeto) o con las dos. El de soltar a la tortuga que lleva en alto
	 * sale solo (con la toma de impulso de UTN_CarryComponent antes, si es con la E).
	 */
	void PlayThrow(bool bBothFlippers);

	/** Cuánto sujetan ahora los brazos lo que lleva en las aletas (0..1): con menos, el objeto solo sigue a la aleta. */
	float GetHoldWeight() const { return Frame.HoldW; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> WalkAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> CheerAnim;

	FTNTurtleAnimFrame Frame;
	float PrevYaw = 0.f;
	bool bWasCarrying = false;
	int32 LastEmote = -1;

	/** Acompañar el lanzamiento: segundos desde que soltó (negativo = nada), fase de partida y duración. */
	float ThrowFollowElapsed = -1.f;
	float ThrowFollowFrom = 0.f;
	float ThrowFollowSeconds = 0.35f;
	/** PlayThrow pedido desde el último fotograma y con cuántas aletas. */
	bool bPendingThrow = false;
	bool bPendingThrowBoth = false;

	/** Panzazo en el fotograma anterior: sobre la tripa en el suelo, en el aire y velocidad (golpes y levantarse). */
	bool bWasBellyGround = false;
	bool bWasBellyAir = false;
	FVector2D PrevBellyVelocity = FVector2D::ZeroVector;
	/** Levantarse de la tripa: segundos desde que empezó (negativo = no se está levantando). */
	float BellyGetUpElapsed = -1.f;

	/** Levantarse: pose del suelo, tiempo transcurrido y duración (0 = no se está levantando). */
	TArray<FTransform> GetUpPose;
	float GetUpElapsed = 0.f;
	float GetUpDuration = 0.f;
	bool bGetUpPoseSent = false;

	/** Celebración pedida con SetCelebration (la del proxy cambia cuando la anterior ya ha salido). */
	ETNTurtleCelebration WantedCelebration = ETNTurtleCelebration::None;

	/** Zambullida del acantilado: activa, cayendo en el fotograma anterior y segundos desde que empezó la caída. */
	bool bCliffDive = false;
	bool bWasFallingForDive = false;
	float FallElapsed = 0.f;
	/** Generador de la playa que dice dónde está el borde del acantilado y cuándo volver a buscarlo. */
	TWeakObjectPtr<AActor> CliffZoneSource;
	double NextCliffZoneLookup = 0.0;
};
