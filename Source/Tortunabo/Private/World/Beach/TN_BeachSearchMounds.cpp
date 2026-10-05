// ─────────────────────────────────────────────────────────────────────────────
// Montículos de arena removida de los rebuscables de la playa (ATN_BeachSearchRegistry;
// Docs/Modo_Carrera.md y Docs/Botin_Decorados.md, «Montículos de arena»). Junto a cada
// punto rebuscable, un montículo pequeño dice «aquí se puede rebuscar»: el servidor
// decide dónde y cómo (FTNBeachSearchMound, replicado una vez por ronda con el registro)
// y cada máquina con pantalla lo monta igual. Lejos, quietos e instanciados; cerca de una
// cámara local, los más cercanos tiemblan a ratos (con granitos que saltan), más a menudo
// y más fuerte con una tortuga cerca. Rebuscado, se aplasta y queda aplanado y quieto.
// ─────────────────────────────────────────────────────────────────────────────

#include "World/Beach/TN_BeachLoot.h"
#include "World/Beach/TN_BeachRaceGenerator.h"
#include "TN_BeachDecorKit.h"
#include "Art/TN_Art.h"
#include "../ProcMap/TN_ProcMapAmbientFX.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto
// del bloque.
namespace TNBeachMoundDetail
{
	TAutoConsoleVariable<int32> CVarMoundTilt(TEXT("TN.Beach.Mound.Tilt"), 1,
		TEXT("Montículos de los rebuscables de la playa: 1 los echa sobre la cuesta como su anillo (#744); 0 los deja derechos, ")
		TEXT("como antes, para comparar. Vale al montar la ronda (en -game, -dpcvars=TN.Beach.Mound.Tilt=0)."));

	/** Cada cuánto (s) se decide qué montículos tiemblan (la pose, en cada fotograma). */
	constexpr float CheckSeconds = 0.25f;
	/** Si la colisión del terreno aún no está lista, cada cuánto (s) se reintenta apoyar un montículo (como su anillo). */
	constexpr double RefitSeconds = 1.0;
	/** Lo que tarda en aplastarse al quedar rebuscado (s). */
	constexpr float FlattenSeconds = 0.35f;

	float SizeOf(uint8 Look)
	{
		return 0.8f + 0.45f * static_cast<float>(Look >> 2) / 63.f;
	}

	int32 VariantOf(uint8 Look)
	{
		return FMath::Clamp(static_cast<int32>(Look & 3u), 0, TNBeachDecorKit::NumSearchMoundVariants - 1);
	}

	/**
	 * Inclinación (en el mundo, desde +Z) del suelo bajo un montículo de tamaño Size: la misma cuenta y a la misma distancia
	 * del centro que el anillo fijo (TNSearchMarker), pero con la altura del generador (GetGroundHeightAt: igual en todas
	 * las máquinas y sin esperar a la colisión de las teselas). Identidad si el suelo es demasiado empinado.
	 */
	FQuat GroundTiltAt(const ATN_BeachRaceGenerator& Gen, const FVector& Center, double Size)
	{
		const double Reach = static_cast<double>(TNSearchMarker::RingRadiusForFoot(static_cast<float>(TNBeachDecorKit::SearchMoundRadius * 1.1 * Size)));
		FVector Rim[4];
		for (int32 k = 0; k < 4; ++k)
		{
			const double Angle = UE_DOUBLE_HALF_PI * static_cast<double>(k);
			Rim[k] = FVector(Center.X + FMath::Cos(Angle) * Reach, Center.Y + FMath::Sin(Angle) * Reach, 0.0);
			Rim[k].Z = static_cast<double>(Gen.GetGroundHeightAt(Rim[k]));
		}
		return TNSearchMarker::GroundTilt(Rim[0], Rim[1], Rim[2], Rim[3]);
	}

	/** Instancia escondida (diminuta y bajo la arena) mientras la mueve un componente de la reserva, o ya aplanada. */
	FTransform HiddenXf(const FTransform& Rest)
	{
		return FTransform(FQuat::Identity, Rest.GetLocation() - FVector(0.0, 0.0, 50000.0), FVector(0.01));
	}
}

