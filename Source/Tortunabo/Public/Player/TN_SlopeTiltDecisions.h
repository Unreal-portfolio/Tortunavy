#pragma once

#include "CoreMinimal.h"

/**
 * Lógica pura de la inclinación visual de la tortuga con la pendiente (#586), sin mundo ni componentes: la usa
 * UTN_SlopeTiltComponent en producción y la prueban los tests Tortunabo.Player.SlopeTilt.*.
 *
 * Convención de la inclinación (FRotator con Yaw = 0, en los ejes de la cápsula, que solo gira en yaw):
 * - Pitch > 0: el morro sube (la cuesta sube por delante).
 * - Roll < 0: el lado derecho sube (la cuesta sube por la derecha); Roll > 0, sube el izquierdo.
 * Con FRotator(Pitch, 0, Roll), el eje Z de la tortuga queda exactamente sobre la normal del suelo (hasta el tope).
 */
namespace TNSlopeTilt
{
	/** Estado de la tortuga que decide si se inclina. */
	struct FTiltGate
	{
		bool bOnGround = false;
		bool bInShell = false;
		bool bCarried = false;
		bool bKnockedDown = false;
		bool bDead = false;
		bool bRagdoll = false;
		/** En la pausa del huevo (TNEggHatch::IsHatching): la eclosión pone su pose sobre la foto de la malla. */
		bool bHatching = false;
	};

	/**
	 * Estados en los que otro sistema manda en la malla (la bola la pone sobre el caparazón, el derribo y la muerte la
	 * giran o la sueltan a la física, la eclosión la anima): la inclinación se quita al momento, sin interpolar, para no
	 * torcer su pose.
	 */
	inline bool IsTakenOver(const FTiltGate& Gate)
	{
		return Gate.bInShell || Gate.bCarried || Gate.bKnockedDown || Gate.bDead || Gate.bRagdoll || Gate.bHatching;
	}

	/**
	 * Estados en los que otro sistema mueve la malla desde la foto que le hizo, inclinación incluida, y la devolverá al
	 * acabar (el ragdoll la suelta a la física; la eclosión del huevo pone su pose sobre la foto y la devuelve al lanzar):
	 * no se escribe nada en la malla, solo se recuerda la inclinación para retomarla cuando vuelva la foto.
	 */
	inline bool HoldsSnapshot(const FTiltGate& Gate)
	{
		return Gate.bRagdoll || Gate.bHatching;
	}

	/** Solo en el suelo y de pie o de tripa: en el aire, en la bola, llevada, derribada, muerta o en ragdoll, no. */
	inline bool ShouldTilt(const FTiltGate& Gate)
	{
		return Gate.bOnGround && !IsTakenOver(Gate);
	}

