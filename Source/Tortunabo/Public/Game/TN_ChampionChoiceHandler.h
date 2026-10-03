#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Game/TN_BeachRaceGameMode.h"
#include "TN_ChampionChoiceHandler.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UTN_ChampionChoiceHandler : public UInterface
{
	GENERATED_BODY()
};

/**
 * Un GameMode que no es el de la carrera en la playa y usa su pantalla de la campeona (UTN_RaceChampionWidget, con los botones
 * «Volver a jugar», «Cambiar de modo» y «Salir»), p. ej. ATN_TctGameMode. ATN_BeachRaceGameMode::RequestChampionChoice y
 * CanLocalPlayerChoose le pasan la elección del anfitrión cuando el GameMode del mundo lo implementa.
 */
class TORTUNABO_API ITN_ChampionChoiceHandler
{
	GENERATED_BODY()

public:
	/** Servidor: aplica la elección del anfitrión en la pantalla de la campeona. false si ahora no se puede. */
	virtual bool HandleChampionChoice(ETNBeachChampionChoice Choice) = 0;

	/** Servidor: la partida ha acabado y aún no se está saliendo (el anfitrión puede elegir). */
	virtual bool CanChooseChampion() const = 0;
};
