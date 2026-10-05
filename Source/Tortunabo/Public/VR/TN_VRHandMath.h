#pragma once

#include "CoreMinimal.h"
#include "VR/TN_VRMath.h"

/**
 * Cuentas de las manos VR sin mundo (las usan ATN_VRRig y UTN_VRGrabComponent y las prueba Tortunabo.VR.Hands*): la
 * velocidad de la mano para lanzar, cuándo se rompe un agarre que se ha quedado enganchado, la vibración de los mandos,
 * la viñeta de confort al moverse y las direcciones con las que se busca sitio para el HUD (Docs/Modo_VR.md).
 */
namespace TNVRHands
{
	// ── Velocidad de la mano para lanzar ─────────────────────────────────────

	/**
	 * Velocidad media de la mano en los últimos WindowSeconds (lo recorrido entre el tiempo, no una muestra suelta): no
	 * depende de los hercios de las gafas (72, 90 o 120) y un salto de un solo fotograma del seguimiento pesa poco, así que
	 * soltar despacio no lanza por un tirón suelto. Las muestras son la velocidad de cada fotograma y su duración.
	 */
	struct FHandVelocityWindow
	{
		static constexpr int32 Capacity = 32;
		/** Ventana de 70 ms: unos 5 fotogramas a 72 Hz y 8 a 120 Hz, lo que dura el final de un gesto de lanzar. */
		static constexpr float DefaultWindowSeconds = 0.07f;

		void Reset()
		{
			Count = 0;
			Next = 0;
		}

		void Add(const FVector& Velocity, float DeltaSeconds)
		{
			if (DeltaSeconds <= UE_KINDA_SMALL_NUMBER)
			{
				return;
			}
			Samples[Next] = Velocity;
			Durations[Next] = DeltaSeconds;
			Next = (Next + 1) % Capacity;
			Count = FMath::Min(Count + 1, Capacity);
		}

		/** Media ponderada por la duración de las muestras más nuevas que caben en la ventana (la última, siempre). */
		FVector Average(float WindowSeconds = DefaultWindowSeconds) const
		{
			FVector Sum = FVector::ZeroVector;
			float Time = 0.f;
			for (int32 i = 0; i < Count; ++i)
			{
				const int32 Index = (Next - 1 - i + Capacity) % Capacity;
				const float Take = FMath::Min(Durations[Index], WindowSeconds - Time);
				if (Take <= 0.f)
				{
					break;
				}
				Sum += Samples[Index] * Take;
				Time += Take;
			}
			return Time > 0.f ? Sum / Time : FVector::ZeroVector;
		}

		int32 Num() const { return Count; }

	private:
		FVector Samples[Capacity];
		float Durations[Capacity] = {};
		int32 Count = 0;
		int32 Next = 0;
	};

	// ── Agarre que se queda enganchado ───────────────────────────────────────

	/** Lo cogido a más de esto (cm) de donde debería estar en la mano cuenta como enganchado (una pared, algo pesado). */
	constexpr float GrabStrainDistance = 45.f;
	/** Enganchado durante más de esto (s), se suelta solo: no se queda tirando de la mano ni atraviesa la pared. */
	constexpr float GrabStrainSeconds = 0.35f;
	/** A más de esto (cm) se suelta al momento (la tortuga se ha teletransportado, un derribo la ha lanzado). */
	constexpr float GrabSnapDistance = 150.f;

	/**
	 * ¿Se rompe el agarre? Separation: lo que hay entre el punto por el que se cogió y donde debería estar en la mano.
	 * StrainSince: desde cuándo (s) va enganchado; negativo si no lo va. Lo actualiza: se pone al empezar y se borra al
	 * volver por debajo de GrabStrainDistance.
	 */
	inline bool ShouldBreakGrab(float Separation, double Now, double& StrainSince, float StrainDistance = GrabStrainDistance,
		float StrainSeconds = GrabStrainSeconds, float SnapDistance = GrabSnapDistance)
	{
		if (Separation >= SnapDistance)
		{
			StrainSince = -1.0;
			return true;
		}
		if (Separation <= StrainDistance)
		{
			StrainSince = -1.0;
			return false;
		}
		if (StrainSince < 0.0)
		{
			StrainSince = Now;
			return false;
		}
		return Now - StrainSince >= StrainSeconds;
	}

