// Puzles del bloque "placements" (#652): placas de presión, plataformas que se rompen y plataformas tambaleantes
// (Docs/2026-10-06-Plan-Maestro-Modo-Unico.md) repartidas por la huella del puzle a lo largo del camino (path_uu), con su
// cota ajustada al terreno. Solo en el servidor.

#include "World/TN_MapPlacementSpawner.h"

#include "Core/TN_Log.h"
#include "World/TN_BreakablePlatform.h"
#include "World/TN_PressurePlate.h"

#include "Engine/World.h"

namespace TNMapPlacementPuzzles
{
	/** Blueprints de las placas (con su malla y su sonido); sin ellos, la clase nativa. */
	const TCHAR* PlateClassPath = TEXT("/Game/Blueprints/Gameplay/Interaction/BP_PressurePlate.BP_PressurePlate_C");
	const TCHAR* PlateManagerClassPath = TEXT("/Game/Blueprints/Gameplay/Interaction/BP_PressurePlateGroupManager.BP_PressurePlateGroupManager_C");

	/** Plataformas que se rompen: zigzag a los lados del eje, altura sobre el suelo y escala del cubo de serie. */
	constexpr double ZigzagCm = 120.0;
	constexpr double BreakableLiftCm = 80.0;
	const FVector BreakableScale(2.2, 2.2, 0.3);

	template <typename T>
	UClass* ClassOr(const TCHAR* Path)
	{
		UClass* Loaded = LoadClass<T>(nullptr, Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
		return Loaded ? Loaded : T::StaticClass();
	}

	/** Fracción de la huella del puzle a Offset cm de su centro, recortada a la huella. */
	double AlphaAt(const TNMapPlacements::FPlacement& P, double OffsetCm)
	{
		return P.LengthCm > 1.0 ? FMath::Clamp(0.5 + OffsetCm / P.LengthCm, 0.0, 1.0) : 0.5;
	}
}

bool ATN_MapPlacementSpawner::SpawnPlateBalance(const TNMapPlacements::FPlacement& P)
{
	using namespace TNMapPlacementPuzzles;
	const int32 Plates = FMath::Clamp(FMath::RoundToInt(P.Param(TEXT("plates"), 3.0)), 3, 5);
	const double Spacing = P.Param(TEXT("spacing_m"), 6.0) * 100.0;
	double CenterYaw = P.YawDeg;
	const FVector Center = Grounded(TNMapPlacements::PointAlong(P, 0.5, CenterYaw));
	ATN_PressurePlateGroupManager* Manager = Cast<ATN_PressurePlateGroupManager>(
		SpawnClass(ClassOr<ATN_PressurePlateGroupManager>(PlateManagerClassPath), Center, CenterYaw));
	if (!Manager)
	{
		return false;
	}
	UClass* PlateClass = ClassOr<ATN_PressurePlate>(PlateClassPath);
	bool bOk = true;
	for (int32 i = 0; i < Plates; ++i)
	{
		double Yaw = P.YawDeg;
		const FVector At = Grounded(TNMapPlacements::PointAlong(P, AlphaAt(P, (i - 0.5 * (Plates - 1)) * Spacing), Yaw));
		ATN_PressurePlate* Plate = Cast<ATN_PressurePlate>(SpawnClass(PlateClass, At, Yaw));
		if (!Plate)
		{
			bOk = false;
			continue;
		}
		Manager->RegisterPlate(Plate);
	}
	// La puerta que abren las placas (ATN_PuzzleDoor, N4 del catálogo) aún no existe: las placas y su gestor quedan
	// puestos y la puerta se añadirá cuando esté la pieza.
	++Stats.MissingPieces;
	UE_LOG(LogTortunabo, Warning, TEXT("[MapPlacements] '%s': plate_balance sin puerta (falta ATN_PuzzleDoor, N4)."), *P.Id);
	return bOk;
}

bool ATN_MapPlacementSpawner::SpawnBreakableChain(const TNMapPlacements::FPlacement& P)
{
	using namespace TNMapPlacementPuzzles;
	const int32 Platforms = FMath::Clamp(FMath::RoundToInt(P.Param(TEXT("platforms"), 8.0)), 2, 12);
	const bool bTrampoline = P.Param(TEXT("trampoline"), 1.0) > 0.5;
	// Huecos iguales por la huella; con trampolín, este ocupa el hueco del medio (salta el tramo roto).
	const int32 Slots = Platforms + (bTrampoline ? 1 : 0);
	const int32 TrampolineSlot = bTrampoline ? Slots / 2 : INDEX_NONE;
	UWorld* World = GetWorld();
	bool bOk = World != nullptr;
	int32 Platform = 0;
	for (int32 Slot = 0; World && Slot < Slots; ++Slot)
	{
		double Yaw = P.YawDeg;
		const double Alpha = Slots > 1 ? static_cast<double>(Slot) / (Slots - 1) : 0.5;
		const FVector OnPath = Grounded(TNMapPlacements::PointAlong(P, Alpha, Yaw));
		if (Slot == TrampolineSlot)
		{
			bOk &= SpawnBeachElement(ETNBeachElement::Trampoline, OnPath, Yaw, P.Id + TEXT("#trampoline"), 1.f, 0.0) != nullptr;
			continue;
		}
		const FVector Side = FRotator(0.0, Yaw + 90.0, 0.0).Vector() * ((Platform++ % 2 == 0) ? ZigzagCm : -ZigzagCm);
		const FVector At = OnPath + Side + FVector(0.0, 0.0, BreakableLiftCm);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.Owner = this;
		AActor* Piece = World->SpawnActor<ATN_BreakablePlatform>(ATN_BreakablePlatform::StaticClass(),
			FTransform(FRotator(0.0, Yaw, 0.0), At, BreakableScale), Params);
		Track(Piece);
		bOk &= Piece != nullptr;
	}
	return bOk;
}

bool ATN_MapPlacementSpawner::SpawnElementRow(const TNMapPlacements::FPlacement& P, ETNBeachElement Element, int32 Count, double LiftCm)
{
	bool bOk = true;
	for (int32 i = 0; i < Count; ++i)
	{
		double Yaw = P.YawDeg;
		const double Alpha = Count > 1 ? static_cast<double>(i) / (Count - 1) : 0.5;
		const FVector At = Grounded(TNMapPlacements::PointAlong(P, Alpha, Yaw)) + FVector(0.0, 0.0, LiftCm);
		bOk &= SpawnBeachElement(Element, At, Yaw, FString::Printf(TEXT("%s#%d"), *P.Id, i), P.SizeScale, 0.0) != nullptr;
	}
	return bOk;
}
