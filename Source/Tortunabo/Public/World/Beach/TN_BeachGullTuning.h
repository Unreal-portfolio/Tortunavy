#pragma once

#include "CoreMinimal.h"

/**
 * Cifras y cuentas puras de las gaviotas de la playa: la zona de gaviotas (ATN_BeachGullZone: picado y cagada). Sin mundo
 * ni objetos: las usan el actor y las pruebas Tortunabo.Beach.Gull (Private/Tests/TN_BeachGullTuningTest.cpp).
 *
 * Ronda 4, tarea 5 (nerf de la gaviota y de su caca): casi no se podían esquivar. Lo que se pidió (GDD, ronda 4): el seguimiento es más lento y la caca se esquiva con una plancha a tiempo.
 * Con las velocidades de verdad de la tortuga (las del Blueprint, comprobadas en BP_TortugaCharacter el 2026-10-04: andando
 * 200 cm/s, corriendo 400; la plancha sale a 350 + la velocidad del salto), el blanco (#636):
 *  1. sigue a la tortuga más rápido de lo que anda y más despacio de lo que corre (GullChaseSpeed, 250): andando te pilla;
 *     esprintando en línea recta le ganas 1,5 m por segundo a la sombra y te libras de todos los tamaños;
 *  2. los últimos CommitSeconds, el pájaro (o lo que cae) ya va lanzado por la línea que llevaba la tortuga en ese momento:
 *     por esa línea la acompaña (hasta su velocidad de entonces, nunca hacia atrás) y hacia los lados apenas corrige
 *     (LateCorrection). Andando, girar al lanzarse no da tiempo a salir del golpe; corriendo, girar o darse la vuelta libra.
 * La plancha en el momento justo libra además de las cagadas (TNBeach::IsDodgingByBellyDive, ventana en
 * BellyDiveDodgeWindow) y del picado entero (el panzazo); el golpe (lo que coge el pico, la mancha) es el de la sombra dura.
 * La primera versión de este nerf (420 cm/s, algo más de lo que se corre) no dejaba esquivar corriendo en línea recta.
 */
namespace TNBeachGullTuning
{
	// ── Velocidades de la tortuga con las que se ajusta (las del Blueprint, Docs/Biblia_Tortunavy.md §13) ──

	constexpr float TurtleWalkSpeed = 200.f;
	constexpr float TurtleRunSpeed = 400.f;

	// ── Líneas de tiempo (s desde que empieza el ataque; sin cambios en el nerf) ──

	/** Cagada de la zona: vuela hasta encima de la tortuga y la suelta a PoopDropTime; tarda PoopFallTime en caer. */
	constexpr float PoopDropTime = 1.5f;
	constexpr float PoopFallTime = 2.1f;

	/** Picado de la zona: sube y se coloca en DiveClimbTime y baja en picado DiveTime (desde que aparece la sombra). */
	constexpr float DiveClimbTime = 1.f;
	constexpr float DiveTime = 2.3f;

	// ── Zona de gaviotas: a quién ataca ──

	/** Radio de ataque (cm) = huella de la zona × AttackFootprintScale + AttackRadiusPad (antes: huella + 800). */
	constexpr float AttackFootprintScale = 0.8f;
	constexpr float AttackRadiusPad = 0.f;

	/** Tiempo entre ataques (s) mientras haya tortugas debajo (antes 3-6). */
	constexpr float AttackIntervalMin = 4.f;
	constexpr float AttackIntervalMax = 7.f;

	// ── Persecución del blanco (los dos ataques) ──

	/**
	 * Velocidad (cm/s) a la que el blanco sigue a la tortuga hasta lanzarse (#636): entre la de andar y la de correr. Andando
	 * (200) el blanco no se despega de ella; esprintando en línea recta (400) le gana 150 cm/s y al golpe la tiene a 3,8 m
	 * (picado) y 4,3 m (cagada), más de lo que alcanza el pájaro más grande (3,1 m). Con 300 el picado
	 * aún cogía a los grandes; con 420 (la versión anterior) nadie se despegaba corriendo.
	 */
	constexpr float GullChaseSpeed = 250.f;

	// ── Picado de la zona ──

	/**
	 * El blanco del picado sigue a la tortuga a DiveChaseSpeed cm/s como mucho (antes 625, pensado para andar a 450 y correr
	 * a 800). Los últimos DiveCommitSeconds antes de llegar abajo (cuando pliega las alas del todo) va lanzado por la línea
	 * de la tortuga y hacia los lados corrige a DiveLateCorrection (antes seguía igual hasta el final).
	 */
	constexpr float DiveChaseSpeed = GullChaseSpeed;
	constexpr float DiveCommitSeconds = 1.5f;
	constexpr float DiveLateCorrection = 75.f;

	/** Coge a la tortuga cuyo centro está a menos de GrabRadius × tamaño + GrabPad cm del blanco (antes 300 y 45). */
	constexpr float GrabRadius = 220.f;
	constexpr float GrabPad = 25.f;

	// ── Cagada de la zona ──