bool ATN_BeachSearchRegistry::HasVisuals() const
{
	const UWorld* World = GetWorld();
	return World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;
}

void ATN_BeachSearchRegistry::BeginPlay()
{
	Super::BeginPlay();
	// Lo replicado puede llegar antes que BeginPlay: se ponen ya los montículos que haya.
	RefreshMounds();
}

void ATN_BeachSearchRegistry::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SleepTimer);
	}
	TNAmbientFX::RemoveOwner(this);
	MoundAnims.Reset();
	Mounds.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATN_BeachSearchRegistry::OnRep_SearchNet()
{
	RefreshMounds();
}

int32 ATN_BeachSearchRegistry::NumMoundAnims() const
{
	int32 Count = 0;
	for (const TNBeachSearchMoundTypes::FMoundAnim& Anim : MoundAnims)
	{
		Count += Anim.Mound != INDEX_NONE ? 1 : 0;
	}
	return Count;
}

bool ATN_BeachSearchRegistry::GetMoundFoot(int32 Index, FVector& OutGround, float& OutRadius) const
{
	if (!Mounds.IsValidIndex(Index))
	{
		return false;
	}
	// Tal como se monta en esta máquina (LiveXf sale de lo replicado y del generador: mismo sitio y tamaño en todas). El
	// radio de la base con los terrones que asoman (R x 1,1) y el tamaño del ejemplar.
	const FTransform& Xf = Mounds[Index].LiveXf;
	OutGround = Xf.GetLocation();
	OutRadius = static_cast<float>(TNBeachDecorKit::SearchMoundRadius * 1.1 * Xf.GetScale3D().X);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Montar y aplanar
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachSearchRegistry::RefreshMounds()
{
	if (!HasVisuals())
	{
		return;
	}
	// Otra ronda, otra tirada u otros puntos: todos de nuevo.
	const uint32 Key = HashCombine(HashCombine(GetTypeHash(SearchNet.Round), GetTypeHash(SearchNet.Salt)), GetTypeHash(SearchNet.Mounds.Num()));
	if (Key != BuiltMoundKey || Mounds.Num() != SearchNet.Mounds.Num())
	{
		BuiltMoundKey = Key;
		RebuildMounds();
	}
	for (int32 i = 0; i < Mounds.Num(); ++i)
	{
		const bool bUsed = IsUsed(i);
		if (bUsed == Mounds[i].bFlatShown)
		{
			continue;
		}
		int32 Slot = INDEX_NONE;
		for (int32 s = 0; s < MoundAnims.Num(); ++s)
		{
			if (MoundAnims[s].Mound == i)
			{
				Slot = s;
				break;
			}
		}
		if (bUsed && Slot != INDEX_NONE)
		{
			// Rebuscado mientras tiembla (cerca de una cámara): se aplasta a la vista y luego queda aplanado.
			if (MoundAnims[Slot].Flatten < 0.f)
			{
				MoundAnims[Slot].Flatten = 0.f;
				EmitGrains(Mounds[i].LiveXf.TransformPosition(FVector(0.0, 0.0, TNBeachDecorKit::SearchMoundHeight)), 8, 0.7f);
			}
			continue;
		}
		if (Slot != INDEX_NONE)
		{
			StopMoundAnim(Slot);
		}
		SetMoundFlat(i, bUsed);
	}
	SetActorTickEnabled(Mounds.Num() > 0);
}

UInstancedStaticMeshComponent* ATN_BeachSearchRegistry::EnsureMoundComp(int32 Index)
{
	const int32 NumVariants = TNBeachDecorKit::NumSearchMoundVariants;
	if (MoundComps.Num() < NumVariants + 1)
	{
		MoundComps.SetNum(NumVariants + 1);
	}
	if (IsValid(MoundComps[Index]))
	{
		return MoundComps[Index];
	}
	const bool bFlat = Index == NumVariants;
	UStaticMesh* Mesh = TNBeachDecorKit::SearchMoundMesh(bFlat ? 0 : Index, bFlat);
	if (!Mesh)
	{
		return nullptr;
	}
	// ISM (no jerárquico): cambia instancias sueltas al empezar y acabar de temblar y al aplanarse.
	UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient | RF_DuplicateTransient);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetupAttachment(RegistryRoot);
	Comp->SetStaticMesh(Mesh);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetGenerateOverlapEvents(false);
	// Pequeños y a ras: sin sombra (la dan los que tiemblan, que están cerca) y hasta 120 m de la cámara.
	Comp->SetCastShadow(false);
	Comp->bAffectDistanceFieldLighting = false;
	Comp->SetCullDistances(FMath::RoundToInt32(MoundCullDistance * 0.85f), FMath::RoundToInt32(MoundCullDistance));
	Comp->RegisterComponent();
	MoundComps[Index] = Comp;
	return Comp;
}

