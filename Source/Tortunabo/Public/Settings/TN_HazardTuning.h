#pragma once

#include "CoreMinimal.h"
#include "Core/TN_DeathCause.h"
#include "Engine/DeveloperSettings.h"
#include "TN_HazardTuning.generated.h"

/** Lo que hace un enemigo u obstáculo a la tortuga que alcanza (hoja EnemyAndObstacleData, columna «Tipo de muerte»). */
USTRUCT(BlueprintType)
struct FTNHazardEffect
{
	GENERATED_BODY()

	/** Vida que quita (sobre 100). */
	UPROPERTY(EditAnywhere, Category = "Efecto", meta = (ClampMin = "0"))
	float Damage = 0.f;

	/** Veneno: daño por segundo y segundos que dura (0 = sin veneno). */
	UPROPERTY(EditAnywhere, Category = "Efecto", meta = (ClampMin = "0"))
	float PoisonDamagePerSecond = 0.f;

	UPROPERTY(EditAnywhere, Category = "Efecto", meta = (ClampMin = "0", Units = "Seconds"))
	float PoisonSeconds = 0.f;

	/** Mata sin más (caída, arrastre, arenas, atropello): no pasa por la vida. */
	UPROPERTY(EditAnywhere, Category = "Efecto")
	bool bKills = false;

	/** Causa de la muerte si mata o si deja la vida a cero (Unknown: la de quién lo causa). */
	UPROPERTY(EditAnywhere, Category = "Efecto")
	ETNDeathCause DeathCause = ETNDeathCause::Unknown;
};

/**
 * @brief Daño, veneno y tipo de muerte de cada enemigo y obstáculo del modo único (#871, plan maestro §3, hoja
 * EnemyAndObstacleData del Excel_DayT). Una sola fuente, editable sin recompilar en Config/DefaultGame.ini
 * ([/Script/Tortunabo.TN_HazardTuning]) o en Ajustes del proyecto > Tortunavy - Enemigos y obstáculos. El test
 * Tortunabo.Hazards.Tuning fija los valores de la hoja.
 *
 * Lo aplica el servidor (TNHazard::Apply, World/TN_HazardEffects.h) sobre los vitales (UTN_VitalsComponent). Los derribos,
 * aturdimientos y empujones de cada enemigo siguen como estaban (UTN_CombatTuning y cada actor): esto solo suma el daño.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Tortunavy - Enemigos y obstáculos"))
class TORTUNABO_API UTN_HazardTuning : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTN_HazardTuning();

	/** @brief Ajustes vigentes (el CDO, con lo que diga el .ini). Nunca nulo. */
	static const UTN_HazardTuning& Get() { return *GetDefault<UTN_HazardTuning>(); }

	/** Gaviota 1 (ATN_BeachGullZone): la sube y la suelta. Caída mortal. */
	UPROPERTY(Config, EditAnywhere, Category = "Enemigos")
	FTNHazardEffect GullDrop;

	/** Gaviota 1: altura (cm) desde la que la suelta para que la caída mate. Más bajo (se escurre pronto), solo aturde. */
	UPROPERTY(Config, EditAnywhere, Category = "Enemigos", meta = (ClampMin = "0", Units = "Centimeters"))
	float GullDropFatalHeight = 500.f;

	/** Gaviota 2: la caca (ATN_SeagullDroppingActor y las cagadas de ATN_BeachGullZone). 30 de daño. */
	UPROPERTY(Config, EditAnywhere, Category = "Enemigos")
	FTNHazardEffect SeagullDropping;

	/** Cangrejo 1 (ATN_CrabActor): 20 por contacto. */
	UPROPERTY(Config, EditAnywhere, Category = "Enemigos")
	FTNHazardEffect CrabContact;

	/** Cangrejo 2 (ATN_BeachDragCrab): el arrastre hasta el final mata. */
	UPROPERTY(Config, EditAnywhere, Category = "Enemigos")
	FTNHazardEffect DragCrab;

	/** Tentáculos de la medusa (ATN_JellyfishActor, ATN_BeachTrampoline): 5/s de veneno. */
	UPROPERTY(Config, EditAnywhere, Category = "Enemigos")
	FTNHazardEffect JellyfishTentacles;

	/** Erizo (ATN_BeachUrchinSpikes): 15 más veneno. */
	UPROPERTY(Config, EditAnywhere, Category = "Enemigos")
	FTNHazardEffect Urchin;

	/** Cangrejo 3 (ATN_BeachBurrowCrab): la pinza, 40. */
	UPROPERTY(Config, EditAnywhere, Category = "Enemigos")
	FTNHazardEffect BurrowCrab;

	/** Arenas movedizas (ATN_Quicksand): hundida hasta el final sin soltarse, muere. */
	UPROPERTY(Config, EditAnywhere, Category = "Enemigos")
	FTNHazardEffect Quicksand;

	/** Quad (ATN_BeachQuadLane): muerte instantánea. */
	UPROPERTY(Config, EditAnywhere, Category = "Enemigos")
	FTNHazardEffect Quad;

	/** Erizos checos (ATN_BeachTankTrap): 15 al chocar. */
	UPROPERTY(Config, EditAnywhere, Category = "Obstáculos")
	FTNHazardEffect TankTrap;

	/** Algas (ATN_BeachSeaweed): guantazos que las cortan (la hoja: de un golpe). */
	UPROPERTY(Config, EditAnywhere, Category = "Obstáculos", meta = (ClampMin = "1"))
	int32 SeaweedHitsToCut = 1;

	/** Anélido poliqueto (ATN_ProcAnnelid): vida que cura al cazarlo. */
	UPROPERTY(Config, EditAnywhere, Category = "Aliados", meta = (ClampMin = "0"))
	float AnnelidHeal = 25.f;
};
