#pragma once

#include "CoreMinimal.h"
#include "Serialization/Archive.h"

/**
 * Lógica pura del panzazo (doble salto físico, plan maestro §3.5; Docs/Analisis/2026-09-29/G_doble_salto_fisico.md §2).
 * Sin mundo ni red: la usan UTN_TurtleMovementComponent y ATortugaCharacter dentro del movimiento, igual en el servidor y
 * en el cliente dueño (también al repetir movimientos), y la recorren las pruebas Tortunabo.Dive.*.
 *
 * - Pendiente (E9-01, #62): desde BellySlopeMinAngle, cuesta abajo, menos rozamiento y menos freno; el tiempo del arrastre
 *   no corre en bajada.
 * - Rebote en vuelo (E9-02, #63): contra una pared (normal con Z < 0,35), la velocidad horizontal contra ella vuelve con
 *   la restitución y la de a lo largo se queda con una parte. Velocidad relativa a lo que se toca. El del arrastre en el
 *   suelo usa la misma cuenta con sus ajustes de siempre.
 * - Inicio del panzazo (E9-04, #24): la petición viaja en el movimiento guardado (marca FLAG_Custom_2 y giro en 16 bits);
 *   el servidor y el dueño deciden con las mismas reglas si empieza y con qué velocidad, en ese mismo movimiento.
 */
namespace TNDiveLogic
{
	// ─────────────────────────────────────────────────────────────────────────
	// Arrastre en pendiente (#62)
	// ─────────────────────────────────────────────────────────────────────────

	/** Ajustes de la pendiente (UTN_TurtleMovementComponent, Belly Slide|Slope). */
	struct FBellySlopeParams
	{
		/** Multiplica la gravedad a lo largo de la cuesta (BellySlopeGravity por TN.Dive.Slope). */
		float SlopeGravity = 1.15f;
		/** Desde esta inclinación del suelo (grados) se sigue cayendo; 90 o más lo apaga (como antes). */
		float MinAngleDeg = 12.f;
		/** Rozamiento y freno por velocidad en bajada, multiplicados. */
		float FrictionScale = 0.3f;
		float DragScale = 0.4f;
		/** Tope de la velocidad arrastrándose (cm/s). */
		float MaxSpeed = 1000.f;
	};

	/** El suelo con normal N (unitaria) está inclinado MinAngleDeg o más: cuesta en la que se sigue cayendo. */
	inline bool IsFallSlope(const FVector& N, float MinAngleDeg)
	{
		if (MinAngleDeg >= 90.f)
		{
			return false;
		}
		return N.Z <= static_cast<double>(FMath::Cos(FMath::DegreesToRadians(FMath::Max(0.f, MinAngleDeg))));
	}

	/**
	 * Gravedad a lo largo del suelo, en horizontal (la velocidad andando es horizontal y el suelo la inclina al moverse). Con
	 * la normal N, la componente horizontal de g·senθ por el suelo es g·(Nx, Ny)·Nz, hacia abajo de la cuesta.
	 */
	inline FVector SlopeAcceleration(const FVector& N, float Gravity, float SlopeGravity)
	{
		return FVector(N.X, N.Y, 0.0) * static_cast<double>(Gravity * static_cast<float>(N.Z) * SlopeGravity);
	}

	/** Cuesta abajo: cuesta de MinAngleDeg o más y la velocidad horizontal V no sube por ella (parada también cuenta). */
	inline bool IsGoingDownhill(const FVector& N, const FVector& V, float MinAngleDeg)
	{
		return IsFallSlope(N, MinAngleDeg) && (N.X * V.X + N.Y * V.Y) >= 0.0;
	}

	/** El tiempo del arrastre (rampa de rozamiento y tope de BellyMaxSeconds) corre en llano y en subida, no en bajada. */
	inline bool ShouldAdvanceBellyTimer(const FVector& N, const FVector& V, float MinAngleDeg)
	{
		return !IsGoingDownhill(N, V, MinAngleDeg);
	}

