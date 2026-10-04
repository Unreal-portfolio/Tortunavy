#pragma once

#include "CoreMinimal.h"

/**
 * Lógica pura del estado de caparazón: decide SI se puede entrar o salir, nunca
 * lo aplica. Sin UWorld, sin UObject, sin estado — todo entra por parámetro.
 *
 * UTN_ShellComponent delega en estas funciones para que los tests de
 * Tortunabo.Shell cubran el código real y no una copia paralela de las reglas.
 */
namespace TNShellLogic
{
	/** Entradas de estado del personaje que condicionan meterse en el caparazón. */
	struct FShellEnterContext
	{
		/**
		 * Nadando no se entra: el caparazón con física se hundiría y saldría en el acto. En el aire sí se entra (la
		 * bolita en pleno salto, que sigue volando como cuerpo físico con la velocidad que llevaba).
		 */
		bool bIsSwimming = false;

		bool bIsDead = false;
		bool bIsKnockedDown = false;
		bool bIsDiving = false;

		/** Con un objeto en la mano no se entra: las manos están ocupadas. */
		bool bHasEquippedItem = false;
	};

	/**
	 * @brief Decide si el personaje puede meterse en el caparazón.
	 * @note Se evalúa en el servidor; el cliente solo pide el cambio.
	 */
	inline bool CanEnterShell(const FShellEnterContext& Context)
	{
		if (Context.bIsDead || Context.bIsKnockedDown || Context.bIsDiving)
		{
			return false;
		}

		if (Context.bIsSwimming || Context.bHasEquippedItem)
		{
			return false;
		}

		return true;
	}

	/**
	 * @brief Decide si el personaje puede salir del caparazón.
	 * @param TimeInShellSeconds Segundos transcurridos desde que entró.
	 * @param MinTimeInShellSeconds Permanencia mínima antes de poder salir.
	 * @note El mínimo evita que machacar la tecla genere un tren de RPCs y
	 *       parpadeo visual en el resto de máquinas.
	 */
	inline bool CanExitShell(float TimeInShellSeconds, float MinTimeInShellSeconds)
	{
		return TimeInShellSeconds >= MinTimeInShellSeconds;
	}

	/**
	 * @brief Si la pose de la malla solo se anima con los movimientos que manda su cliente
	 *        (USkeletalMeshComponent::bOnlyAllowAutonomousTickPose).
	 * @param bServerOfRemotePlayer En el servidor, la tortuga de un jugador remoto: el motor la deja así al
	 *        poseerla (ACharacter::PossessedBy) para que la animación vaya al ritmo de sus movimientos.
	 * @param bDrivenByShellBody La mueve su caja física: su movimiento está apagado aquí y en su cliente, que
	 *        deja de mandar movimientos. Sin animarse cada fotograma, el anfitrión la veía congelada con la pose
	 *        de antes de meterse, con las patas fuera de la bola (#246).
	 */
	inline bool OnlyTickPoseFromClientMoves(bool bServerOfRemotePlayer, bool bDrivenByShellBody)
	{
		return bServerOfRemotePlayer && !bDrivenByShellBody;
	}

	/** Reglas de la recolocación de una caja que ha cruzado la malla fina del terreno (servidor, ATN_ShellBody). */
	struct FSunkRescueRules
	{
		/** cm de la parte de abajo de la caja bajo el terreno de verdad (la misma medida que el instrumento). */
		float Depth = 25.f;
		/** Muestras seguidas (cada 0,1 s) por debajo para recolocarla: un paso de la física no cuenta. */
		int32 Samples = 2;
		/** Subida máxima (cm): más que esto no es «encima de donde estaba», y se deja al rescate de la carrera. */
		float MaxLift = 400.f;
		/** Giro máximo (rad/s) con el que sale de la recolocación. */
		float MaxSpinAfter = 6.f;
	};

	/**
	 * @brief Cuenta una muestra de hondura de la caja y decide si hay que recolocarla encima del terreno.
	 * @param InOutStrikes Muestras seguidas por debajo (vuelve a 0 al decidir o al salir).
	 */
	inline bool ShouldRescueSunkenBody(float BottomDepthUnderTerrain, int32& InOutStrikes, const FSunkRescueRules& Rules = FSunkRescueRules())
	{
		InOutStrikes = BottomDepthUnderTerrain > Rules.Depth ? InOutStrikes + 1 : 0;
		if (InOutStrikes >= Rules.Samples)
		{
			InOutStrikes = 0;
			return true;
		}
		return false;
	}