	/**
	 * Cabeceo y alabeo (grados) que dejan la tortuga paralela a un suelo de normal FloorNormal (mundo) mirando a YawDeg.
	 * La inclinación total se limita a MaxTiltDeg conservando la dirección de la cuesta; por debajo de MinTiltDeg se
	 * considera llano (0). Una normal nula o que no apunta hacia arriba da 0.
	 */
	inline FRotator ComputeTilt(const FVector& FloorNormal, float YawDeg, float MaxTiltDeg, float MinTiltDeg = 0.f)
	{
		const FVector WorldNormal = FloorNormal.GetSafeNormal();
		if (WorldNormal.Z <= UE_KINDA_SMALL_NUMBER)
		{
			return FRotator::ZeroRotator;
		}

		const FVector Local = FRotator(0.f, YawDeg, 0.f).UnrotateVector(WorldNormal);
		const double TiltDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Local.Z, -1.0, 1.0)));
		if (TiltDeg < FMath::Max(0.f, MinTiltDeg))
		{
			return FRotator::ZeroRotator;
		}

		FVector Clamped = Local;
		const double MaxDeg = FMath::Max(0.f, MaxTiltDeg);
		if (TiltDeg > MaxDeg)
		{
			const FVector2D Downhill = FVector2D(Local.X, Local.Y).GetSafeNormal();
			const double MaxRad = FMath::DegreesToRadians(MaxDeg);
			Clamped = FVector(Downhill.X * FMath::Sin(MaxRad), Downhill.Y * FMath::Sin(MaxRad), FMath::Cos(MaxRad));
		}

		const double Pitch = FMath::RadiansToDegrees(FMath::Atan2(-Clamped.X, Clamped.Z));
		const double Roll = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Clamped.Y, -1.0, 1.0)));
		return FRotator(Pitch, 0.0, Roll);
	}

	/** Velocidad por defecto de la interpolación exponencial hacia la inclinación del suelo (1/s). */
	constexpr float DefaultInterpSpeed = 8.f;

	/**
	 * Giro máximo por segundo por defecto (grados/s). La interpolación exponencial da el paso más grande en el primer
	 * fotograma: al aterrizar de un panzazo en una cuesta de 35° eran 9° de golpe a 30 fps. Con este tope, el primer paso
	 * es de 4° a 30 fps y de 2° a 60 fps.
	 */
	constexpr float DefaultMaxRateDegPerSec = 120.f;

	/**
	 * Un paso de la interpolación exponencial hacia Target (Speed en 1/s), con el giro del paso limitado a
	 * MaxRateDegPerSec · DeltaTime (0 = sin límite) sin cambiar su dirección; al quedar a menos de 0,05° se pega.
	 */
	inline FRotator StepTilt(const FRotator& Current, const FRotator& Target, float DeltaTime, float Speed, float MaxRateDegPerSec = 0.f)
	{
		constexpr float SnapDeg = 0.05f;
		FVector2D Step(
			FMath::FInterpTo(Current.Pitch, Target.Pitch, DeltaTime, Speed) - Current.Pitch,
			FMath::FInterpTo(Current.Roll, Target.Roll, DeltaTime, Speed) - Current.Roll);
		const double MaxStepDeg = static_cast<double>(MaxRateDegPerSec) * FMath::Max(0.f, DeltaTime);
		if (MaxRateDegPerSec > 0.f && Step.Size() > MaxStepDeg)
		{
			Step = Step.GetSafeNormal() * MaxStepDeg;
		}
		FRotator Next(Current.Pitch + Step.X, 0.f, Current.Roll + Step.Y);
		if (FMath::Abs(Next.Pitch - Target.Pitch) < SnapDeg && FMath::Abs(Next.Roll - Target.Roll) < SnapDeg)
		{
			Next = FRotator(Target.Pitch, 0.f, Target.Roll);
		}
		return Next;
	}

	/** Giro relativo de la malla: la inclinación (en los ejes de la cápsula) sobre la base que han dejado los demás sistemas. */
	inline FQuat ComposeTilt(const FQuat& BaseRelative, const FRotator& Tilt)
	{
		return FRotator(Tilt.Pitch, 0.f, Tilt.Roll).Quaternion() * BaseRelative;
	}

	/**
	 * Inclinación que otro sistema le quitó a la malla tras hacerle una foto (el derribo y la eclosión del huevo guardan el
	 * giro relativo inclinado y lo devuelven al acabar). Cuando la malla vuelve a tener exactamente ese giro, se retoma la
	 * inclinación desde ahí y se interpola hacia la del suelo, en vez de enderezarla de golpe o de tomar la pose inclinada
	 * como base (inclinación doble).
	 */
	struct FTiltResume
	{
		bool bPending = false;
		/** Giro relativo escrito (base + inclinación) cuando se perdió. */
		FRotator Relative = FRotator::ZeroRotator;
		FQuat Base = FQuat::Identity;
		FRotator Tilt = FRotator::ZeroRotator;

		/** Guarda la última inclinación escrita; si ya hay una pendiente se queda la primera (la de la foto). */
		void Remember(const FRotator& Written, const FQuat& InBase, const FRotator& InTilt)
		{
			if (bPending)
			{
				return;
			}
			Relative = Written;
			Base = InBase;
			Tilt = InTilt;
			bPending = true;
		}

		/** Si la malla ha vuelto al giro guardado, da la base y la inclinación desde las que seguir y la olvida. */
		bool TryResume(const FRotator& MeshRelative, float Tolerance, FQuat& OutBase, FRotator& OutTilt)
		{
			if (!bPending || !MeshRelative.Equals(Relative, Tolerance))
			{
				return false;
			}
			OutBase = Base;
			OutTilt = Tilt;
			bPending = false;
			return true;
		}

		void Forget() { bPending = false; }
	};

	/** Giro relativo igual al escrito (con margen por la conversión cuaternión ↔ rotador). */
	constexpr float SameRotationTolerance = 1.e-2f;

	/**
	 * Estado de la inclinación de una malla y su paso por fotograma, sin componentes: UTN_SlopeTiltComponent lo usa con la
	 * malla de la tortuga. Quien lo usa lee el giro relativo de la malla, escribe el que le devuelven (true + OutRelative)
	 * y, tras escribirlo, anota el que queda con NoteWritten.
	 */
	struct FTiltDriver
	{
		/** Inclinación aplicada ahora y hacia la que va (Pitch y Roll en grados, ejes de la cápsula). */
		FRotator CurrentTilt = FRotator::ZeroRotator;
		FRotator TargetTilt = FRotator::ZeroRotator;

		/** Giro relativo de la malla sin la inclinación (lo que han dejado los demás sistemas). */
		FQuat BaseRelative = FQuat::Identity;

		/** Giro relativo que escribimos la última vez: si la malla no lo tiene, otro sistema lo ha cambiado. */
		FRotator LastWrittenRelative = FRotator::ZeroRotator;

		/** Inclinación de la última escritura (para no volver a escribir la misma). */
		FRotator LastAppliedTilt = FRotator::ZeroRotator;

		/** Hay inclinación aplicada sobre la malla (BaseRelative y LastWrittenRelative valen). */
		bool bTiltApplied = false;

		/** Inclinación perdida por el ragdoll, la eclosión u otro sistema: se retoma si la malla vuelve a su giro. */
		FTiltResume Resume;

		/** Otro sistema mueve la malla desde su foto (HoldsSnapshot): se recuerda la inclinación y la malla no se toca. */
		void Suspend()
		{
			RememberTilt();
			bTiltApplied = false;
			CurrentTilt = TargetTilt = FRotator::ZeroRotator;
		}

		/**
		 * Quita la inclinación al momento. true si la malla sigue con lo que escribimos: hay que devolverla a su base
		 * (OutRelative); si otro sistema ya la ha colocado, se deja como está.
		 */
		bool Drop(const FRotator& MeshRelative, FQuat& OutRelative)
		{
			RememberTilt();
			const bool bWrite = bTiltApplied && MeshRelative.Equals(LastWrittenRelative, SameRotationTolerance);
			OutRelative = BaseRelative;
			bTiltApplied = false;
			CurrentTilt = TargetTilt = FRotator::ZeroRotator;
			return bWrite;
		}

		/**
		 * Un fotograma con la malla libre, hacia Target. Si la malla ha vuelto a la foto de la inclinación perdida, sigue
		 * desde ella (sin enderezarla de golpe ni inclinarla dos veces); si tiene otro giro, esa foto ya no vuelve y se
		 * olvida. true si hay que escribir OutRelative en la malla.
		 */
		bool Step(const FRotator& MeshRelative, const FRotator& Target, float DeltaTime, float InterpSpeed, float MaxRateDegPerSec,
			FQuat& OutRelative)
		{
			if (Resume.TryResume(MeshRelative, SameRotationTolerance, BaseRelative, CurrentTilt))
			{
				LastWrittenRelative = MeshRelative;
				LastAppliedTilt = CurrentTilt;
				bTiltApplied = true;
			}
			else
			{
				Resume.Forget();
			}

			TargetTilt = Target;
			// Recto y sin nada aplicado: no se escribe en la malla (el caso normal en llano no cuesta nada más).
			if (!bTiltApplied && TargetTilt.IsZero() && CurrentTilt.IsZero())
			{
				return false;
			}
			CurrentTilt = StepTilt(CurrentTilt, TargetTilt, DeltaTime, InterpSpeed, MaxRateDegPerSec);

			// Otro sistema ha escrito el giro desde la última vez (o es la primera): ese es el nuevo giro sin inclinar. Si
			// luego devuelve lo que escribimos (una foto), se retoma.
			const bool bExternalWrite = !bTiltApplied || !MeshRelative.Equals(LastWrittenRelative, SameRotationTolerance);
			if (bExternalWrite)
			{
				RememberTilt();
				BaseRelative = MeshRelative.Quaternion();
			}

			if (CurrentTilt.IsZero())
			{
				// Llegada a recto: se deja la base tal cual y se deja de escribir.
				OutRelative = BaseRelative;
				bTiltApplied = false;
				return !bExternalWrite;
			}

			// Parada en la cuesta y sin cambios de nadie: no se mueve la malla (ni sus hijos) otra vez.
			if (!bExternalWrite && CurrentTilt.Equals(LastAppliedTilt, SameRotationTolerance))
			{
				return false;
			}

			// Escribiendo sobre una base propia de nuevo: la inclinación perdida ya no se va a devolver.
			if (!bExternalWrite)
			{
				Resume.Forget();
			}
			OutRelative = ComposeTilt(BaseRelative, CurrentTilt);
			LastWrittenRelative = OutRelative.Rotator();
			LastAppliedTilt = CurrentTilt;
			bTiltApplied = true;
			return true;
		}

		/**
		 * El fotograma completo según el estado de la tortuga: con la foto en manos de otro sistema (HoldsSnapshot) no se
		 * toca la malla; si otro sistema manda en ella (IsTakenOver), se quita al momento; si no, un paso hacia FloorTilt
		 * (la inclinación del suelo, o cero en el aire). true si hay que escribir OutRelative en la malla.
		 */
		bool Tick(const FRotator& MeshRelative, const FTiltGate& Gate, const FRotator& FloorTilt, float DeltaTime, float InterpSpeed,
			float MaxRateDegPerSec, FQuat& OutRelative)
		{
			if (HoldsSnapshot(Gate))
			{
				Suspend();
				return false;
			}
			if (IsTakenOver(Gate))
			{
				return Drop(MeshRelative, OutRelative);
			}
			const FRotator Target = ShouldTilt(Gate) ? FloorTilt : FRotator::ZeroRotator;
			return Step(MeshRelative, Target, DeltaTime, InterpSpeed, MaxRateDegPerSec, OutRelative);
		}

		/** El giro relativo que ha quedado en la malla tras escribir OutRelative (el componente puede redondearlo). */
		void NoteWritten(const FRotator& MeshRelative)
		{
			LastWrittenRelative = MeshRelative;
		}

	private:
		/** Guarda la inclinación aplicada (si la hay) para retomarla si la malla vuelve a ese giro. */
		void RememberTilt()
		{
			if (bTiltApplied)
			{
				Resume.Remember(LastWrittenRelative, BaseRelative, LastAppliedTilt);
			}
		}
	};
}