void ATN_BeachSearchRegistry::RebuildMounds()
{
	StopAllMoundAnims();
	for (UInstancedStaticMeshComponent* Comp : MoundComps)
	{
		if (IsValid(Comp) && Comp->GetInstanceCount() > 0)
		{
			Comp->ClearInstances();
		}
	}
	Mounds.Reset();
	const int32 NumVariants = TNBeachDecorKit::NumSearchMoundVariants;
	if (SearchNet.Mounds.Num() == 0)
	{
		return;
	}
	// En el espacio del generador (el mismo en todas las máquinas), llevado al mundo.
	const ATN_BeachRaceGenerator* Gen = ATN_BeachRaceGenerator::Find(this);
	const FTransform GenXf = Gen ? Gen->GetActorTransform() : FTransform::Identity;
	TArray<TArray<FTransform>> Live;
	Live.SetNum(NumVariants);
	TArray<FTransform> Flat;
	Mounds.Reserve(SearchNet.Mounds.Num());
	for (int32 i = 0; i < SearchNet.Mounds.Num(); ++i)
	{
		const FTNBeachSearchMound& Net = SearchNet.Mounds[i];
		const FVector Local(static_cast<double>(Net.X) * 2.0, static_cast<double>(Net.Y), static_cast<double>(Net.Z));
		const double Yaw = static_cast<double>(Net.Yaw) * 360.0 / 256.0;
		FTransform Xf = FTransform(FQuat(FVector::UpVector, FMath::DegreesToRadians(Yaw)), Local,
			FVector(static_cast<double>(TNBeachMoundDetail::SizeOf(Net.Look)))) * GenXf;
		const FQuat YawRot = Xf.GetRotation();
		const bool bTilt = TNBeachMoundDetail::CVarMoundTilt.GetValueOnGameThread() != 0;
		// Echado sobre la cuesta, como su anillo (#744): en una pendiente no queda medio enterrado ni flotando. De entrada con
		// la altura del generador; cerca de una cámara se apoya en la malla de verdad (FitMoundToGround).
		if (Gen && bTilt)
		{
			Xf.SetRotation(TNBeachMoundDetail::GroundTiltAt(*Gen, Xf.GetLocation(), Xf.GetScale3D().X) * YawRot);
		}
		TNBeachSearchMoundTypes::FMound& Mound = Mounds.AddDefaulted_GetRef();
		Mound.YawRot = YawRot;
		// Con TN.Beach.Mound.Tilt 0 (para comparar), derechos como antes: tampoco se apoyan después.
		Mound.bGroundFitted = !bTilt;
		Mound.Variant = TNBeachMoundDetail::VariantOf(Net.Look);
		Mound.LiveXf = Xf;
		Mound.FlatXf = Xf;
		Mound.bFlatShown = IsUsed(i);
		Mound.LiveInstance = Live[Mound.Variant].Num();
		Live[Mound.Variant].Add(Mound.bFlatShown ? TNBeachMoundDetail::HiddenXf(Xf) : Xf);
		Mound.FlatInstance = Flat.Num();
		Flat.Add(Mound.bFlatShown ? Xf : TNBeachMoundDetail::HiddenXf(Xf));
	}
	for (int32 v = 0; v <= NumVariants; ++v)
	{
		const TArray<FTransform>& Transforms = v < NumVariants ? Live[v] : Flat;
		if (Transforms.Num() == 0)
		{
			continue;
		}
		if (UInstancedStaticMeshComponent* Comp = EnsureMoundComp(v))
		{
			// Con la malla de arte de una ronda anterior (TNArt), vuelve a la generada antes de poner las instancias.
			UStaticMesh* Generated = TNBeachDecorKit::SearchMoundMesh(v < NumVariants ? v : 0, v == NumVariants);
			if (Generated && Comp->GetStaticMesh() != Generated)
			{
				Comp->EmptyOverrideMaterials();
				Comp->SetStaticMesh(Generated);
			}
			Comp->AddInstances(Transforms, false, true, false);
			TNArt::ApplyToInstances(Comp, v < NumVariants ? TN_ART("Beach.Search.Mound") : TN_ART("Beach.Search.MoundFlat"));
		}
	}
}

