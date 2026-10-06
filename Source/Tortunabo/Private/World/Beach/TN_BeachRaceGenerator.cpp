// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachRaceGenerator — la playa del modo carrera: ciclo de vida, rondas (reparto,
// asientos y elementos), consultas de la salida, la meta y la zambullida, y el aviso de
// «tortuga en el agua». El terreno, el acantilado, el mar y los muros van en
// TN_BeachRaceGenerator_Build.cpp; la salida, la meta, la selva y las huellas, en
// TN_BeachRaceGenerator_Scenery.cpp; la ronda por partes, el decorado local y
// TN.Beach.Perf, en TN_BeachRaceGenerator_Round.cpp. La lógica pura, en TN_BeachLayout.h.
// ─────────────────────────────────────────────────────────────────────────────

#include "World/Beach/TN_BeachRaceGenerator.h"
#include "Game/TN_BeachRaceGameState.h"
#include "World/Beach/TN_BeachDecorField.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachLoot.h"
#include "World/ProcMap/TN_ProcWaterActors.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Core/TN_Log.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "UObject/UObjectGlobals.h"
#include "../ProcMap/TN_ProcMapAmbientFX.h"
#include "World/Beach/TN_BeachTankTrap.h"

namespace TNBeachRace
{
	TAutoConsoleVariable<int32> CVarShowFootprints(TEXT("TN.Beach.ShowFootprints"), 0,
		TEXT("1 = enseña en juego las huellas del reparto de la playa del modo carrera (ATN_BeachRaceGenerator)."));

	/** Segundos sin que nadie reparta antes de repartir solo: sin el GameMode de la carrera y con él (por si no lo hace). */
	constexpr float IdleSecondsAlone = 3.f;
	constexpr float IdleSecondsWithRaceMode = 20.f;
}

ATN_BeachRaceGenerator::ATN_BeachRaceGenerator()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	// Colocado en el nivel: se replica solo la ronda (semilla y número); cada máquina construye el terreno igual.
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(2.f);
	SetCanBeDamaged(false);

	BeachRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BeachRoot"));
	SetRootComponent(BeachRoot);

	// Mallas generadas en código (editor y ejecución) que no se guardan con el nivel: RF_Transient (el segundo parámetro de
	// CreateDefaultSubobject no basta) y sus punteros, Transient.
	auto MakeMesh = [this](const TCHAR* Name, bool bCollision, bool bShadow)
	{
		UProceduralMeshComponent* Comp = CreateDefaultSubobject<UProceduralMeshComponent>(Name);
		Comp->SetFlags(RF_Transient);
		Comp->SetupAttachment(BeachRoot);
		// Colisión cocinada al momento: el suelo tiene que estar antes de crear los elementos y de soltar a las tortugas.
		Comp->bUseAsyncCooking = false;
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetCastShadow(bShadow);
		if (bCollision)
		{
			Comp->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		}
		else
		{
			Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		return Comp;
	};
	// Sombra dinámica solo en lo que está en la playa (salida, acantilado, arco de meta, relieve fijo); las banderolas de
	// la ladera, las boyas, el mar y las tarimas, sin sombra (sombras virtuales más baratas). Las copas del bosquecillo
	// de la salida tampoco, ni sus troncos con el cartel: con el sol casi cenital dejaban a oscuras los huevos de la salida y
	// los primeros cien metros de playa.
	CliffMesh = MakeMesh(TEXT("CliffMesh"), true, true);
	SeabedMesh = MakeMesh(TEXT("SeabedMesh"), true, false);
	SeaMesh = MakeMesh(TEXT("SeaMesh"), false, false);
	GroveSolidMesh = MakeMesh(TEXT("GroveSolidMesh"), true, false);
	GroveDecoMesh = MakeMesh(TEXT("GroveDecoMesh"), false, false);
	FinishSolidMesh = MakeMesh(TEXT("FinishSolidMesh"), true, true);
	FinishDecoMesh = MakeMesh(TEXT("FinishDecoMesh"), false, false);
	FloatMesh = MakeMesh(TEXT("FloatMesh"), false, false);
	FootprintMesh = MakeMesh(TEXT("FootprintMesh"), false, false);
	FootprintMesh->SetHiddenInGame(true);
	FeatureMesh = MakeMesh(TEXT("FeatureMesh"), true, true);
	FeatureDecoMesh = MakeMesh(TEXT("FeatureDecoMesh"), false, false);
	PoolMesh = MakeMesh(TEXT("PoolMesh"), false, false);
}