	/** Lo que necesita un paso del arrastre sobre la tripa. */
	struct FBellyStepInput
	{
		/** Normal del suelo (unitaria; arriba si no hay suelo caminable). */
		FVector Normal = FVector::UpVector;
		/** Gravedad (cm/s², positiva). */
		float Gravity = 980.f;
		/** Rozamiento de la superficie (cm/s²), ya con la rampa del tiempo y TN.Dive.Friction. */
		float Friction = 800.f;
		/** Freno proporcional a la velocidad (1/s). */
		float Drag = 1.5f;
		FBellySlopeParams Slope;
	};

	/**
	 * Velocidad horizontal tras DeltaTime arrastrándose desde V0, en pasos de 1/60 s como mucho (igual a cualquier ritmo de
	 * fotogramas y al repetir movimientos). Sin la entrada del jugador: sobre la tripa no se dirige. Rozamiento seco con el
	 * peso que apoya (menos en cuesta) más freno por velocidad; cuesta abajo, los dos multiplicados por los de la pendiente.
	 * OutFriction (opcional): el rozamiento del último paso (cm/s², para TN.Dive.Debug).
	 */
	inline FVector IntegrateBellyVelocity(const FVector& V0, const FBellyStepInput& In, float DeltaTime, float* OutFriction = nullptr)
	{
		const FVector SlopeAccel = SlopeAcceleration(In.Normal, In.Gravity, In.Slope.SlopeGravity);
		const float NormalShare = FMath::Clamp(static_cast<float>(In.Normal.Z), 0.2f, 1.f);
		const float Friction = FMath::Max(0.f, In.Friction);
		const float Drag = FMath::Max(0.f, In.Drag);
		FVector V(V0.X, V0.Y, 0.0);
		float LastFriction = Friction * NormalShare;
		float Remaining = DeltaTime;
		constexpr float MaxStep = 1.f / 60.f;
		while (Remaining > UE_KINDA_SMALL_NUMBER)
		{
			const float H = FMath::Min(Remaining, MaxStep);
			Remaining -= H;
			const bool bDownhill = IsGoingDownhill(In.Normal, V, In.Slope.MinAngleDeg);
			const float StepFriction = Friction * NormalShare * (bDownhill ? In.Slope.FrictionScale : 1.f);
			const float StepDrag = Drag * (bDownhill ? In.Slope.DragScale : 1.f);
			LastFriction = StepFriction;
			V += SlopeAccel * static_cast<double>(H);
			const float Speed = static_cast<float>(V.Size());
			if (Speed <= UE_KINDA_SMALL_NUMBER)
			{
				V = FVector::ZeroVector;
				continue;
			}
			// En una cuesta suave el rozamiento puede más y se queda quieta.
			const float Loss = (StepFriction + StepDrag * Speed) * H;
			V = Loss >= Speed ? FVector::ZeroVector : V * static_cast<double>((Speed - Loss) / Speed);
		}
		if (OutFriction)
		{
			*OutFriction = LastFriction;
		}
		return V.GetClampedToMaxSize(static_cast<double>(In.Slope.MaxSpeed));
	}

	/**
	 * Velocidad a la que tiende cuesta abajo (cm/s): la gravedad a lo largo de la cuesta menos el rozamiento, entre el freno
	 * por velocidad. 0 si no es cuesta de caer o el rozamiento puede más. Por debajo de BellyStopSpeed no sigue cayendo de
	 * verdad (se arrastraría a paso de tortuga hasta el tope): se levanta como en llano.
	 */
	inline float DownhillTerminalSpeed(const FBellyStepInput& In)
	{
		if (!IsFallSlope(In.Normal, In.Slope.MinAngleDeg))
		{
			return 0.f;
		}
		const float Pull = static_cast<float>(SlopeAcceleration(In.Normal, In.Gravity, In.Slope.SlopeGravity).Size());
		const float NormalShare = FMath::Clamp(static_cast<float>(In.Normal.Z), 0.2f, 1.f);
		const float Net = Pull - FMath::Max(0.f, In.Friction) * NormalShare * In.Slope.FrictionScale;
		const float Drag = FMath::Max(0.f, In.Drag) * In.Slope.DragScale;
		if (Net <= 0.f)
		{
			return 0.f;
		}
		return Drag > UE_KINDA_SMALL_NUMBER ? FMath::Min(Net / Drag, In.Slope.MaxSpeed) : In.Slope.MaxSpeed;
	}

