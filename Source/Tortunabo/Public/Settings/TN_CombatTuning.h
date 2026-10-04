#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "TN_CombatTuning.generated.h"

/**
 * @brief Equilibrado del combate de la playa y de la carrera: derribos y aturdimientos de las tortugas, mareo de los
 * enemigos, tiempo que un peligro ignora a quien acaba de golpear y gravedades propias de los proyectiles y botes.
 *
 * Una sola fuente, editable sin recompilar en Config/DefaultGame.ini ([/Script/Tortunabo.TN_CombatTuning]) o en
 * Ajustes del proyecto > Tortunavy - Equilibrado del combate. Los valores por defecto son los que tenían los constexpr
 * locales de cada actor; el test Tortunabo.Tuning.CombatDefaults falla si alguno cambia sin querer.
 *
 * Lo leen el servidor (derribos, mareos e inmunidades son autoritativos) y, para las trayectorias que se simulan en
 * los dos lados (bolita del tanque, rodada del ermitaño), también el cliente: el .ini empaquetado es el mismo en ambos.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Tortunavy - Equilibrado del combate"))
class TORTUNABO_API UTN_CombatTuning : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** @brief Ajustes vigentes (el CDO, con lo que diga el .ini). Nunca nulo. */
	static const UTN_CombatTuning& Get() { return *GetDefault<UTN_CombatTuning>(); }

	// ── Carrera: objetos ──

	/** Disco: derribo de la tortuga alcanzada. */
	UPROPERTY(Config, EditAnywhere, Category = "Carrera|Disco", meta = (ClampMin = "0", Units = "Seconds"))
	float FrisbeeKnockSeconds = 1.9f;

	/** Disco: mareo del enemigo alcanzado. */
	UPROPERTY(Config, EditAnywhere, Category = "Carrera|Disco", meta = (ClampMin = "0", Units = "Seconds"))
	float FrisbeeEnemyStunSeconds = 4.f;

	/** Gaviota de ataque: derribo de la tortuga en el radio del impacto. */
	UPROPERTY(Config, EditAnywhere, Category = "Carrera|Gaviota de ataque", meta = (ClampMin = "0", Units = "Seconds"))
	float GullStrikeKnockSeconds = 2.6f;

	/** Gaviota de ataque: mareo del enemigo en el radio del impacto. */
	UPROPERTY(Config, EditAnywhere, Category = "Carrera|Gaviota de ataque", meta = (ClampMin = "0", Units = "Seconds"))
	float GullStrikeEnemyStunSeconds = 4.f;

	/** Cangrejo teledirigido: derribo de la tortuga perseguida. */
	UPROPERTY(Config, EditAnywhere, Category = "Carrera|Cangrejo teledirigido", meta = (ClampMin = "0", Units = "Seconds"))
	float HomingCrabKnockSeconds = 2.2f;

	/** Cangrejo teledirigido: mareo del enemigo alcanzado. */
	UPROPERTY(Config, EditAnywhere, Category = "Carrera|Cangrejo teledirigido", meta = (ClampMin = "0", Units = "Seconds"))
	float HomingCrabEnemyStunSeconds = 4.f;

	/** Mina: aturdimiento en bola de las tortugas en la explosión. */
	UPROPERTY(Config, EditAnywhere, Category = "Carrera|Mina", meta = (ClampMin = "0", Units = "Seconds"))
	float MineStunSeconds = 3.f;

	/** Mina: mareo de los enemigos cercanos a la explosión. */
	UPROPERTY(Config, EditAnywhere, Category = "Carrera|Mina", meta = (ClampMin = "0", Units = "Seconds"))
	float MineEnemyStunSeconds = 5.f;

	/** Protector solar: derribo de la tortuga que toca quien lo lleva. */
	UPROPERTY(Config, EditAnywhere, Category = "Carrera|Protector solar", meta = (ClampMin = "0", Units = "Seconds"))
	float StarKnockSeconds = 2.f;

	/** Protector solar: mareo del enemigo que toca quien lo lleva. */
	UPROPERTY(Config, EditAnywhere, Category = "Carrera|Protector solar", meta = (ClampMin = "0", Units = "Seconds"))
	float StarEnemyStunSeconds = 4.f;

	// ── Playa: peligros ──

	/** Cangrejo ermitaño: derribo de la tortuga arrollada. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Cangrejo ermitaño", meta = (ClampMin = "0", Units = "Seconds"))
	float HermitCrabKnockSeconds = 2.5f;

	/** Cangrejo ermitaño: tiempo que ignora a la tortuga tras derribarla. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Cangrejo ermitaño", meta = (ClampMin = "0", Units = "Seconds"))
	float HermitCrabIgnoreSeconds = 3.f;

	/** Cangrejo ermitaño: gravedad de la bola rodando (botes). */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Cangrejo ermitaño", meta = (ClampMin = "1", Units = "CentimetersPerSecondSquared"))
	float HermitCrabGravity = 1250.f;

	/** Erizo: derribo de la tortuga pinchada. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Erizo", meta = (ClampMin = "0", Units = "Seconds"))
	float SeaUrchinKnockSeconds = 2.4f;

	/** Erizo: tiempo que ignora a la tortuga tras pincharla. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Erizo", meta = (ClampMin = "0", Units = "Seconds"))
	float SeaUrchinIgnoreSeconds = 4.5f;

	/** Cangrejo gigante: aturdimiento de la tortuga alcanzada por la pinza. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Cangrejo gigante", meta = (ClampMin = "0", Units = "Seconds"))
	float GiantCrabStunSeconds = 3.5f;

	/** Cangrejo gigante: derribo de la tortuga arrollada en la embestida. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Cangrejo gigante", meta = (ClampMin = "0", Units = "Seconds"))
	float GiantCrabChargeKnockSeconds = 2.4f;

	/** Cangrejo gigante: su propio mareo al estamparse contra algo grande. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Cangrejo gigante", meta = (ClampMin = "0", Units = "Seconds"))
	float GiantCrabCrashStunSeconds = 1.6f;

	/** Cangrejo gigante: tiempo que ignora a la tortuga golpeada. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Cangrejo gigante", meta = (ClampMin = "0", Units = "Seconds"))
	float GiantCrabIgnoreSeconds = 6.f;

	/** Pulpo de la poza: aturdimiento que se suma al vuelo de la tortuga lanzada. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Pulpo de la poza", meta = (ClampMin = "0", Units = "Seconds"))
	float PoolOctopusStunExtraSeconds = 1.2f;

	/** Pulpo de la poza: tiempo que ignora a la tortuga tras lanzarla. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Pulpo de la poza", meta = (ClampMin = "0", Units = "Seconds"))
	float PoolOctopusIgnoreSeconds = 5.f;

	/** Pulpo de la poza: gravedad con la que calcula el lanzamiento (la de la bola del caparazón). */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Pulpo de la poza", meta = (ClampMin = "1", Units = "CentimetersPerSecondSquared"))
	float PoolOctopusThrowGravity = 980.f;

	/** Pulgas de arena: mareo al final de la picada. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Pulgas de arena", meta = (ClampMin = "0", Units = "Seconds"))
	float SandFleasDizzySeconds = 1.f;

	/** Pulgas de arena: tiempo que ignoran a la tortuga picada. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Pulgas de arena", meta = (ClampMin = "0", Units = "Seconds"))
	float SandFleasIgnoreSeconds = 6.f;

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

	/** Tanque de juguete: aturdimiento en bola de la tortuga alcanzada por la bolita. */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Tanque de juguete", meta = (ClampMin = "0", Units = "Seconds"))
	float ToyTankHitStunSeconds = 0.8f;

	/** Tanque de juguete: gravedad de la bolita de espuma (flota un poco). */
	UPROPERTY(Config, EditAnywhere, Category = "Playa|Tanque de juguete", meta = (ClampMin = "1", Units = "CentimetersPerSecondSquared"))
	float ToyTankFoamGravity = 700.f;
};
