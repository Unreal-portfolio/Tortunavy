#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Core/TN_CosmeticsTypes.h"
#include "Multiplayer/TN_SaveGameDecisions.h"
#include "TN_CosmeticSaveGame.generated.h"

/**
 * @brief SaveGame con el perfil cosmético del jugador local.
 *
 * Guarda cascos desbloqueados, equipados (helmet y skin) y el score
 * acumulado entre carreras. Persistido por UMP_GameInstance en la ranura de
 * TNCosmeticSlot::SlotFor: CosmeticSaveSlotPrefix + SteamID64 con Steam, o _Local sin él.
 */
UCLASS()
class TORTUNABO_API UTN_CosmeticSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/**
	 * Versión del formato (TNSaveLogic::COSMETIC_SAVE_VERSION). 0 = guardado anterior al campo: se migra al cargar.
	 * Primera propiedad a propósito: junto con bWriteComplete (la última) detecta ficheros truncados.
	 */
	UPROPERTY()
	int32 SaveVersion = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Cosmetics")
	TArray<FName> UnlockedHelmetIds;

	UPROPERTY(BlueprintReadWrite, Category = "Cosmetics")
	FName EquippedHelmetId = NAME_None;

	/** ID del skin de personaje activo. NAME_None = aspecto por defecto. */
	UPROPERTY(BlueprintReadWrite, Category = "Cosmetics")
	FName EquippedSkinId = NAME_None;

	/** Colores y caparazones desbloqueados en la tienda (filas de DT_Skins). */
	UPROPERTY(BlueprintReadWrite, Category = "Cosmetics")
	TArray<FName> UnlockedSkinIds;

	/** Caparazón equipado (fila de DT_Skins de categoría Shell). NAME_None = el de serie. */
	UPROPERTY(BlueprintReadWrite, Category = "Cosmetics")
	FName EquippedShellId = NAME_None;

	/** Ojos equipados (fila de DT_Skins de categoría Eyes). NAME_None = los clásicos. */
	UPROPERTY(BlueprintReadWrite, Category = "Cosmetics")
	FName EquippedEyesId = NAME_None;

	/** Modelos y pinturas del buggy del Rally comprados en la tienda (Ids de TNBuggyCosmetics). */
	UPROPERTY(BlueprintReadWrite, Category = "Cosmetics")
	TArray<FName> UnlockedBuggyIds;

	/** Buggy equipado en el probador (modelo y pintura). NAME_None = el de serie. */
	UPROPERTY(BlueprintReadWrite, Category = "Cosmetics")
	FTN_BuggyLook EquippedBuggyLook;

	/**
	 * Puntos de carrera acumulados (#26).
	 * Se suman al terminar cada carrera según posición de llegada.
	 * 1º=400, 2º=300, 3º=200, 4º=100. Eliminados = 0.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Score")
	int32 AccumulatedRaceScore = 0;

	/**
	 * En el perfil de la máquina (_Local), la cuenta de Steam que lo heredó (#83, TNCosmeticSlot). Vacío = sin heredar:
	 * la primera cuenta que entre se lo queda; las demás empiezan de cero.
	 */
	UPROPERTY()
	FString ClaimedByAccountId;

	/** Marca de fin (última propiedad a propósito). Falta en un fichero truncado. */
	UPROPERTY()
	bool bWriteComplete = false;

	/** @brief Sella el perfil con la versión actual y la marca de fin (perfil nuevo o recién migrado). */
	void StampCurrentVersion()
	{
		SaveVersion = TNSaveLogic::COSMETIC_SAVE_VERSION;
		bWriteComplete = true;
	}

	/** @brief false si el fichero del que sale estaba truncado. */
	bool IsIntact() const
	{
		return !TNSaveLogic::IsTruncated(SaveVersion, bWriteComplete);
	}
};
