// Lógica pura del combate entre buggies (sin mundo ni actores): fuerza de los impactos según el punto donde dan, daño
// por munición y por choque, freno del ancla, atropello de tortugas y noqueo de la artillera. La usan
// UTN_BuggyHealthComponent, ATN_RallyProjectile y ATN_RallyAnchorTether. Tests en Tortunabo.Rally.Combat.*.
#pragma once

#include "CoreMinimal.h"
#include "Rally/TN_RallyVehicle.h"

namespace TNRallyCombat
{
	// ── Medidas del buggy ───────────────────────────────────────────────────────

	/** Semilongitud y semianchura de la carrocería (cm) si no se pueden leer de la malla. */
	constexpr float DefaultHalfLengthCm = 190.f;
	constexpr float DefaultHalfWidthCm = 95.f;

	// ── Impactos de munición ────────────────────────────────────────────────────

	/** Zona del buggy donde da un impacto, en su espacio local (X al morro, Y a la derecha). */
	enum class EHitZone : uint8
	{
		Front,
		Side,
		Rear
	};

	/** Fuerza y daño de cada munición al dar en un buggy. Los cambios de velocidad son en cm/s (no dependen de la masa). */
	struct FImpactSpec
	{
		/** Empujón en la dirección del disparo, aplicado en el punto de impacto. */
		float PushCms = 0.f;
		/** Empujón hacia arriba si da en el morro (lo levanta). */
		float NoseLiftCms = 0.f;
		/** Vida que quita. */
		float Damage = 0.f;
	};

	TORTUNABO_API FImpactSpec ImpactFor(ETNRallyAmmo Ammo);

	/** Zona según el punto local normalizado por las medidas: lateral si |Y|/semianchura domina; si no, morro o trasera. */
	TORTUNABO_API EHitZone ClassifyHitZone(const FVector& LocalPoint, float HalfLengthCm, float HalfWidthCm);

	/** Fracción mínima de la semilongitud a la que se aplica un impacto lateral: siempre hace girar. */
	constexpr float SideLeverFraction = 0.6f;

	/** Empujón de un impacto, en espacio local del buggy, listo para AddImpulseAtLocation (multiplicado por la masa). */
	struct FImpactPush
	{
		FVector LocalImpulseCms = FVector::ZeroVector;
		FVector LocalPoint = FVector::ZeroVector;
		EHitZone Zone = EHitZone::Side;
	};

	/**
	 * Empujón en el punto de impacto: en el morro frena (el disparo viene de delante) y lo levanta; en un lateral empuja de
	 * lado lejos del centro, así que hace girar; en la trasera empuja hacia delante. LocalShotDir es la dirección del
	 * proyectil en espacio del buggy; si es nula, se usa la del punto hacia el centro.
	 */
	TORTUNABO_API FImpactPush ComputeImpactPush(ETNRallyAmmo Ammo, const FVector& LocalPoint, const FVector& LocalShotDir,
		float HalfLengthCm, float HalfWidthCm);

	// ── Vida ────────────────────────────────────────────────────────────────────

	constexpr float DefaultMaxHealth = 100.f;
	/** Con esta fracción de vida o menos, el buggy echa humo. */
	constexpr float SmokeHealthFraction = 0.5f;
	/** Segundos que el buggy no recibe daño tras reventar (cubre la reaparición y su fantasma). */
	constexpr float DeathInvulnerableSeconds = 4.f;
	/** Segundos entre reventar y volver a tener la vida llena. */
	constexpr float DeathRestoreSeconds = 1.f;

	/** Vida tras recibir Damage (negativo cuenta como 0), en [0, MaxHealth]. */
	TORTUNABO_API float ApplyDamage(float Health, float Damage, float MaxHealth);
	TORTUNABO_API bool IsSmoking(float Health, float MaxHealth);

