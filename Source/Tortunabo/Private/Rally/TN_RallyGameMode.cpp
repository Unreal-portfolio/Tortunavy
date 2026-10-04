// ATN_RallyGameMode: opciones, emparejado en buggies y fases. El progreso de carrera (puertas, puestos, reaparición) está
// en TN_RallyGameModeRace.cpp.
#include "Rally/TN_RallyGameMode.h"

#include "ChaosVehicleWheel.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kart/TN_KartPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Rally/TN_RallyAIController.h"
#include "Rally/TN_RallyKartBuggy.h"
#include "Rally/TN_RallyPlayerController.h"
#include "Rally/TN_RallyPlayerState.h"
#include "Rally/TN_RallyTrack.h"
#include "Rally/TN_RallyVehicle.h"

#include <limits>

namespace
{
	TAutoConsoleVariable<int32> CVarRallyBotDriver(TEXT("TN.Rally.BotDriver"), 0,
		TEXT("Rally (servidor): 1 = cada jugadora entra de artillera y su buggy lo conduce el piloto IA (como ?BotDriver)."),
		ECVF_Default);

	/**
	 * Altura del origen del vehículo sobre el punto más bajo de sus ruedas (hueso de cada rueda menos su radio). Sin ruedas
	 * Chaos, la caja de colisión del actor. NaN si no se puede medir (TNRallyRace::RestingLiftCm usa entonces la de reserva).
	 */
	double MeasureOriginAboveBottomCm(const APawn& Vehicle)
	{
		const UChaosWheeledVehicleMovementComponent* Move = Vehicle.FindComponentByClass<UChaosWheeledVehicleMovementComponent>();
		const USkinnedMeshComponent* Mesh = Cast<USkinnedMeshComponent>(Vehicle.GetRootComponent());
		double Lowest = TNumericLimits<double>::Max();
		for (int32 Index = 0; Move && Mesh && Index < Move->WheelSetups.Num(); ++Index)
		{
			const FChaosWheelSetup& Setup = Move->WheelSetups[Index];
			const UChaosVehicleWheel* Wheel = Move->Wheels.IsValidIndex(Index) && Move->Wheels[Index] ? Move->Wheels[Index].Get()
				: (Setup.WheelClass ? Setup.WheelClass->GetDefaultObject<UChaosVehicleWheel>() : nullptr);
			if (Wheel && !Setup.BoneName.IsNone() && Mesh->GetBoneIndex(Setup.BoneName) != INDEX_NONE)
			{
				Lowest = FMath::Min(Lowest, Mesh->GetSocketLocation(Setup.BoneName).Z - Wheel->WheelRadius);
			}
		}
		if (Lowest == TNumericLimits<double>::Max())
		{
			FVector Origin;
			FVector Extent;
			Vehicle.GetActorBounds(true, Origin, Extent);
			if (Extent.IsNearlyZero())
			{
				return std::numeric_limits<double>::quiet_NaN();
			}
			Lowest = Origin.Z - Extent.Z;
		}
		return Vehicle.GetActorLocation().Z - Lowest;
	}
}

ATN_RallyGameMode::ATN_RallyGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	GameStateClass = ATN_RallyGameState::StaticClass();
	PlayerStateClass = ATN_RallyPlayerState::StaticClass();
	// El del Rally más el HUD del buggy (peso de la artillera y, en el mapa generado, los kilómetros que quedan).
	PlayerControllerClass = ATN_KartPlayerController::StaticClass();
	// Nadie nace como tortuga: cada jugador se sienta en un buggy (o mira la carrera).
	DefaultPawnClass = nullptr;
	AIControllerClass = ATN_RallyAIController::StaticClass();
	// #631: el buggy de Karts (mirada libre de la conductora y peso de la artillera) sin sus objetos: las cajas «?» dan
	// munición de la torreta (#629). ATN_KartGameMode pone el de Karts, con objetos.
	VehicleClass = TSoftClassPtr<APawn>(ATN_RallyKartBuggy::StaticClass());
	// Del lobby se llega y al lobby se vuelve sin cortar la conexión.
	bUseSeamlessTravel = true;
}