void ATN_BeachSearchRegistry::SetMoundFlat(int32 Index, bool bFlat)
{
	if (!Mounds.IsValidIndex(Index))
	{
		return;
	}
	TNBeachSearchMoundTypes::FMound& Mound = Mounds[Index];
	const int32 NumVariants = TNBeachDecorKit::NumSearchMoundVariants;
	UInstancedStaticMeshComponent* LiveComp = MoundComps.IsValidIndex(Mound.Variant) ? MoundComps[Mound.Variant].Get() : nullptr;
	UInstancedStaticMeshComponent* FlatComp = MoundComps.IsValidIndex(NumVariants) ? MoundComps[NumVariants].Get() : nullptr;
	if (IsValid(LiveComp))
	{
		TNArt::UpdateInstances(LiveComp, Mound.LiveInstance, { bFlat ? TNBeachMoundDetail::HiddenXf(Mound.LiveXf) : Mound.LiveXf }, true, true, true);
	}
	if (IsValid(FlatComp))
	{
		TNArt::UpdateInstances(FlatComp, Mound.FlatInstance, { bFlat ? Mound.FlatXf : TNBeachMoundDetail::HiddenXf(Mound.FlatXf) }, true, true, true);
	}
	Mound.bFlatShown = bFlat;
}

// ─────────────────────────────────────────────────────────────────────────────
// Temblar (cerca de una cámara local)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachSearchRegistry::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasVisuals() || Mounds.Num() == 0)
	{
		return;
	}
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	MoundCheckClock -= Dt;
	if (MoundCheckClock <= 0.f)
	{
		MoundCheckClock = TNBeachMoundDetail::CheckSeconds;
		UpdateMoundAnims();
	}
	for (int32 Slot = 0; Slot < MoundAnims.Num(); ++Slot)
	{
		const int32 Index = MoundAnims[Slot].Mound;
		if (Index == INDEX_NONE || PoseMoundAnim(Slot, Dt))
		{
			continue;
		}
		// Acabado de aplastar: aplanado y quieto (primero el aplanado, para que al soltarlo no vuelva el montón).
		SetMoundFlat(Index, true);
		StopMoundAnim(Slot);
	}
	if (FxGrains != INDEX_NONE)
	{
		TNAmbientFX::TickOwner(this, Dt);
	}
}