	// ── Pulsar botones con la punta de la aleta ──────────────────────────────

	/** La punta a menos de esto (cm) de la colisión del botón cuenta como tocándolo. */
	constexpr float PokeTouchDistance = 3.f;
	/** Tras pulsar, la punta tiene que alejarse esto (cm) para poder volver a pulsar. */
	constexpr float PokeRearmDistance = 10.f;
	/** Velocidad mínima (cm/s) hacia el botón: apoyar la mano o que la pare el escenario encima no pulsa. */
	constexpr float PokeMinApproachSpeed = 40.f;
	/** Entre dos pulsaciones de la misma mano, como poco (s). */
	constexpr float PokeCooldownSeconds = 0.5f;

	/** Estado de cada mano para pulsar botones. */
	struct FPokeState
	{
		float PrevDistance = -1.f;
		bool bArmed = true;
		double LastPoke = -1000.0;
	};

	/**
	 * ¿Pulsa la punta el botón este fotograma? Distance: de la punta a la colisión del botón más cercano (negativo si no hay
	 * ninguno cerca). Pulsa al tocarlo (PokeTouchDistance) yendo hacia él (PokeMinApproachSpeed), y no vuelve a pulsar hasta
	 * alejarse PokeRearmDistance y pasar PokeCooldownSeconds: dejar la mano apoyada no lo pulsa una y otra vez.
	 */
	inline bool UpdatePoke(FPokeState& State, float Distance, float DeltaSeconds, double Now)
	{
		if (Distance < 0.f || Distance > PokeRearmDistance)
		{
			State.PrevDistance = -1.f;
			State.bArmed = true;
			return false;
		}
		const float Approach = State.PrevDistance >= 0.f && DeltaSeconds > UE_KINDA_SMALL_NUMBER
			? (State.PrevDistance - Distance) / DeltaSeconds : 0.f;
		State.PrevDistance = Distance;
		if (!State.bArmed || Distance > PokeTouchDistance || Approach < PokeMinApproachSpeed || Now - State.LastPoke < PokeCooldownSeconds)
		{
			return false;
		}
		State.bArmed = false;
		State.LastPoke = Now;
		return true;
	}

	// ── Gatillo entre los menús y el juego ───────────────────────────────────

	/**
	 * ¿Se come el procesador de menús este valor del eje del gatillo (no llega al juego)? Con un menú delante, sí (es el clic
	 * del láser), salvo al soltarlo: el juego tiene que verlo abierto o se queda con el valor de antes del menú. Apretado en el
	 * menú, tampoco llega al juego tras cerrarlo hasta soltarlo: si no, el clic en «Cerrar» con el gatillo interactuaría otra
	 * vez con lo que abrió el menú (la tienda, el general). bPressedInMenu: estado de cada gatillo, lo actualiza.
	 */
	inline bool ShouldEatTriggerAxis(bool bMenuUp, bool bJustPressed, float Value, bool& bPressedInMenu)
	{
		if (Value < TNVRMath::AnalogReleaseThreshold)
		{
			bPressedInMenu = false;
			return false;
		}
		if (bMenuUp)
		{
			bPressedInMenu |= bJustPressed;
			return true;
		}
		return bPressedInMenu;
	}

	// ── Vibración de los mandos ──────────────────────────────────────────────



	/** Un toque de vibración: fuerza (0..1) y duración (s). */
	struct FHapticPulse
	{
		float Amplitude = 0.f;
		float Seconds = 0.f;
	};

	/** Los toques del juego, de más suave a más fuerte. */
	namespace Haptics
	{
		/** El láser entra en un botón. */
		constexpr FHapticPulse Hover{ 0.12f, 0.02f };
		/** Clic del láser en un menú. */
		constexpr FHapticPulse Click{ 0.3f, 0.03f };
		/** La mano toca la pared o el suelo. */
		constexpr FHapticPulse Bump{ 0.25f, 0.04f };
		/** Soltar despacio (dejar algo). */
		constexpr FHapticPulse Drop{ 0.2f, 0.04f };
		/** Coger algo o interactuar con la mano. */
		constexpr FHapticPulse Grab{ 0.45f, 0.06f };
		/** Lanzar con el gesto. */
		constexpr FHapticPulse Throw{ 0.7f, 0.08f };
		/** Se escapa lo que se llevaba (enganchado, rechazado por el servidor, se ha roto). */
		constexpr FHapticPulse Slip{ 0.8f, 0.12f };
		/** Derribo propio: las dos manos. */
		constexpr FHapticPulse Knock{ 0.9f, 0.25f };
	}