void ATN_BeachRaceGenerator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachRaceGenerator, RoundNet);
	DOREPLIFETIME(ATN_BeachRaceGenerator, DecorCuts);
}

ATN_BeachRaceGenerator* ATN_BeachRaceGenerator::Find(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World) { return nullptr; }
	TActorIterator<ATN_BeachRaceGenerator> It(World);
	return It ? *It : nullptr;
}

void ATN_BeachRaceGenerator::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildAll();
}

void ATN_BeachRaceGenerator::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	// Al cargar el nivel (editor) o al duplicarlo para jugar, las mallas transitorias llegan vacías: se rehacen.
	if (!IsTemplate() && GetWorld()) { BuildAll(); }
}

void ATN_BeachRaceGenerator::BeginPlay()
{
	Super::BeginPlay();
	BuildAll();
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) { return; }
	// La dificultad que eligió el anfitrión con el general (sin lobby, la del nivel). La consola puede cambiarla luego.
	if (HasAuthority())
	{
		if (const UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance()))
		{
			if (GI->SelectedProcMode == ETNProcGameMode::Race) { Difficulty = GI->SelectedProcDifficulty; }
		}
	}
	SpawnWaterVolume();
	if (FootprintMesh) { FootprintMesh->SetHiddenInGame(!ShouldShowFootprints()); }
	if (GetNetMode() != NM_DedicatedServer) { StartLiving(); }
	IdleTime = 0.f;
	SetActorTickEnabled(true);
}

void ATN_BeachRaceGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopLiving();
	CancelRoundBuild();
	// Al cambiar de nivel o salir, el mundo se lleva todo; solo si se destruye el generador se quita lo suyo.
	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		if (HasAuthority()) { DestroyRoundElements(); }
		if (ATN_ProcWaterVolume* Water = WaterVolume.Get()) { Water->Destroy(); }
		if (IsValid(DecorField)) { DecorField->Destroy(); }
	}
	DecorField = nullptr;
	RoundElements.Reset();
	ElementByItem.Reset();
	WaterVolume.Reset();
	Finishers.Reset();
	WetTurtles.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATN_BeachRaceGenerator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	// La ronda a medias sigue montándose (unos milisegundos por fotograma). Si ya se corre, de una vez: lo que falta en esta
	// máquina (asientos, decorado) es colisión que el anfitrión ya tiene (#828).
	const bool bRacing = IsRaceRunning();
	if (PendingBuild.IsValid()) { TickRoundBuild(bRacing); }
	HoldLocalPawnsWhileBuilding(bRacing);
	TickAutoGenerate(Dt);
	TickTurtles(Dt);
	if (bEggsAnimating) { bEggsAnimating = UpdateStartEggs(); }
	if (bLiving)
	{
		TNAmbientFX::TickOwner(this, Dt);
		// Las boyas de meta se mecen (solo arriba y abajo: la línea entera está a 0,8 km del origen).
		FloatClock += Dt;
		if (FloatMesh) { FloatMesh->SetRelativeLocation(FVector(0.0, 0.0, 35.0 * FMath::Sin(FloatClock * 0.9f))); }
		if (FootprintMesh)
		{
			const bool bShow = ShouldShowFootprints();
			if (bShow && FootprintMesh->GetNumSections() == 0 && Layout.Items.Num() > 0) { BuildFootprints(); }
			FootprintMesh->SetHiddenInGame(!bShow);
		}
	}
}

