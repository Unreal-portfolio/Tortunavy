#include "World/TN_AirdropSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "World/TN_AirdropPoint.h"
#include "World/TN_LootTable.h"
#include "World/TN_SupplyDrop.h"

namespace TNAirdropSubsystemDetail
{
	/** Distancia (cm, en planta) a la que una caja sin abrir ocupa un punto. */
	constexpr double BusyRadius = 300.0;
}

// ── Reglas del gestor ────────────────────────────────────────────────────────

bool TNAirdropSchedule::CanLaunch(int32 Launched, int32 MaxDrops, int32 FreePoints)
{
	return Launched < MaxDrops && FreePoints > 0;
}

int32 TNAirdropSchedule::PickPoint(TConstArrayView<bool> Busy, int32 LastIndex, float Roll)
{
	TArray<int32> Free;
	for (int32 i = 0; i < Busy.Num(); ++i)
	{
		if (!Busy[i])
		{
			Free.Add(i);
		}
	}
	// El último usado, solo si no queda otro.
	if (Free.Num() > 1)
	{
		Free.Remove(LastIndex);
	}
	if (Free.Num() == 0)
	{
		return INDEX_NONE;
	}
	const int32 Pick = FMath::Clamp(FMath::FloorToInt(FMath::Clamp(Roll, 0.f, 1.f) * Free.Num()), 0, Free.Num() - 1);
	return Free[Pick];
}

// ── Gestor ───────────────────────────────────────────────────────────────────

bool UTN_AirdropSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTN_AirdropSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const UTN_AirdropSettings* Settings = GetDefault<UTN_AirdropSettings>();
	if (InWorld.GetNetMode() == NM_Client || !Settings || !Settings->bEnabled || Settings->MaxDrops <= 0)
	{
		return;
	}
	// El temporizador corre siempre en el servidor; sin puntos en el mapa, cada turno no hace nada.
	InWorld.GetTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UTN_AirdropSubsystem::OnTimer),
		FMath::Max(5.f, Settings->IntervalSeconds), true, FMath::Max(0.1f, Settings->FirstDropSeconds));
}

void UTN_AirdropSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(Timer);
	}
	Super::Deinitialize();
}

void UTN_AirdropSubsystem::OnTimer()
{
	const UTN_AirdropSettings* Settings = GetDefault<UTN_AirdropSettings>();
	if (!Settings || Launched >= Settings->MaxDrops)
	{
		GetWorld()->GetTimerManager().ClearTimer(Timer);
		return;
	}
	if (LaunchAtFreePoint())
	{
		++Launched;
		UE_LOG(LogTNLoot, Log, TEXT("Airdrop %d de %d de la partida."), Launched, Settings->MaxDrops);
	}
}

ATN_SupplyDrop* UTN_AirdropSubsystem::ForceDrop(const FVector* Fallback)
{
	if (ATN_SupplyDrop* Drop = LaunchAtFreePoint())
	{
		return Drop;
	}
	if (!Fallback)
	{
		UE_LOG(LogTNLoot, Warning, TEXT("No hay puntos de airdrop libres en el mapa."));
		return nullptr;
	}
	return LaunchAt(*Fallback);
}

ATN_SupplyDrop* UTN_AirdropSubsystem::LaunchAt(const FVector& Location)
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	return Launch(GroundBelow(Location), 0.f);
}

ATN_SupplyDrop* UTN_AirdropSubsystem::LaunchAtFreePoint()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	Drops.RemoveAll([](const TWeakObjectPtr<ATN_SupplyDrop>& Weak) { return !Weak.IsValid() || Weak->IsOpened(); });

	TArray<ATN_AirdropPoint*> Points;
	TArray<bool> Busy;
	for (TActorIterator<ATN_AirdropPoint> It(World); It; ++It)
	{
		if (!It->IsEnabled())
		{
			continue;
		}
		const FVector Where = It->GetActorLocation();
		const bool bBusy = Drops.ContainsByPredicate([&Where](const TWeakObjectPtr<ATN_SupplyDrop>& Weak)
		{
			return FVector::Dist2D(FVector(Weak->GetFlight().Landing), Where) < TNAirdropSubsystemDetail::BusyRadius;
		});
		Points.Add(*It);
		Busy.Add(bBusy);
	}
	const int32 FreeCount = Busy.FilterByPredicate([](bool bBusy) { return !bBusy; }).Num();
	if (!TNAirdropSchedule::CanLaunch(0, 1, FreeCount))
	{
		return nullptr;
	}
	const int32 Index = TNAirdropSchedule::PickPoint(Busy, LastPoint, FMath::FRand());
	if (!Points.IsValidIndex(Index))
	{
		return nullptr;
	}
	LastPoint = Index;
	const ATN_AirdropPoint* Point = Points[Index];
	return Launch(GroundBelow(Point->GetActorLocation()), Point->GetDropHeight());
}

ATN_SupplyDrop* UTN_AirdropSubsystem::Launch(const FVector& Ground, float Height)
{
	const UTN_AirdropSettings* Settings = GetDefault<UTN_AirdropSettings>();
	TSubclassOf<ATN_SupplyDrop> DropClass = Settings && !Settings->DropClass.IsNull() ? Settings->DropClass.LoadSynchronous() : nullptr;
	const float DropHeight = Height > 0.f ? Height : (Settings ? Settings->DropHeight : 4000.f);
	ATN_SupplyDrop* Drop = ATN_SupplyDrop::ServerLaunch(GetWorld(), DropClass, Ground, DropHeight,
		Settings ? Settings->WarnSeconds : 8.f, Settings ? Settings->FallSeconds : 12.f);
	if (Drop)
	{
		Drops.Add(Drop);
	}
	return Drop;
}

FVector UTN_AirdropSubsystem::GroundBelow(const FVector& Location) const
{
	FHitResult Hit;
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(TN_AirdropGround), false);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Location + FVector(0.f, 0.f, 200.f), Location - FVector(0.f, 0.f, 5000.f),
		FCollisionObjectQueryParams(ECC_WorldStatic), Query))
	{
		return Hit.ImpactPoint;
	}
	return Location;
}
