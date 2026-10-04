// Aviso «Mantén R para volver a la pista» del HUD del Rally (#303): sale solo cuando el buggy propio está atascado o volcado y
// la reaparición automática aún no ha empezado; se oculta en cuanto empieza (o se pide) y no vuelve justo después. Lógica
// pura con lo que ya tiene cada máquina (física replicada del buggy, pista local y la fila de puestos); la prueban
// Tortunabo.Rally.RespawnHint.*.
#pragma once

#include "CoreMinimal.h"

namespace TNRallyRespawnHint
{
	/** Más lento que esto (cm/s, ~5 km/h) cuenta como parado. */
	constexpr float SlowSpeedCms = 150.f;
	/** Más lejos del eje que esto (cm) está fuera de la calzada (14 m de ancho más el arcén; las vallas, a 13 m). */
	constexpr float OffRoadDistanceCm = 900.f;
	/** Segundos parado fuera de la calzada antes del aviso (la reaparición por atasco llega a los 8 s, TNRally::StuckSeconds). */
	constexpr float StuckHintSeconds = 2.5f;
	/** Segundos volcado antes del aviso (el enderezado automático llega a los 4 s, UTN_BuggyData::SelfRightAutoDelay). */
	constexpr float FlippedHintSeconds = 1.5f;
	/** Segundos sin aviso tras una reaparición (contados desde que empieza, con la espera incluida). */
	constexpr float QuietAfterRespawnSeconds = 5.f;

	/** Lo que se ve del buggy propio en este momento. */
	struct FInput
	{
		/** En carrera (Racing o Finishing), sin haber llegado ni estar retirado. */
		bool bRacing = false;
		/** Hay una reaparición en curso (RespawnEndServerTime de la fila aún no ha pasado). */
		bool bRespawning = false;
		/** Segundos desde la última reaparición (negativo = ninguna). */
		float SecondsSinceRespawn = -1.f;
		bool bFlipped = false;
		float SpeedCms = 0.f;
		/** Distancia al eje de la pista (cm); negativa si no se sabe (sin pista): entonces no cuenta como fuera. */
		float DistanceToAxisCm = -1.f;
	};

	/** Tiempo seguido que lleva atascado o volcado. */
	struct FState
	{
		float StrandedSeconds = 0.f;
		bool bWasFlipped = false;
	};

	/** Si el buggy está ahora atascado (parado fuera de la calzada) o volcado. */
	TORTUNABO_API bool IsStranded(const FInput& Input);

	/** Avanza DeltaSeconds y dice si el aviso se ve. Una reaparición en curso o reciente lo apaga y reinicia la cuenta. */
	TORTUNABO_API bool Update(FState& State, const FInput& Input, float DeltaSeconds);
}
