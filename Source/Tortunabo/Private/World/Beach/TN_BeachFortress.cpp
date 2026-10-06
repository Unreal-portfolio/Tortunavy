#include "World/Beach/TN_BeachFortress.h"
#include "Settings/TN_GameplayAssetSettings.h"
#include "World/TN_ScorePickup.h"
#include "World/TN_ScoreShells.h"
#include "Core/TN_Log.h"
#include "CollisionQueryParams.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "ProceduralMeshComponent.h"
#include "UObject/UObjectGlobals.h"
#include "TN_BeachFortressKit.h"
#include "TN_BeachSignKit.h"

// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachFortress
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachFortress::ATN_BeachFortress()
{
	// La red de la ronda (relevancia por su huella y dormancy) la pone SpawnElement: aquí no cambia nada tras crearse.
	SetNetUpdateFrequency(1.f);

	CastleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CastleMesh"));
	CastleMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(CastleMesh);

	DecorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DecorMesh"));
	DecorMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(DecorMesh);

	// Todo lo que se pisa o para, también la cámara (en el patio y junto a las terrazas no se ve a través de la arena).
	CastleCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CastleCollision"));
	CastleCollision->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureSolid(CastleCollision, true);
}

float ATN_BeachFortress::GetFootprintRadius() const
{
	return static_cast<float>(TNBeach::FootprintRadius(Spec.Element) * TNBeachFortressKit::ScaleOf(Spec.SizeScale));
}

FVector ATN_BeachFortress::GetSummitLocation() const
{
	return GetActorTransform().TransformPosition(SummitLocal);
}

FVector ATN_BeachFortress::GetLaunchDirection() const
{
	return GetActorForwardVector().GetSafeNormal2D();
}

void ATN_BeachFortress::ApplySpec()
{
	const uint32 Seed = TNBeachTrapKit::SeedOf(Spec.Seed, 151u);
	const TNBeachFortressKit::FPlan Plan = TNBeachFortressKit::MakePlan(Spec.Element, Spec.SizeScale, Seed);

	// Espejo en Y según la semilla: la espiral de subida, las torrecillas y la cornisa, hacia un lado u otro.
	const bool bMirror = (TNBeachTrapKit::SeedOf(Spec.Seed, 157u) & 1u) != 0u;

	TNBeachTrapKit::FBuffers Castle;
	TNBeachTrapKit::FBuffers Decor;
	TNBeachTrapKit::FHulls Hulls;
	// Piezas que Arte puede sustituir (Docs/Arte_Assets.md), con su pivote ya reflejado; la colisión es la de los cascos.
	TNArt::FPieceLog Log(TEXT("Fortress"));
	TNBeachFortressKit::BuildFortress(Castle, Decor, Hulls, Plan, Seed, &Log, bMirror);
	TNBeachFortressKit::TintSand(Castle, Seed);

	if (bMirror)
	{
		TNBeachFortressKit::MirrorY(Castle);
		TNBeachFortressKit::MirrorY(Decor);
		TNBeachFortressKit::MirrorY(Hulls);
	}
	TNBeachTrapKit::SetMeshWithPieces(CastleMesh, this, Castle, Log);
	TNBeachTrapKit::SetMeshWithPieces(DecorMesh, this, Decor, Log);
	TNArt::SpawnPieceArt(CastleMesh, Log);
	CastleCollision->SetCollisionConvexMeshes(Hulls);

	// Lo de la cima, con el espejo.
	const double SideSign = bMirror ? -1.0 : 1.0;
	const auto Flip = [SideSign](const FVector& V) { return FVector(V.X, SideSign * V.Y, V.Z); };
	SummitLocal = FVector(0.0, 0.0, Plan.Summit().Z);
	SummitHalf = static_cast<float>(Plan.Summit().K);
	LauncherLocal = Flip(Plan.LauncherAt);
	bLauncherIsCatapult = Plan.bCatapult;
	LauncherSize = Plan.LauncherSize;
	LauncherSeed = static_cast<int32>(TNBeachTrapKit::SeedOf(Spec.Seed, 211u) & 0x7FFFFFFFu);
	// El cofre, en el lado de la cima contrario al cartel del lanzador (TNBeachSignKit::SideOf de su semilla).
	ChestLocal = FVector(Plan.ChestAt.X, -TNBeachSignKit::SideOf(LauncherSeed) * FMath::Abs(Plan.ChestAt.Y), Plan.ChestAt.Z);
	ChestYaw = Plan.ChestYaw;
	PrizeShells.Reset();
	for (const TNBeachFortressKit::FShellSpot& Spot : Plan.Shells)
	{
		FPrizeShell Prize;
		Prize.Local = Flip(Spot.At);
		Prize.Value = Spot.Value;
		PrizeShells.Add(Prize);
	}
	UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Fortaleza %s: %.0f m de muralla, adarve a %.1f m, %d terrazas, cima a %.1f m (%.0f m de lado), %d torrecillas, %d cascos, %d + %d triángulos%s."),
		*GetName(), 0.02 * Plan.A0, 0.01 * Plan.Z0, Plan.Tiers.Num(), 0.01 * Plan.Summit().Z, 0.02 * Plan.Summit().K, Plan.ChainA.Tops.Num() + Plan.ChainB.Tops.Num(), Hulls.Num(),
		Castle.Tris.Num() / 3, Decor.Tris.Num() / 3, bMirror ? TEXT(", en espejo") : TEXT(""));
}