void ATN_RallyGameMode::InitGame(const FString& MapName, const FString& UrlOptions, FString& ErrorMessage)
{
	Super::InitGame(MapName, UrlOptions, ErrorMessage);
	const FString Options = ResolveLobbyOptions(UrlOptions);
	const FString VariantOption = UGameplayStatics::ParseOption(Options, TEXT("Variant"));
	Variant = VariantOption.IsEmpty() ? DefaultVariant : FName(*VariantOption);
	Seats = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Seats"), 2), 1, 2);
	Bots = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Bots"), 0), 0, TNRally::MaxGridSlots);
	bLapsFromUrl = UGameplayStatics::HasOption(Options, TEXT("Laps"));
	Laps = FMath::Clamp(UGameplayStatics::GetIntOption(Options, TEXT("Laps"), DefaultLaps), 1, 9);
	bAutoStart = UGameplayStatics::HasOption(Options, TEXT("AutoStart"));
	RaceTimeoutSeconds = FMath::Max(0, UGameplayStatics::GetIntOption(Options, TEXT("RaceTimeout"), 0));
	RaceLimit = FMath::Max(0, UGameplayStatics::GetIntOption(Options, TEXT("Races"), 0));
	bBotDriverFromUrl = UGameplayStatics::HasOption(Options, TEXT("BotDriver"));
	UE_LOG(LogTNRally, Log, TEXT("[RallyGameMode] %s: variante %s, %d plaza(s) por buggy, %d bots (%s), %d vueltas%s%s."),
		*MapName, *Variant.ToString(), Seats, Bots, *UEnum::GetValueAsString(Difficulty), Laps, bAutoStart ? TEXT(", salida sin jugadoras") : TEXT(""),
		bBotDriverFromUrl ? TEXT(", artilleras con piloto IA") : TEXT(""));
}

void ATN_RallyGameMode::StartPlay()
{
	// La pista se construye antes del BeginPlay de los actores: el terreno de la variante tiene que estar cargado para que
	// las puertas, los bordes y la parrilla encuentren el suelo.
	if (ATN_RallyGameState* RallyState = GetRallyGameState())
	{
		RallyState->Variant = Variant;
		Track = RallyState->PrepareTrack(Variant);
		bTrackReady = Track && Track->IsBuilt();
		if (bTrackReady && !bLapsFromUrl && Track->GetManifestLaps() > 0)
		{
			Laps = FMath::Clamp(Track->GetManifestLaps(), 1, 9);
		}
		RallyState->bCircuit = bTrackReady && Track->IsCircuit();
		RallyState->NumGates = bTrackReady ? Track->GetGateCount() : 0;
		RallyState->Laps = RallyState->bCircuit ? Laps : 1;
		RallyState->Phase = ETNRallyPhase::Warmup;
	}
	if (!bTrackReady)
	{
		UE_LOG(LogTNRally, Error, TEXT("[RallyGameMode] Sin pista para la variante '%s': la carrera no empieza."), *Variant.ToString());
	}

	Super::StartPlay();
	PlayStartTime = Now();

	TArray<TWeakObjectPtr<APlayerController>> Waiting = MoveTemp(PendingPlayers);
	PendingPlayers.Reset();
	for (const TWeakObjectPtr<APlayerController>& Player : Waiting)
	{
		if (Player.IsValid())
		{
			bTrackReady ? AssignPlayer(Player.Get()) : Spectate(Player.Get());
		}
	}
	if (bTrackReady)
	{
		SpawnPodium();
		SpawnBots();
		RebuildStandings();
		if (bAutoStart && WarmupEndTime <= 0.0 && Teams.Num() > 0)
		{
			WarmupEndTime = Now() + WarmupSeconds;
		}
	}
}

void ATN_RallyGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// No llama a Super: no hay peón por defecto. El anfitrión llega antes de StartPlay, con la pista aún sin construir.
	if (!HasActorBegunPlay() && !bTrackReady)
	{
		PendingPlayers.AddUnique(NewPlayer);
		return;
	}
	bTrackReady ? AssignPlayer(NewPlayer) : Spectate(NewPlayer);
}

void ATN_RallyGameMode::Logout(AController* Exiting)
{
	PendingPlayers.Remove(Cast<APlayerController>(Exiting));
	// ATN_RallyPlayerController::PawnLeavingGame deja el buggy y el peón de la artillera en su sitio: aquí la jugadora sale de
	// su plaza (UnPossess) y, si conducía y hay artillera, la artillera pasa al volante. Si el buggy queda vacío, CleanupTeams
	// quita el equipo (antes de la salida) o lo retira, y destruye el buggy.
	if (FTeamRuntime* Team = FindTeamByController(Exiting))
	{
		const int32 TeamIndex = Team->TeamIndex;
		if (ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Team->Vehicle.Get()))
		{
			RallyVehicle->UnseatController(Exiting);
		}
		UE_LOG(LogTNRally, Log, TEXT("[RallyGameMode] %s se va del equipo %d."), *GetNameSafe(Exiting), TeamIndex);
	}
	CleanupTeams();
	RebuildStandings();
	Super::Logout(Exiting);
}