void ATN_BeachSearchRegistry::UpdateMoundAnims()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	TArray<FVector, TInlineAllocator<4>> Cameras;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (PC && PC->IsLocalController() && PC->PlayerCameraManager)
		{
			Cameras.Add(PC->PlayerCameraManager->GetCameraLocation());
		}
	}
	TArray<FVector, TInlineAllocator<8>> Turtles;
	if (const AGameStateBase* GS = World->GetGameState())
	{
		for (const APlayerState* PS : GS->PlayerArray)
		{
			if (const APawn* Pawn = PS ? PS->GetPawn() : nullptr)
			{
				Turtles.Add(Pawn->GetActorLocation());
			}
		}
	}
	const double RangeSq = FMath::Square(static_cast<double>(MoundAnimRange));
	const double NearSq = FMath::Square(static_cast<double>(MoundNearTurtle));
	const double FitSq = FMath::Square(static_cast<double>(MoundFitRange));
	int32 FitsLeft = MaxMoundFitsPerCheck;
	TArray<TPair<double, int32>> Wanted;
	for (int32 i = 0; i < Mounds.Num(); ++i)
	{
		TNBeachSearchMoundTypes::FMound& Mound = Mounds[i];
		const FVector At = Mound.LiveXf.GetLocation();
		bool bNear = false;
		for (const FVector& Turtle : Turtles)
		{
			bNear |= FVector::DistSquared2D(Turtle, At) < NearSq;
		}
		Mound.bTurtleNear = bNear;
		double Best = TNumericLimits<double>::Max();
		for (const FVector& Camera : Cameras)
		{
			Best = FMath::Min(Best, FVector::DistSquared(Camera, At));
		}
		// Cerca de una cámara se apoya en la malla del terreno, como su anillo (también los ya aplanados).
		if (!Mound.bGroundFitted && FitsLeft > 0 && Best < FitSq && Mound.FitTries < MaxMoundFitTries && World->GetTimeSeconds() >= Mound.NextFitTime)
		{
			--FitsLeft;
			FitMoundToGround(i);
		}
		if (Mound.bFlatShown)
		{
			continue;
		}
		if (Best < RangeSq)
		{
			Wanted.Emplace(Best, i);
		}
	}
	Wanted.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	if (Wanted.Num() > MaxMoundAnims)
	{
		Wanted.SetNum(MaxMoundAnims);
	}
	TSet<int32> Keep;
	for (const TPair<double, int32>& Entry : Wanted)
	{
		Keep.Add(Entry.Value);
	}
	TSet<int32> Running;
	for (int32 Slot = 0; Slot < MoundAnims.Num(); ++Slot)
	{
		TNBeachSearchMoundTypes::FMoundAnim& Anim = MoundAnims[Slot];
		if (Anim.Mound == INDEX_NONE)
		{
			continue;
		}
		// El que se está aplastando acaba aunque ya no toque.
		if (!Keep.Contains(Anim.Mound) && Anim.Flatten < 0.f)
		{
			StopMoundAnim(Slot);
			continue;
		}
		Running.Add(Anim.Mound);
		// Una tortuga que llega: tiembla ya.
		if (Mounds[Anim.Mound].bTurtleNear && Anim.BurstLeft <= 0.f)
		{
			Anim.NextBurst = FMath::Min(Anim.NextBurst, 0.1f);
		}
	}
	for (const TPair<double, int32>& Entry : Wanted)
	{
		if (!Running.Contains(Entry.Value))
		{
			StartMoundAnim(Entry.Value);
		}
	}
}