	/** true si subir la caja Lift cm es una recolocación en el sitio (hacia arriba y no un teletransporte grande). */
	inline bool IsSunkLiftAllowed(float Lift, const FSunkRescueRules& Rules = FSunkRescueRules())
	{
		return Lift > 0.f && Lift <= Rules.MaxLift;
	}

	/** Velocidad de la caja al recolocarla: sin bajar y con el giro limitado. */
	inline void SettleRescuedVelocity(FVector& InOutLinear, FVector& InOutAngular, const FSunkRescueRules& Rules = FSunkRescueRules())
	{
		InOutLinear.Z = FMath::Max(InOutLinear.Z, 0.0);
		InOutAngular = InOutAngular.GetClampedToMaxSize(Rules.MaxSpinAfter);
	}

	/** Del centro al punto más bajo (cm) de una caja de semiejes Extent con el giro Rotation (de pie mide más que tumbada). */
	inline double BoxHalfHeight(const FQuat& Rotation, const FVector& Extent)
	{
		return FMath::Abs(Rotation.GetAxisX().Z) * Extent.X + FMath::Abs(Rotation.GetAxisY().Z) * Extent.Y
			+ FMath::Abs(Rotation.GetAxisZ().Z) * Extent.Z;
	}

	/** Reglas para crear la caja encima del terreno (UTN_ShellComponent::FindFreeBodySpot). */
	struct FSpawnClearRules
	{
		/** cm que queda su parte de abajo por encima del terreno de verdad cuando hay que subirla. */
		float Skin = 2.f;
		/** Subida máxima (cm), la de la recolocación: más que esto no es «encima de donde estaba». */
		float MaxLift = 400.f;
	};

	/** true si la parte de abajo de la caja no queda bajo el terreno de verdad (apoyada en la arena vale). */
	inline bool IsBottomAboveTerrain(float BottomDepthUnderTerrain)
	{
		return BottomDepthUnderTerrain <= 0.f;
	}

	/**
	 * @brief Cuánto subir la caja al crearla para que su parte de abajo quede Skin por encima del terreno de verdad.
	 *
	 * La malla del terreno de la playa es fina: una caja que nace con la parte de abajo dentro (o entera por debajo, que ni
	 * la solapa) sale escupida al primer contacto (#54; 9-15 m/s en el monkey del 2026-09-29).
	 *
	 * @param BottomDepthUnderTerrain cm de su parte de abajo bajo el terreno (negativo si está encima).
	 * @return 0 si ya está encima; -1 si haría falta subirla más de MaxLift (hay que buscar otro sitio).
	 */
	inline float SpawnLiftAboveTerrain(float BottomDepthUnderTerrain, const FSpawnClearRules& Rules = FSpawnClearRules())
	{
		if (IsBottomAboveTerrain(BottomDepthUnderTerrain))
		{
			return 0.f;
		}
		const float Lift = BottomDepthUnderTerrain + Rules.Skin;
		return Lift <= Rules.MaxLift ? Lift : -1.f;
	}

	/** Tope a lo que el terreno empuja la caja al sacarla de dentro (ATN_ShellBody, servidor). */
	struct FTerrainPushOutRules
	{
		/** cm dentro del terreno de verdad, al acabar el paso anterior, para contarla como metida en él. */
		float EmbeddedDepth = 10.f;
		/** cm/s que puede añadir la depenetración además de parar la caída: lo mismo que al nacer metida en algo. */
		float MaxPushOutSpeed = 300.f;
	};

