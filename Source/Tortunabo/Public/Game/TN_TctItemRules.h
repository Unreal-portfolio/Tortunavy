#pragma once

#include "CoreMinimal.h"

/**
 * Objetos de combate de Todos contra Todos (#651, plan maestro §5: «física a medias», sin vida, veneno ni curas). Cada uno
 * empuja, derriba, atrae o marea; nadie pierde vida: se cae al agua o se queda en pie.
 *
 * Nuevos (con las mallas IA de #600, /Game/Art/IA/todos_contra_todos):
 *  - KnockoutPistol: pistola de noqueo; el primer cuerpo en la línea de tiro cae derribado (el derribo de siempre).
 *  - AirBlunderbuss: trabuco de aire; cono corto que empuja a todas las que pilla (más cuanto más cerca) y retrocede a quien
 *    dispara.
 *  - Grapple: garfio; atrae hacia ti a la primera tortuga de la línea o, si da en el escenario, te lleva a ti hasta allí.
 *  - Shovel: pala de mano; golpe en arco delante que lanza a las que pilla.
 *  - BeachBall: balón de playa; rebota mucho y empuja a quien toca.
 *  - Anchor: ancla; se lanza en parábola, derriba en un círculo al caer y lastra (lenta y sin apenas salto) a las de dentro.
 *  - JellyDart: dardo de medusa; rápido y casi recto, marea a la tortuga que toca.
 *  - InkPistol: pistola de tinta; la tinta de calamar de siempre (pantalla manchada), con tres cargas.
 * Reutilizados tal cual: la bola (DT_Items, Throwable), la concha trampa (Conch), la cabezota (BigHead), la mina de arena y el
 * disco volador (objetos de la carrera).
 *
 * De ataque y control (#714):
 *  - Cocobomba: coco con mecha que se lanza en parábola; a los 2 s explota (3,5 m) y lanza a las que pilla, más cuanto más
 *    cerca. El caparazón protege. 2 cargas.
 *  - Alga: charco de alga que se suelta a los pies (mirando abajo) o se lanza corto; 8 s de charco de 3 m donde se resbala
 *    (poco agarre: la tortuga sigue derivando hacia donde iba al girar) y el salto se queda corto. 2 cargas.
 *  - GaviotaLadrona: una gaviota va a por la tortuga más cercana a menos de 20 m, le quita el objeto de la mano y te lo trae;
 *    si no lleva nada, la marea 2 s. 1 carga.
 *
 * Lógica pura, sin mundo ni red (ítems, cargas, reaparición en los puntos de objetos, reparto de los puntos y cuánto empuja
 * cada golpe): la recorren las pruebas Tortunabo.Tct.Items.*.
 */
enum class ETNTctItem : uint8
{
	None,
	KnockoutPistol,
	AirBlunderbuss,
	Grapple,
	Shovel,
	BeachBall,
	Anchor,
	JellyDart,
	InkPistol,
	Ball,
	ConchTrap,
	BigHead,
	SandMine,
	Frisbee,
	Cocobomba,
	Alga,
	GaviotaLadrona,
	Count
};

/** De dónde sale un objeto de Todos contra Todos. */
enum class ETNTctItemSource : uint8
{
	/** Definido en código (TNTctItems, UseType TctItem). */
	Code,
	/** Una fila de DT_Items, buscada por su UseType. */
	Catalog,
	/** Un objeto de la carrera de la playa (TNRaceItems). */
	Race
};

/** Lo fijo de cada objeto. */
struct FTNTctItemSpec
{
	ETNTctItem Kind = ETNTctItem::None;
	/** Nombre para el ItemId, el registro y la consola («Shovel»...). */
	const TCHAR* Code = TEXT("");
	ETNTctItemSource Source = ETNTctItemSource::Code;
	/** Usos (solo los de código; los demás se gastan como siempre). */
	int32 Charges = 1;
	/** Peso en el sorteo de los puntos de objetos. */
	float PadWeight = 1.f;
};

/** Valores de los efectos (cm, cm/s y s). */
namespace TNTctItemTuning
{
	inline constexpr float KnockoutRange = 2200.f;
	inline constexpr float KnockoutRadius = 40.f;
	inline constexpr float KnockoutSeconds = 2.5f;
	inline constexpr float KnockoutPush = 650.f;
	inline constexpr float KnockoutUp = 300.f;

