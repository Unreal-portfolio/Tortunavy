#pragma once

#include "CoreMinimal.h"

/**
 * Reglas puras de las criaturas y peligros del Excel de diseño (lote #691): forcejeo para escapar machacando salto,
 * arenas movedizas (#684), cangrejo arrastrador (#685), cangrejo subterráneo (#686), erizo enterrado (#687), erizos
 * checos (#688), refugio del búnker (#689), basura y trinchera (#690). Los tentáculos de la medusa (#683) están en
 * TN_BeachTrampolineRules.h, junto al rebote.
 *
 * Sin mundo ni actores: los actores las aplican en el servidor y las prueba Tortunabo.Beach.Creatures.*. Aquí van los
 * estados (derribo, bola aturdida, lanzamiento, ralentización, arrastre). La vida, el veneno y las muertes de cada uno
 * (decisión del 06-10, plan maestro §3) están en UTN_HazardTuning y TN_HazardEffects.h (#871).
 */
namespace TNBeachCreatureRules
{
	// ─────────────────────────────────────────────────────────────────────────
	// Forcejeo: machacar salto para soltarse
	// ─────────────────────────────────────────────────────────────────────────

	/**
	 * Cuenta de pulsaciones de salto de quien está atrapada. Cada pulsación suma 1 y lo acumulado se pierde a DecayPerSecond:
	 * pulsar despacio no basta, machacar sí.
	 */
	struct FMashCounter
	{
		float Progress = 0.f;
		double LastAt = -1.0;

		float Value(double Now, float DecayPerSecond) const
		{
			if (LastAt < 0.0) { return 0.f; }
			const double Lost = FMath::Max(0.0, Now - LastAt) * DecayPerSecond;
			return static_cast<float>(FMath::Max(0.0, Progress - Lost));
		}

		void Press(double Now, float DecayPerSecond)
		{
			Progress = Value(Now, DecayPerSecond) + 1.f;
			LastAt = Now;
		}

		void Reset()
		{
			Progress = 0.f;
			LastAt = -1.0;
		}
	};

	/** Pulsaciones netas que hacen falta para soltarse y lo que se pierde por segundo. */
	constexpr float EscapePresses = 6.f;
	constexpr float EscapeDecay = 1.5f;

	inline bool HasEscaped(const FMashCounter& Counter, double Now, float Required = EscapePresses, float Decay = EscapeDecay)
	{
		return Counter.Value(Now, Decay) >= Required;
	}

	// ─────────────────────────────────────────────────────────────────────────
	// #684 Arenas movedizas
	// ─────────────────────────────────────────────────────────────────────────

	namespace Quicksand
	{
		/**
		 * Fracción de la velocidad de andar que se conserva tras SecondsInside dentro: de StartFactor al entrar baja en línea
		 * recta hasta MinFactor a los RampSeconds y ahí se queda.
		 */
		inline float SpeedFactor(float SecondsInside, float StartFactor = 0.7f, float MinFactor = 0.25f, float RampSeconds = 2.5f)
		{
			const float Alpha = RampSeconds > 0.f ? FMath::Clamp(SecondsInside / RampSeconds, 0.f, 1.f) : 1.f;
			return FMath::Lerp(StartFactor, MinFactor, Alpha);
		}

		/** Queda atrapada (sin control) tras TrapAfter segundos seguidos dentro. */
		inline bool ShouldTrap(float SecondsInside, float TrapAfter)
		{
			return TrapAfter > 0.f && SecondsInside >= TrapAfter;
		}