	/**
	 * @brief Limita la velocidad con que el terreno escupe la caja que se le había metido dentro.
	 *
	 * El tope de depenetración del motor (SetMaxDepenetrationVelocity) solo vale para lo que solapa al nacer o al
	 * teletransportarla. Si la caja se mete en la malla fina del terreno, el contacto la saca con todo su impulso: 9-15 m/s
	 * en el monkey del 2026-09-29 (impulsos de 21 000-76 000) y más de 100 m/s junto a una fortaleza (432 690). Parar la caída
	 * vale; de lo demás que haya cambiado en el paso, si la acelera, se deja como mucho MaxPushOutSpeed. Lo que la frena no
	 * se toca.
	 *
	 * @param PrevVelocity    Velocidad al acabar el paso anterior.
	 * @param PrevDepthInTerrain cm que estaba dentro del terreno al acabar el paso anterior (negativo si estaba encima).
	 * @param InOutVelocity   Velocidad tras el paso; se limita.
	 * @return true si la ha limitado.
	 */
	inline bool LimitTerrainPushOut(const FVector& PrevVelocity, float PrevDepthInTerrain, FVector& InOutVelocity,
		const FTerrainPushOutRules& Rules = FTerrainPushOutRules())
	{
		if (PrevDepthInTerrain <= Rules.EmbeddedDepth)
		{
			return false;
		}
		FVector Allowed = PrevVelocity;
		Allowed.Z = FMath::Max(Allowed.Z, 0.0);
		if (InOutVelocity.Size() <= Allowed.Size() + Rules.MaxPushOutSpeed)
		{
			return false;
		}
		InOutVelocity = Allowed + (InOutVelocity - Allowed).GetClampedToMaxSize(Rules.MaxPushOutSpeed);
		return true;
	}

	/**
	 * @brief Empuje propio de un bloque sólido de enemigo (cangrejo gigante, tanque) sobre la bola del caparazón.
	 *
	 * Los bloques no chocan con la caja en la física (ignoran PhysicsBody): movidos sin barrido, se metían en la bola y la
	 * aplastaban contra la malla fina del terreno (torbellino y bola bajo el mapa); en los clientes, con su posición
	 * extrapolada, empujaban una bola que es del servidor. En su lugar, el servidor saca la bola por el lado horizontal más
	 * cercano del bloque y nunca hacia abajo.
	 *
	 * @param LocalBall     Centro de la bola en el espacio del bloque (sin escala).
	 * @param BlockExtent   Semiejes del bloque (con escala).
	 * @param BallRadius    Radio con el que cuenta la bola.
	 * @param PushSpeed     Velocidad mínima de salida (cm/s) hacia fuera.
	 * @param InOutLocalVelocity Velocidad de la bola en el espacio del bloque.
	 * @return true si la bola tocaba el bloque (y se ha cambiado su velocidad).
	 */
	inline bool PushBallOutOfBlock(const FVector& LocalBall, const FVector& BlockExtent, float BallRadius, float PushSpeed, FVector& InOutLocalVelocity)
	{
		const double ReachX = BlockExtent.X + BallRadius;
		const double ReachY = BlockExtent.Y + BallRadius;
		const double ReachZ = BlockExtent.Z + BallRadius;
		if (FMath::Abs(LocalBall.X) >= ReachX || FMath::Abs(LocalBall.Y) >= ReachY || FMath::Abs(LocalBall.Z) >= ReachZ)
		{
			return false;
		}
		const double SideX = LocalBall.X < 0.0 ? -1.0 : 1.0;
		const double SideY = LocalBall.Y < 0.0 ? -1.0 : 1.0;
		const FVector Out = (ReachX - FMath::Abs(LocalBall.X)) <= (ReachY - FMath::Abs(LocalBall.Y)) ? FVector(SideX, 0.0, 0.0) : FVector(0.0, SideY, 0.0);
		const double Along = FVector::DotProduct(InOutLocalVelocity, Out);
		if (Along < PushSpeed)
		{
			InOutLocalVelocity += Out * (PushSpeed - Along);
		}
		InOutLocalVelocity.Z = FMath::Max(InOutLocalVelocity.Z, 0.0);
		return true;
	}

	/** Movimiento anómalo de la bola del caparazón (instrumento TN.Shell.Debug de ATN_ShellBody). */
	enum class EShellMotionAnomaly : uint8
	{
		None,
		/** Giro sostenido cerca del tope de la caja (el «torbellino»). */
		Spin,
		/** La parte de abajo de la caja está bajo la superficie del terreno (ha cruzado la malla fina). */
		Sunk,
		/** Cambio brusco de velocidad en un solo paso, fuera de un lanzamiento recién hecho. */
		VelocityJump,
	};

