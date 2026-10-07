#pragma once

#include "CoreMinimal.h"
#include "World/TN_CrabActor.h"

/**
 * Enemigos que crean las pruebas de estrés (TN.Stress y TN.Stress caos) aparte de los elementos de la playa: el cangrejo
 * de patrulla del Excel (ATN_CrabActor), con su Blueprint si existe (malla y sonidos del equipo) o la clase nativa.
 */
namespace TNStressEnemies
{
	inline TSubclassOf<ATN_CrabActor> PatrolCrabClass()
	{
		UClass* Class = LoadClass<ATN_CrabActor>(nullptr, TEXT("/Game/Blueprints/Gameplay/Enemies/Crabs/BP_CrabActor.BP_CrabActor_C"),
			nullptr, LOAD_NoWarn);
		return Class ? Class : ATN_CrabActor::StaticClass();
	}
}