bool ATN_RallyGameMode::IsRaceRunning() const
{
	const ATN_RallyGameState* RallyState = GetRallyGameState();
	return RallyState && (RallyState->Phase == ETNRallyPhase::Racing || RallyState->Phase == ETNRallyPhase::Finishing);
}

bool ATN_RallyGameMode::IsBeforeStart() const
{
	const ATN_RallyGameState* RallyState = GetRallyGameState();
	return !RallyState || RallyState->Phase == ETNRallyPhase::Warmup || RallyState->Phase == ETNRallyPhase::Countdown;
}

void ATN_RallyGameMode::CleanupTeams()
{
	const bool bBeforeStart = IsBeforeStart();
	TArray<int32> Removed;
	for (FTeamRuntime& Team : Teams)
	{
		APawn* Vehicle = Team.Vehicle.Get();
		ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Vehicle);
		AController* Driver = RallyVehicle ? RallyVehicle->GetSeatController(ETNRallySeat::Driver) : nullptr;
		// Con piloto IA de prueba (#298), el buggy está vacío cuando se va la artillera: el piloto no cuenta.
		const bool bOccupied = RallyVehicle
			&& ((Driver && !Team.bBotDriver) || RallyVehicle->GetSeatController(ETNRallySeat::Gunner));
		const TNRally::ETeamCleanup Action = TNRally::DecideTeamCleanup(RallyVehicle != nullptr, bOccupied, bBeforeStart, Team.bRetired);
		if (Action == TNRally::ETeamCleanup::Keep)
		{
			continue;
		}
		if (Action == TNRally::ETeamCleanup::Remove)
		{
			Removed.Add(Team.TeamIndex);
		}
		else
		{
			Team.bRetired = true;
		}
		UE_LOG(LogTNRally, Log, TEXT("[RallyGameMode] Equipo %d %s: %s."), Team.TeamIndex,
			Action == TNRally::ETeamCleanup::Remove ? TEXT("fuera de la parrilla") : TEXT("retirado"),
			RallyVehicle ? TEXT("buggy vacío") : TEXT("sin buggy"));
		if (Driver && Team.bBotDriver)
		{
			Driver->Destroy();
		}
		if (Vehicle)
		{
			Vehicle->Destroy();
		}
	}
	if (Removed.Num() > 0)
	{
		Teams.RemoveAll([&Removed](const FTeamRuntime& Team) { return Removed.Contains(Team.TeamIndex); });
	}
}

void ATN_RallyGameMode::ApplyWeaponLocks()
{
	const bool bRunning = IsRaceRunning();
	for (const FTeamRuntime& Team : Teams)
	{
		if (ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Team.Vehicle.Get()))
		{
			// Aparcado en el podio cuenta como retirado: no dispara desde allí.
			RallyVehicle->SetWeaponsLocked(!TNRally::AreWeaponsLive(bRunning, Team.bRetired || Team.bParked));
		}
	}
}

ATN_RallyGameState* ATN_RallyGameMode::GetRallyGameState() const
{
	return GetGameState<ATN_RallyGameState>();
}

double ATN_RallyGameMode::Now() const
{
	const ATN_RallyGameState* RallyState = GetRallyGameState();
	return RallyState ? RallyState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

TNRally::FLapRules ATN_RallyGameMode::MakeLapRules() const
{
	TNRally::FLapRules Rules;
	Rules.NumGates = Track ? Track->GetGateCount() : 0;
	Rules.bCircuit = Track && Track->IsCircuit();
	Rules.Laps = Rules.bCircuit ? Laps : 1;
	return Rules;
}

UTN_RallyRaceCounter* ATN_RallyGameMode::GetRaceCounter() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UTN_RallyRaceCounter>() : nullptr;
}

// ---- Jugadores y equipos ----

