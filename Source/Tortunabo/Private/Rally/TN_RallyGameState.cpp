#include "Rally/TN_RallyGameState.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Crc.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "Rally/TN_RallyAmmoBox.h"
#include "Rally/TN_RallyLogic.h"
#include "Rally/TN_RallyPlayerState.h"
#include "Rally/TN_RallyTrack.h"
#include "Rally/TN_RallyTrackDressing.h"
#include "Rally/TN_RallyVehicle.h"
#include "World/TN_MapVariantLoader.h"

void ATN_RallyGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_RallyGameState, Phase);
	DOREPLIFETIME(ATN_RallyGameState, StartServerTime);
	DOREPLIFETIME(ATN_RallyGameState, PhaseEndServerTime);
	DOREPLIFETIME(ATN_RallyGameState, Laps);
	DOREPLIFETIME(ATN_RallyGameState, NumGates);
	DOREPLIFETIME(ATN_RallyGameState, bCircuit);
	DOREPLIFETIME(ATN_RallyGameState, Variant);
	DOREPLIFETIME(ATN_RallyGameState, Standings);
}

const FTNRallyStanding* ATN_RallyGameState::FindStandingForPlayer(const APlayerState* Player) const
{
	if (!Player)
	{
		return nullptr;
	}
	return Standings.FindByPredicate([Player](const FTNRallyStanding& Entry) { return Entry.Driver == Player || Entry.Gunner == Player; });
}

const FTNRallyStanding* ATN_RallyGameState::FindStandingForVehicle(const APawn* Vehicle) const
{
	if (!Vehicle)
	{
		return nullptr;
	}
	return Standings.FindByPredicate([Vehicle](const FTNRallyStanding& Entry) { return Entry.Vehicle == Vehicle; });
}

namespace
{
	/** Un cliente que llega tarde no avisa de reapariciones más viejas que esto (s). */
	constexpr float RallyRespawnNoticeMaxAgeSeconds = 3.f;

	/** Terreno de la variante cargado: usa el cargador del nivel (o crea uno) y lo recarga si tiene otra variante o está vacío. */
	void EnsureVariantTerrain(UWorld& World, FName InVariant)
	{
		ATN_MapVariantLoader* Loader = nullptr;
		for (TActorIterator<ATN_MapVariantLoader> It(&World); It; ++It)
		{
			Loader = *It;
			break;
		}
		if (!Loader && !InVariant.IsNone())
		{
			// Nivel sin cargador (p. ej. un mapa de pruebas): se crea con la variante ya puesta, así OnConstruction carga el
			// terreno y BeginPlay pone las zonas de muerte de la misma variante.
			Loader = World.SpawnActorDeferred<ATN_MapVariantLoader>(ATN_MapVariantLoader::StaticClass(), FTransform::Identity, nullptr,
				nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Loader)
			{
				Loader->Variant = InVariant;
				Loader->FinishSpawning(FTransform::Identity);
			}
		}
		if (Loader && !InVariant.IsNone())
		{
			TArray<UProceduralMeshComponent*> Chunks;
			Loader->GetComponents(Chunks);
			if (Loader->Variant != InVariant || Chunks.Num() == 0)
			{
				Loader->Variant = InVariant;
				Loader->Recargar();
			}
		}
	}
}

void ATN_RallyGameState::NotifyTeamRespawned(int32 TeamIndex, ETNRallyRespawnReason Reason, float ServerTime)
{
	NotifiedRespawnTimes.Add(TeamIndex, ServerTime);
	OnTeamRespawned.Broadcast(TeamIndex, Reason);
}

void ATN_RallyGameState::OnRep_Standings()
{
	const float Now = static_cast<float>(GetServerWorldTimeSeconds());
	for (const FTNRallyStanding& Entry : Standings)
	{
		if (Entry.LastRespawnReason == ETNRallyRespawnReason::None || Entry.LastRespawnServerTime <= 0.f)
		{
			continue;
		}
		const float* Notified = NotifiedRespawnTimes.Find(Entry.TeamIndex);
		if (Notified && *Notified >= Entry.LastRespawnServerTime)
		{
			continue;
		}
		NotifiedRespawnTimes.Add(Entry.TeamIndex, Entry.LastRespawnServerTime);
		if (Now - Entry.LastRespawnServerTime <= RallyRespawnNoticeMaxAgeSeconds)
		{
			OnTeamRespawned.Broadcast(Entry.TeamIndex, Entry.LastRespawnReason);
		}
	}
}

void ATN_RallyGameState::OnRep_Variant()
{
	// En el servidor la pista la prepara el GameMode antes de que empiece la partida.
	if (!HasAuthority() && !Variant.IsNone())
	{
		PrepareTrack(Variant);
	}
}