	/** Fuerza de la vibración mientras se lleva algo enganchado: 0 por debajo de la mitad de StrainDistance, sube hasta 0,5. */
	inline float StrainAmplitude(float Separation, float StrainDistance = GrabStrainDistance)
	{
		const float From = StrainDistance * 0.5f;
		if (Separation <= From || StrainDistance <= 0.f)
		{
			return 0.f;
		}
		return 0.5f * FMath::Clamp((Separation - From) / (StrainDistance - From), 0.f, 1.f);
	}

	// ── Viñeta de confort al moverse ─────────────────────────────────────────

	/** Velocidad (cm/s) a partir de la que se oscurecen los bordes, y la que los deja del todo. */
	constexpr float VignetteStartSpeed = 150.f;
	constexpr float VignetteFullSpeed = 900.f;
	/** Giro suave (grados/s) que oscurece del todo. */
	constexpr float VignetteFullTurnRate = 120.f;

	// ── Ajustes de VR (#647) ─────────────────────────────────────────────────

	/** Fuerza de la viñeta de confort del ajuste (0 apagada, 1 la de serie, 2 el doble); fuera de rango se recorta. */
	inline float VignetteStrengthFromSetting(uint8 Setting)
	{
		return static_cast<float>(FMath::Clamp<int32>(Setting, 0, 2));
	}

	/** Fuerza de la vibración del ajuste (encendida 1, apagada 0). */
	inline float HapticScaleFromSetting(bool bOn)
	{
		return bOn ? 1.f : 0.f;
	}

	/**
	 * Lo que vale una fuerza que tiene ajuste y variable de consola (TN.VR.ComfortVignette, TN.VR.Haptics): si alguien
	 * tocó la consola (o la línea de comandos), manda ella; si no, el ajuste del menú.
	 */
	inline float ConsoleOrSetting(float ConsoleValue, bool bConsoleTouched, float SettingValue)
	{
		return bConsoleTouched ? ConsoleValue : SettingValue;
	}

	/** Intensidad de viñeta del motor (VignetteIntensity) a tope, con Strength 1. */
	constexpr float VignetteMaxIntensity = 1.1f;

	/**
	 * Viñeta (VignetteIntensity) que toca al moverse sin mover la cabeza: andar, caer, salir lanzado o el giro suave
	 * marean menos con los bordes oscuros. 0 parado, sube con la velocidad del cuerpo y con el giro suave (lo que más de
	 * los dos); Strength escala (0 la quita).
	 */
	inline float ComfortVignette(float SpeedCmS, float TurnDegS, float Strength)
	{
		if (Strength <= 0.f)
		{
			return 0.f;
		}
		const float FromSpeed = FMath::Clamp((SpeedCmS - VignetteStartSpeed) / (VignetteFullSpeed - VignetteStartSpeed), 0.f, 1.f);
		const float FromTurn = FMath::Clamp(FMath::Abs(TurnDegS) / VignetteFullTurnRate, 0.f, 1.f);
		return VignetteMaxIntensity * Strength * FMath::Max(FromSpeed, FromTurn);
	}