	/**
	 * Velocidad horizontal con que se arrastra al tocar el suelo de normal FloorNormal (cero: sin suelo): se quita lo que iba
	 * contra el suelo (el golpe), queda Keep de lo demás y como mucho Cap. En una bajada de MinAngleDeg o más, la caída que
	 * se convierte en arrastre cuenta entera (módulo 3D, no solo su parte horizontal), con tope DownhillCap.
	 */
	inline FVector LandingSlideVelocity(const FVector& V, const FVector& FloorNormal, float Keep, float Cap, float DownhillCap, float MinAngleDeg)
	{
		FVector Along = V;
		bool bDownhill = false;
		if (!FloorNormal.IsNearlyZero())
		{
			const FVector N = FloorNormal.GetSafeNormal();
			const double Into = FVector::DotProduct(Along, N);
			if (Into < 0.0)
			{
				Along -= N * Into;
			}
			bDownhill = IsFallSlope(N, MinAngleDeg) && Along.Z < 0.0;
		}
		const FVector Flat(Along.X, Along.Y, 0.0);
		const double Speed = bDownhill ? Along.Size() : Flat.Size();
		const double NewSpeed = FMath::Min(Speed * static_cast<double>(Keep), static_cast<double>(bDownhill ? DownhillCap : Cap));
		return Flat.GetSafeNormal() * NewSpeed;
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Rebote en vuelo (#63)
	// ─────────────────────────────────────────────────────────────────────────

	/** Ajustes del rebote en el vuelo del panzazo (UTN_TurtleMovementComponent, Dive|Wall). */
	struct FDiveWallParams
	{
		/** Pared: normal con Z por debajo de esto (más es suelo o pendiente: nada). */
		float MaxNormalZ = 0.35f;
		/** Velocidad contra la pared (relativa, cm/s) desde la que rebota; por debajo resbala como siempre. */
		float MinSpeed = 120.f;
		/** Lo que vuelve de la velocidad contra la pared. */
		float Restitution = 0.45f;
		/** Lo que conserva de la velocidad horizontal a lo largo de la pared. */
		float TangentKeep = 0.6f;
	};

	enum class EDiveImpact : uint8
	{
		/** Suelo, pendiente, techo, roce lento u objeto que se aleja: el movimiento de siempre. */
		None,
		Bounce,
	};

	/** Normal horizontal (unitaria) de una pared con normal HitNormal; cero si no es pared (suelo, pendiente o techo). */
	inline FVector WallNormal(const FVector& HitNormal, float MaxNormalZ)
	{
		if (HitNormal.Z >= static_cast<double>(MaxNormalZ))
		{
			return FVector::ZeroVector;
		}
		const FVector Flat(HitNormal.X, HitNormal.Y, 0.0);
		// Casi horizontal (un techo o el canto de abajo de algo): no es pared.
		if (Flat.SizeSquared() < 0.25)
		{
			return FVector::ZeroVector;
		}
		return Flat.GetSafeNormal();
	}

	/** Velocidad horizontal (cm/s) con que V va contra la pared de normal WallN, relativa a lo que se toca (OtherV). */
	inline float WallImpactSpeed(const FVector& V, const FVector& OtherV, const FVector& WallN)
	{
		const FVector Rel = V - OtherV;
		return static_cast<float>(-(Rel.X * WallN.X + Rel.Y * WallN.Y));
	}

	/** Qué pasa al chocar en el vuelo del panzazo con algo de normal HitNormal yendo a V (lo tocado va a OtherV). */
	inline EDiveImpact ClassifyDiveImpact(const FVector& HitNormal, const FVector& V, const FVector& OtherV, const FDiveWallParams& P)
	{
		const FVector WallN = WallNormal(HitNormal, P.MaxNormalZ);
		if (WallN.IsZero())
		{
			return EDiveImpact::None;
		}
		return WallImpactSpeed(V, OtherV, WallN) >= P.MinSpeed ? EDiveImpact::Bounce : EDiveImpact::None;
	}

	/**
	 * Rebote contra la pared de normal horizontal WallN: de la velocidad horizontal relativa, lo que iba contra la pared
	 * vuelve con Restitution y lo de a lo largo se queda con TangentKeep. La vertical no cambia (sigue la caída).
	 */
	inline FVector ReflectDiveVelocity(const FVector& V, const FVector& OtherV, const FVector& WallN, const FDiveWallParams& P)
	{
		const FVector Rel(V.X - OtherV.X, V.Y - OtherV.Y, 0.0);
		const double Into = FVector::DotProduct(Rel, WallN);
		const FVector Tangent = Rel - WallN * Into;
		const FVector Out = Tangent * static_cast<double>(P.TangentKeep) - WallN * (Into * static_cast<double>(P.Restitution));
		return FVector(Out.X + OtherV.X, Out.Y + OtherV.Y, V.Z);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Inicio del panzazo (#24)
	// ─────────────────────────────────────────────────────────────────────────

	/** Marca del movimiento guardado que pide el panzazo: FSavedMove_Character::FLAG_Custom_2 (comprobado en el .cpp). */
	constexpr uint8 DiveRequestFlag = 0x40;

	/** Ajustes de la velocidad del panzazo (ATortugaCharacter, Dive y Dive|Momentum). */
	struct FDiveMomentumParams
	{
		float BaseSpeed = 420.f;
		float ForwardFactor = 1.f;
		float LateralFactor = 0.f;
		float BackwardFactor = -0.5f;
		float MaxTotalSpeed = 1500.f;
	};

	/**
	 * Velocidad hacia DiveDir: la base más la inercia del salto según cómo se alinee la dirección del panzazo con la del salto
	 * (de frente, ForwardFactor; de lado, LateralFactor; hacia atrás, BackwardFactor, que puede restar). Con tope simétrico:
	 * negativa, el panzazo va hacia atrás.
	 */
	inline float DiveForwardSpeed(const FVector& DiveDir, const FVector& JumpStartHorizontalVelocity, const FDiveMomentumParams& P)
	{
		float MomentumBonus = 0.f;
		const FVector JumpFlat(JumpStartHorizontalVelocity.X, JumpStartHorizontalVelocity.Y, 0.0);
		const float JumpStartSpeed = static_cast<float>(JumpFlat.Size());
		if (JumpStartSpeed > 1.f)
		{
			const FVector JumpStartDir = JumpFlat / static_cast<double>(JumpStartSpeed);
			const float Alignment = static_cast<float>(FVector::DotProduct(DiveDir, JumpStartDir));
			const float Factor = Alignment >= 0.f
				? FMath::Lerp(P.LateralFactor, P.ForwardFactor, Alignment)
				: FMath::Lerp(P.LateralFactor, P.BackwardFactor, -Alignment);
			MomentumBonus = JumpStartSpeed * Factor;
		}
		return FMath::Clamp(P.BaseSpeed + MomentumBonus, -P.MaxTotalSpeed, P.MaxTotalSpeed);
	}

	/** Velocidad con que sale el panzazo: ForwardSpeed hacia DiveDir y DownwardSpeed hacia abajo. */
	inline FVector DiveLaunchVelocity(const FVector& DiveDir, float ForwardSpeed, float DownwardSpeed)
	{
		return DiveDir * static_cast<double>(ForwardSpeed) + FVector(0.0, 0.0, -static_cast<double>(DownwardSpeed));
	}

	/** Dirección del panzazo tal como viaja por red: el giro en 16 bits. */
	inline uint16 CompressDiveYaw(const FVector& DiveDir)
	{
		const FVector Flat(DiveDir.X, DiveDir.Y, 0.0);
		return FRotator::CompressAxisToShort(Flat.IsNearlyZero() ? 0.f : static_cast<float>(Flat.Rotation().Yaw));
	}

	/** Dirección horizontal unitaria del giro comprimido (la misma en el servidor y en el dueño). */
	inline FVector DiveDirFromYaw(uint16 CompressedYaw)
	{
		return FRotator(0.f, FRotator::DecompressAxisFromShort(CompressedYaw), 0.f).Vector();
	}

	/** Datos del movimiento: con la marca del panzazo, el giro (16 bits); sin ella, nada. */
	inline void SerializeDiveRequest(FArchive& Ar, uint8 CompressedFlags, uint16& CompressedYaw)
	{
		if ((CompressedFlags & DiveRequestFlag) != 0)
		{
			Ar << CompressedYaw;
		}
		else
		{
			CompressedYaw = 0;
		}
	}

	/** Por qué empieza o no un panzazo pedido. */
	enum class EDiveStart : uint8
	{
		Accept,
		/** Derribada, muerta, en el caparazón o en brazos de otra. */
		Blocked,
		/** Ya está en un panzazo. */
		AlreadyDiving,
		/** El panzazo es el segundo salto: solo en el aire (en el suelo, nadando o sin movimiento, no). */
		NotInAir,
		/** Otro lanzamiento (el del servidor, un trampolín) manda en este paso. */
		LaunchPending,
		/** La inercia hacia atrás la deja casi sin velocidad: no sería un panzazo. */
		TooSlow,
	};

	/** Lo que decide si un panzazo pedido empieza en este paso del movimiento. */
	struct FDiveStartContext
	{
		bool bBlocked = false;
		bool bAlreadyDiving = false;
		bool bInAir = false;
		bool bLaunchPending = false;
		/** Velocidad hacia la dirección del panzazo (DiveForwardSpeed). */
		float ForwardSpeed = 0.f;
		/** Por debajo de esto (en valor absoluto) no empieza (DiveStopSpeedThreshold: lo que ya lo acabaría en el aire). */
		float MinSpeed = 0.f;
	};

	/** Las mismas reglas en el servidor y en el dueño, dentro del movimiento que lleva la petición. */
	inline EDiveStart DecideDiveStart(const FDiveStartContext& C)
	{
		if (C.bBlocked)
		{
			return EDiveStart::Blocked;
		}
		if (C.bAlreadyDiving)
		{
			return EDiveStart::AlreadyDiving;
		}
		if (!C.bInAir)
		{
			return EDiveStart::NotInAir;
		}
		if (C.bLaunchPending)
		{
			return EDiveStart::LaunchPending;
		}
		if (FMath::Abs(C.ForwardSpeed) < C.MinSpeed)
		{
			return EDiveStart::TooSlow;
		}
		return EDiveStart::Accept;
	}

	inline const TCHAR* DiveStartName(EDiveStart Result)
	{
		switch (Result)
		{
		case EDiveStart::Accept: return TEXT("empieza");
		case EDiveStart::Blocked: return TEXT("bloqueada");
		case EDiveStart::AlreadyDiving: return TEXT("ya en un panzazo");
		case EDiveStart::NotInAir: return TEXT("no está en el aire");
		case EDiveStart::LaunchPending: return TEXT("otro lanzamiento en este paso");
		case EDiveStart::TooSlow: return TEXT("sin velocidad");
		default: return TEXT("?");
		}
	}

	/** Número del panzazo siguiente (1-255; al dar la vuelta se salta el 0, que significa «ninguno»). */
	inline uint8 NextDiveSerial(uint8 Serial)
	{
		return Serial >= 255 ? static_cast<uint8>(1) : static_cast<uint8>(Serial + 1);
	}
}