bool ATN_BeachRaceGenerator::IsRaceRunning() const
{
	const UWorld* World = GetWorld();
	const ATN_BeachRaceGameState* BeachState = World ? World->GetGameState<ATN_BeachRaceGameState>() : nullptr;
	return BeachState && BeachState->RacePhase == ETNBeachRacePhase::Racing;
}

void ATN_BeachRaceGenerator::HoldLocalPawnsWhileBuilding(bool bRacing)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld()) { return; }
	// Esperando la salida, el GameMode ya tiene a todas paradas (FreezePlayers): aquí solo lo que queda en plena carrera.
	const bool bBuilding = PendingBuild.IsValid() || (RoundNet.Round > 0 && !RoundNet.bCleared && AppliedRound != RoundNet.Round);
	if (bRacing && bBuilding)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			ACharacter* Character = PC && PC->IsLocalController() ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
			UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
			if (!Move || Move->MovementMode == MOVE_None) { continue; }
			Move->StopMovementImmediately();
			Move->DisableMovement();
			if (HeldLocalPawns.Num() == 0)
			{
				HeldSince = World->GetTimeSeconds();
				UE_LOG(LogTortunabo, Warning, TEXT("[Playa] Ronda %d en marcha y aún a medio montar en esta máquina: las tortugas locales esperan quietas."),
					RoundNet.Round);
			}
			HeldLocalPawns.AddUnique(Character);
		}
		return;
	}
	if (HeldLocalPawns.Num() == 0) { return; }
	// Solo se sueltan las que se pararon aquí (y siguen paradas): las del GameMode las suelta él.
	for (const TWeakObjectPtr<ACharacter>& Held : HeldLocalPawns)
	{
		UCharacterMovementComponent* Move = Held.IsValid() ? Held->GetCharacterMovement() : nullptr;
		if (Move && Move->MovementMode == MOVE_None) { Move->SetMovementMode(MOVE_Falling); }
	}
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] Ronda %d montada en esta máquina: las tortugas locales siguen (%.1f s paradas)."), RoundNet.Round,
		World->GetTimeSeconds() - HeldSince);
	HeldLocalPawns.Reset();
}

bool ATN_BeachRaceGenerator::ShouldShowFootprints() const
{
	const UWorld* World = GetWorld();
	if (World && World->IsGameWorld()) { return TNBeachRace::CVarShowFootprints.GetValueOnGameThread() != 0; }
	return bShowFootprints;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rondas
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachRaceGenerator::GenerateRound(int32 InSeed)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority()) { return; }
	BuildAll();
	DestroyRoundElements();
	RoundNet.Seed = InSeed;
	RoundNet.Round += 1;
	RoundNet.bCleared = false;
	RoundNet.Difficulty = Difficulty;
	RoundNet.bSprintLayout = IsSprintFinalRound();
	// Huevos cerrados otra vez y en la salida: las tortugas de la ronda nueva aparecen dentro (el sprint final los lleva a
	// su línea después, con SetStartEggsAtSprint).
	RoundNet.bStartOpen = false;
	RoundNet.StartOpenTime = 0.f;
	RoundNet.bSprintEggs = false;
	DecorCuts.Round = RoundNet.Round;
	DecorCuts.Circles.Reset();
	ApplyStartEggs(false);
	Finishers.Reset();
	WetTurtles.Reset();
	bRoundReady = false;
	IdleTime = 0.f;
	// Se replica ya: los clientes montan su parte (reparto, asientos y decorado) a la vez que el servidor.
	ForceNetUpdate();
	// El reparto en otro hilo; los asientos, el decorado, los actores y el botín, por partes en los fotogramas siguientes
	// (TickRoundBuild). IsRoundReady espera a que esté todo.
	StartRoundBuild(false);
}

bool ATN_BeachRaceGenerator::IsSprintFinalRound() const
{
	// El GameMode pone bSprintFinal al anunciar el sprint (EnterSprintIntro) y lo quita al empezar otra partida
	// (ResetMatchScores), antes de repartir: GenerateRound lo ve ya puesto en la ronda del sprint y solo en ella.
	const UWorld* World = GetWorld();
	const ATN_BeachRaceGameState* BeachState = World ? World->GetGameState<ATN_BeachRaceGameState>() : nullptr;
	return BeachState && BeachState->bSprintFinal;
}