		/** Sale al soltarse machacando salto o al acabar el tope de tiempo atrapada. Nunca muere ni pierde nada. */
		inline bool ShouldRelease(float SecondsTrapped, float MaxTrapped, bool bEscaped)
		{
			return bEscaped || SecondsTrapped >= MaxTrapped;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// #685 Cangrejo arrastrador
	// ─────────────────────────────────────────────────────────────────────────

	namespace DragCrab
	{
		enum class EState : uint8
		{
			Roam,
			Chase,
			Drag,
			Recover,
		};

		/** Por qué acaba un arrastre (None: sigue). */
		enum class EDragEnd : uint8
		{
			None,
			/** Ha llegado a la distancia máxima: la suelta derribada con un empujón. */
			Distance,
			/** Se ha soltado machacando salto. */
			Escaped,
			/** Un objeto lanzado lo ha mareado. */
			HitStunned,
			/** Delante hay un desnivel, agua honda o una zona de muerte o de rescate: la suelta antes de llevarla allí. */
			Unsafe,
		};

		/** Velocidad de persecución por debajo de la de una tortuga corriendo: se le puede escapar. */
		inline bool IsEscapableChaseSpeed(float ChaseSpeed, float TurtleRunSpeed)
		{
			return ChaseSpeed < TurtleRunSpeed;
		}

		/** Lo que avanza el arrastre en este paso sin pasarse de la distancia máxima. */
		inline float DragStep(float Dragged, float MaxDrag, float Speed, float DeltaSeconds)
		{
			return FMath::Clamp(Speed * DeltaSeconds, 0.f, FMath::Max(0.f, MaxDrag - Dragged));
		}

		/** El suelo de delante sirve para seguir arrastrando: hay suelo, no baja más de MaxDrop y no es zona de muerte ni de rescate. */
		inline bool IsSafeAhead(bool bHasGround, float GroundDelta, bool bDangerZone, float MaxDrop = 120.f)
		{
			return bHasGround && GroundDelta >= -MaxDrop && !bDangerZone;
		}

		/** Prioridad: mareo, escape, peligro delante y distancia. */
		inline EDragEnd DragEnd(float Dragged, float MaxDrag, bool bEscaped, bool bHitStunned, bool bSafeAhead)
		{
			if (bHitStunned) { return EDragEnd::HitStunned; }
			if (bEscaped) { return EDragEnd::Escaped; }
			if (!bSafeAhead) { return EDragEnd::Unsafe; }
			return Dragged >= MaxDrag - 1.f ? EDragEnd::Distance : EDragEnd::None;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// #686 Cangrejo subterráneo
	// ─────────────────────────────────────────────────────────────────────────

	namespace BurrowCrab
	{
		enum class EState : uint8
		{
			/** Enterrado: solo se ve el montículo. */
			Buried,
			/** Aviso: el montículo tiembla. */
			Tell,
			/** La pinza sale de la arena. */
			Strike,
			/** Sujeta a una tortuga en la pinza. */
			Hold,
			/** Tras lanzar o fallar: no vuelve a atacar. */
			Recharge,
			/** Mareado por un objeto lanzado: escondido, sin atacar. */
			Hide,
		};

		struct FTimes
		{
			float Tell = 0.6f;
			float Strike = 0.35f;
			float HoldMax = 2.5f;
			float Recharge = 4.f;
			float Hide = 3.f;
		};

		struct FInput
		{
			/** Segundos en el estado actual. */
			float Age = 0.f;
			/** Hay una tortuga atacable en el radio de aviso. */
			bool bTargetNear = false;
			/** Al acabar de salir la pinza, hay una al alcance de la pinza. */
			bool bTargetInGrab = false;
			/** Sigue sujetando a la que cogió. */
			bool bHolding = false;
			bool bEscaped = false;
			bool bHitStunned = false;
		};

		/** Siguiente estado (el mismo si no cambia). */
		inline EState Next(EState State, const FInput& In, const FTimes& T = FTimes())
		{
			if (In.bHitStunned && State != EState::Hide) { return EState::Hide; }
			switch (State)
			{
				case EState::Buried:   return In.bTargetNear ? EState::Tell : EState::Buried;
				case EState::Tell:     return In.Age >= T.Tell ? EState::Strike : EState::Tell;
				case EState::Strike:   return In.Age >= T.Strike ? (In.bTargetInGrab ? EState::Hold : EState::Recharge) : EState::Strike;
				case EState::Hold:     return (In.bEscaped || !In.bHolding || In.Age >= T.HoldMax) ? EState::Recharge : EState::Hold;
				case EState::Recharge: return In.Age >= T.Recharge ? EState::Buried : EState::Recharge;
				case EState::Hide:     return (!In.bHitStunned && In.Age >= T.Hide) ? EState::Recharge : EState::Hide;
				default:               return EState::Buried;
			}
		}

		/** Solo ataca desde enterrado: en recarga, escondido o sujetando, no. */
		inline bool CanStartAttack(EState State)
		{
			return State == EState::Buried;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// #687 Erizo enterrado
	// ─────────────────────────────────────────────────────────────────────────

	namespace UrchinSpikes
	{
		enum class EPhase : uint8
		{
			/** Pinchos casi ocultos: listo para saltar. */
			Hidden,
			/** Aviso muy corto: asoman. */
			Tell,
			/** Fuera: derriban a quien esté encima. */
			Out,
			/** Recargando: escondidos, sin derribar. */
			Recharge,
		};

		struct FTimes
		{
			float Tell = 0.15f;
			float Out = 1.2f;
			float Recharge = 3.f;
		};

		/** Fase a la hora Now de un disparo que empezó en TriggerAt (< 0: nunca ha saltado). */
		inline EPhase PhaseAt(double Now, double TriggerAt, const FTimes& T = FTimes())
		{
			if (TriggerAt < 0.0 || Now < TriggerAt) { return EPhase::Hidden; }
			const double Age = Now - TriggerAt;
			if (Age < T.Tell) { return EPhase::Tell; }
			if (Age < T.Tell + T.Out) { return EPhase::Out; }
			if (Age < T.Tell + T.Out + T.Recharge) { return EPhase::Recharge; }
			return EPhase::Hidden;
		}

		/** Se puede disparar de nuevo: escondido y con la recarga acabada. */
		inline bool CanTrigger(double Now, double TriggerAt, const FTimes& T = FTimes())
		{
			return PhaseAt(Now, TriggerAt, T) == EPhase::Hidden;
		}

		/** Derriba solo con los pinchos fuera (durante la recarga, no). */
		inline bool Strikes(EPhase Phase)
		{
			return Phase == EPhase::Out;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// #688 Erizos checos
	// ─────────────────────────────────────────────────────────────────────────

	namespace TankTrap
	{
		enum class EImpact : uint8
		{
			/** No va contra él. */
			None,
			/** Andando: la colisión la para y ya está. */
			Block,
			/** Corriendo o rodando en bola: rebote y derribo. */
			KnockDown,
		};

		/** Velocidad (cm/s) hacia el erizo desde la que el choque derriba: más que andar (450) y menos que correr (800). */
		constexpr float KnockSpeed = 600.f;

		inline EImpact Impact(float SpeedToward, float Threshold = KnockSpeed)
		{
			if (SpeedToward <= 30.f) { return EImpact::None; }
			return SpeedToward >= Threshold ? EImpact::KnockDown : EImpact::Block;
		}

		/** Lo que choca contra el erizo. */
		enum class EBody : uint8
		{
			/** Tortuga a pie (andando o corriendo). */
			Walker,
			/** Tortuga rodando en su bola de caparazón. */
			Ball,
		};

		/** Cómo se aplica el derribo según lo que choca. */
		enum class EResponse : uint8
		{
			/** Sin derribo: no va contra él o solo lo para la colisión. */
			None,
			/** Tortuga a pie: derribo con ragdoll y rebote. */
			KnockDownWalker,
			/** Bola: rebota y la tortuga queda mareada dentro del caparazón. */
			StunBall,
		};

		inline EResponse ResponseFor(EBody Body, float SpeedToward, float Threshold = KnockSpeed)
		{
			if (Impact(SpeedToward, Threshold) != EImpact::KnockDown)
			{
				return EResponse::None;
			}
			switch (Body)
			{
			case EBody::Ball: return EResponse::StunBall;
			default: return EResponse::KnockDownWalker;
			}
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// #689 Refugio del búnker
	// ─────────────────────────────────────────────────────────────────────────

	namespace Shelter
	{
		/** Point (en los ejes del refugio, origen en el suelo del centro) está dentro de la caja del refugio. */
		inline bool IsInside(const FVector& Local, const FVector& HalfExtent)
		{
			return FMath::Abs(Local.X) <= HalfExtent.X && FMath::Abs(Local.Y) <= HalfExtent.Y && Local.Z >= -50.f && Local.Z <= 2.f * HalfExtent.Z;
		}

		/** Una caja (centro y semiejes) de las paredes o el techo de un búnker. */
		struct FBunkerBox
		{
			FVector Center = FVector::ZeroVector;
			FVector Half = FVector::ZeroVector;
		};

		/** Medidas de un búnker: interior (X, Y y alto libre en Z), muros, puerta en -X y losa del techo. */
		struct FBunkerDims
		{
			FVector Interior = FVector(290.0, 215.0, 240.0);
			double Wall = 40.0;
			double DoorHalf = 95.0;
			double DoorHeight = 190.0;
			double Roof = 45.0;
		};

		/**
		 * Paredes y techo de un búnker con la puerta en -X (ejes del búnker, origen en el suelo del centro). Las comparten
		 * la malla y la colisión del búnker de la carrera (ATN_BeachBunker) y la formación Bunker del ProcMap.
		 */
		inline TArray<FBunkerBox> BunkerBoxes(const FBunkerDims& D)
		{
			const double IX = D.Interior.X;
			const double IY = D.Interior.Y;
			const double H = D.Interior.Z;
			const double W = D.Wall;
			const double DoorHalf = FMath::Min(D.DoorHalf, IY - 20.0);
			const double SideHalf = FMath::Max(1.0, 0.5 * (IY - DoorHalf));
			return {
				{ FVector(0.0, IY + W * 0.5, H * 0.5), FVector(IX + W, W * 0.5, H * 0.5) },
				{ FVector(0.0, -IY - W * 0.5, H * 0.5), FVector(IX + W, W * 0.5, H * 0.5) },
				{ FVector(IX + W * 0.5, 0.0, H * 0.5), FVector(W * 0.5, IY, H * 0.5) },
				{ FVector(-IX - W * 0.5, DoorHalf + SideHalf, H * 0.5), FVector(W * 0.5, SideHalf, H * 0.5) },
				{ FVector(-IX - W * 0.5, -DoorHalf - SideHalf, H * 0.5), FVector(W * 0.5, SideHalf, H * 0.5) },
				{ FVector(-IX - W * 0.5, 0.0, 0.5 * (D.DoorHeight + H)), FVector(W * 0.5, DoorHalf, 0.5 * (H - D.DoorHeight)) },
				{ FVector(0.0, 0.0, H + D.Roof * 0.5), FVector(IX + W + 30.0, IY + W + 30.0, D.Roof * 0.5) },
			};
		}

		/** Una tortuga (radio de cápsula Radius, alto HalfHeight * 2) cabe por la puerta y dentro de pie. */
		inline bool TurtleFitsDoor(const FBunkerDims& D, double Radius, double HalfHeight)
		{
			return FMath::Min(D.DoorHalf, D.Interior.Y - 20.0) >= Radius + 15.0 && D.DoorHeight >= 2.0 * HalfHeight + 10.0
				&& D.Interior.Z >= D.DoorHeight;
		}

		/** Medidas del búnker de una formación del ProcMap de radio R y alto H (interior dentro de la planta). */
		inline FBunkerDims FormationBunker(double R, double H)
		{
			FBunkerDims D;
			D.Interior = FVector(FMath::Max(200.0, R * 0.7), FMath::Max(150.0, R * 0.55), FMath::Max(200.0, H - 45.0));
			return D;
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// #690 Basura y trinchera
	// ─────────────────────────────────────────────────────────────────────────

	namespace TrashPile
	{
		/** Velocidad (cm/s) contra el montón desde la que se tropieza (correr, no andar). */
		constexpr float TripSpeed = 520.f;

		inline bool Trips(float SpeedToward, bool bBroken, float Threshold = TripSpeed)
		{
			return !bBroken && SpeedToward >= Threshold;
		}

		/** Un golpe lo rompe: un objeto lanzado o una bola de caparazón deprisa. Una tortuga andando, no. */
		inline bool BreaksFrom(bool bThrownObject, bool bShellBall, float ShellSpeed, float MinShellSpeed = 900.f)
		{
			return bThrownObject || (bShellBall && ShellSpeed >= MinShellSpeed);
		}
	}

	namespace Trench
	{
		/**
		 * Velocidad de salto (cm/s) que se deja dentro de la fosa para no salir de un salto: el vértice se queda en
		 * HeightFraction del alto de la pared. GravityZ en valor absoluto (cm/s²).
		 */
		inline float MaxJumpSpeedInPit(float WallHeight, float GravityZ, float HeightFraction = 0.55f)
		{
			return FMath::Sqrt(FMath::Max(0.f, 2.f * FMath::Abs(GravityZ) * WallHeight * HeightFraction));
		}

		/** La rampa de salida se sube andando (pendiente por debajo de MaxAngleDeg). */
		inline bool IsRampWalkable(float Height, float RampLength, float MaxAngleDeg = 40.f)
		{
			return RampLength > 0.f && FMath::RadiansToDegrees(FMath::Atan2(Height, RampLength)) <= MaxAngleDeg;
		}

		/** Pies dentro del hoyo (ejes de la trinchera, origen en el fondo del centro): en planta dentro y por debajo del borde. */
		inline bool IsInPit(const FVector& LocalFeet, const FVector2D& PitHalf, float RimHeight)
		{
			return FMath::Abs(LocalFeet.X) <= PitHalf.X && FMath::Abs(LocalFeet.Y) <= PitHalf.Y && LocalFeet.Z < RimHeight - 20.f;
		}
	}
}