	inline constexpr float BlunderbussRange = 1000.f;
	inline constexpr float BlunderbussHalfAngleDeg = 35.f;
	inline constexpr float BlunderbussPushNear = 1750.f;
	inline constexpr float BlunderbussPushFar = 700.f;
	inline constexpr float BlunderbussUp = 520.f;
	inline constexpr float BlunderbussRecoilSpeed = 550.f;
	inline constexpr float BlunderbussRecoilUp = 160.f;

	inline constexpr float GrappleRange = 2600.f;
	inline constexpr float GrappleRadius = 45.f;
	inline constexpr float GrapplePullMin = 900.f;
	inline constexpr float GrapplePullMax = 2100.f;
	inline constexpr float GrapplePullUp = 520.f;
	/** Más cerca que esto del punto del escenario, el garfio no te lleva (no hay a dónde ir). */
	inline constexpr float GrappleSelfMinDistance = 350.f;

	inline constexpr float ShovelReach = 260.f;
	inline constexpr float ShovelHalfAngleDeg = 65.f;
	inline constexpr float ShovelPush = 1350.f;
	inline constexpr float ShovelUp = 600.f;

	inline constexpr float BallSpeed = 1700.f;
	inline constexpr float BallPitchDeg = 14.f;
	inline constexpr float BallPushSpeed = 1150.f;
	inline constexpr float BallUp = 380.f;
	inline constexpr float BallMinPushSpeed = 250.f;
	/** Lo que tarda el balón en poder volver a empujar a la misma tortuga. */
	inline constexpr float BallRehitSeconds = 0.6f;
	inline constexpr float BallLifeSeconds = 10.f;

	inline constexpr float AnchorSpeed = 1150.f;
	inline constexpr float AnchorPitchDeg = 32.f;
	inline constexpr float AnchorSplashRadius = 360.f;
	inline constexpr float AnchorKnockSeconds = 2.4f;
	inline constexpr float AnchorImpulse = 900.f;
	inline constexpr float AnchorImpulseUp = 400.f;
	/** Lastre: segundos, tope de velocidad y multiplicador del salto. */
	inline constexpr float AnchorHeavySeconds = 5.f;
	inline constexpr float AnchorHeavySpeedCap = 260.f;
	inline constexpr float AnchorHeavyJumpMultiplier = 0.4f;
	inline constexpr float AnchorLifeSeconds = 6.f;

	inline constexpr float DartSpeed = 3400.f;
	inline constexpr float DartPitchDeg = 3.f;
	inline constexpr float DartDizzySeconds = 4.5f;
	inline constexpr float DartLifeSeconds = 3.f;

	inline constexpr float InkPistolSpeed = 1500.f;

	inline constexpr float CocoSpeed = 1150.f;
	inline constexpr float CocoPitchDeg = 30.f;
	/** Mecha: segundos desde que se lanza hasta que explota. */
	inline constexpr float CocoFuseSeconds = 2.f;
	inline constexpr float CocoBlastRadius = 350.f;
	inline constexpr float CocoPushNear = 1600.f;
	inline constexpr float CocoPushFar = 600.f;
	inline constexpr float CocoUpNear = 720.f;
	inline constexpr float CocoUpFar = 360.f;

	/** Charco de alga: lanzamiento corto, duración, radio (3 m de ancho) y lo que queda del agarre dentro. */
	inline constexpr float AlgaSpeed = 650.f;
	inline constexpr float AlgaPitchDeg = 25.f;
	/** Mirando más abajo que esto, el charco se suelta a los pies. */
	inline constexpr float AlgaDropPitchDeg = -10.f;
	inline constexpr float AlgaPuddleSeconds = 8.f;
	inline constexpr float AlgaPuddleRadius = 150.f;
	/** Altura por encima y por debajo del charco en la que los pies cuentan como dentro (uu). */
	inline constexpr float AlgaPuddleReachUp = 60.f;
	inline constexpr float AlgaPuddleReachDown = 40.f;
	inline constexpr float AlgaFrictionScale = 0.06f;
	inline constexpr float AlgaBrakingScale = 0.08f;
	inline constexpr float AlgaAccelerationScale = 0.35f;
	inline constexpr float AlgaJumpMultiplier = 0.45f;