void ATN_RallyGameMode::AssignPlayer(APlayerController* Player)
{
	ATN_RallyPlayerState* RallyPlayer = Player ? Player->GetPlayerState<ATN_RallyPlayerState>() : nullptr;
	if (!RallyPlayer)
	{
		return;
	}
	// ?Spectate: la jugadora solo mira (con ?AutoStart la carrera de bots sale sin ella).
	if (UGameplayStatics::HasOption(OptionsString, TEXT("Spectate")))
	{
		Spectate(Player);
		return;
	}
	if (Seats == 2 && TrySeatAsGunner(Player, *RallyPlayer))
	{
		return;
	}
	// Con la carrera en marcha solo se entra de artillera (late-join fuera del MVP).
	const ETNRallyPhase Phase = GetRallyGameState() ? GetRallyGameState()->Phase : ETNRallyPhase::Warmup;
	if (Phase != ETNRallyPhase::Warmup && Phase != ETNRallyPhase::Countdown)
	{
		Spectate(Player);
		return;
	}
	const bool bBotDriver = IsBotDriverMode();
	const int32 Index = CreateTeam(false);
	ITN_RallyVehicle* RallyVehicle = Teams.IsValidIndex(Index) ? Cast<ITN_RallyVehicle>(Teams[Index].Vehicle.Get()) : nullptr;
	const bool bSeated = RallyVehicle
		&& (bBotDriver ? SeatWithBotDriver(Index, Player) : RallyVehicle->SeatController(Player, ETNRallySeat::Driver));
	if (!bSeated)
	{
		if (Teams.IsValidIndex(Index))
		{
			if (APawn* Vehicle = Teams[Index].Vehicle.Get()) { Vehicle->Destroy(); }
			Teams.RemoveAt(Index);
		}
		Spectate(Player);
		return;
	}
	const ETNRallySeat Seat = bBotDriver ? ETNRallySeat::Gunner : ETNRallySeat::Driver;
	RallyPlayer->SetRallySeat(Teams[Index].TeamIndex, Seat);
	UE_LOG(LogTNRally, Log, TEXT("[RallyGameMode] %s %s el equipo %d (hueco %d)."), *RallyPlayer->GetPlayerName(),
		bBotDriver ? TEXT("es la artillera, con piloto IA, en") : TEXT("conduce"), Teams[Index].TeamIndex, Teams[Index].GridSlot);
	OnHumanSeated();
	RebuildStandings();
}

bool ATN_RallyGameMode::TrySeatAsGunner(APlayerController* Player, ATN_RallyPlayerState& RallyPlayer)
{
	// Biplaza: la 2.ª tortuga de cada pareja es la artillera del primer buggy de jugadoras con la plaza libre.
	for (FTeamRuntime& Team : Teams)
	{
		ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Team.Vehicle.Get());
		if (Team.bBot || Team.bBotDriver || Team.bRetired || !RallyVehicle || RallyVehicle->HasFreeSeat(ETNRallySeat::Driver)
			|| !RallyVehicle->HasFreeSeat(ETNRallySeat::Gunner))
		{
			continue;
		}
		if (RallyVehicle->SeatController(Player, ETNRallySeat::Gunner))
		{
			RallyPlayer.SetRallySeat(Team.TeamIndex, ETNRallySeat::Gunner);
			UE_LOG(LogTNRally, Log, TEXT("[RallyGameMode] %s, artillera del equipo %d."), *RallyPlayer.GetPlayerName(), Team.TeamIndex);
			OnHumanSeated();
			RebuildStandings();
			return true;
		}
	}
	return false;
}

bool ATN_RallyGameMode::IsBotDriverMode() const
{
	return bBotDriverFromUrl || CVarRallyBotDriver.GetValueOnGameThread() != 0;
}

bool ATN_RallyGameMode::SeatWithBotDriver(int32 Index, APlayerController* Player)
{
	AController* Pilot = SpawnPilotFor(Index, NSLOCTEXT("Rally", "BotDriverName", "Piloto IA"));
	ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Teams[Index].Vehicle.Get());
	if (!Pilot || !RallyVehicle || !RallyVehicle->SeatController(Player, ETNRallySeat::Gunner))
	{
		UE_LOG(LogTNRally, Error, TEXT("[RallyGameMode] No se puede sentar a %s de artillera con piloto IA."), *GetNameSafe(Player));
		if (Pilot) { Pilot->Destroy(); }
		return false;
	}
	// La torreta es de la artillera: el piloto IA no dispara mientras ella ocupa la plaza (ATN_RallyAIController::TryFire).
	Teams[Index].bBotDriver = true;
	return true;
}

AController* ATN_RallyGameMode::SpawnPilotFor(int32 Index, const FText& PilotName)
{
	UClass* AIClass = AIControllerClass ? AIControllerClass.Get() : ATN_RallyAIController::StaticClass();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AController* Pilot = GetWorld()->SpawnActor<AController>(AIClass, Teams[Index].GridTransform, Params);
	ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Teams[Index].Vehicle.Get());
	if (!Pilot || !RallyVehicle || !RallyVehicle->SeatController(Pilot, ETNRallySeat::Driver))
	{
		if (Pilot) { Pilot->Destroy(); }
		return nullptr;
	}
	if (APlayerState* PilotState = Pilot->GetPlayerState<APlayerState>())
	{
		PilotState->SetPlayerName(PilotName.ToString());
	}
	if (ATN_RallyAIController* RallyPilot = Cast<ATN_RallyAIController>(Pilot))
	{
		ConfigureBot(*RallyPilot);
	}
	return Pilot;
}

