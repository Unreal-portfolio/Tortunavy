// Cámara de llegada, podio y espectador del Rally (#306): qué plano toca a cada jugador local, dónde va la cámara en cada uno y
// dónde quedan el podio y sus huecos. Lógica pura sin mundo (tests Tortunabo.Rally.Camera.*); la usan
// UTN_RallyCameraDirector (cliente, cosmético) y ATN_RallyGameMode (servidor, aparca los buggies que llegan en el podio).
#pragma once

#include "CoreMinimal.h"
#include "Rally/TN_RallyGameState.h"

namespace TNRallyCamera
{
	enum class EShot : uint8
	{
		/** La cámara de siempre del peón propio (buggy o artillera). */
		Own,
		/** Al cruzar la meta: plano lateral del buggy propio, a cámara lenta si se puede (sin VR). */
		FinishSide,
		/** El podio con los tres primeros buggies y sus tortugas. */
		Podium,
		/** Espectador: los buggies que siguen corriendo y el dron que sigue al líder. */
		Spectate
	};

	/** Duración del plano lateral (s de tiempo real). */
	inline constexpr double FinishSideSeconds = 1.5;
	/** Lo que se ve el podio tras el plano lateral antes de pasar a espectador (s). */
	inline constexpr double PodiumHoldSeconds = 4.0;
	/** Dilatación del tiempo en la cámara lenta. */
	inline constexpr float SlowMotionDilation = 0.35f;
	/** El servidor aparca en el podio el buggy que llega pasado esto (tras el plano lateral del cliente; s). */
	inline constexpr double ParkDelaySeconds = 1.75;

	/** Lo que ve el jugador local en este fotograma. */
	struct FShotInput
	{
		ETNRallyPhase Phase = ETNRallyPhase::Warmup;
		/** Tiene buggy (conductora o artillera). */
		bool bSeated = false;
		/** Su buggy ha cruzado la meta. */
		bool bFinished = false;
		/** Segundos de tiempo real desde que vio llegar su buggy (negativo si no lo vio: llegó ya en meta). */
		double SecondsSinceFinish = -1.0;
		/** Modo VR (TNVR::KeepFirstPersonView): sin cámara lenta ni travelling. */
		bool bVR = false;
	};

	TORTUNABO_API EShot DecideShot(const FShotInput& In);

	/** La cámara lenta del plano lateral solo cuando no afecta a nadie más: sin VR y sin otros jugadores en la partida. */
	TORTUNABO_API bool CanUseSlowMotion(bool bVR, bool bStandalone, int32 HumanPlayers);

	/**
	 * Hueco del espectador al pulsar anterior (-1) o siguiente (+1): 0..NumRacing-1 son los buggies que corren (por puesto)
	 * y NumRacing el dron (si se permite). Da la vuelta. INDEX_NONE si no hay nada que mirar.
	 */
	TORTUNABO_API int32 CycleSpectate(int32 Current, int32 Delta, int32 NumRacing, bool bAllowDrone);

	/** A quién mira el espectador: hueco (0..N-1 buggies por puesto, N el dron), su equipo y si es el dron. */
	struct FSpectatePick
	{
		int32 Slot = INDEX_NONE;
		/** INDEX_NONE con el dron o sin nadie a quien mirar. */
		int32 Team = INDEX_NONE;
		bool bDrone = false;

		bool HasTarget() const { return Slot != INDEX_NONE; }
	};

	/**
	 * Lo que se mira este fotograma (RacingTeams: equipos que corren, por puesto). Se sigue al mismo equipo aunque cambie de
	 * puesto; el dron sigue siendo el dron aunque cambie cuántos corren; si el equipo ya no corre, el mismo hueco (o el primero).
	 */
	TORTUNABO_API FSpectatePick FollowSpectate(TConstArrayView<int32> RacingTeams, const FSpectatePick& Current, bool bAllowDrone);

	/** Anterior (-1) o siguiente (+1) desde lo que se mira (FollowSpectate), con el dron al final si se permite. */
	TORTUNABO_API FSpectatePick StepSpectate(TConstArrayView<int32> RacingTeams, const FSpectatePick& Current, int32 Delta,
		bool bAllowDrone);

	// ── Posiciones ───────────────────────────────────────────────────────────

	/** Plano lateral: por delante y a un lado del buggy, a la altura de la cabina. */
	TORTUNABO_API FVector FinishSideLocation(const FVector& BuggyLocation, const FVector& BuggyForward);

	/** Dron: detrás y por encima del líder. */
	TORTUNABO_API FVector DroneLocation(const FVector& LeaderLocation, const FVector& LeaderForward);

	/** Acerca Current a Target con un suavizado exponencial de Speed (1/s); salta si está muy lejos (reaparición). */
	TORTUNABO_API FVector SmoothFollow(const FVector& Current, const FVector& Target, float DeltaSeconds, float Speed);

	/** Marco del podio: flota sobre la meta (no estorba a los que siguen) y mira hacia el tramo por el que llegan. */
	struct FPodiumFrame
	{
		FVector Origin = FVector::ZeroVector;
		/** Hacia dónde miran los buggies del podio (y desde dónde lo mira la cámara). */
		FVector Facing = FVector::ForwardVector;
		FVector Right = FVector::RightVector;
	};

	/** Altura del podio sobre el centro de la puerta de meta (cm). */
	inline constexpr double PodiumHeightCm = 1600.0;
	/** Losa del podio (cm): ancho, fondo y grueso. */
	inline const FVector PodiumSlabSizeCm(1400.0, 2400.0, 60.0);
	/** Escalones (ancho a lo largo de Right, fondo a lo largo de Facing). */
	inline constexpr double PodiumStepWidthCm = 400.0;
	inline constexpr double PodiumStepDepthCm = 520.0;

	TORTUNABO_API FPodiumFrame PodiumFrameFromFinish(const FVector& FinishCenter, const FVector& RaceForward);

	/** Alto del escalón del puesto FinishOrder (1 = el más alto; 0 a partir del 4.º, que aparca en la losa). */
	TORTUNABO_API double PodiumStepHeightCm(int32 FinishOrder);

	/** Centro de la cara de arriba del escalón (o del hueco de la losa) del FinishOrder-ésimo en llegar. */
	TORTUNABO_API FVector PodiumSlotLocation(const FPodiumFrame& Frame, int32 FinishOrder);

	/** Cámara del podio: delante y algo por encima; sin VR gira despacio alrededor (TimeSeconds), con VR fija. */
	TORTUNABO_API FVector PodiumCameraLocation(const FPodiumFrame& Frame, bool bVR, double TimeSeconds);

	/** Punto al que mira la cámara del podio (los escalones). */
	TORTUNABO_API FVector PodiumLookAt(const FPodiumFrame& Frame);
}
