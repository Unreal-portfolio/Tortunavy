// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachRaceGenerator — la ronda por partes (Docs/Modo_Carrera.md, «Rendimiento y
// red»): el reparto y las teselas del terreno se calculan en otro hilo; en el hilo de
// juego, unos milisegundos por fotograma, se suben las teselas con los asientos, se monta
// el decorado local e instanciado (ATN_BeachDecorField), se crean los actores replicados
// (servidor) y se reparte el botín (servidor). IsRoundReady espera a que esté todo. Y la
// consola TN.Beach.Perf.
// ─────────────────────────────────────────────────────────────────────────────

#include "World/Beach/TN_BeachRaceGenerator.h"
#include "World/Beach/TN_BeachDecorField.h"
#include "World/Beach/TN_BeachElement.h"
#include "World/Beach/TN_BeachLoot.h"
#include "World/TN_PickupInteractableBase.h"
#include "World/TN_ScorePickup.h"
#include "Core/TN_Log.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/NetworkObjectList.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Tasks/Task.h"
#include <atomic>

/** El reparto de una ronda hecho en otro hilo, con las teselas del terreno ya calculadas con sus asientos. */
struct FTNBeachLayoutJob
{
	TNBeachLayout::FRoundLayout Layout;
	TSharedPtr<FTNBeachTileBatch> Tiles;
	double LayoutMs = 0.0;
	double TilesMs = 0.0;
	std::atomic<bool> bDone{ false };
};

/** Una ronda a medias en esta máquina: por dónde va y lo que lleva gastado cada parte. */
struct FTNBeachRoundBuild
{
	enum class EStage : uint8
	{
		/** Esperando al reparto (en otro hilo). */
		Layout,
		/** Subiendo las teselas con los asientos (con su colisión). */
		Terrain,
		/** Montando el decorado local. */
		Decor,
		/** Creando los actores replicados (servidor). */
		Elements,
		Finish
	};

	int32 Round = 0;
	bool bServer = false;
	EStage Stage = EStage::Layout;
	TSharedPtr<FTNBeachLayoutJob> Job;
	double StartTime = 0.0;
	FTNBeachRoundTimings Timings;
	int32 NextItem = 0;
	TArray<int8> HasClass;
	TMap<FString, int32> MissingByClass;
};

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto
// del bloque.
namespace TNBeachRoundDetail
{
	TAutoConsoleVariable<float> CVarBuildBudgetMs(TEXT("TN.Beach.BuildBudgetMs"), 6.f,
		TEXT("Playa del modo carrera: milisegundos por fotograma para montar la ronda (asientos, decorado local y actores). Más = antes, con más tirones."));

	TAutoConsoleVariable<int32> CVarAsyncBuild(TEXT("TN.Beach.AsyncBuild"), 1,
		TEXT("Playa del modo carrera: 1 = la ronda se monta por partes (el reparto en otro hilo); 0 = toda de una vez en el mismo fotograma (como antes, para comparar)."));

	double Ms(double FromSeconds)
	{
		return (FPlatformTime::Seconds() - FromSeconds) * 1000.0;
	}

