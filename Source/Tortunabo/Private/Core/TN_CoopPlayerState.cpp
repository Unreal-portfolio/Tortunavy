#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_CoopGameState.h"
#include "Game/TN_LateJoinRules.h"
#include "Player/TortugaCharacter.h"
#include "World/TN_ScoreShellBurst.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

FTNOnBuggyLookChanged ATN_CoopPlayerState::OnAnyBuggyLookChanged;

ATN_CoopPlayerState::ATN_CoopPlayerState()
{
	// 5 Hz (1 Hz en reposo con la frecuencia adaptativa): con ocho jugadores, 30 Hz por PlayerState eran casi 1700 miradas
	// por segundo en el anfitrión para datos que cambian poco. Lo que tiene que llegar ya (llegada, muerte, derribo, revive,
	// conchas de la carrera, puntos) lo empuja quien lo cambia con ForceNetUpdate.
	SetNetUpdateFrequency(5.f);
	SetMinNetUpdateFrequency(1.f);
}

bool ATN_CoopPlayerState::CanServerSendQuickChat(float Now, float CooldownSeconds) const
{
	return (Now - ServerLastQuickChatTime) >= CooldownSeconds;
}

void ATN_CoopPlayerState::MarkServerQuickChatSent(float Now)
{
	ServerLastQuickChatTime = Now;
}

bool ATN_CoopPlayerState::CanServerPlayEmote(uint8 EmoteID, float Now, float CooldownSeconds) const
{
	const float* LastUse = ServerLastEmoteTimes.Find(EmoteID);
	return !LastUse || ((Now - *LastUse) >= CooldownSeconds);
}

void ATN_CoopPlayerState::MarkServerEmotePlayed(uint8 EmoteID, float Now)
{
	ServerLastEmoteTimes.FindOrAdd(EmoteID) = Now;
}

void ATN_CoopPlayerState::OnRep_EquippedHelmetId()
{
	// Actualiza el mesh del casco en el personaje local cuando el servidor replica el cambio.
	// GetPawn() puede ser null si el PlayerState se recibe antes de la posesión del pawn.
	if (ATortugaCharacter* TurtleChar = Cast<ATortugaCharacter>(GetPawn()))
	{
		TurtleChar->UpdateHelmetMesh(EquippedHelmetId);
	}
}

void ATN_CoopPlayerState::OnRep_EquippedSkinId()
{
	if (ATortugaCharacter* TurtleChar = Cast<ATortugaCharacter>(GetPawn()))
	{
		TurtleChar->UpdateSkinVisual(EquippedSkinId);
	}
}

void ATN_CoopPlayerState::OnRep_EquippedShellId()
{
	if (ATortugaCharacter* TurtleChar = Cast<ATortugaCharacter>(GetPawn()))
	{
		TurtleChar->UpdateSkinVisual(EquippedSkinId);
	}
}

void ATN_CoopPlayerState::OnRep_EquippedEyesId()
{
	if (ATortugaCharacter* TurtleChar = Cast<ATortugaCharacter>(GetPawn()))
	{
		TurtleChar->UpdateSkinVisual(EquippedSkinId);
	}
}

void ATN_CoopPlayerState::SetEquippedBuggyLook(const FTN_BuggyLook& Look)
{
	if (!HasAuthority())
	{
		return;
	}
	EquippedBuggyLook = Look;
	ForceNetUpdate();
	OnRep_EquippedBuggyLook();
}

void ATN_CoopPlayerState::OnRep_EquippedBuggyLook()
{
	// El buggy que conduzca se repinta ya, sin esperar a su repaso periódico.
	OnAnyBuggyLookChanged.Broadcast(this);
}

void ATN_CoopPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATN_CoopPlayerState, bIsInReadyZone);
	DOREPLIFETIME(ATN_CoopPlayerState, bHasFinishedRun);
	DOREPLIFETIME(ATN_CoopPlayerState, bIsAlive);
	DOREPLIFETIME(ATN_CoopPlayerState, bIsDBNO);
	DOREPLIFETIME_CONDITION(ATN_CoopPlayerState, DBNOBleedoutTimeRemaining, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ATN_CoopPlayerState, DeathZoneTimeRemaining, COND_OwnerOnly);
	DOREPLIFETIME(ATN_CoopPlayerState, EquippedHelmetId);
	DOREPLIFETIME(ATN_CoopPlayerState, EquippedSkinId);
	DOREPLIFETIME(ATN_CoopPlayerState, EquippedShellId);
	DOREPLIFETIME(ATN_CoopPlayerState, EquippedEyesId);
	DOREPLIFETIME(ATN_CoopPlayerState, EquippedBuggyLook);
	DOREPLIFETIME(ATN_CoopPlayerState, FinishTimeSeconds);
	DOREPLIFETIME(ATN_CoopPlayerState, FinishRank);
	DOREPLIFETIME(ATN_CoopPlayerState, bIsEliminated);
	DOREPLIFETIME(ATN_CoopPlayerState, RaceScore);
	DOREPLIFETIME(ATN_CoopPlayerState, RoundWins);
	DOREPLIFETIME(ATN_CoopPlayerState, RaceShellHalves);
	DOREPLIFETIME(ATN_CoopPlayerState, TeamIndex);
	DOREPLIFETIME(ATN_CoopPlayerState, TurtleDollsCollected);
}