void ATN_BeachSearchRegistry::FitMoundToGround(int32 Index)
{
	UWorld* World = GetWorld();
	if (!World || !Mounds.IsValidIndex(Index))
	{
		return;
	}
	TNBeachSearchMoundTypes::FMound& Mound = Mounds[Index];
	Mound.FitTries = static_cast<uint8>(FMath::Min(255, static_cast<int32>(Mound.FitTries) + 1));
	// Sin colisión todavía (se está cocinando): otra vez en un segundo.
	Mound.NextFitTime = World->GetTimeSeconds() + TNBeachMoundDetail::RefitSeconds;

	// Las mismas cuatro trazas, a la misma distancia del centro, que las de su anillo (FitMarkerToGround): el mismo suelo.
	const FVector Center = Mound.LiveXf.GetLocation();
	const FVector Scale = Mound.LiveXf.GetScale3D();
	const double Reach = static_cast<double>(TNSearchMarker::RingRadiusForFoot(static_cast<float>(TNBeachDecorKit::SearchMoundRadius * 1.1 * Scale.X)));
	FVector Rim[4];
	const int32 Hits = TNSearchMarker::TraceRimGround(World, Center, Reach, this, Rim);
	if (Hits == 0)
	{
		return;
	}
	Mound.bGroundFitted = Hits == 4;
	// La inclinación, la del plano del anillo; la altura de su pie, la del terreno justo debajo (solo las teselas: sin el
	// decorado que le quede encima). En una cresta o una hondonada la media del anillo se aleja de ella y el montículo
	// quedaría medio enterrado o flotando; sin ese dato (o si se aleja mucho), la media.
	double GroundZ = (Rim[0].Z + Rim[1].Z + Rim[2].Z + Rim[3].Z) * 0.25;
	float TerrainZ = 0.f;
	const ATN_BeachRaceGenerator* Gen = ATN_BeachRaceGenerator::Find(this);
	if (Gen && Gen->TraceTerrainAt(Center, TerrainZ) && FMath::Abs(static_cast<double>(TerrainZ) - GroundZ) <= 120.0 + 0.4 * Reach)
	{
		GroundZ = static_cast<double>(TerrainZ);
	}
	const FVector Ground(Center.X, Center.Y, GroundZ);
	const FTransform Fitted(TNSearchMarker::GroundTilt(Rim[0], Rim[1], Rim[2], Rim[3]) * Mound.YawRot, Ground, Scale);
	if (Fitted.Equals(Mound.LiveXf, 0.1f))
	{
		return;
	}
	Mound.LiveXf = Fitted;
	Mound.FlatXf = Fitted;

	// Su instancia visible (la viva, o la aplanada si ya está rebuscado) se pone al día. La del que tiembla la mueve su
	// componente de la reserva con LiveXf en cada fotograma, y su instancia viva sigue escondida.
	const int32 NumVariants = TNBeachDecorKit::NumSearchMoundVariants;
	if (Mound.bFlatShown)
	{
		UInstancedStaticMeshComponent* FlatComp = MoundComps.IsValidIndex(NumVariants) ? MoundComps[NumVariants].Get() : nullptr;
		if (IsValid(FlatComp))
		{
			TNArt::UpdateInstances(FlatComp, Mound.FlatInstance, { Fitted }, true, true, true);
		}
		return;
	}
	for (const TNBeachSearchMoundTypes::FMoundAnim& Anim : MoundAnims)
	{
		if (Anim.Mound == Index)
		{
			return;
		}
	}
	UInstancedStaticMeshComponent* LiveComp = MoundComps.IsValidIndex(Mound.Variant) ? MoundComps[Mound.Variant].Get() : nullptr;
	if (IsValid(LiveComp))
	{
		TNArt::UpdateInstances(LiveComp, Mound.LiveInstance, { Fitted }, true, true, true);
	}
}

void ATN_BeachSearchRegistry::StartMoundAnim(int32 Index)
{
	if (!Mounds.IsValidIndex(Index))
	{
		return;
	}
	int32 Slot = INDEX_NONE;
	for (int32 s = 0; s < MoundAnims.Num(); ++s)
	{
		if (MoundAnims[s].Mound == INDEX_NONE)
		{
			Slot = s;
			break;
		}
	}
	if (Slot == INDEX_NONE)
	{
		Slot = MoundAnims.AddDefaulted();
		MoundAnimPool.SetNum(MoundAnims.Num());
	}
	const TNBeachSearchMoundTypes::FMound& Mound = Mounds[Index];
	UStaticMeshComponent* Comp = MoundAnimPool[Slot];
	if (!IsValid(Comp))
	{
		Comp = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient | RF_DuplicateTransient);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetupAttachment(RegistryRoot);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetGenerateOverlapEvents(false);
		Comp->bAffectDistanceFieldLighting = false;
		Comp->RegisterComponent();
		MoundAnimPool[Slot] = Comp;
	}
	// El componente de la reserva cambia de montículo: sin la malla de arte del anterior mientras se prepara y con la de este
	// después (Docs/Arte_Assets.md). Sin sustitutos no hace nada.
	TNArt::ApplyToComponent(Comp, NAME_None);
	Comp->SetStaticMesh(TNBeachDecorKit::SearchMoundMesh(Mound.Variant, false));
	// Cerca de la cámara: con sombra, que se lee mejor el bulto.
	Comp->SetCastShadow(true);
	Comp->SetVisibility(true);
	TNArt::ApplyToComponent(Comp, TN_ART("Beach.Search.Mound"));
	Comp->SetWorldTransform(Mound.LiveXf);
	TNBeachSearchMoundTypes::FMoundAnim& Anim = MoundAnims[Slot];
	Anim = TNBeachSearchMoundTypes::FMoundAnim();
	Anim.Mound = Index;
	// No todos a la vez: el primer temblor, al azar en los primeros segundos (enseguida si hay una tortuga cerca).
	Anim.NextBurst = Mound.bTurtleNear ? FMath::FRandRange(0.05f, 0.3f) : FMath::FRandRange(0.3f, 3.f);
	// Su instancia quieta se esconde mientras la mueve el componente.
	if (UInstancedStaticMeshComponent* LiveComp = MoundComps.IsValidIndex(Mound.Variant) ? MoundComps[Mound.Variant].Get() : nullptr)
	{
		TNArt::UpdateInstances(LiveComp, Mound.LiveInstance, { TNBeachMoundDetail::HiddenXf(Mound.LiveXf) }, true, true, true);
	}
}

