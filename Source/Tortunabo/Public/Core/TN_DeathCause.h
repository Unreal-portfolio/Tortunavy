#pragma once

#include "CoreMinimal.h"
#include "TN_DeathCause.generated.h"

class AActor;

/**
 * Qué ha eliminado a una tortuga (#728). La apunta el servidor en ATN_CoopPlayerState::DeathCause al eliminarla
 * (ATN_RunGameMode::MarkPlayerDeadBy) y la enseña el panel de resultados bajo «¡Eliminado!». Los valores se replican como
 * número: los nuevos van al final.
 */
UENUM(BlueprintType)
enum class ETNDeathCause : uint8
{
	Unknown          UMETA(DisplayName = "Sin causa conocida"),
	Storm            UMETA(DisplayName = "Tormenta"),
	Fall             UMETA(DisplayName = "Caída larga"),
	Void             UMETA(DisplayName = "Fuera del mapa"),
	DeathZone        UMETA(DisplayName = "Zona de muerte"),
	Water            UMETA(DisplayName = "Agua"),
	Crab             UMETA(DisplayName = "Cangrejo"),
	Seagull          UMETA(DisplayName = "Gaviota"),
	SeagullDropping  UMETA(DisplayName = "Caca de gaviota"),
	Quad             UMETA(DisplayName = "Quad"),
	Bleedout         UMETA(DisplayName = "Sin reanimación"),
	ThrownItem       UMETA(DisplayName = "Objeto lanzado"),
	Poison           UMETA(DisplayName = "Veneno"),
	Dehydration      UMETA(DisplayName = "Deshidratación"),
	Quicksand        UMETA(DisplayName = "Arenas movedizas"),
	Count            UMETA(Hidden)
};

namespace TNDeathCause
{
	/** Texto de la causa para el panel de resultados (sin causa conocida, «Eliminado»). */
	TORTUNABO_API FText Describe(ETNDeathCause Cause);

	/**
	 * La causa según quién pidió la muerte en ATortugaCharacter::RequestKill: la propia tortuga es una caída larga; el resto,
	 * por la clase del actor (cangrejo, gaviota, caca, quad u objeto lanzado).
	 */
	TORTUNABO_API ETNDeathCause FromInstigator(const AActor* KillInstigator, const AActor* Victim);
}