	/**
	 * Mientras la gaviota vuela hasta encima y mientras cae, el blanco sigue a la tortuga a PoopChaseSpeed (antes 625 y 420). Los
	 * últimos PoopCommitSeconds (el «!» deja de parpadear y se queda fijo) cae por la línea que llevaba la tortuga y hacia
	 * los lados corrige a PoopLateCorrection.
	 */
	constexpr float PoopChaseSpeed = GullChaseSpeed;
	constexpr float PoopCommitSeconds = 1.5f;
	constexpr float PoopLateCorrection = 75.f;

	/** Mancha (cm, por el tamaño) y holgura del golpe (cm): antes 280 y 45. La sombra dura crece hasta la mancha. */
	constexpr float SplatRadius = 200.f;
	constexpr float SplatPad = 35.f;

	// ── Plancha ──

	/**
	 * La plancha libra de la cagada si al caer la tortuga va en plancha (pose de panzazo) en el aire, o arrastrándose sobre
	 * la tripa aún a BellyDodgeMinSpeed cm/s o más: tirarse en el momento justo, no tumbarse a esperar.
	 */
	constexpr float BellyDodgeMinSpeed = 250.f;

	/**
	 * Ventana de la plancha (#636). La plancha es el segundo salto en el aire: sale a BellyDiveForwardSpeed + la velocidad que
	 * llevaba (ATortugaCharacter::DiveForwardSpeed, 350 en BP_TortugaCharacter), va BellyDiveAirSecondsMin-Max por el aire
	 * (según cuándo se pulse tras el primer salto) y al tocar la arena conserva BellyLandingKeep de la velocidad
	 * (UTN_TurtleMovementComponent::BellyLandingKeep). Sobre la arena frena con BellySandFriction cm/s² más BellySandDrag por
	 * la velocidad (BellyFrictionSand y BellyDrag del mismo componente; el rozamiento solo crece pasado 1,2 s, fuera de la
	 * ventana). Libra mientras va por el aire y mientras se arrastra a BellyDodgeMinSpeed o más: corriendo, 0,59-0,69 s desde
	 * que despega; andando, 0,48-0,58 s. Para que la cagada le pase por encima, tiene que caer dentro de esa ventana.
	 * Valores comprobados en BP_TortugaCharacter el 2026-10-04; si cambian allí, hay que cambiarlos aquí.
	 */
	constexpr float BellyDiveForwardSpeed = 350.f;
	constexpr float BellyDiveAirSecondsMin = 0.3f;
	constexpr float BellyDiveAirSecondsMax = 0.4f;
	constexpr float BellyLandingKeep = 0.9f;
	constexpr float BellySandFriction = 800.f;
	constexpr float BellySandDrag = 1.5f;

	/** Velocidad en planta (cm/s) con la que la plancha toca la arena si se lanza yendo a RunSpeed (675 corriendo). */
	inline float BellyDiveLandingSpeed(float RunSpeed)
	{
		return (BellyDiveForwardSpeed + FMath::Max(0.f, RunSpeed)) * BellyLandingKeep;
	}

	/** Velocidad (cm/s) del arrastre sobre la arena a los SlideSeconds de tocarla con LandingSpeed: dv/dt = -(F + D·v). */
	inline float BellySlideSpeedAt(float LandingSpeed, float SlideSeconds)
	{
		const float K = BellySandFriction / BellySandDrag;
		const float Speed = (LandingSpeed + K) * FMath::Exp(-BellySandDrag * FMath::Max(0.f, SlideSeconds)) - K;
		return FMath::Max(0.f, Speed);
	}

	/** Segundos que se arrastra a BellyDodgeMinSpeed o más tras tocar la arena con LandingSpeed (0 si ya toca más despacio). */
	inline float BellySlideDodgeSeconds(float LandingSpeed)
	{
		if (LandingSpeed <= BellyDodgeMinSpeed)
		{
			return 0.f;
		}
		const float K = BellySandFriction / BellySandDrag;
		return FMath::Loge((LandingSpeed + K) / (BellyDodgeMinSpeed + K)) / BellySandDrag;
	}

	/** La ventana (s desde que despega en plancha) en la que libra: AirSeconds por el aire más el arrastre deprisa. */
	inline float BellyDiveDodgeWindow(float RunSpeed, float AirSeconds)
	{
		return FMath::Max(0.f, AirSeconds) + BellySlideDodgeSeconds(BellyDiveLandingSpeed(RunSpeed));
	}

	// ── Cuentas ──

	/** Cómo persigue el blanco en un ataque: hasta CommitAt, a ChaseSpeed; de CommitAt a EndAt, lanzado; después, quieto. */
	struct FChasePlan
	{
		float CommitAt = 0.f;
		float EndAt = 0.f;
		float ChaseSpeed = 0.f;
		float LateCorrection = 0.f;
	};

	/** Lo que el blanco recuerda al lanzarse: la línea (unitaria; cero si la tortuga estaba parada) y su velocidad por ella. */
	struct FChaseState
	{
		bool bCommitted = false;
		FVector2D Dir = FVector2D::ZeroVector;
		float Speed = 0.f;
	};