void ATN_CoopPlayerState::OnRep_RaceScore()
{
	OnRaceScoreChanged.Broadcast(RaceScore);

	// Race de replicación: si Results llegó ANTES que este update del score, la
	// persistencia se hizo con un valor stale. Reinvocar — persiste por delta
	// (idempotente) y solo actúa en Results y sobre el PlayerState local.
	if (ATN_CoopGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATN_CoopGameState>() : nullptr)
	{
		GS->PersistLocalPlayerScoreIfResults();
	}
}

void ATN_CoopPlayerState::AddRaceScore(int32 Delta)
{
	if (!HasAuthority() || Delta == 0)
	{
		return;
	}

	RaceScore += Delta;
	ForceNetUpdate();

	// Listen-server: OnRep_RaceScore no llega a la máquina con autoridad (el host),
	// así que difundimos manualmente para refrescar su propio HUD. En clientes remotos
	// el cambio llega vía replicación → OnRep_RaceScore (no se llama este método allí).
	OnRaceScoreChanged.Broadcast(RaceScore);
}

void ATN_CoopPlayerState::MulticastScoreShellCollected_Implementation(FVector_NetQuantize10 WorldLocation, uint8 Tier, int32 Value)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// Destello, chispas y «¡plin!» en el sitio (solo si la cámara local está a menos de 150 m).
	ATN_ScoreShellBurst::SpawnAt(World, WorldLocation, Tier);
	// El dueño de este PlayerState es su PlayerController, que solo existe en el servidor y en la máquina del jugador.
	const APlayerController* PC = GetPlayerController();
	if (PC && PC->IsLocalController())
	{
		OnScoreShellCollected.Broadcast(Value, Tier, WorldLocation);
	}
}

void ATN_CoopPlayerState::SeamlessTravelTo(APlayerState* NewPlayerState)
{
	TGuardValue<bool> TravelGuard(bCopyingForSeamlessTravel, true);
	Super::SeamlessTravelTo(NewPlayerState);
}

void ATN_CoopPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	ATN_CoopPlayerState* Target = Cast<ATN_CoopPlayerState>(PlayerState);
	if (!Target || bCopyingForSeamlessTravel)
	{
		return;
	}

	// Lo copia AGameMode::AddInactivePlayer (Duplicate) al desconectarse: es lo que recupera quien vuelve a la sala.
	FTNReconnectState State;
	State.bIsAlive = bIsAlive;
	State.bIsDBNO = bIsDBNO;
	State.bHasFinishedRun = bHasFinishedRun;
	State.bIsEliminated = bIsEliminated;
	const FTNReconnectState Saved = TNLateJoinLogic::SanitizeForReconnect(State);

	Target->bIsAlive = Saved.bIsAlive;
	Target->bIsDBNO = Saved.bIsDBNO;
	Target->bHasFinishedRun = Saved.bHasFinishedRun;
	Target->bIsEliminated = Saved.bIsEliminated;
	Target->FinishRank = FinishRank;
	Target->FinishTimeSeconds = FinishTimeSeconds;
	Target->RaceScore = RaceScore;
	Target->RoundWins = RoundWins;
	Target->RaceShellHalves = RaceShellHalves;
	Target->TeamIndex = TeamIndex;
	Target->TurtleDollsCollected = TurtleDollsCollected;
	Target->EquippedHelmetId = EquippedHelmetId;
	Target->EquippedSkinId = EquippedSkinId;
	Target->EquippedShellId = EquippedShellId;
	Target->EquippedEyesId = EquippedEyesId;
}

void ATN_CoopPlayerState::OnRep_TurtleDollsCollected()
{
	if (ATN_CoopGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATN_CoopGameState>() : nullptr)
	{
		GS->PersistLocalPlayerScoreIfResults();
	}
}

void ATN_CoopPlayerState::AddTurtleDoll()
{
	if (!HasAuthority())
	{
		return;
	}
	++TurtleDollsCollected;
	ForceNetUpdate();
}

void ATN_CoopPlayerState::ResetForNewRace()
{
	bIsAlive = true;
	bHasFinishedRun = false;
	bIsDBNO = false;
	DBNOBleedoutTimeRemaining = -1.f;
	FinishRank = 0;
	bIsEliminated = false;
	FinishTimeSeconds = -1.f;
	DeathZoneTimeRemaining = -1.f;
	RaceScore = 0;
}