	/** El mundo con autoridad del mismo proceso (el propio si no es un cliente; en PIE, el del servidor del mismo mapa). */
	UWorld* FindAuthorityWorld(UWorld* InWorld)
	{
		if (!InWorld || InWorld->GetNetMode() != NM_Client)
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

	const TCHAR* DifficultyName(ETNProcDifficulty Difficulty)
	{
		switch (Difficulty)
		{
			case ETNProcDifficulty::Easy: return TEXT("fácil");
			case ETNProcDifficulty::Hard: return TEXT("difícil");
			default:                      return TEXT("normal");
		}
	}

	/** Recuento de la playa de un mundo para TN.Beach.Perf. */
	void ReportWorld(UWorld* World)
	{
		ATN_BeachRaceGenerator* Gen = World ? ATN_BeachRaceGenerator::Find(World) : nullptr;
		if (!Gen)
		{
			UE_LOG(LogTortunabo, Display, TEXT("[Playa] TN.Beach.Perf: no hay playa en este mundo."));
			return;
		}
		const bool bServer = World->GetNetMode() != NM_Client;
		const FTNBeachRoundTimings& T = Gen->GetLastRoundTimings();
		UE_LOG(LogTortunabo, Display, TEXT("[Playa] TN.Beach.Perf (%s) · ronda %d (semilla %d, %s)%s"), bServer ? TEXT("servidor") : TEXT("cliente"),
			Gen->GetRoundNumber(), Gen->GetRoundSeed(), DifficultyName(Gen->GetRoundDifficulty()), Gen->IsRoundReady() ? TEXT("") : TEXT(" · MONTÁNDOSE"));
		UE_LOG(LogTortunabo, Display, TEXT("  tiempos de la ronda %d: reparto %.0f ms (en otro hilo; espera %.0f ms), asientos %.0f ms, decorado %.0f ms, actores %.0f ms, botín %.0f ms · %d fotogramas, %.0f ms de principio a fin"),
			T.Round, T.LayoutMs, T.LayoutWaitMs, T.TerrainMs, T.DecorMs, T.ElementsMs, T.LootMs, T.Frames, T.WallMs);

		if (const ATN_BeachDecorField* Field = Gen->GetDecorField())
		{
			const FTNBeachDecorStats S = Field->GetStats();
			UE_LOG(LogTortunabo, Display, TEXT("  decorado local: %d piezas (%d quitadas) · instancias: %d fijas (%d con colisión, %d con sombra siempre), %d partes que se mueven, %d copias de sombra cercana · %d componentes · %d partes animándose · montado en %.0f ms y %d fotogramas"),
				S.Items, S.CutItems, S.BodyInstances, S.CollisionInstances, S.ShadowInstances, S.MovingInstances, S.ShadowTwinInstances, S.Components, S.Animators,
				S.BuildMs, S.BuildFrames);
		}

		int32 Elements = 0;
		int32 ByCategory[3] = {};
		int32 Dormant = 0;
		int32 AlwaysRelevant = 0;
		double RelevanceSum = 0.0;
		for (TActorIterator<ATN_BeachElement> It(World); It; ++It)
		{
			const ATN_BeachElement* Element = *It;
			if (!IsValid(Element))
			{
				continue;
			}
			++Elements;
			++ByCategory[FMath::Clamp(static_cast<int32>(TNBeach::CategoryOf(Element->GetSpec().Element)), 0, 2)];
			Dormant += Element->NetDormancy > DORM_Awake ? 1 : 0;
			AlwaysRelevant += Element->bAlwaysRelevant ? 1 : 0;
			RelevanceSum += FMath::Sqrt(static_cast<double>(Element->GetNetCullDistanceSquared()));
		}
		UE_LOG(LogTortunabo, Display, TEXT("  actores de la playa en este mundo: %d (%d decorado, %d trampas y lanzadores, %d enemigos) · con dormancy %d · siempre relevantes %d · relevancia media %.0f m"),
			Elements, ByCategory[0], ByCategory[1], ByCategory[2], Dormant, AlwaysRelevant, Elements > 0 ? RelevanceSum / Elements / 100.0 : 0.0);

		int32 Spots = 0;
		for (TActorIterator<ATN_BeachSearchSpot> It(World); It; ++It)
		{
			++Spots;
		}
		int32 Points = 0;
		int32 Used = 0;
		int32 Shaking = 0;
		if (const ATN_BeachSearchRegistry* Registry = ATN_BeachSearchRegistry::Find(World))
		{
			Points = Registry->NumPoints();
			Used = Registry->NumUsed();
			Shaking = Registry->NumMoundAnims();
		}
		int32 Pickups = 0;
		for (TActorIterator<ATN_PickupInteractableBase> It(World); It; ++It)
		{
			++Pickups;
		}
		int32 Shells = 0;
		for (TActorIterator<ATN_ScorePickup> It(World); It; ++It)
		{
			++Shells;
		}
		UE_LOG(LogTortunabo, Display, TEXT("  rebuscables: %d puntos (%d ya rebuscados, con su montículo aplanado) y %d actores ahora (solo cerca de alguna tortuga) · %d montículos temblando · objetos sueltos %d · conchas %d"),
			Points, Used, Spots, Shaking, Pickups, Shells);

		if (const UNetDriver* Driver = World->GetNetDriver())
		{
			const FNetworkObjectList& Objects = Driver->GetNetworkObjectList();
			UE_LOG(LogTortunabo, Display, TEXT("  red: %d actores replicados en la lista, %d activos, %d dormidos en todas las conexiones · %d conexiones"),
				Objects.GetAllObjects().Num(), Objects.GetActiveObjects().Num(), Objects.GetDormantObjectsOnAllConnections().Num(), Driver->ClientConnections.Num());
		}
	}

	FAutoConsoleCommandWithWorld CmdPerf(TEXT("TN.Beach.Perf"),
		TEXT("Playa del modo carrera: instancias del decorado local, actores replicados y dormidos, rebuscables y tiempos de la última ronda (en esta ventana y, en PIE, también el servidor)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			ReportWorld(World);
			UWorld* Authority = FindAuthorityWorld(World);
			if (Authority && Authority != World)
			{
				ReportWorld(Authority);
			}
		}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Ronda por partes
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachRaceGenerator::MakeRoundLayout(int32 Seed, ETNProcDifficulty InDifficulty, bool bSprint, TNBeachLayout::FRoundLayout& Out)
{
	TNBeachLayout::GenerateRound(Seed, InDifficulty, Out, bSprint);
}

void ATN_BeachRaceGenerator::StartRoundBuild(bool bNow)
{
	CancelRoundBuild();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const bool bSync = bNow || !World->IsGameWorld() || TNBeachRoundDetail::CVarAsyncBuild.GetValueOnGameThread() == 0;
	TSharedPtr<FTNBeachRoundBuild> Build = MakeShared<FTNBeachRoundBuild>();
	Build->Round = RoundNet.Round;
	Build->bServer = HasAuthority();
	Build->StartTime = FPlatformTime::Seconds();
	Build->Timings.Round = RoundNet.Round;
	Build->Timings.bServer = Build->bServer;
	Build->Job = MakeShared<FTNBeachLayoutJob>();
	PendingBuild = Build;
	PendingRound = RoundNet.Round;

	// Todo lo que necesita el otro hilo, copiado: el generador puede cambiar (o irse) mientras tanto. Las teselas se
	// calculan con los asientos que tiene ahora el terreno (para quitarlos) y los nuevos; todas si alguna quedó a medias.
	const int32 Seed = RoundNet.Seed;
	const ETNProcDifficulty RoundDifficulty = RoundNet.Difficulty;
	const bool bSprintLayout = RoundNet.bSprintLayout;
	const bool bTerrain = bBuilt && TilesX > 0 && TilesY > 0;
	const bool bAllTiles = bTerrainDirtyAll;
	bTerrainDirtyAll = false;
	const int32 NumTilesX = TilesX;
	const int32 NumTilesY = TilesY;
	TSharedPtr<FTNBeachLayoutJob> Job = Build->Job;
	auto Work = [Job, Seed, RoundDifficulty, bSprintLayout, bTerrain, bAllTiles, NumTilesX, NumTilesY, Xs = GridXs, Ys = GridYs, OldStamps = Layout.Stamps]()
	{
		const double T0 = FPlatformTime::Seconds();
		MakeRoundLayout(Seed, RoundDifficulty, bSprintLayout, Job->Layout);
		const double T1 = FPlatformTime::Seconds();
		if (bTerrain)
		{
			TArray<int32> Touched;
			FindTouchedTiles(Xs, Ys, NumTilesX, NumTilesY, OldStamps, Job->Layout.Stamps, bAllTiles, Touched);
			Job->Tiles = ComputeTileBatch(Xs, Ys, NumTilesX, Touched, Job->Layout.Stamps);
		}
		Job->LayoutMs = (T1 - T0) * 1000.0;
		Job->TilesMs = (FPlatformTime::Seconds() - T1) * 1000.0;
		Job->bDone.store(true);
	};
	if (bSync)
	{
		Work();
		TickRoundBuild(true);
		return;
	}
	UE::Tasks::Launch(UE_SOURCE_LOCATION, MoveTemp(Work));
}

void ATN_BeachRaceGenerator::CancelRoundBuild()
{
	// Teselas a medio subir: unas llevan los asientos nuevos y otras no; la ronda siguiente las rehace todas.
	if (PendingTiles.IsValid())
	{
		bTerrainDirtyAll = true;
	}
	PendingTiles.Reset();
	PendingBuild.Reset();
	PendingRound = 0;
}

void ATN_BeachRaceGenerator::TickRoundBuild(bool bNow)
{
	using EStage = FTNBeachRoundBuild::EStage;
	// Sujeta la ronda mientras dura esta llamada (algo de dentro podría empezar otra o soltarla).
	const TSharedPtr<FTNBeachRoundBuild> Build = PendingBuild;
	if (!Build.IsValid())
	{
		return;
	}
	++Build->Timings.Frames;
	const double Budget = bNow ? 1.0e9 : FMath::Max(0.5, static_cast<double>(TNBeachRoundDetail::CVarBuildBudgetMs.GetValueOnGameThread())) / 1000.0;
	const double FrameStart = FPlatformTime::Seconds();
	auto Left = [Budget, FrameStart]() { return Budget - (FPlatformTime::Seconds() - FrameStart); };
	FTNBeachRoundTimings& Timings = Build->Timings;
	while (PendingBuild == Build)
	{
		const double T0 = FPlatformTime::Seconds();
		switch (Build->Stage)
		{
		case EStage::Layout:
		{
			if (!Build->Job.IsValid() || !Build->Job->bDone.load())
			{
				return;
			}
			// El reparto ya está: se aplica en esta máquina (los asientos, en las teselas ya calculadas en el otro hilo).
			FTNBeachLayoutJob& Job = *Build->Job;
			Timings.LayoutMs = Job.LayoutMs + Job.TilesMs;
			Timings.LayoutWaitMs = (T0 - Build->StartTime) * 1000.0;
			Layout = MoveTemp(Job.Layout);
			AppliedRound = Build->Round;
			PendingTiles = Job.Tiles;
			Build->Job.Reset();
			ElementByItem.Reset();
			ElementByItem.SetNum(Layout.Items.Num());
			// El nido del sprint (si lo hay) se hizo con el reparto anterior: se rehace con los asientos nuevos.
			SyncStartEggsWithLayout();
			if (Build->bServer && !Layout.bPassageOk)
			{
				UE_LOG(LogTortunabo, Warning, TEXT("[Playa] ronda %d: el paso libre de %.0f m no llega de la salida al borde."), Build->Round,
					TNBeachLayout::MinPassage / 100.0);
			}
			Build->Stage = EStage::Terrain;
			Timings.TerrainMs += TNBeachRoundDetail::Ms(T0);
			break;
		}
		case EStage::Terrain:
		{
			const bool bDone = UploadPendingTiles(Left());
			Timings.TerrainMs += TNBeachRoundDetail::Ms(T0);
			if (!bDone)
			{
				return;
			}
			PendingTiles.Reset();
			BuildFootprints();
			AppliedDecorCuts = 0;
			if (ATN_BeachDecorField* Field = EnsureDecorField())
			{
				Field->BeginBuild(Layout, Build->Round);
			}
			Build->Stage = EStage::Decor;
			break;
		}
		case EStage::Decor:
		{
			const bool bDone = !IsValid(DecorField) || DecorField->StepBuild(Left());
			Timings.DecorMs += TNBeachRoundDetail::Ms(T0);
			if (!bDone)
			{
				return;
			}
			// Lo que se haya quitado ya de esta ronda (el nido del sprint), también del decorado recién montado.
			ApplyDecorCuts();
			if (Build->bServer)
			{
				Build->HasClass.Init(-1, static_cast<int32>(ETNBeachElement::Count));
				Build->Stage = EStage::Elements;
			}
			else
			{
				Build->Stage = EStage::Finish;
			}
			break;
		}
		case EStage::Elements:
		{
			// Unos cuantos por fotograma: cada uno monta sus mallas al aparecer.
			bool bFirst = true;
			while (Build->NextItem < Layout.Items.Num() && (bFirst || Left() > 0.0))
			{
				bFirst = false;
				SpawnRoundElement(Build->NextItem, Build->HasClass, Build->MissingByClass);
				++Build->NextItem;
			}
			Timings.ElementsMs += TNBeachRoundDetail::Ms(T0);
			if (Build->NextItem < Layout.Items.Num())
			{
				return;
			}
			Build->Stage = EStage::Finish;
			break;
		}
		case EStage::Finish:
		default:
			FinishRoundBuild();
			return;
		}
		if (Left() <= 0.0)
		{
			return;
		}
	}
}

void ATN_BeachRaceGenerator::FinishRoundBuild()
{
	const TSharedPtr<FTNBeachRoundBuild> Build = PendingBuild;
	if (!Build.IsValid())
	{
		return;
	}
	FTNBeachRoundTimings& Timings = Build->Timings;
	int32 Actors = 0;
	FString Missing;
	if (Build->bServer)
	{
		// El botín, con todo ya en su sitio (las conchas que van encima de algo buscan su suelo con trazas).
		const double T0 = FPlatformTime::Seconds();
		bRoundReady = true;
		TNBeachLoot::SpawnRoundLoot(*this);
		Timings.LootMs = TNBeachRoundDetail::Ms(T0);
		Actors = RoundElements.Num();
		Missing = DescribeMissing(Build->MissingByClass);
		UE_LOG(LogTortunabo, Log, TEXT("[Playa] ronda %d: %s · %d elementos replicados creados%s."), Build->Round, *Layout.Summary(), Actors,
			Missing.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (sin clase todavía: %s)"), *Missing));
	}
	else if (UWorld* World = GetWorld())
	{
		// En un cliente, los actores de la playa que ya le han llegado (el resto, según se acerque).
		for (TActorIterator<ATN_BeachElement> It(World); It; ++It)
		{
			++Actors;
		}
	}
	Timings.Actors = Actors;
	Timings.DecorInstances = IsValid(DecorField) ? DecorField->GetStats().BodyInstances : 0;
	Timings.WallMs = TNBeachRoundDetail::Ms(Build->StartTime);
	LastTimings = Timings;
	PendingBuild.Reset();
	PendingRound = 0;
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] ronda %d: reparto %.0f ms, decorado %.0f ms, actores %d · asientos %.0f ms, elementos %.0f ms, botín %.0f ms · %d piezas de decorado instanciadas · %d fotogramas, %.0f ms de principio a fin (%s)."),
		Build->Round, Timings.LayoutMs, Timings.DecorMs, Actors, Timings.TerrainMs, Timings.ElementsMs, Timings.LootMs, Timings.DecorInstances, Timings.Frames,
		Timings.WallMs, Build->bServer ? TEXT("servidor") : TEXT("cliente"));
	OnRoundLayoutReady.Broadcast(this);
}

ATN_BeachDecorField* ATN_BeachRaceGenerator::EnsureDecorField()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	// El de este mundo (el de la ronda de prueba del editor no pasa a PIE: es transitorio).
	if (IsValid(DecorField) && DecorField->GetWorld() == World)
	{
		return DecorField;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient | RF_DuplicateTransient;
	ATN_BeachDecorField* Field = World->SpawnActor<ATN_BeachDecorField>(ATN_BeachDecorField::StaticClass(), GetActorTransform(), Params);
	if (!Field)
	{
		UE_LOG(LogTortunabo, Error, TEXT("[Playa] No se ha podido crear el decorado local de la playa."));
		return nullptr;
	}
	if (BeachRoot)
	{
		Field->AttachToComponent(BeachRoot, FAttachmentTransformRules::KeepWorldTransform);
	}
	DecorField = Field;
	return Field;
}

void ATN_BeachRaceGenerator::SyncStartEggsWithLayout()
{
	if (IsStartEggLineStale())
	{
		ApplyStartEggs(false);
	}
}

void ATN_BeachRaceGenerator::ApplyDecorCuts()
{
	if (!IsValid(DecorField) || DecorCuts.Round != AppliedRound || !DecorField->IsBuiltFor(AppliedRound))
	{
		// Aún no está el decorado de esta ronda (o los cortes son de otra): se quitan al acabar de montarlo.
		return;
	}
	for (int32 i = AppliedDecorCuts; i < DecorCuts.Circles.Num(); ++i)
	{
		const FVector& Circle = DecorCuts.Circles[i];
		DecorField->CutCircle(FVector2D(Circle.X, Circle.Y), Circle.Z);
	}
	AppliedDecorCuts = DecorCuts.Circles.Num();
}

void ATN_BeachRaceGenerator::OnRep_DecorCuts()
{
	ApplyDecorCuts();
}