void ATN_BeachFortress::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && bSpawnPrizes)
	{
		SpawnPrizes();
	}
}

void ATN_BeachFortress::SpawnPrizes()
{
	if (bPrizesSpawned)
	{
		return;
	}
	bPrizesSpawned = true;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FTransform ActorXf = GetActorTransform();
	const double Yaw = GetActorRotation().Yaw;
	const FQuat Facing = FRotator(0.0, Yaw, 0.0).Quaternion();

	// Lanzador potenciado en el borde +X de la cima, mirando al +X de la fortaleza (si no queda hacia el mar, se gira solo).
	FTNBeachElementSpec LauncherSpec;
	LauncherSpec.Element = bLauncherIsCatapult ? ETNBeachElement::Catapult : ETNBeachElement::Trampoline;
	LauncherSpec.Seed = LauncherSeed;
	LauncherSpec.SizeScale = LauncherSize;
	LauncherSpec.Flags = TNBeach::FlagBoosted;
	if (ATN_BeachElement* Launcher = ATN_BeachElement::SpawnElement(World, FTransform(Facing, ActorXf.TransformPosition(LauncherLocal)), LauncherSpec))
	{
		SpawnedPrizes.Add(Launcher);
	}

	// Cofre (ATN_BeachChest, de otra parte del trabajo): solo si ya existe su clase, con el frente hacia el centro de la cima.
	bool bChest = false;
	const FString ChestPath = FString::Printf(TEXT("/Script/Tortunabo.%s"), TNBeach::ClassNameOf(ETNBeachElement::TreasureChest));
	if (FindObject<UClass>(nullptr, *ChestPath))
	{
		FTNBeachElementSpec ChestSpec;
		ChestSpec.Element = ETNBeachElement::TreasureChest;
		ChestSpec.Seed = static_cast<int32>(TNBeachTrapKit::SeedOf(Spec.Seed, 223u) & 0x7FFFFFFFu);
		ChestSpec.SizeScale = 1.f;
		// El de la cima da lo mejor de la carrera para cualquier puesto (ETNRaceLootSource::Summit).
		ChestSpec.Flags = TNBeach::FlagSummitPrize;
		const FTransform ChestXf(FRotator(0.0, Yaw + ChestYaw, 0.0), ActorXf.TransformPosition(ChestLocal));
		if (ATN_BeachElement* Chest = ATN_BeachElement::SpawnElement(World, ChestXf, ChestSpec))
		{
			SpawnedPrizes.Add(Chest);
			bChest = true;
		}
	}
	else
	{
		UE_LOG(LogTortunabo, Log, TEXT("[Playa] Fortaleza %s: aún no existe %s; la cima va sin cofre."), *GetName(), *ChestPath);
	}

	// Conchas de puntos (las del botín: suman a RaceScore), a su altura sobre el suelo.
	UClass* ShellClass = UTN_GameplayAssetSettings::GetScorePickupClass();
	int32 Points = 0;
	int32 Shells = 0;
	for (const FPrizeShell& Prize : PrizeShells)
	{
		const FTransform Where(Facing, ActorXf.TransformPosition(Prize.Local + FVector(0.0, 0.0, TNScoreShells::Hover)));
		ATN_ScorePickup* Shell = World->SpawnActorDeferred<ATN_ScorePickup>(ShellClass, Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Shell)
		{
			continue;
		}
		// El valor antes de que aparezca: nace ya con su tamaño y su aspecto en todas las máquinas.
		Shell->SetScoreValue(Prize.Value);
		Shell->FinishSpawning(Where);
		SpawnedPrizes.Add(Shell);
		Points += Prize.Value;
		++Shells;
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] Fortaleza %s (%s): cima a %.1f m con %s potenciado, %s y %d conchas (%d puntos)."), *GetName(),
		*UEnum::GetValueAsString(Spec.Element), 0.01 * SummitLocal.Z, bLauncherIsCatapult ? TEXT("catapulta") : TEXT("trampolín"), bChest ? TEXT("cofre") : TEXT("sin cofre"),
		Shells, Points);
}

