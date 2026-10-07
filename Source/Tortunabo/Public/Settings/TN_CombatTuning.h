#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "TN_CombatTuning.generated.h"

/**
 * @brief Equilibrado del combate de la playa: derribos y aturdimientos de las tortugas, tiempo que un peligro ignora a
 * quien acaba de golpear y gravedades propias de las caídas.
 *
 * Una sola fuente, editable sin recompilar en Config/DefaultGame.ini ([/Script/Tortunabo.TN_CombatTuning]) o en
 * Ajustes del proyecto > Tortunavy - Equilibrado del combate. Los valores por defecto son los que tenían los constexpr
 * locales de cada actor; el test Tortunabo.Tuning.CombatDefaults falla si alguno cambia sin querer.
 *
 * Lo leen el servidor (derribos, mareos e inmunidades son autoritativos) y, para las trayectorias que se simulan en
 * los dos lados, también el cliente: el .ini empaquetado es el mismo en ambos.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Tortunavy - Equilibrado del combate"))
class TORTUNABO_API UTN_CombatTuning : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** @brief Ajustes vigentes (el CDO, con lo que diga el .ini). Nunca nulo. */
	static const UTN_CombatTuning& Get() { return *GetDefault<UTN_CombatTuning>(); }

	// ── Playa: peligros ──

	/** Quad: derribo de la tortuga atropellada. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Quad", meta = (ClampMin = "0", Units = "Seconds"))
	float QuadLaneKnockSeconds = 3.f;

	/** Zona de gaviotas: derribo de la tortuga manchada por la caca. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Zona de gaviotas", meta = (ClampMin = "0", Units = "Seconds"))
	float GullZonePoopKnockSeconds = 2.4f;

	/** Zona de gaviotas: tiempo que deja en paz a la tortuga manchada. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Zona de gaviotas", meta = (ClampMin = "0", Units = "Seconds"))
	float GullZonePoopIgnoreSeconds = 6.f;

	/** Zona de gaviotas: aturdimiento tras caer la tortuga soltada (además de la caída). */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Zona de gaviotas", meta = (ClampMin = "0", Units = "Seconds"))
	float GullZoneAfterDropStunSeconds = 2.f;

	/** Zona de gaviotas: tiempo que ignora a la tortuga que acaba de atrapar. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Zona de gaviotas", meta = (ClampMin = "0", Units = "Seconds"))
	float GullZoneGrabIgnoreSeconds = 12.f;

	/** Zona de gaviotas: gravedad con la que calcula la caída de la tortuga soltada. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Zona de gaviotas", meta = (ClampMin = "1", Units = "CentimetersPerSecondSquared"))
	float GullZoneGravity = 980.f;
};