void ATN_BeachSearchRegistry::StopMoundAnim(int32 Slot)
{
	if (!MoundAnims.IsValidIndex(Slot))
	{
		return;
	}
	TNBeachSearchMoundTypes::FMoundAnim& Anim = MoundAnims[Slot];
	if (Mounds.IsValidIndex(Anim.Mound) && !Mounds[Anim.Mound].bFlatShown)
	{
		// Vuelve su instancia quieta (si no se ha aplanado).
		const TNBeachSearchMoundTypes::FMound& Mound = Mounds[Anim.Mound];
		if (UInstancedStaticMeshComponent* LiveComp = MoundComps.IsValidIndex(Mound.Variant) ? MoundComps[Mound.Variant].Get() : nullptr)
		{
			TNArt::UpdateInstances(LiveComp, Mound.LiveInstance, { Mound.LiveXf }, true, true, true);
		}
	}
	if (MoundAnimPool.IsValidIndex(Slot) && IsValid(MoundAnimPool[Slot]))
	{
		MoundAnimPool[Slot]->SetVisibility(false);
	}
	Anim.Mound = INDEX_NONE;
}

void ATN_BeachSearchRegistry::StopAllMoundAnims()
{
	for (int32 Slot = 0; Slot < MoundAnims.Num(); ++Slot)
	{
		if (MoundAnims[Slot].Mound != INDEX_NONE)
		{
			StopMoundAnim(Slot);
		}
	}
}