	/** Bocanadas de humo por segundo a media vida y con la vida casi a 0 (#296: humo de malla propia, sin Niagara). */
	constexpr float SmokeMinPuffsPerSecond = 3.f;
	constexpr float SmokeMaxPuffsPerSecond = 9.f;

	/** Bocanadas por segundo: 0 si no echa humo; de SmokeMinPuffsPerSecond a media vida a SmokeMaxPuffsPerSecond en 0. */
	TORTUNABO_API float SmokePuffsPerSecond(float Health, float MaxHealth);
	TORTUNABO_API bool IsDestroyed(float Health);

	// ── Choques ─────────────────────────────────────────────────────────────────

	/** Cambio de velocidad por el impulso normal (cm/s) a partir del que un choque hace daño, y el que hace el máximo. */
	constexpr float CrashMinDeltaVCms = 700.f;
	constexpr float CrashMaxDeltaVCms = 2500.f;
	constexpr float CrashMaxDamage = 35.f;
	/** Contactos casi verticales (suelo al aterrizar o algo encima): no son choques. */
	constexpr float CrashMaxNormalZ = 0.75f;
	/** Segundos mínimos entre dos daños por choque del mismo buggy (un choque dispara varios contactos). */
	constexpr float CrashCooldownSeconds = 0.5f;

	/** Daño de un choque: 0 por debajo del umbral o en contactos verticales; lineal hasta CrashMaxDamage. */
	TORTUNABO_API float CrashDamage(float DeltaVCms, float ImpactNormalZ);

	/** Golpe por detrás: impulso hacia delante (cm/s) al buggy de delante si el contacto está en su trasera. */
	constexpr float RearBumpMinClosingCms = 300.f;
	constexpr float RearBumpCms = 250.f;
	TORTUNABO_API float RearBumpCmsFor(float LocalHitX, float HalfLengthCm, float ClosingSpeedCms);

	// ── Atropello de tortugas ───────────────────────────────────────────────────

	/** Velocidad mínima del buggy (cm/s, horizontal) para lanzar a una tortuga a pie. */
	constexpr float RunOverMinSpeedCms = 400.f;
	constexpr float RunOverMaxHorizontalCms = 1600.f;
	constexpr float RunOverBaseUpCms = 500.f;
	constexpr float RunOverMaxUpCms = 900.f;
	/** Segundos sin volver a lanzar a la misma tortuga. */
	constexpr float RunOverCooldownSeconds = 1.f;

	/**
	 * Velocidad con la que sale la tortuga atropellada: sobre todo en la dirección del buggy, algo hacia fuera de su
	 * trayectoria y hacia arriba. Cero si el buggy va más lento que RunOverMinSpeedCms.
	 */
	TORTUNABO_API FVector TurtleLaunchVelocity(const FVector& BuggyVelocity, const FVector& BuggyToTurtle);

	// ── Ancla ───────────────────────────────────────────────────────────────────

	/** Deceleración máxima del ancla (cm/s²) y velocidad a la que llega a ella. */
	constexpr float AnchorMaxDecelCms2 = 1800.f;
	constexpr float AnchorFullSpeedCms = 600.f;
	/** Largo de la cuerda (cm): el ancla se arrastra detrás del buggy a esta distancia. */
	constexpr float AnchorRopeLengthCm = 500.f;

	/** Aceleración que frena al buggy enganchado: opuesta a su velocidad horizontal, nula en parado. */
	TORTUNABO_API FVector AnchorDragAccel(const FVector& Velocity);

	/** Posición del ancla arrastrada: si la cuerda se estira más de RopeLengthCm, el ancla se acerca al gancho. */
	TORTUNABO_API FVector DragAnchor(const FVector& Anchor, const FVector& Hook, float RopeLengthCm);

	// ── Artillera ───────────────────────────────────────────────────────────────

	/** Segundos que un impacto directo deja noqueada a la artillera (la torreta no dispara). */
	constexpr float GunnerKnockSeconds = 2.f;
}