	/** Umbrales del instrumento. El tope de giro de la caja es 900 °/s (15,7 rad/s). */
	struct FShellMotionThresholds
	{
		/** rad/s: unos 800 °/s, el 90 % del tope. Rodar deprisa por la arena se queda por debajo. */
		float SpinRadPerSecond = 14.f;
		/** Segundos seguidos por encima de SpinRadPerSecond para contarlo como torbellino (un rebote no cuenta). */
		float SpinSustainSeconds = 0.4f;
		/** cm de la parte de abajo de la caja bajo el terreno de verdad (tocar la arena no es estar dentro). */
		float SunkDepth = 25.f;
		/** cm/s de cambio de velocidad en un paso que no se explica por frenar un choque (UnexplainedVelocityChange). */
		float VelocityJump = 900.f;
		/** Segundos desde el nacimiento o el último InitBody en los que un salto de velocidad es el propio lanzamiento. */
		float LaunchGraceSeconds = 0.15f;
	};

	/** Una muestra del movimiento de la caja en un paso. */
	struct FShellMotionSample
	{
		float DeltaSeconds = 0.f;
		/** |ω| en rad/s. */
		float AngularSpeed = 0.f;
		/** Cambio de velocidad del paso que no se explica por frenar un choque (UnexplainedVelocityChange), en cm/s. */
		float VelocityChange = 0.f;
		/** cm de la parte de abajo de la caja bajo el terreno (negativo si está encima). */
		float BottomDepthUnderTerrain = 0.f;
		/** Segundos desde el nacimiento o el último InitBody de la caja. */
		float AgeSeconds = 0.f;
	};

	/**
	 * @brief Parte del cambio de velocidad de un paso que no se explica por un choque que frena la caja.
	 *
	 * Un contacto pasivo (aterrizar, chocar con una pared) quita la velocidad con la que la caja iba contra él y, como mucho,
	 * la hace rebotar un poco (rebote 0,2). Lo que queda del cambio, la velocidad con la que sale en la dirección del empujón,
	 * es lo que no explica ningún choque: la depenetración que la escupe o un empujón del juego sin registrar. Una bola lanzada
	 * que aterriza a 11 m/s y 9 m/s hacia abajo cambia 10-14 m/s en un paso y aquí da menos de 1,5 m/s (#54, monkey del 2026-10-03).
	 *
	 * @param PrevVelocity Velocidad al acabar el paso anterior.
	 * @param Velocity     Velocidad al acabar este paso.
	 * @return cm/s; 0 si el paso solo la frena.
	 */
	inline float UnexplainedVelocityChange(const FVector& PrevVelocity, const FVector& Velocity)
	{
		const FVector Change = Velocity - PrevVelocity;
		const double ChangeSize = Change.Size();
		if (ChangeSize <= UE_KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}
		const double Incoming = FMath::Max(0.0, -FVector::DotProduct(PrevVelocity, Change / ChangeSize));
		return static_cast<float>(FMath::Max(0.0, ChangeSize - Incoming));
	}

	/**
	 * @brief Clasifica una muestra del movimiento de la caja. Prioridad: hundida, salto de velocidad, torbellino.
	 * @param InOutSpinSeconds Acumulador de giro alto de esa caja (se pone a 0 en cuanto baja del umbral).
	 */
	inline EShellMotionAnomaly ClassifyShellMotion(const FShellMotionSample& Sample, float& InOutSpinSeconds,
		const FShellMotionThresholds& Thresholds = FShellMotionThresholds())
	{
		InOutSpinSeconds = Sample.AngularSpeed >= Thresholds.SpinRadPerSecond ? InOutSpinSeconds + Sample.DeltaSeconds : 0.f;
		if (Sample.BottomDepthUnderTerrain > Thresholds.SunkDepth)
		{
			return EShellMotionAnomaly::Sunk;
		}
		if (Sample.AgeSeconds >= Thresholds.LaunchGraceSeconds && Sample.VelocityChange > Thresholds.VelocityJump)
		{
			return EShellMotionAnomaly::VelocityJump;
		}
		if (InOutSpinSeconds >= Thresholds.SpinSustainSeconds)
		{
			return EShellMotionAnomaly::Spin;
		}
		return EShellMotionAnomaly::None;
	}
}