int32 ATN_RallyGameMode::FindFreeGridSlot() const
{
	for (int32 Slot = 0; Slot < TNRally::MaxGridSlots; ++Slot)
	{
		if (!Teams.ContainsByPredicate([Slot](const FTeamRuntime& Team) { return Team.GridSlot == Slot; }))
		{
			return Slot;
		}
	}
	return INDEX_NONE;
}

int32 ATN_RallyGameMode::ClaimGridSlot(bool bBot)
{
	const int32 Free = FindFreeGridSlot();
	if (bBot || Free == INDEX_NONE)
	{
		return Free;
	}
	// Jugadoras delante por orden de llegada y la IA detrás: el bot más adelantado que la nueva jugadora le cede su hueco.
	// Se hace al llegar (en el calentamiento), no al empezar el semáforo (#289).
	FTeamRuntime* Ahead = nullptr;
	for (FTeamRuntime& Team : Teams)
	{
		if (Team.bBot && Team.GridSlot < Free && (!Ahead || Team.GridSlot < Ahead->GridSlot))
		{
			Ahead = &Team;
		}
	}
	if (!Ahead)
	{
		return Free;
	}
	const int32 Claimed = Ahead->GridSlot;
	PlaceTeamOnGrid(*Ahead, Free);
	return Claimed;
}

FTransform ATN_RallyGameMode::GetRestingGridTransform(int32 Slot, double OriginAboveBottomCm) const
{
	return Track->GetGridSlotTransform(Slot, TNRallyRace::RestingLiftCm(OriginAboveBottomCm));
}

void ATN_RallyGameMode::PlaceTeamOnGrid(FTeamRuntime& Team, int32 Slot)
{
	Team.GridSlot = Slot;
	Team.GridTransform = GetRestingGridTransform(Slot, Team.OriginAboveBottomCm);
	if (ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Team.Vehicle.Get()))
	{
		RallyVehicle->RallyTeleport(Team.GridTransform, 0.f, 0.f);
	}
	Team.PrevLocation = Team.GridTransform.GetLocation();
	Team.Arc = Track->FindArcGlobal(Team.PrevLocation);
}

int32 ATN_RallyGameMode::CreateTeam(bool bBot)
{
	UClass* Class = VehicleClass.LoadSynchronous();
	if (!Class)
	{
		if (!bLoggedMissingVehicle)
		{
			UE_LOG(LogTNRally, Error, TEXT("[RallyGameMode] No existe la clase del buggy '%s': nadie se sienta."),
				*VehicleClass.ToString());
			bLoggedMissingVehicle = true;
		}
		return INDEX_NONE;
	}
	const int32 Slot = Track ? ClaimGridSlot(bBot) : INDEX_NONE;
	if (Slot == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	// Se crea con la holgura de reserva y, medido el buggy, se apoya en el suelo: así no cae al nacer (#289).
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	APawn* Vehicle = GetWorld()->SpawnActor<APawn>(Class, GetRestingGridTransform(Slot, TNRallyRace::FallbackOriginAboveBottomCm), Params);
	ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Vehicle);
	if (!RallyVehicle)
	{
		UE_LOG(LogTNRally, Error, TEXT("[RallyGameMode] '%s' no implementa ITN_RallyVehicle."), *GetNameSafe(Class));
		if (Vehicle) { Vehicle->Destroy(); }
		return INDEX_NONE;
	}
	FTeamRuntime Team;
	Team.TeamIndex = NextTeamIndex++;
	Team.Vehicle = Vehicle;
	Team.bBot = bBot;
	Team.OriginAboveBottomCm = MeasureOriginAboveBottomCm(*Vehicle);
	PlaceTeamOnGrid(Team, Slot);
	RallyVehicle->SetRallyTeamIndex(Team.TeamIndex);
	RallyVehicle->SetEngineLocked(true);
	RallyVehicle->SetWeaponsLocked(true);
	UE_LOG(LogTNRally, Verbose, TEXT("[RallyGameMode] Equipo %d en el hueco %d, origen a %.0f cm sobre las ruedas."), Team.TeamIndex,
		Slot, Team.OriginAboveBottomCm);
	return Teams.Add(Team);
}