	/** Gaviota ladrona: alcance para elegir víctima, velocidad, distancia a la que roba o entrega, mareo y vida máxima. */
	inline constexpr float ThiefRange = 2000.f;
	inline constexpr float ThiefSpeed = 1500.f;
	inline constexpr float ThiefReach = 120.f;
	inline constexpr float ThiefFlyHeight = 220.f;
	inline constexpr float ThiefDizzySeconds = 2.f;
	inline constexpr float ThiefMaxSeconds = 10.f;

	/**
	 * Puntos de objetos: reaparición tras cogerlo (#778: 6 s, antes 12 s; más puntos con objeto a la vez, nunca más de uno
	 * por punto), primera aparición de la ronda y escalonado entre puntos.
	 */
	inline constexpr float PadRespawnSeconds = 6.f;
	inline constexpr float PadFirstSpawnSeconds = 0.75f;
	inline constexpr float PadStaggerSeconds = 0.35f;
	/** Un punto con el agua a menos de esto por debajo ya no saca objetos. */
	inline constexpr float PadWaterClearance = 40.f;
}

/**
 * Reloj de un punto de objetos (ATN_TctItemPad). Solo el servidor. Entre rondas está parado; al empezar una, saca su primer
 * objeto pasado un momento; cuando alguien lo coge, el siguiente sale RespawnSeconds después. Bajo el agua no saca nada.
 */
struct FTNTctPadClock
{
	/** Hora (s) a la que toca sacar un objeto; < 0 = parado. */
	double NextSpawnTime = -1.0;
	bool bHasItem = false;

	/** Empieza la ronda a la hora Now: el primer objeto, pasado FirstDelay. */
	void StartRound(double Now, float FirstDelay)
	{
		bHasItem = false;
		NextSpawnTime = Now + FMath::Max(0.f, FirstDelay);
	}

	/** Fin de la ronda (o punto inundado): sin objeto y sin reaparición. */
	void Stop()
	{
		bHasItem = false;
		NextSpawnTime = -1.0;
	}

	bool IsRunning() const { return NextSpawnTime >= 0.0 || bHasItem; }

	/** ¿Toca sacar un objeto ahora? */
	bool ShouldSpawn(double Now, bool bSubmerged) const
	{
		return !bHasItem && NextSpawnTime >= 0.0 && Now >= NextSpawnTime && !bSubmerged;
	}

	void MarkSpawned()
	{
		bHasItem = true;
		NextSpawnTime = -1.0;
	}

	/** Alguien lo ha cogido a la hora Now: el siguiente, RespawnSeconds después. Sin objeto puesto, no hace nada. */
	void MarkTaken(double Now, float RespawnSeconds)
	{
		if (!bHasItem)
		{
			return;
		}
		bHasItem = false;
		NextSpawnTime = Now + FMath::Max(0.f, RespawnSeconds);
	}
};

namespace TNTctItemRules
{
	/** Ficha de Kind (la de None si no es ninguno). */
	TORTUNABO_API const FTNTctItemSpec& Spec(ETNTctItem Kind);

	/** Todos los objetos de verdad (sin None ni Count). */
	TORTUNABO_API TArray<ETNTctItem> AllKinds();

	/** ItemId de un objeto de código con Charges usos: «Tct_<Code>_<Charges>». */
	TORTUNABO_API FName MakeItemId(ETNTctItem Kind, int32 Charges);

	/** El objeto y sus usos de un ItemId de MakeItemId. false si no lo es. */
	TORTUNABO_API bool ParseItemId(FName ItemId, ETNTctItem& OutKind, int32& OutCharges);

	/** Lo que queda en la mano tras usar uno: el mismo con un uso menos, o nada (NAME_None) si era el último. */
	TORTUNABO_API FName ItemIdAfterUse(FName ItemId);

	/** Índice elegido de Weights con Roll en [0, 1); INDEX_NONE si no hay peso. */
	TORTUNABO_API int32 PickWeighted(const TArray<float>& Weights, float Roll);

	/**
	 * Objeto para un punto de objetos entre Available (los que se pueden dar ahora), con su peso; nunca el mismo que salió la
	 * última vez en ese punto (Last) si hay otro. None si no hay ninguno.
	 */
	TORTUNABO_API ETNTctItem PickPadItem(const TArray<ETNTctItem>& Available, ETNTctItem Last, float Roll);