bool ATN_BeachSearchRegistry::PoseMoundAnim(int32 Slot, float DeltaSeconds)
{
	TNBeachSearchMoundTypes::FMoundAnim& Anim = MoundAnims[Slot];
	UStaticMeshComponent* Comp = MoundAnimPool.IsValidIndex(Slot) ? MoundAnimPool[Slot].Get() : nullptr;
	if (!IsValid(Comp) || !Mounds.IsValidIndex(Anim.Mound))
	{
		return true;
	}
	const TNBeachSearchMoundTypes::FMound& Mound = Mounds[Anim.Mound];
	Anim.Time += DeltaSeconds;
	const FVector Top = Mound.LiveXf.TransformPosition(FVector(0.0, 0.0, TNBeachDecorKit::SearchMoundHeight));
	if (Anim.Flatten >= 0.f)
	{
		// Rebuscado: se aplasta (se ensancha un poco y baja casi a ras), temblando lo que le queda.
		Anim.Flatten += DeltaSeconds;
		const float K = FMath::Clamp(Anim.Flatten / TNBeachMoundDetail::FlattenSeconds, 0.f, 1.f);
		const float Shake = (1.f - K) * 0.6f;
		const float T = Anim.Time;
		const FQuat Rot(Anim.ShakeAxis, FMath::DegreesToRadians(3.0 * Shake * FMath::Sin(T * 47.f)));
		const FVector Scale(1.0 + 0.18 * K, 1.0 + 0.18 * K, FMath::Max(0.12, 1.0 - 0.88 * K));
		Comp->SetWorldTransform(FTransform(Rot, FVector::ZeroVector, Scale) * Mound.LiveXf);
		return K < 1.f;
	}
	if (Anim.BurstLeft > 0.f)
	{
		Anim.BurstLeft -= DeltaSeconds;
	}
	else
	{
		Anim.NextBurst -= DeltaSeconds;
		if (Anim.NextBurst <= 0.f)
		{
			// Un temblor: corto y suave a ratos; con una tortuga cerca, más largo, más fuerte y más a menudo.
			const bool bNear = Mound.bTurtleNear;
			Anim.BurstLength = bNear ? FMath::FRandRange(0.55f, 0.9f) : FMath::FRandRange(0.35f, 0.6f);
			Anim.BurstLeft = Anim.BurstLength;
			Anim.Strength = bNear ? 1.f : 0.5f;
			const float Ang = FMath::FRandRange(0.f, UE_TWO_PI);
			Anim.ShakeAxis = FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.0);
			Anim.NextBurst = bNear ? FMath::FRandRange(0.4f, 1.2f) : FMath::FRandRange(2.5f, 6.f);
			EmitGrains(Top, bNear ? 7 : 3, bNear ? 1.1f : 0.8f);
		}
	}
	const float Envelope = Anim.BurstLeft > 0.f && Anim.BurstLength > 0.f
		? FMath::Sin(UE_PI * (1.f - Anim.BurstLeft / Anim.BurstLength)) * Anim.Strength : 0.f;
	const float T = Anim.Time;
	const FQuat Rot(Anim.ShakeAxis, FMath::DegreesToRadians(3.5 * Envelope * FMath::Sin(T * 47.f)));
	const FVector Scale(1.0 + 0.05 * Envelope * FMath::Sin(T * 41.f), 1.0 + 0.05 * Envelope * FMath::Sin(T * 43.f + 0.7f),
		1.0 + 0.12 * Envelope * FMath::Sin(T * 37.f + 1.3f));
	const FVector Offset(2.5 * Envelope * FMath::Sin(T * 53.f), 2.5 * Envelope * FMath::Cos(T * 59.f), 0.0);
	Comp->SetWorldTransform(FTransform(Rot, Offset, Scale) * Mound.LiveXf);
	return true;
}

void ATN_BeachSearchRegistry::EmitGrains(const FVector& Where, int32 Count, float SpeedScale)
{
	if (!HasVisuals() || Count <= 0)
	{
		return;
	}
	if (FxGrains == INDEX_NONE)
	{
		// Granitos de arena que saltan del montículo y caen (opacos, algo más oscuros que la arena).
		TNAmbientFX::FEmitterDesc Grains;
		Grains.Shape = TNAmbientFX::EShape::Ember;
		Grains.Color = FLinearColor(0.72f, 0.6f, 0.42f);
		Grains.MaxParticles = 80;
		Grains.Rate = 0.f;
		Grains.SpawnRadius = 35.f;
		Grains.SpawnHeight = 6.f;
		Grains.Speed = 260.f;
		Grains.SpeedJitter = 0.4f;
		Grains.Spread = 0.6f;
		Grains.Gravity = -980.f;
		Grains.Drag = 0.4f;
		Grains.LifeMin = 0.35f;
		Grains.LifeMax = 0.65f;
		Grains.SizeStart = 6.f;
		Grains.SizeEnd = 4.f;
		Grains.WakeDistance = 6000.f;
		FxGrains = TNAmbientFX::AddEmitter(this, Grains, Where);
	}
	TNAmbientFX::FEmitter* Emitter = TNAmbientFX::GetEmitter(this, FxGrains);
	if (!Emitter)
	{
		return;
	}
	Emitter->Origin = Where;
	Emitter->Desc.Direction = FVector::UpVector;
	// La velocidad solo se lee al nacer: se escala para este estallido y se deja como estaba.
	const float BaseSpeed = Emitter->Desc.Speed;
	Emitter->Desc.Speed = BaseSpeed * SpeedScale;
	TNAmbientFX::Burst(*Emitter, Count);
	Emitter->Desc.Speed = BaseSpeed;
	if (const UWorld* World = GetWorld())
	{
		LastGrainsTime = World->GetTimeSeconds();
	}
}