void ATN_BeachRaceGenerator::ClearRound()
{
	if (!HasAuthority()) { return; }
	CancelRoundBuild();
	DestroyRoundElements();
	ApplyLayoutLocal(TNBeachLayout::FRoundLayout());
	if (IsValid(DecorField)) { DecorField->ClearDecor(); }
	RoundNet.Round += 1;
	RoundNet.bCleared = true;
	RoundNet.bStartOpen = false;
	RoundNet.bSprintEggs = false;
	DecorCuts.Round = RoundNet.Round;
	DecorCuts.Circles.Reset();
	ApplyStartEggs(false);
	AppliedRound = RoundNet.Round;
	bRoundReady = false;
	Finishers.Reset();
	ForceNetUpdate();
}

bool ATN_BeachRaceGenerator::IsRoundReady() const
{
	return bBuilt && RoundNet.Round > 0 && !RoundNet.bCleared && AppliedRound == RoundNet.Round && !PendingBuild.IsValid()
		&& (!HasAuthority() || bRoundReady);
}

void ATN_BeachRaceGenerator::OnRep_RoundNet()
{
	BuildAll();
	if (RoundNet.Round != AppliedRound)
	{
		WetTurtles.Reset();
		if (RoundNet.Round > 0 && !RoundNet.bCleared)
		{
			// Ronda nueva: el mismo reparto que el servidor (semilla y dificultad), montado por partes (TickRoundBuild). Esto
			// llega otra vez con cada cambio de la ronda (los huevos, el sprint): la que ya se está montando sigue.
			if (!PendingBuild.IsValid() || PendingRound != RoundNet.Round) { StartRoundBuild(false); }
		}
		else
		{
			// Ronda quitada: playa vacía.
			CancelRoundBuild();
			ApplyLayoutLocal(TNBeachLayout::FRoundLayout());
			if (IsValid(DecorField)) { DecorField->ClearDecor(); }
			AppliedRound = RoundNet.Round;
		}
	}
	// Huevos (en su línea, la salida o la del sprint): se rompen con el salto si llega a tiempo (el servidor lanzó hace
	// poco); si no, ya rotos.
	if (RoundNet.bStartOpen != bEggsOpenLocal || IsStartEggLineStale())
	{
		const UWorld* World = GetWorld();
		const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
		const double Since = GS ? GS->GetServerWorldTimeSeconds() - static_cast<double>(RoundNet.StartOpenTime) : 0.0;
		ApplyStartEggs(RoundNet.bStartOpen && Since < 1.5);
	}
}

void ATN_BeachRaceGenerator::ApplyLayoutLocal(const TNBeachLayout::FRoundLayout& NewLayout)
{
	const TArray<TNBeachLayout::FStamp> OldStamps = Layout.Stamps;
	Layout = NewLayout;
	if (bBuilt && TilesX > 0 && TilesY > 0)
	{
		// Solo las teselas que tocan un asiento nuevo o uno de la ronda anterior (para quitarlo), con dos filas de margen para
		// que las normales de los bordes cuadren; todas si una ronda a medias dejó teselas sin subir.
		TArray<int32> Touched;
		FindTouchedTiles(GridXs, GridYs, TilesX, TilesY, OldStamps, Layout.Stamps, bTerrainDirtyAll, Touched);
		bTerrainDirtyAll = false;
		// Casi toda la playa se toca en cada ronda: las alturas, en paralelo; la subida y la colisión, en este hilo.
		BuildTerrainTiles(Touched, Layout.Stamps);
	}
	BuildFootprints();
}