	/**
	 * La viñeta de confort encima de la que pone otro en la misma cámara (la del caparazón, ATortugaCharacter::
	 * ApplyShellDarkness) o, si la cámara no pone ninguna, de la de la escena (SceneBase: 0,4 del motor y los volúmenes de
	 * posproceso): se pinta la más oscura de las dos sin pisar la otra (antes, sin la del caparazón, la base era 0 y al
	 * empezar a andar la vista se aclaraba). Si lo que tiene la cámara es lo que escribió
	 * esta capa el fotograma anterior (nadie lo ha vuelto a poner: juego en pausa, la tortuga no ha hecho Tick), la base es la
	 * que guardó, no su propio valor (antes se tomaba como base y la viñeta no bajaba nunca); al quitarse deja la cámara como
	 * estaba.
	 */
	/**
	 * Peso con el que un volumen de posproceso entra en la vista, como lo mezcla el motor (UWorld::AddPostProcessingSettings):
	 * BlendWeight sin límites o dentro; fuera, bajando con la distancia hasta BlendRadius. DistanceToPoint: lo que da
	 * EncompassesPoint (0 dentro, negativo si no se sabe).
	 */
	inline float PostProcessVolumeWeight(float BlendWeight, bool bUnbound, float DistanceToPoint, float BlendRadius)
	{
		float Weight = FMath::Clamp(BlendWeight, 0.f, 1.f);
		if (bUnbound)
		{
			return Weight;
		}
		if (DistanceToPoint < 0.f || DistanceToPoint > BlendRadius)
		{
			return 0.f;
		}
		if (BlendRadius >= 1.f)
		{
			Weight *= 1.f - DistanceToPoint / BlendRadius;
		}
		return FMath::Clamp(Weight, 0.f, 1.f);
	}

	struct FVignetteLayer
	{
		/** bOverride y Value: los de la cámara (bOverride_VignetteIntensity, VignetteIntensity); los cambia. */
		void Apply(bool& bOverride, float& Value, float Comfort, float MinVisible, float SceneBase)
		{
			const bool bStillOurs = Written >= 0.f && bOverride && Value == Written;
			if (!bStillOurs)
			{
				bBaseOverride = bOverride;
				Base = bOverride ? Value : SceneBase;
			}
			if (Comfort <= MinVisible)
			{
				Restore(bOverride, Value);
				return;
			}
			bOverride = true;
			Value = FMath::Max(Base, Comfort);
			Written = Value;
		}

		/** Deja la cámara con lo que tenía antes de esta capa (si escribió algo). */
		void Restore(bool& bOverride, float& Value)
		{
			if (Written >= 0.f)
			{
				bOverride = bBaseOverride;
				Value = bBaseOverride ? Base : 0.f;
			}
			Written = -1.f;
		}

		bool HasWritten() const { return Written >= 0.f; }
		/** La base no cambia mientras siga lo que escribió esta capa: solo hace falta la de la escena si no. */
		bool NeedsSceneBase(bool bOverride, float Value) const { return !bOverride && !(Written >= 0.f && Value == Written); }

	private:
		bool bBaseOverride = false;
		float Base = 0.f;
		/** Lo que escribió el último fotograma; negativo si nada. */
		float Written = -1.f;
	};

	// ── HUD: dónde cabe sin que lo tape el escenario ─────────────────────────

	/**
	 * Dirección (en los ejes de la cámara: +X delante, +Y derecha, +Z arriba) desde los ojos hasta el punto (U, V) del HUD
	 * curvo (U de izquierda a derecha y V de arriba abajo, 0..1), con el eje del cilindro en los ojos, ArcDeg grados de
	 * ancho y DrawAspect = alto / ancho del dibujo.
	 */
	inline FVector HudProbeDirection(float ArcDeg, float DrawAspect, float U, float V)
	{
		const double ArcRad = FMath::DegreesToRadians(static_cast<double>(FMath::Clamp(ArcDeg, 10.f, 300.f)));
		const double Yaw = (static_cast<double>(U) - 0.5) * ArcRad;
		// La altura del panel, en radios: el ancho del arco (ArcRad radios) por el alto entre el ancho.
		const double Height = (0.5 - static_cast<double>(V)) * ArcRad * static_cast<double>(DrawAspect);
		return FVector(FMath::Cos(Yaw), FMath::Sin(Yaw), Height).GetSafeNormal();
	}

	/**
	 * Radio al que cabe el HUD en esa dirección si algo la corta a HitDistance de los ojos: el panel es un cilindro con el eje
	 * en los ojos, así que un punto que se ve DirZ por encima o por debajo está a radio = distancia × cos (lo horizontal).
	 */
	inline float HudRadiusForHit(const FVector& CameraDirection, float HitDistance)
	{
		const double Horizontal = FVector(CameraDirection.X, CameraDirection.Y, 0.0).Size();
		return static_cast<float>(HitDistance * Horizontal);
	}
}