	/** Picado de la zona (T: segundos desde que empieza el ataque; llega abajo a DiveClimbTime + DiveTime). */
	inline FChasePlan DivePlan()
	{
		const float Strike = DiveClimbTime + DiveTime;
		return FChasePlan{ Strike - DiveCommitSeconds, Strike, DiveChaseSpeed, DiveLateCorrection };
	}

	/** Cagada de la zona (T: segundos desde que empieza el ataque; cae a PoopDropTime + PoopFallTime). */
	inline FChasePlan PoopPlan()
	{
		const float Impact = PoopDropTime + PoopFallTime;
		return FChasePlan{ Impact - PoopCommitSeconds, Impact, PoopChaseSpeed, PoopLateCorrection };
	}

	/** La más rápida de todas (para el suavizado del blanco en los clientes). */
	constexpr float MaxChaseSpeed()
	{
		return DiveChaseSpeed > PoopChaseSpeed ? DiveChaseSpeed : PoopChaseSpeed;
	}

	/** El blanco From, hacia Target a MaxSpeed cm/s como mucho durante DeltaSeconds (en planta). */
	inline FVector2D StepToward(const FVector2D& From, const FVector2D& Target, float MaxSpeed, float DeltaSeconds)
	{
		const FVector2D Delta = Target - From;
		const double Dist = Delta.Size();
		const double MaxStep = FMath::Max(0.0, static_cast<double>(MaxSpeed) * static_cast<double>(DeltaSeconds));
		if (Dist <= MaxStep || Dist <= UE_KINDA_SMALL_NUMBER)
		{
			return Target;
		}
		return From + Delta * (MaxStep / Dist);
	}

	/**
	 * Un paso del blanco Aim a los T segundos del ataque, con la tortuga en Target yendo a TargetVelocity (cm/s, en planta).
	 * Antes de lanzarse, hacia ella a Plan.ChaseSpeed; al lanzarse apunta en State su línea y su velocidad (como mucho
	 * ChaseSpeed) y desde ahí la acompaña por esa línea (lo que ella avance por ella, nunca hacia atrás ni más que entonces)
	 * y hacia ella corrige a Plan.LateCorrection. Pasado Plan.EndAt, quieto.
	 */
	inline FVector2D StepAim(const FChasePlan& Plan, float T, FChaseState& State, const FVector2D& Aim, const FVector2D& Target,
		const FVector2D& TargetVelocity, float DeltaSeconds)
	{
		if (T >= Plan.EndAt)
		{
			return Aim;
		}
		if (T < Plan.CommitAt)
		{
			return StepToward(Aim, Target, Plan.ChaseSpeed, DeltaSeconds);
		}
		if (!State.bCommitted)
		{
			State.bCommitted = true;
			const double Speed = TargetVelocity.Size();
			State.Speed = static_cast<float>(FMath::Min(Speed, static_cast<double>(Plan.ChaseSpeed)));
			State.Dir = Speed > 1.0 ? TargetVelocity / Speed : FVector2D::ZeroVector;
		}
		const double Along = FMath::Clamp(FVector2D::DotProduct(TargetVelocity, State.Dir), 0.0, static_cast<double>(State.Speed));
		return StepToward(Aim + State.Dir * (Along * static_cast<double>(DeltaSeconds)), Target, Plan.LateCorrection, DeltaSeconds);
	}

	/** true si ya va lanzado (a los T segundos del ataque): la gaviota pliega las alas; el «!» de la cagada se queda fijo. */
	inline bool IsCommitted(const FChasePlan& Plan, float T)
	{
		return T >= Plan.CommitAt;
	}

	/** true si lo que cae en planta a Dist2D cm de la tortuga le da con el radio Radius × SizeK + Pad. */
	inline bool IsInsideHit(double Dist2D, float Radius, float SizeK, float Pad)
	{
		return Dist2D <= static_cast<double>(Radius * SizeK + Pad);
	}

	/** true si la plancha la libra: pose de panzazo y, además, en el aire o arrastrándose aún deprisa. */
	inline bool DodgesByBellyDive(bool bBellyPose, bool bAirborne, float Speed2D)
	{
		return bBellyPose && (bAirborne || Speed2D >= BellyDodgeMinSpeed);
	}

	/**
	 * true si una plancha lanzada yendo a RunSpeed hace SecondsSinceDive s (AirSeconds por el aire) libra en este momento: el
	 * mismo DodgesByBellyDive que mira el servidor, con el estado que tendría la tortuga (en el aire o arrastrándose).
	 */
	inline bool DodgesByBellyDiveAt(float SecondsSinceDive, float RunSpeed, float AirSeconds)
	{
		if (SecondsSinceDive < 0.f)
		{
			return false;
		}
		if (SecondsSinceDive < AirSeconds)
		{
			return DodgesByBellyDive(true, true, BellyDiveForwardSpeed + RunSpeed);
		}
		return DodgesByBellyDive(true, false, BellySlideSpeedAt(BellyDiveLandingSpeed(RunSpeed), SecondsSinceDive - AirSeconds));
	}
}