FString ATN_BeachRaceGenerator::SpawnRoundElements()
{
	// Las clases que faltan (otro agente aún no las ha escrito) se cuentan una vez por clase, sin crear nada.
	TMap<FString, int32> MissingByClass;
	TArray<int8> HasClass;
	HasClass.Init(-1, static_cast<int32>(ETNBeachElement::Count));
	for (int32 Index = 0; Index < Layout.Items.Num(); ++Index)
	{
		SpawnRoundElement(Index, HasClass, MissingByClass);
	}
	SpawnTankTrapGuard();
	return DescribeMissing(MissingByClass);
}

void ATN_BeachRaceGenerator::SpawnTankTrapGuard()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	// Los erizos son decorado instanciado (colisión de cada máquina); el choque lo vigila uno solo en el servidor.
	TArray<FVector4> Spots;
	for (const TNBeachLayout::FItem& Item : Layout.Items)
	{
		if (Item.Element != ETNBeachElement::TankTrap)
		{
			continue;
		}
		const FVector Local(Item.Pos.X, Item.Pos.Y, TNBeachLayout::PlacementZ(Item));
		const FVector World3 = GetActorTransform().TransformPosition(Local);
		Spots.Add(FVector4(World3.X, World3.Y, World3.Z + 100.0, Item.Radius * 0.45));
	}
	if (ATN_BeachTankTrap* Guard = ATN_BeachTankTrap::SpawnGuard(World, Spots))
	{
		RoundElements.Add(Guard);
	}
}

void ATN_BeachRaceGenerator::SpawnRoundElement(int32 Index, TArray<int8>& HasClass, TMap<FString, int32>& MissingByClass)
{
	UWorld* World = GetWorld();
	if (!World || !Layout.Items.IsValidIndex(Index)) { return; }
	const TNBeachLayout::FItem& Item = Layout.Items[Index];
	// El decorado no es un actor: lo monta cada máquina en su ATN_BeachDecorField (instanciado y sin replicar).
	if (TNBeach::CategoryOf(Item.Element) == ETNBeachCategory::Decor) { return; }
	const int32 Kind = static_cast<int32>(Item.Element);
	if (!HasClass.IsValidIndex(Kind)) { return; }
	const TCHAR* ClassName = TNBeach::ClassNameOf(Item.Element);
	if (HasClass[Kind] < 0)
	{
		const UClass* Class = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Tortunabo.%s"), ClassName));
		HasClass[Kind] = static_cast<int8>(Class && Class->IsChildOf(ATN_BeachElement::StaticClass()) ? 1 : 0);
	}
	if (HasClass[Kind] == 0)
	{
		++MissingByClass.FindOrAdd(ClassName);
		return;
	}
	// A la cota de su asiento (la arena natural de su centro, con las pozas y las trincheras).
	const FVector Local(Item.Pos.X, Item.Pos.Y, TNBeachLayout::PlacementZ(Item));
	const FTransform ElementXf = FTransform(FRotator(0.0, Item.Yaw, 0.0), Local) * GetActorTransform();
	// SpawnElement le pone su red (ATN_BeachElement::ApplyRoundNetProfile): relevante por distancia, dormido si está quieto.
	ATN_BeachElement* Element = ATN_BeachElement::SpawnElement(World, ElementXf, Item.Spec);
	if (!Element) { return; }
	if (!World->IsGameWorld())
	{
		// Ronda de prueba del editor: no se guarda con el nivel y se construye ya (en el editor no hay BeginPlay).
		Element->SetFlags(RF_Transient);
		if (UFunction* Build = Element->FindFunction(TEXT("OnRep_Spec"))) { Element->ProcessEvent(Build, nullptr); }
	}
	RoundElements.Add(Element);
	if (ElementByItem.Num() != Layout.Items.Num()) { ElementByItem.SetNum(Layout.Items.Num()); }
	ElementByItem[Index] = Element;
}

FString ATN_BeachRaceGenerator::DescribeMissing(const TMap<FString, int32>& MissingByClass)
{
	FString Missing;
	for (const TPair<FString, int32>& Entry : MissingByClass)
	{
		Missing += FString::Printf(TEXT("%s%s x%d"), Missing.IsEmpty() ? TEXT("") : TEXT(", "), *Entry.Key, Entry.Value);
	}
	return Missing;
}