void ATN_RallyGameMode::SpawnBots()
{
	for (int32 Bot = 0; Bot < Bots; ++Bot)
	{
		const int32 Index = CreateTeam(true);
		if (Index == INDEX_NONE)
		{
			break;
		}
		const FText Name = FText::Format(NSLOCTEXT("Rally", "BotName", "Tortuga IA {0}"), FText::AsNumber(Bot + 1));
		if (!SpawnPilotFor(Index, Name))
		{
			UE_LOG(LogTNRally, Error, TEXT("[RallyGameMode] No se puede sentar al piloto IA %d."), Bot + 1);
			if (APawn* Vehicle = Teams[Index].Vehicle.Get()) { Vehicle->Destroy(); }
			Teams.RemoveAt(Index);
			break;
		}
	}
}

ATN_RallyGameMode::FTeamRuntime* ATN_RallyGameMode::FindTeamByController(const AController* Controller)
{
	if (!Controller)
	{
		return nullptr;
	}
	return Teams.FindByPredicate([Controller](const FTeamRuntime& Team)
	{
		const ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Team.Vehicle.Get());
		return RallyVehicle && (RallyVehicle->GetSeatController(ETNRallySeat::Driver) == Controller
			|| RallyVehicle->GetSeatController(ETNRallySeat::Gunner) == Controller);
	});
}

ATN_RallyGameMode::FTeamRuntime* ATN_RallyGameMode::FindTeamByVehicle(const AActor* Vehicle)
{
	return Vehicle ? Teams.FindByPredicate([Vehicle](const FTeamRuntime& Team) { return Team.Vehicle.Get() == Vehicle; }) : nullptr;
}

const ATN_RallyGameMode::FTeamRuntime* ATN_RallyGameMode::FindTeamByVehicle(const AActor* Vehicle) const
{
	return Vehicle ? Teams.FindByPredicate([Vehicle](const FTeamRuntime& Team) { return Team.Vehicle.Get() == Vehicle; }) : nullptr;
}

void ATN_RallyGameMode::Spectate(APlayerController* Player)
{
	if (!Player)
	{
		return;
	}
	if (Track && Track->IsBuilt())
	{
		const FTransform Grid = Track->GetGridSlotTransform(0, 1500.0);
		Player->SetInitialLocationAndRotation(Grid.GetLocation(), Grid.Rotator());
	}
	Player->StartSpectatingOnly();
	UE_LOG(LogTNRally, Log, TEXT("[RallyGameMode] %s mira la carrera."), *GetNameSafe(Player));
}

int32 ATN_RallyGameMode::CountSeatedHumans() const
{
	int32 Count = 0;
	for (const FTeamRuntime& Team : Teams)
	{
		const ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Team.Vehicle.Get());
		if (!RallyVehicle)
		{
			continue;
		}
		Count += Cast<APlayerController>(RallyVehicle->GetSeatController(ETNRallySeat::Driver)) ? 1 : 0;
		Count += Cast<APlayerController>(RallyVehicle->GetSeatController(ETNRallySeat::Gunner)) ? 1 : 0;
	}
	return Count;
}

void ATN_RallyGameMode::OnHumanSeated()
{
	ATN_RallyGameState* RallyState = GetRallyGameState();
	if (!RallyState || RallyState->Phase != ETNRallyPhase::Warmup)
	{
		return;
	}
	const double Time = Now();
	if (FirstSeatTime < 0.0)
	{
		FirstSeatTime = Time;
	}
	// Cada llegada da unos segundos más para que la siguiente se siente, con tope desde la primera. La espera no se publica
	// en el GameState: el único temporizador visible de la salida es el semáforo (#289).
	// Viniendo del lobby, el tope cuenta también desde el principio de la partida: la que tarda en cargar recibe sus segundos.
	double Cap = FirstSeatTime + WarmupMaxSeconds;
	if (ShouldWaitForLobby() && PlayStartTime >= 0.0)
	{
		Cap = FMath::Max(Cap, PlayStartTime + LobbyArrivalMaxSeconds);
	}
	const double Wanted = FMath::Max(WarmupEndTime, Time + WarmupSeconds);
	WarmupEndTime = FMath::Min(Wanted, Cap);
}

// ---- Fases ----

