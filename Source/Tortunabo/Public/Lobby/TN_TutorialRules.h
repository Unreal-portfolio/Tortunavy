#pragma once

#include "CoreMinimal.h"
#include "VR/TN_VRMath.h"

/**
 * Reglas del recorrido del tutorial que decide el servidor, como lógica pura (sin mundo ni controladores). Las usan
 * ATN_TutorialCourse y UTN_TutorialPlayerComponent; los tests de Tortunabo.Tutorial las cubren.
 */
namespace TNTutorialRules
{
	/**
	 * @brief Estación de un punto de control del recorrido. Pasado el cañón hay un punto de control más, que sigue siendo
	 *        de la estación de la catapulta (TNTutorial::CheckpointX).
	 */
	inline int32 StationOfCheckpoint(int32 Checkpoint, int32 CatapultStation)
	{
		return Checkpoint <= CatapultStation ? Checkpoint : Checkpoint - 1;
	}

	/**
	 * @brief ¿Atiende el servidor la petición de ir a la estación StationIndex (ServerGoToStation, #17)?
	 * @param ReachedStation Estación más avanzada que ha pisado quien la pide; INDEX_NONE si no está en el tutorial.
	 * @param bDebugJump     Salto de pruebas: solo el anfitrión (o en Standalone) y fuera de Shipping
	 *                       (TNDebugRpcLogic::CanRunHostOnlyDebugRpc). Va a cualquiera y mete en el tutorial si hace falta.
	 * @return Sin el salto de pruebas, solo a una estación que exista y a la que ya haya llegado: volver atrás sí,
	 *         adelantarse o entrar en el tutorial por aquí no (antes un cliente se teletransportaba a donde quería).
	 */
	inline bool CanGoToStation(int32 StationIndex, int32 NumStations, int32 ReachedStation, bool bDebugJump)
	{
		if (StationIndex < 0 || StationIndex >= NumStations)
		{
			return false;
		}
		if (bDebugJump)
		{
			return true;
		}
		return ReachedStation != INDEX_NONE && StationIndex <= ReachedStation;
	}

	/**
	 * @brief ¿Hay que avisar al cliente de que su HUD empiece de cero (ClientResetProgress, #85)?
	 *        Quien entra de nuevas lo hace solo: bInTutorial pasa a true y su máquina se reinicia al verlo. Quien repite
	 *        estando ya dentro (volver a la salida) no cambia nada que replicar, y su HUD seguiría con las tareas tachadas.
	 * @param bWasInside    El jugador ya estaba en el recorrido antes de la petición.
	 * @param TargetStation Estación a la que se le lleva (0 = la salida).
	 */
	inline bool ShouldResetProgress(bool bWasInside, int32 TargetStation)
	{
		return bWasInside && TargetStation == 0;
	}

	// ── Mandos de las gafas (#645): las tareas del tutorial que miran un botón también cuentan con los Touch ─────────────

	/**
	 * @brief ¿Está pulsado un gatillo o un agarre de los Touch? Con OpenXR solo dan su valor (no un «clic» fiable): cuenta a
	 *        partir del mismo umbral que el resto de la VR (TNVRMath::AnalogPressThreshold), o con el clic si lo hay.
	 */
	inline bool VRButtonDown(float Axis, bool bClick)
	{
		return bClick || Axis >= TNVRMath::AnalogPressThreshold;
	}

	/** @brief ¿Se mueve el stick izquierdo de los Touch (andar, liberarse de Berta)? Mismo umbral que el stick del mando. */
	inline bool VRStickMoved(float X, float Y, float Threshold = 0.3f)
	{
		return FMath::Abs(X) > Threshold || FMath::Abs(Y) > Threshold;
	}

	/**
	 * @brief ¿Cuenta como «soltar» (estación de las ranuras) lo que ha pasado con las gafas? El objeto de la aleta ha dejado de
	 *        estar en ella sin pasar al caparazón, con el agarre derecho apretado hace poco: abrirlo despacio lo deja caer y
	 *        abrirlo con impulso lo lanza (ATortugaCharacter::VRGripReleased).
	 * @param bHadItem          Llevaba un objeto en la aleta el fotograma anterior.
	 * @param bHasItem          Lo lleva ahora.
	 * @param bStoredChanged    Lo guardado en el caparazón ha cambiado (el objeto se ha guardado, no soltado).
	 * @param SecondsSinceGrip  Segundos desde que el agarre derecho estaba apretado (0 si lo está).
	 * @param WindowSeconds     Margen: el servidor tarda un viaje en quitarle el objeto.
	 */
	inline bool VRDropCounts(bool bHadItem, bool bHasItem, bool bStoredChanged, float SecondsSinceGrip, float WindowSeconds = 1.5f)
	{
		return bHadItem && !bHasItem && !bStoredChanged && SecondsSinceGrip <= WindowSeconds;
	}
}