ATN_BeachElement* ATN_BeachRaceGenerator::GetElementForItem(int32 ItemIndex) const
{
	return ElementByItem.IsValidIndex(ItemIndex) ? ElementByItem[ItemIndex].Get() : nullptr;
}

void ATN_BeachRaceGenerator::DestroyRoundElements()
{
	for (ATN_BeachElement* Element : RoundElements)
	{
		if (IsValid(Element)) { Element->Destroy(); }
	}
	RoundElements.Reset();
	ElementByItem.Reset();
}

void ATN_BeachRaceGenerator::PreviewRound()
{
	UWorld* World = GetWorld();
	if (!World) { return; }
	if (World->IsGameWorld())
	{
		if (HasAuthority()) { GenerateRound(bEditorRandomSeed ? FMath::Rand() : EditorSeed); }
		return;
	}
	BuildAll();
	const int32 Seed = bEditorRandomSeed ? FMath::Rand() : EditorSeed;
	DestroyRoundElements();
	TNBeachLayout::FRoundLayout NewLayout;
	MakeRoundLayout(Seed, Difficulty, false, NewLayout);
	ApplyLayoutLocal(NewLayout);
	// El decorado, instanciado como en juego y de una vez (en el editor no hay fotogramas que repartir).
	int32 DecorInstances = 0;
	if (ATN_BeachDecorField* Field = EnsureDecorField())
	{
		Field->BeginBuild(Layout, 0);
		while (!Field->StepBuild(1000.0)) {}
		DecorInstances = Field->GetStats().BodyInstances;
	}
	const FString Missing = SpawnRoundElements();
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] ronda de prueba del editor: %s · %d elementos creados y %d piezas de decorado instanciadas%s."), *Layout.Summary(),
		RoundElements.Num(), DecorInstances, Missing.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (sin clase todavía: %s)"), *Missing));
	OnRoundLayoutReady.Broadcast(this);
}

void ATN_BeachRaceGenerator::ClearPreview()
{
	DestroyRoundElements();
	ApplyLayoutLocal(TNBeachLayout::FRoundLayout());
	if (IsValid(DecorField))
	{
		DecorField->Destroy();
		DecorField = nullptr;
	}
}

void ATN_BeachRaceGenerator::TickAutoGenerate(float DeltaSeconds)
{
	if (!bAutoGenerateIfIdle || !HasAuthority() || RoundNet.Round > 0) { return; }
	IdleTime += DeltaSeconds;
	if (IdleTime < TNBeachRace::IdleSecondsAlone) { return; }
	const UWorld* World = GetWorld();
	const AGameModeBase* GM = World ? World->GetAuthGameMode() : nullptr;
	const UClass* RaceMode = FindObject<UClass>(nullptr, TEXT("/Script/Tortunabo.TN_BeachRaceGameMode"));
	const bool bRaceMode = GM && RaceMode && GM->IsA(RaceMode);
	if (bRaceMode && IdleTime < TNBeachRace::IdleSecondsWithRaceMode) { return; }
	if (bRaceMode)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Playa] El GameMode de la carrera no ha repartido en %.0f s: reparto una ronda al azar."), IdleTime);
	}
	GenerateRound(FMath::Rand());
}

