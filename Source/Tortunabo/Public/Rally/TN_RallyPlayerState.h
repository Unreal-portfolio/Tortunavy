// PlayerState del Rally: hereda el del juego (nombre de Steam y cosméticos de la tortuga, que pinta el buggy) y añade la
// plaza que ocupa en su buggy. Las dos ocupantes de un buggy comparten interfono de voz (ITN_VoiceIntercom, #329).
#pragma once

#include "CoreMinimal.h"
#include "Core/TN_CoopPlayerState.h"
#include "Rally/TN_RallyVehicle.h"
#include "Voice/TN_VoiceRouting.h"
#include "TN_RallyPlayerState.generated.h"

UCLASS()
class TORTUNABO_API ATN_RallyPlayerState : public ATN_CoopPlayerState, public ITN_VoiceIntercom
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Servidor: plaza y equipo (INDEX_NONE = sin buggy, mira la carrera). */
	void SetRallySeat(int32 InTeamIndex, ETNRallySeat InSeat);
	void ClearRallySeat();

	UFUNCTION(BlueprintPure, Category = "Rally")
	bool IsSeated() const { return RallyTeamIndex != INDEX_NONE; }

	UFUNCTION(BlueprintPure, Category = "Rally")
	bool IsGunner() const { return IsSeated() && RallySeat == ETNRallySeat::Gunner; }

	UFUNCTION(BlueprintPure, Category = "Rally")
	int32 GetRallyTeamIndex() const { return RallyTeamIndex; }

	UFUNCTION(BlueprintPure, Category = "Rally")
	ETNRallySeat GetRallySeat() const { return RallySeat; }

	/** Interfono de voz: el equipo del buggy en que va sentada (INDEX_NONE mirando la carrera). */
	virtual int32 GetVoiceIntercomGroup() const override { return RallyTeamIndex; }

private:
	UPROPERTY(Replicated)
	int32 RallyTeamIndex = INDEX_NONE;

	UPROPERTY(Replicated)
	ETNRallySeat RallySeat = ETNRallySeat::Driver;
};