void ATN_RallyGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ATN_RallyGameState* RallyState = GetRallyGameState();
	if (!bTrackReady || !RallyState || bReturning)
	{
		return;
	}
	UpdatePhase();
	const ETNRallyPhase Phase = RallyState->Phase;
	if (Phase == ETNRallyPhase::Warmup || Phase == ETNRallyPhase::Countdown)
	{
		HoldBuggiesOnGrid();
	}
	const bool bRacing = Phase == ETNRallyPhase::Racing || Phase == ETNRallyPhase::Finishing;
	ParkFinishedTeams();
	ConsumeRespawnRequests(bRacing);
	ApplyVehicleHolds(Phase);
	if (bRacing)
	{
		TickProgress();
	}
	EvaluateAccumulator += DeltaSeconds;
	if (EvaluateAccumulator >= EvaluateInterval)
	{
		if (bRacing)
		{
			EvaluateTeams(EvaluateAccumulator);
		}
		CleanupTeams();
		RebuildStandings();
		EvaluateAccumulator = 0.0;
	}
}

void ATN_RallyGameMode::UpdatePhase()
{
	ATN_RallyGameState* RallyState = GetRallyGameState();
	const double Time = Now();
	switch (RallyState->Phase)
	{
	case ETNRallyPhase::Warmup:
	{
		TNRallyRace::FWarmupGate Gate;
		Gate.Now = Time;
		Gate.WarmupEndTime = WarmupEndTime;
		Gate.bHasTeams = Teams.Num() > 0;
		Gate.bWaitForLobby = ShouldWaitForLobby();
		Gate.ExpectedHumans = ExpectedHumans;
		Gate.ArrivedHumans = Gate.bWaitForLobby ? CountArrivedHumans() : 0;
		Gate.WaitedSeconds = PlayStartTime >= 0.0 ? Time - PlayStartTime : 0.0;
		Gate.WaitMaxSeconds = LobbyArrivalMaxSeconds;
		if (TNRallyRace::ShouldEndWarmup(Gate))
		{
			if (Gate.bWaitForLobby && Gate.ArrivedHumans < ExpectedHumans)
			{
				UE_LOG(LogTNRally, Warning, TEXT("[RallyGameMode] Tope de espera: salen %d de %d tortugas del lobby."),
					Gate.ArrivedHumans, ExpectedHumans);
			}
			StartCountdown();
		}
		else if (Gate.bWaitForLobby && !bLoggedLobbyWait && Gate.bHasTeams && WarmupEndTime > 0.0 && Time >= WarmupEndTime)
		{
			bLoggedLobbyWait = true;
			UE_LOG(LogTNRally, Log, TEXT("[RallyGameMode] Esperando a las del lobby: %d de %d dentro."), Gate.ArrivedHumans, ExpectedHumans);
		}
		break;
	}
	case ETNRallyPhase::Countdown:
		if (Time >= RallyState->StartServerTime)
		{
			StartRacing();
		}
		break;
	case ETNRallyPhase::Racing:
		if (!Teams.ContainsByPredicate([](const FTeamRuntime& Team) { return !Team.bRetired; }))
		{
			StartResults();
		}
		else if (RaceTimeoutSeconds > 0.f && Time >= RallyState->StartServerTime + RaceTimeoutSeconds)
		{
			UE_LOG(LogTNRally, Warning, TEXT("[RallyGameMode] Tope de %.0f s sin nadie en meta: resultados."), RaceTimeoutSeconds);
			StartResults();
		}
		break;
	case ETNRallyPhase::Finishing:
		if (Time >= RallyState->PhaseEndServerTime
			|| !Teams.ContainsByPredicate([](const FTeamRuntime& Team) { return !Team.bRetired && !Team.bFinished; }))
		{
			StartResults();
		}
		break;
	case ETNRallyPhase::Results:
		if (Time >= RallyState->PhaseEndServerTime && !bRestartRequested)
		{
			RestartOrQuit();
		}
		break;
	}
}

void ATN_RallyGameMode::RestartOrQuit()
{
	bRestartRequested = true;
	const UTN_RallyRaceCounter* Counter = GetRaceCounter();
	const int32 RacesRun = Counter ? Counter->RacesRun : 0;
	if (RaceLimit > 0 && RacesRun >= RaceLimit)
	{
		UE_LOG(LogTNRally, Log, TEXT("[RallyStats] %d carreras hechas (?Races=%d): fin."), RacesRun, RaceLimit);
		FPlatformMisc::RequestExit(false, TEXT("TN Rally ?Races"));
		return;
	}
	if (bReturnToLobbyAfterResults)
	{
		ReturnToLobbyNow();
		return;
	}
	// ?Restart reutiliza la URL actual: mismo mapa y mismas opciones (?Variant, ?Seats, ?Bots, ?Laps, ?BotDriver).
	UE_LOG(LogTNRally, Log, TEXT("[RallyGameMode] Carrera nueva en el mismo mapa."));
	GetWorld()->ServerTravel(TEXT("?Restart"), false);
}