void ATN_BeachFortress::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		for (const TWeakObjectPtr<AActor>& Prize : SpawnedPrizes)
		{
			if (AActor* Piece = Prize.Get())
			{
				Piece->Destroy();
			}
		}
	}
	SpawnedPrizes.Reset();
	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Consola de prueba
// ─────────────────────────────────────────────────────────────────────────────

/**
 *   TN.Beach.PlaceBoosted <Catapult|Trampoline> [Tamaño=1] [Semilla]
 *       Lanzador potenciado (TNBeach::FlagBoosted) delante de tu tortuga, mirando hacia donde miras (como TN.Beach.Place;
 *       «TN.Beach.Place clear» también lo borra).
 *   TN.Beach.Fortress.Top [jugador=0]
 *       Sube a esa tortuga a la cima de la fortaleza más cercana, detrás del lanzador y mirando hacia él.
 *
 * Se escriben en la ventana del anfitrión (desde un cliente del PIE van al servidor del mismo proceso).
 */
namespace TNBeachFortressDebug
{
	/** Misma etiqueta que TN.Beach.Place: su «clear» borra también lo creado aquí. */
	const FName PlaceTag(TEXT("TNBeachDebug"));

	UWorld* AuthorityWorldOf(UWorld* InWorld)
	{
		if (!InWorld)
		{
			return nullptr;
		}
		if (InWorld->GetNetMode() != NM_Client)
		{
			return InWorld;
		}
		if (!GEngine)
		{
			return nullptr;
		}
		const FString MapName = UWorld::RemovePIEPrefix(InWorld->GetMapName());
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* Candidate = Context.World();
			if (Candidate && Candidate != InWorld && Candidate->IsGameWorld() && Candidate->GetNetMode() != NM_Client
				&& UWorld::RemovePIEPrefix(Candidate->GetMapName()) == MapName)
			{
				return Candidate;
			}
		}
		return nullptr;
	}

	double FloorBelow(UWorld* World, const FVector& At, double Fallback)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TNBeachFortressDebugFloor), false);
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		if (World->LineTraceSingleByObjectType(Hit, At + FVector(0.0, 0.0, 3000.0), At - FVector(0.0, 0.0, 6000.0), Objects, Params))
		{
			return Hit.ImpactPoint.Z;
		}
		return Fallback;
	}

	void PlaceBoosted(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* AuthWorld = AuthorityWorldOf(InWorld);
		if (!AuthWorld)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Playa] TN.Beach.PlaceBoosted: solo en el servidor (escríbelo en la ventana del anfitrión)."));
			return;
		}
		const FString Kind = Args.Num() > 0 ? Args[0] : FString();
		ETNBeachElement Element = ETNBeachElement::Catapult;
		if (Kind.Equals(TEXT("Trampoline"), ESearchCase::IgnoreCase) || Kind.Equals(TEXT("Trampolin"), ESearchCase::IgnoreCase)
			|| Kind.Equals(TEXT("Trampolín"), ESearchCase::IgnoreCase))
		{
			Element = ETNBeachElement::Trampoline;
		}
		else if (!Kind.Equals(TEXT("Catapult"), ESearchCase::IgnoreCase) && !Kind.Equals(TEXT("Catapulta"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogTortunabo, Display, TEXT("[Playa] Uso: TN.Beach.PlaceBoosted <Catapult|Trampoline> [Tamaño=1] [Semilla]."));
			return;
		}
		const float Size = Args.Num() > 1 ? FMath::Clamp(FCString::Atof(*Args[1]), 0.3f, 2.f) : 1.f;
		const int32 PlaceSeed = Args.Num() > 2 ? FCString::Atoi(*Args[2]) : FMath::Rand();

		const APlayerController* PC = InWorld->GetFirstPlayerController();
		const APawn* Viewer = PC ? PC->GetPawn() : nullptr;
		FVector From = FVector::ZeroVector;
		FRotator Heading = FRotator::ZeroRotator;
		if (Viewer)
		{
			From = Viewer->GetActorLocation();
			Heading = FRotator(0.0, Viewer->GetActorRotation().Yaw, 0.0);
		}
		else if (PC)
		{
			FVector ViewLoc;
			FRotator ViewRot;
			PC->GetPlayerViewPoint(ViewLoc, ViewRot);
			From = ViewLoc;
			Heading = FRotator(0.0, ViewRot.Yaw, 0.0);
		}
		FVector At = From + Heading.Vector() * (TNBeach::FootprintRadius(Element) * Size + 400.0);
		At.Z = FloorBelow(AuthWorld, At, From.Z - 90.0);

		FTNBeachElementSpec PlaceSpec;
		PlaceSpec.Element = Element;
		PlaceSpec.Seed = PlaceSeed;
		PlaceSpec.SizeScale = Size;
		PlaceSpec.Flags = TNBeach::FlagBoosted;
		ATN_BeachElement* Spawned = ATN_BeachElement::SpawnElement(AuthWorld, FTransform(Heading, At), PlaceSpec);
		if (!Spawned)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Playa] TN.Beach.PlaceBoosted: no se ha podido crear %s."), *UEnum::GetValueAsString(Element));
			return;
		}
		Spawned->Tags.AddUnique(PlaceTag);
		UE_LOG(LogTortunabo, Log, TEXT("[Playa] TN.Beach.PlaceBoosted: %s potenciado (%s) en %s, tamaño %.2f, semilla %d."), *UEnum::GetValueAsString(Element),
			*Spawned->GetName(), *At.ToString(), Size, PlaceSeed);
	}

	APlayerController* ControllerByIndex(UWorld* World, int32 Index)
	{
		int32 Current = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* Candidate = It->Get())
			{
				if (Current == Index)
				{
					return Candidate;
				}
				++Current;
			}
		}
		return nullptr;
	}

	void GoToTop(const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* AuthWorld = AuthorityWorldOf(InWorld);
		if (!AuthWorld)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Playa] TN.Beach.Fortress.Top: solo en el servidor (escríbelo en la ventana del anfitrión)."));
			return;
		}
		const int32 Index = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		APlayerController* PC = ControllerByIndex(AuthWorld, Index);
		APawn* Climber = PC ? PC->GetPawn() : nullptr;
		if (!Climber)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Playa] TN.Beach.Fortress.Top: el jugador %d no tiene tortuga."), Index);
			return;
		}
		ATN_BeachFortress* Nearest = nullptr;
		double Best = TNumericLimits<double>::Max();
		for (TActorIterator<ATN_BeachFortress> It(AuthWorld); It; ++It)
		{
			const double D = FVector::DistSquared2D(It->GetActorLocation(), Climber->GetActorLocation());
			if (D < Best)
			{
				Best = D;
				Nearest = *It;
			}
		}
		if (!Nearest)
		{
			UE_LOG(LogTortunabo, Warning, TEXT("[Playa] TN.Beach.Fortress.Top: no hay ninguna fortaleza (TN.Beach.Place FortressColossal 1 0 <semilla>)."));
			return;
		}
		// En el cuarto -X de la cima contrario al cofre: fuera del cazo de la catapulta y del trampolín, mirando al lanzador.
		const double Half = Nearest->GetSummitHalfSize();
		const FVector Forward = Nearest->GetLaunchDirection();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		const FVector Dest = Nearest->GetSummitLocation() - Forward * (0.72 * Half) - Right * (Nearest->GetChestSide() * 0.55 * Half) + FVector(0.0, 0.0, 120.0);
		const FRotator Face(0.0, Forward.Rotation().Yaw, 0.0);
		Climber->TeleportTo(Dest, Face, false, true);
		PC->ClientSetRotation(Face, true);
		UE_LOG(LogTortunabo, Log, TEXT("[Playa] TN.Beach.Fortress.Top: %s a la cima de %s (%.1f m)."), *GetNameSafe(Climber), *Nearest->GetName(),
			0.01 * Nearest->GetSummitHeight());
	}

	static FAutoConsoleCommandWithWorldAndArgs PlaceBoostedCommand(
		TEXT("TN.Beach.PlaceBoosted"),
		TEXT("Lanzador potenciado delante de tu tortuga: TN.Beach.PlaceBoosted <Catapult|Trampoline> [Tamaño=1] [Semilla]. «TN.Beach.Place clear» lo borra."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlaceBoosted),
		ECVF_Cheat);

	static FAutoConsoleCommandWithWorldAndArgs FortressTopCommand(
		TEXT("TN.Beach.Fortress.Top"),
		TEXT("Sube a la tortuga del jugador a la cima de la fortaleza más cercana: TN.Beach.Fortress.Top [jugador=0]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GoToTop),
		ECVF_Cheat);
}