// ─────────────────────────────────────────────────────────────────────────────
// Meta: tortugas en el agua
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachRaceGenerator::TickTurtles(float /*DeltaSeconds*/)
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	if (!GS) { return; }
	const bool bServer = HasAuthority();
	const bool bArmed = RoundNet.Round > 0 && !RoundNet.bCleared;
	for (APlayerState* PS : GS->PlayerArray)
	{
		ACharacter* Turtle = PS ? Cast<ACharacter>(PS->GetPawn()) : nullptr;
		if (!Turtle) { continue; }
		const UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
		const double HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 50.0;
		const FVector Feet = Turtle->GetActorLocation() - FVector(0.0, 0.0, HalfHeight);
		const bool bWet = IsFinishWater(Feet);
		if (bLiving)
		{
			// Chapuzón al entrar en el agua de meta o en una poza.
			const FVector Local = GetActorTransform().InverseTransformPosition(Feet);
			const int32 PoolIndex = TNBeachLayout::PoolAt(FVector2D(Local.X, Local.Y), 1.0);
			const bool bInPool = PoolIndex != INDEX_NONE && Local.Z <= TNBeachLayout::Pools()[PoolIndex].Water + 30.0;
			const bool bAnyWet = bWet || bInPool;
			bool& bWasWet = WetTurtles.FindOrAdd(Turtle);
			if (bAnyWet && !bWasWet)
			{
				const double Surface = bInPool ? TNBeachLayout::Pools()[PoolIndex].Water : TNBeachLayout::WaterZ;
				Splash(GetActorTransform().TransformPosition(FVector(Local.X, Local.Y, Surface)));
			}
			bWasWet = bAnyWet;
		}
		if (bServer && bArmed && bWet && !Finishers.Contains(Turtle))
		{
			Finishers.Add(Turtle);
			UE_LOG(LogTortunabo, Log, TEXT("[Playa] ronda %d: %s ha tocado el agua de meta."), RoundNet.Round, *PS->GetPlayerName());
			OnTurtleReachedWater.Broadcast(Turtle);
			OnTurtleReachedWaterNative.Broadcast(Turtle);
		}
	}
}

void ATN_BeachRaceGenerator::ResetFinishWater()
{
	if (HasAuthority()) { Finishers.Reset(); }
}

// ─────────────────────────────────────────────────────────────────────────────
// Consultas
// ─────────────────────────────────────────────────────────────────────────────

FTransform ATN_BeachRaceGenerator::GetStartTransform(int32 PlayerIndex) const
{
	const FVector Local = TNBeachLayout::StartSpot(PlayerIndex) + FVector(0.0, 0.0, 110.0);
	return FTransform(GetActorRotation(), GetActorTransform().TransformPosition(Local));
}

FTransform ATN_BeachRaceGenerator::GetSprintStartTransform(int32 Index) const
{
	const FVector Spot = TNBeachLayout::SprintSpot(Index);
	const double Ground = TNBeachLayout::StampedZ(Layout.Stamps, Spot.X, Spot.Y, TNBeachLayout::SurfaceZ(Spot.X, Spot.Y));
	return FTransform(GetActorRotation(), GetActorTransform().TransformPosition(FVector(Spot.X, Spot.Y, Ground + 110.0)));
}

int32 ATN_BeachRaceGenerator::ClearElementsAround(const FVector& WorldCenter, float Radius)
{
	if (!HasAuthority()) { return 0; }
	const FTransform& Xf = GetActorTransform();
	const FVector LocalCenter = Xf.InverseTransformPosition(WorldCenter);
	const FVector2D Center(LocalCenter.X, LocalCenter.Y);
	int32 Removed = 0;
	for (int32 i = RoundElements.Num() - 1; i >= 0; --i)
	{
		ATN_BeachElement* Element = RoundElements[i];
		if (!IsValid(Element))
		{
			RoundElements.RemoveAt(i);
			continue;
		}
		// La huella: un disco o, en los alargados (Extent > 0 en el reparto), una cápsula a lo largo de su X.
		const FVector Local = Xf.InverseTransformPosition(Element->GetActorLocation());
		const FVector LocalAxis = Xf.InverseTransformVectorNoScale(Element->GetActorForwardVector());
		const FVector2D Axis = FVector2D(LocalAxis.X, LocalAxis.Y).GetSafeNormal();
		const double Half = 0.5 * FMath::Max(0.0, static_cast<double>(Element->GetSpec().Extent));
		const FVector2D P(Local.X, Local.Y);
		double T = 0.0;
		const double Dist = TNProcMap::DistPointSegment(Center, P - Axis * Half, P + Axis * Half, T);
		if (Dist > static_cast<double>(Radius) + Element->GetFootprintRadius()) { continue; }
		const int32 ItemIndex = ElementByItem.Find(Element);
		if (ItemIndex != INDEX_NONE) { ElementByItem[ItemIndex] = nullptr; }
		Element->Destroy();
		RoundElements.RemoveAt(i);
		++Removed;
	}
	// El decorado es local: el círculo se replica (DecorCuts) y cada máquina lo quita del suyo. También sus rebuscables.
	DecorCuts.Round = RoundNet.Round;
	DecorCuts.Circles.Add(FVector(Center.X, Center.Y, static_cast<double>(Radius)));
	const int32 DecorBefore = IsValid(DecorField) ? DecorField->GetStats().CutItems : 0;
	ApplyDecorCuts();
	const int32 DecorRemoved = IsValid(DecorField) ? DecorField->GetStats().CutItems - DecorBefore : 0;
	TNBeachLoot::ClearSearchAround(*this, WorldCenter, Radius);
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] ronda %d: %d elementos y %d piezas de decorado quitados en %.0f m alrededor de (%.0f, %.0f) m."), RoundNet.Round,
		Removed, DecorRemoved, Radius / 100.f, Center.X / 100.0, Center.Y / 100.0);
	return Removed + DecorRemoved;
}