void ATN_RallyGameMode::StartCountdown()
{
	ATN_RallyGameState* RallyState = GetRallyGameState();
	// Los buggies ya están apoyados en su hueco desde que nacieron (jugadoras delante, IA detrás: ClaimGridSlot): el
	// semáforo no los recoloca (#289).
	for (FTeamRuntime& Team : Teams)
	{
		if (const APawn* Vehicle = Team.Vehicle.Get())
		{
			Team.PrevLocation = Vehicle->GetActorLocation();
			Team.Arc = Track->FindArcGlobal(Team.PrevLocation);
		}
	}
	RallyState->Phase = ETNRallyPhase::Countdown;
	RallyState->StartServerTime = static_cast<float>(Now() + CountdownSeconds);
	RallyState->PhaseEndServerTime = RallyState->StartServerTime;
	// Motor cortado hasta el verde y buggy frenado en su hueco (HoldBuggiesOnGrid): nadie puede salir antes.
	SetAllEnginesLocked(true);
	ApplyWeaponLocks();
	RebuildStandings();
	RallyState->ForceNetUpdate();
	UE_LOG(LogTNRally, Log, TEXT("[RallyGameMode] Semáforo: %d buggies, salida a los %.1f s."), Teams.Num(), CountdownSeconds);
}

void ATN_RallyGameMode::StartRacing()
{
	ATN_RallyGameState* RallyState = GetRallyGameState();
	RallyState->Phase = ETNRallyPhase::Racing;
	RallyState->PhaseEndServerTime = 0.f;
	for (FTeamRuntime& Team : Teams)
	{
		ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Team.Vehicle.Get());
		if (RallyVehicle)
		{
			// Verde: fuera el freno de la parrilla (HoldBuggiesOnGrid) y, si el equipo sigue, motor en marcha.
			RallyVehicle->SetRaceBrakeHeld(false);
			if (!Team.bRetired)
			{
				RallyVehicle->SetEngineLocked(false);
			}
		}
		if (const APawn* Vehicle = Team.Vehicle.Get())
		{
			Team.PrevLocation = Vehicle->GetActorLocation();
		}
		Team.OdometerCm = 0.0;
	}
	ApplyWeaponLocks();
	RallyState->ForceNetUpdate();
	UE_LOG(LogTNRally, Log, TEXT("[RallyGameMode] ¡Salida!"));
}

void ATN_RallyGameMode::StartFinishing()
{
	ATN_RallyGameState* RallyState = GetRallyGameState();
	RallyState->Phase = ETNRallyPhase::Finishing;
	RallyState->PhaseEndServerTime = static_cast<float>(Now() + FinishGraceSeconds);
	ApplyWeaponLocks();
	RallyState->ForceNetUpdate();
	UE_LOG(LogTNRally, Log, TEXT("[RallyGameMode] Primer buggy en meta: quedan %.0f s."), FinishGraceSeconds);
}

void ATN_RallyGameMode::StartResults()
{
	ATN_RallyGameState* RallyState = GetRallyGameState();
	if (RallyState->Phase != ETNRallyPhase::Results)
	{
		if (UTN_RallyRaceCounter* Counter = GetRaceCounter())
		{
			++Counter->RacesRun;
		}
		const bool bTimedOut = RallyState->Phase == ETNRallyPhase::Racing && RaceTimeoutSeconds > 0.f
			&& Now() >= RallyState->StartServerTime + RaceTimeoutSeconds;
		LogRaceStats(bTimedOut);
	}
	RallyState->Phase = ETNRallyPhase::Results;
	RallyState->PhaseEndServerTime = static_cast<float>(Now() + ResultsSeconds);
	SetAllEnginesLocked(true);
	ApplyWeaponLocks();
	RebuildStandings();
	RallyState->ForceNetUpdate();
	UE_LOG(LogTNRally, Log, TEXT("[RallyGameMode] Resultados.\n%s"), *RallyState->DescribeStatus());
}

void ATN_RallyGameMode::SetAllEnginesLocked(bool bLocked)
{
	for (const FTeamRuntime& Team : Teams)
	{
		if (ITN_RallyVehicle* RallyVehicle = Cast<ITN_RallyVehicle>(Team.Vehicle.Get()))
		{
			RallyVehicle->SetEngineLocked(bLocked || Team.bRetired || Team.bParked);
		}
	}
}