ATN_RallyTrack* ATN_RallyGameState::PrepareTrack(FName InVariant)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	EnsureVariantTerrain(*World, InVariant);

	if (!Track)
	{
		for (TActorIterator<ATN_RallyTrack> It(World); It; ++It)
		{
			Track = *It;
			break;
		}
	}
	if (!Track)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Track = World->SpawnActor<ATN_RallyTrack>(ATN_RallyTrack::StaticClass(), FTransform::Identity, Params);
	}
	if (Track)
	{
		Track->BuildFromVariant(InVariant);
		// Límites, decorado y público: deterministas por variante, así el servidor y cada cliente construyen lo mismo.
		ATN_RallyTrackDressing::BuildForTrack(Track, static_cast<int32>(FCrc::StrCrc32(*InVariant.ToString())));
		OnTrackReady.Broadcast();
	}
	return Track;
}

FString ATN_RallyGameState::DescribeStatus() const
{
	const UEnum* PhaseEnum = StaticEnum<ETNRallyPhase>();
	// Cajas de munición en este mundo: propias (autoridad local) y replicadas. Un cliente solo debe tener replicadas (las
	// relevantes, a menos de NetCullDistance); una propia en un cliente es una caja local duplicada.
	int32 OwnBoxes = 0;
	int32 ReplicatedBoxes = 0;
	if (const UWorld* World = GetWorld())
	{
		for (TActorIterator<ATN_RallyAmmoBox> It(World); It; ++It)
		{
			++(It->GetLocalRole() == ROLE_Authority ? OwnBoxes : ReplicatedBoxes);
		}
	}
	FString Text = FString::Printf(TEXT("Rally: fase %s, variante %s, %s, %d vueltas, %d puertas, %d buggies, cajas %d propias y %d replicadas"),
		*PhaseEnum->GetNameStringByValue(static_cast<int64>(Phase)), *Variant.ToString(),
		bCircuit ? TEXT("circuito") : TEXT("punto a punto"), Laps, NumGates, Standings.Num(), OwnBoxes, ReplicatedBoxes);
	for (const FTNRallyStanding& Entry : Standings)
	{
		const FString DriverName = Entry.Driver ? Entry.Driver->GetPlayerName() : TEXT("-");
		const FString GunnerName = Entry.Gunner ? Entry.Gunner->GetPlayerName() : TEXT("-");
		const ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Entry.Vehicle);
		const FVector Where = Entry.Vehicle ? Entry.Vehicle->GetActorLocation() : FVector::ZeroVector;
		const FString Motion = RallyVehicle ? FString::Printf(TEXT(" %.0f km/h en (%.0f, %.0f, %.0f)"),
			TNRally::CmsToKmh(RallyVehicle->GetForwardSpeedCms()), Where.X, Where.Y, Where.Z) : FString(TEXT(" sin buggy"));
		Text += FString::Printf(TEXT("\n  %d. equipo %d [%s / %s]%s vuelta %d puerta %d%s%s%s puntos %d%s"),
			Entry.Place, Entry.TeamIndex, *DriverName, *GunnerName, Entry.bBot ? TEXT(" (IA)") : TEXT(""),
			Entry.Lap, Entry.NextGate,
			Entry.bFinished ? *FString::Printf(TEXT(" META %.2f s"), Entry.FinishSeconds) : TEXT(""),
			Entry.bWrongWay ? TEXT(" CONTRAMANO") : TEXT(""), Entry.bRetired ? TEXT(" RETIRADO") : TEXT(""), Entry.Points, *Motion);
	}
	// Plaza y cosméticos replicados de cada jugador (en un cliente, lo que le ha llegado).
	for (const APlayerState* Player : PlayerArray)
	{
		const ATN_RallyPlayerState* Rally = Cast<ATN_RallyPlayerState>(Player);
		if (!Rally)
		{
			continue;
		}
		Text += FString::Printf(TEXT("\n  jugador %s: %s equipo %d, color=%s caparazón=%s ojos=%s casco=%s"),
			*Rally->GetPlayerName(), Rally->IsSeated() ? (Rally->IsGunner() ? TEXT("artillera") : TEXT("conductora")) : TEXT("sin plaza"),
			Rally->GetRallyTeamIndex(), *Rally->EquippedSkinId.ToString(), *Rally->EquippedShellId.ToString(),
			*Rally->EquippedEyesId.ToString(), *Rally->EquippedHelmetId.ToString());
	}
	return Text;
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld GTNRallyStatusCommand(
	TEXT("TN.Rally.Status"),
	TEXT("Imprime en LogTNRally la fase de la carrera del Rally y los puestos."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		const ATN_RallyGameState* GameState = World ? World->GetGameState<ATN_RallyGameState>() : nullptr;
		if (!GameState)
		{
			UE_LOG(LogTNRally, Display, TEXT("TN.Rally.Status: este mundo no tiene una partida de Rally."));
			return;
		}
		UE_LOG(LogTNRally, Display, TEXT("%s"), *GameState->DescribeStatus());
	}));
#endif
