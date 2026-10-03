#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Settings/TN_SettingsSaveGame.h"
#include "TN_LocalPlayerProfile.generated.h"

class APlayerController;
class ULocalPlayer;
class UTN_CosmeticSaveGame;

/**
 * @brief Lo de cada jugador local que no se guarda (modo local, #311). Vive lo que vive su jugador local (ULocalPlayer): toda
 * la partida, con los viajes del lobby a la partida y de vuelta, y se va con él al salir o al volver al menú.
 *
 * Solo lo usan los invitados (jugadores 2 a 4): su número, su aspecto (empiezan con el de serie; lo que cambien en el
 * probador o la tienda dura la partida) y sus ajustes de jugador (cámara, controles; los del PC son del jugador 1). El
 * jugador 1 usa su perfil guardado de siempre (UMP_GameInstance, UTN_GameSettingsSubsystem). Docs/Modo_Local.md.
 */
UCLASS()
class TORTUNABO_API UTN_LocalPlayerProfile : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/** El perfil del jugador local de PC (null si PC no es local). */
	static UTN_LocalPlayerProfile* Get(const APlayerController* PC);
	static UTN_LocalPlayerProfile* Get(const ULocalPlayer* Player);

	/** Número del jugador en la partida local (1 a 4; 0 hasta que se le da uno). Se queda aunque otro se vaya. */
	int32 GetPlayerNumber() const { return PlayerNumber; }
	void SetPlayerNumber(int32 InNumber) { PlayerNumber = InNumber; }

	/**
	 * Aspecto del invitado: se crea al pedirlo con el de serie (los cascos de DefaultHelmets desbloqueados y el primero
	 * puesto, sin color, caparazón ni ojos) y nunca se escribe en disco.
	 */
	UTN_CosmeticSaveGame* GetGuestCosmetics(const TArray<FName>& DefaultHelmets);

	/** Ajustes de jugador del invitado (cámara, controles; empiezan de serie). Solo en memoria. */
	FTNGameSettings& GetGuestSettings() { return GuestSettings; }
	const FTNGameSettings& GetGuestSettings() const { return GuestSettings; }

private:
	UPROPERTY(Transient)
	TObjectPtr<UTN_CosmeticSaveGame> GuestCosmetics;

	FTNGameSettings GuestSettings;

	int32 PlayerNumber = 0;
};