	/** Un punto en PadZ con el agua en WaterZ ya no saca objetos. */
	TORTUNABO_API bool IsPadSubmerged(float PadZ, float WaterZ, float Clearance = TNTctItemTuning::PadWaterClearance);

	/** Puntos de objetos en uso en una ronda con Players tortugas de PadCount que hay: dos más que tortugas, de 4 a PadCount. */
	TORTUNABO_API int32 ActivePadCount(int32 Players, int32 PadCount);

	/** Segundos hasta el primer objeto de la ronda del punto PadIndex (escalonados para que no salgan todos a la vez). */
	TORTUNABO_API float PadFirstSpawnDelay(int32 PadIndex);

	/**
	 * Count puntos de objetos entre Candidates (índices): el primero, el más cercano a Center (el centro se disputa); cada
	 * siguiente, el que más lejos queda de los ya elegidos. Nunca a menos de MinFromAvoid de un punto de Avoid (las salidas)
	 * salvo que no haya otros. Determinista.
	 */
	TORTUNABO_API TArray<int32> PickPadPoints(const TArray<FVector>& Candidates, int32 Count, const FVector& Center,
		const TArray<FVector>& Avoid, float MinFromAvoid);

	// ── Golpes (servidor; velocidades que se dan con UTN_TurtleMovementComponent::LaunchFromServer) ──────────────────────

	/** Pistola de noqueo: el empujón del derribo hacia donde iba el tiro. */
	TORTUNABO_API FVector KnockoutImpulse(const FVector& ShotDirection);

	/**
	 * Trabuco de aire desde Origin mirando a Forward (en el plano): si Victim está en el cono, true y el empujón (más fuerte
	 * cuanto más cerca, hacia fuera del cañón y algo hacia arriba).
	 */
	TORTUNABO_API bool BlunderbussPush(const FVector& Origin, const FVector& Forward, const FVector& Victim, FVector& OutVelocity);

	/** Retroceso del trabuco para quien dispara. */
	TORTUNABO_API FVector BlunderbussRecoil(const FVector& Forward);

	/** Garfio: velocidad que lleva a la tortuga de From hacia To (la víctima hacia quien dispara, o quien dispara al escenario). */
	TORTUNABO_API FVector GrapplePull(const FVector& From, const FVector& To);

	/** Pala desde Origin mirando a Forward: si Victim está al alcance y en el arco, true y el golpe. */
	TORTUNABO_API bool ShovelHit(const FVector& Origin, const FVector& Forward, const FVector& Victim, FVector& OutVelocity);

	/** Balón de playa que va a BallVelocity: empujón a quien toca (cero si va demasiado lento). */
	TORTUNABO_API FVector BallPush(const FVector& BallVelocity);

	/** Ancla que cae en Center: si Victim está dentro, true y el empujón del derribo (hacia fuera). */
	TORTUNABO_API bool AnchorSplash(const FVector& Center, const FVector& Victim, FVector& OutImpulse);

	/** Cocobomba que explota en Center: si Victim está en el radio, true y el empujón (más fuerte cuanto más cerca, hacia fuera). */
	TORTUNABO_API bool CocoBlast(const FVector& Center, const FVector& Victim, FVector& OutVelocity);

	/** Pies en Feet dentro del charco de alga de Center y Radius (en planta y a la altura del suelo). */
	TORTUNABO_API bool IsInPuddle(const FVector& Center, float Radius, const FVector& Feet);

	/**
	 * Gaviota ladrona lanzada desde Origin: índice de la víctima entre Candidates (las posiciones de las demás tortugas que se
	 * pueden molestar), la más cercana a menos de Range. INDEX_NONE si no hay ninguna.
	 */
	TORTUNABO_API int32 PickThiefVictim(const FVector& Origin, const TArray<FVector>& Candidates, float Range = TNTctItemTuning::ThiefRange);
}

/** Agarre de una tortuga en el suelo: lo que el charco de alga cambia de su UCharacterMovementComponent. */
struct FTNTctGrip
{
	float GroundFriction = 8.f;
	float BrakingDeceleration = 2048.f;
	float MaxAcceleration = 2048.f;
};

namespace TNTctItemRules
{
	/** El agarre dentro del charco de alga a partir del de siempre: casi sin rozamiento ni frenada y con poca aceleración. */
	TORTUNABO_API FTNTctGrip SlipperyGrip(const FTNTctGrip& Base);
}