bool ATN_BeachRaceGenerator::IsFinishWater(const FVector& WorldLocation) const
{
	return TNBeachLayout::IsFinishWaterLocal(GetActorTransform().InverseTransformPosition(WorldLocation));
}

bool ATN_BeachRaceGenerator::IsCliffJumpZone(const FVector& WorldLocation) const
{
	return TNBeachLayout::IsCliffJumpZoneLocal(GetActorTransform().InverseTransformPosition(WorldLocation));
}

float ATN_BeachRaceGenerator::GetCliffEdgeDistance(const FVector& WorldLocation) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	return static_cast<float>(Local.X - TNBeachLayout::EdgeX(Local.Y));
}

float ATN_BeachRaceGenerator::GetCourseProgress(const FVector& WorldLocation) const
{
	return static_cast<float>(TNBeachLayout::CourseProgress(GetActorTransform().InverseTransformPosition(WorldLocation)));
}

float ATN_BeachRaceGenerator::GetGroundHeightAt(const FVector& WorldLocation) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	// Con el índice de asientos por casillas del reparto (con ~5000 asientos, mirarlos todos en cada consulta era caro).
	const double Z = TNBeachLayout::SeatedZ(Layout, Local.X, Local.Y, TNBeachLayout::SurfaceZ(Local.X, Local.Y));
	return static_cast<float>(GetActorTransform().TransformPosition(FVector(Local.X, Local.Y, Z)).Z);
}

bool ATN_BeachRaceGenerator::TraceTerrainAt(const FVector& WorldLocation, float& OutZ) const
{
	// Solo las teselas con colisión cuya caja cubre el punto (unas pocas de cientos): la traza no ve nada más.
	const FVector Start(WorldLocation.X, WorldLocation.Y, WorldLocation.Z + 20000.0);
	const FVector End(WorldLocation.X, WorldLocation.Y, WorldLocation.Z - 20000.0);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TNBeachTerrainTrace), true);
	bool bFound = false;
	for (const TObjectPtr<UProceduralMeshComponent>& Tile : TerrainTiles)
	{
		if (!Tile || !Tile->IsRegistered() || Tile->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
		{
			continue;
		}
		const FBox Bounds = Tile->Bounds.GetBox();
		if (WorldLocation.X < Bounds.Min.X || WorldLocation.X > Bounds.Max.X || WorldLocation.Y < Bounds.Min.Y || WorldLocation.Y > Bounds.Max.Y)
		{
			continue;
		}
		FHitResult Hit;
		if (Tile->LineTraceComponent(Hit, Start, End, Params) && (!bFound || Hit.ImpactPoint.Z > OutZ))
		{
			OutZ = static_cast<float>(Hit.ImpactPoint.Z);
			bFound = true;
		}
	}
	return bFound;
}
