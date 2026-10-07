// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachDecorField — el decorado de cada ronda de la playa, local en cada máquina y
// agrupado en mallas instanciadas (Docs/Modo_Carrera.md, «Rendimiento y red»). Las
// recetas, la colocación y la animación son las de ATN_BeachDecor (TNBeachDecorKit):
// una pieza sale igual como actor (TN.Beach.Place) que aquí.
// ─────────────────────────────────────────────────────────────────────────────

#include "World/Beach/TN_BeachDecorField.h"
#include "TN_BeachDecorKit.h"
#include "World/ProcMap/TN_ProcMapMath.h"
#include "Art/TN_Art.h"
#include "Core/TN_Log.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto
// del bloque.
namespace TNBeachDecorFieldDetail
{
	/** Clave de lote de una receta (elemento y variante) o de una pieza de tramo; con MovingBit, su parte que se mueve. */
	uint32 SingleKey(ETNBeachElement Element, int32 Variant)
	{
		return (static_cast<uint32>(Element) << 16) | (static_cast<uint32>(Variant) & 0xFFu);
	}

	uint32 PieceKey(ETNBeachElement Element, int32 Piece)
	{
		return (static_cast<uint32>(Element) << 16) | 0x8000u | (static_cast<uint32>(Piece) & 0xFFu);
	}

	constexpr uint32 MovingBit = 0x4000u;

	/** Tipos de componente de un lote: el que se ve (con colisión), el de la parte que se mueve y la copia de sombra. */
	constexpr uint8 KindBody = 0;
	constexpr uint8 KindMoving = 1;
	constexpr uint8 KindTwin = 2;

	/** Instancia escondida (diminuta y bajo la arena) mientras la mueve un componente de la reserva. */
	FTransform HiddenXf(const FTransform& Rest)
	{
		return FTransform(FQuat::Identity, Rest.GetLocation() - FVector(0.0, 0.0, 50000.0), FVector(0.01));
	}

	/** Pieza de arte de un lote (Docs/Arte_Assets.md): su parte que se mueve, su pieza de tramo o su malla fija. */
	FName SlotOf(const TNBeachDecorFieldTypes::FBatch& Batch)
	{
		if (Batch.bMoving)
		{
			return TNBeachDecorKit::MovingSlot(Batch.Element);
		}
		if (TNBeachProp::IsTiled(Batch.Element))
		{
			return TNBeachDecorKit::PieceSlot(Batch.Element, static_cast<int32>(Batch.Key & 0xFFu));
		}
		return TNBeachDecorKit::BodySlot(Batch.Element);
	}

	/**
	 * Quita los gemelos de colisión que deja TNArt::ApplyToInstances con un sustituto de arte (TNArt::IsCollisionTwin) con la
	 * malla generada Generated, o todos con nullptr. Sin sustitutos no hay ninguno.
	 */
	void DestroyArtTwins(AActor* Owner, const TArray<TObjectPtr<UInstancedStaticMeshComponent>>& Comps, const UStaticMesh* Generated)
	{
		TInlineComponentArray<UInstancedStaticMeshComponent*> All;
		Owner->GetComponents(All);
		for (UInstancedStaticMeshComponent* Ism : All)
		{
			if (!IsValid(Ism) || !TNArt::IsCollisionTwin(Ism) || Comps.Contains(Ism))
			{
				continue;
			}
			if (!Generated || Ism->GetStaticMesh() == Generated)
			{
				Ism->DestroyComponent();
			}
		}
	}

	/** Receta de la parte animada como la entiende TNBeachDecorKit::AnimPose. */
	TNBeachProp::FPropInfo ToPropInfo(const TNBeachDecorFieldTypes::FAnimRecipe& Recipe)
	{
		TNBeachProp::FPropInfo Info;
		Info.Anim = static_cast<TNBeachProp::EAnim>(Recipe.Anim);
		Info.AnimPivot = Recipe.Pivot;
		Info.AnimAxis = Recipe.Axis;
		Info.AnimAmp = Recipe.Amp;
		Info.AnimRate = Recipe.Rate;
		Info.bCastShadow = Recipe.bCastShadow;
		return Info;
	}
}

ATN_BeachDecorField::ATN_BeachDecorField()
{
	// Solo mueve las partes animadas (cerca de una cámara local); lo demás no tiene nada que hacer cada fotograma.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = false;
	SetCanBeDamaged(false);

	FieldRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FieldRoot"));
	FieldRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(FieldRoot);
}

void ATN_BeachDecorField::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (TNBeachDecorFieldTypes::FAnimator& Anim : Animators)
	{
		Anim.Item = INDEX_NONE;
	}
	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────────────────────
// Montaje por partes
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachDecorField::BeginBuildPlaced(const TArray<TNBeachLayout::FItem>& InItems, const TArray<FTransform>& InPlacements)
{
	ClearDecor();
	const int32 Num = FMath::Min(InItems.Num(), InPlacements.Num());
	if (Num != InItems.Num() || Num != InPlacements.Num())
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[BeachDecorField] %d piezas y %d sitios: solo se montan %d."), InItems.Num(),
			InPlacements.Num(), Num);
	}
	for (int32 i = 0; i < Num; ++i)
	{
		if (TNBeach::CategoryOf(InItems[i].Element) != ETNBeachCategory::Decor)
		{
			continue;
		}
		PendingItems.Add(InItems[i]);
		PendingXf.Add(InPlacements[i]);
	}
	StartPendingBuild();
}

void ATN_BeachDecorField::StartPendingBuild()
{
	const UWorld* World = GetWorld();
	// Sin pantalla (servidor dedicado) basta con la colisión: ni partes que se mueven ni copias de sombra.
	bVisuals = World && World->GetNetMode() != NM_DedicatedServer;
	BuildSeconds = 0.0;
	BuildFrames = 0;
	Stage = TNBeachDecorFieldTypes::EStage::Collect;
}

bool ATN_BeachDecorField::StepBuild(double BudgetSeconds)
{
	using EStage = TNBeachDecorFieldTypes::EStage;
	if (Stage == EStage::Done || Stage == EStage::Idle)
	{
		return Stage == EStage::Done;
	}
	const double T0 = FPlatformTime::Seconds();
	++BuildFrames;
	bool bFirst = true;
	while (Stage == EStage::Collect || Stage == EStage::Components)
	{
		// Al menos un paso por llamada, aunque el presupuesto ya esté gastado (si no, no acabaría nunca).
		if (!bFirst && FPlatformTime::Seconds() - T0 >= BudgetSeconds)
		{
			break;
		}
		bFirst = false;
		if (Stage == EStage::Collect)
		{
			if (NextPending < PendingItems.Num())
			{
				AddItem(PendingItems[NextPending], PendingXf[NextPending]);
				++NextPending;
				continue;
			}
			Stage = EStage::Components;
			NextBatch = 0;
			continue;
		}
		if (NextBatch < Batches.Num())
		{
			SetupBatchComps(NextBatch);
			++NextBatch;
			continue;
		}
		FinishBuild();
	}
	BuildSeconds += FPlatformTime::Seconds() - T0;
	return Stage == EStage::Done;
}

void ATN_BeachDecorField::ClearDecor()
{
	// Las instancias se van; los componentes se quedan para la ronda siguiente (se reutilizan por clave).
	for (TNBeachDecorFieldTypes::FAnimator& Anim : Animators)
	{
		Anim.Item = INDEX_NONE;
	}
	for (UStaticMeshComponent* Comp : AnimatorPool)
	{
		if (IsValid(Comp))
		{
			Comp->SetVisibility(false);
		}
	}
	for (UInstancedStaticMeshComponent* Comp : Comps)
	{
		if (IsValid(Comp) && Comp->GetInstanceCount() > 0)
		{
			Comp->ClearInstances();
		}
	}
	// Los gemelos de colisión de las piezas de arte se rehacen con las instancias nuevas (SetupBatchComps).
	TNBeachDecorFieldDetail::DestroyArtTwins(this, Comps, nullptr);
	Items.Reset();
	Batches.Reset();
	BatchByKey.Reset();
	AnimRecipes.Reset();
	AnimRecipeByKey.Reset();
	AnimItems.Reset();
	PendingItems.Reset();
	PendingXf.Reset();
	NextPending = 0;
	NextBatch = 0;
	Stage = TNBeachDecorFieldTypes::EStage::Idle;
}

int32 ATN_BeachDecorField::BatchFor(uint32 Key, ETNBeachElement Element, UStaticMesh* Mesh, bool bMoving, bool bCollision, bool bBlocksCamera, bool bCastShadow)
{
	if (const int32* Found = BatchByKey.Find(Key))
	{
		return *Found;
	}
	TNBeachDecorFieldTypes::FBatch& Batch = Batches.AddDefaulted_GetRef();
	Batch.Key = Key;
	Batch.Element = Element;
	Batch.Mesh = Mesh;
	Batch.bMoving = bMoving;
	Batch.bCollision = bCollision && !bMoving;
	Batch.bBlocksCamera = bBlocksCamera;
	Batch.bRecipeShadow = bCastShadow;
	const int32 Index = Batches.Num() - 1;
	BatchByKey.Add(Key, Index);
	return Index;
}

void ATN_BeachDecorField::AddItem(const TNBeachLayout::FItem& LayoutItem, const FTransform& ItemXf)
{
	const ETNBeachElement Element = LayoutItem.Element;
	const int32 ItemIndex = Items.AddDefaulted();
	TNBeachDecorFieldTypes::FItem& Item = Items[ItemIndex];
	Item.Element = Element;
	Item.Seed = LayoutItem.Spec.Seed;
	Item.Size = TNBeachDecorKit::ClampSize(LayoutItem.Spec.SizeScale);
	Item.Pos = LayoutItem.Pos;
	Item.Axis = LayoutItem.Axis();
	Item.Radius = LayoutItem.Radius;
	Item.HalfLength = LayoutItem.HalfLength;
	Item.ItemXf = ItemXf;

	if (TNBeachProp::IsTiled(Element))
	{
		// Pasarela o caminito de palos: muchas piezas a lo largo de Extent, cada una en el lote de su malla.
		Item.bTiled = true;
		TMap<int32, TArray<FTransform>> ByPiece;
		TNBeachDecorKit::TilePlacements(Element, Item.Seed, Item.Size, LayoutItem.Spec.Extent, ByPiece);
		for (const TPair<int32, TArray<FTransform>>& Entry : ByPiece)
		{
			const TNBeachDecorKit::FRecipe Recipe = TNBeachDecorKit::Piece(Element, Entry.Key);
			if (!Recipe.Body)
			{
				continue;
			}
			// Como en ATN_BeachDecor: la cámara atraviesa los tramos.
			const int32 BatchIndex = BatchFor(TNBeachDecorFieldDetail::PieceKey(Element, Entry.Key), Element, Recipe.Body, false, Recipe.bCollision, false,
				Recipe.Info.bCastShadow);
			TNBeachDecorFieldTypes::FBatch& Batch = Batches[BatchIndex];
			Batch.MaxSize = FMath::Max(Batch.MaxSize, Item.Size);
			for (const FTransform& PieceXf : Entry.Value)
			{
				Batch.Transforms.Add(PieceXf * Item.ItemXf);
				Batch.Owners.Add(ItemIndex);
			}
		}
		return;
	}

	const int32 Variant = TNBeachDecorKit::VariantOf(Element, Item.Seed);
	const TNBeachDecorKit::FRecipe Recipe = TNBeachDecorKit::Single(Element, Variant);
	if (!Recipe.Body)
	{
		// Sin malla (material que no carga): no hay nada que poner.
		return;
	}
	const uint32 Key = TNBeachDecorFieldDetail::SingleKey(Element, Variant);
	Item.BodyXf = TNBeachDecorKit::BodyPlacement(Recipe.Info, Item.Seed, Item.Size) * Item.ItemXf;
	Item.BodyBatch = BatchFor(Key, Element, Recipe.Body, false, Recipe.bCollision, Recipe.Info.bBlocksCamera, Recipe.Info.bCastShadow);
	{
		TNBeachDecorFieldTypes::FBatch& Batch = Batches[Item.BodyBatch];
		Batch.MaxSize = FMath::Max(Batch.MaxSize, Item.Size);
		Batch.Transforms.Add(Item.BodyXf);
		Batch.Owners.Add(ItemIndex);
	}

	if (!bVisuals || !Recipe.Moving)
	{
		return;
	}
	// La parte que se mueve: quieta en su lote en la pose del instante 0 (la de ATN_BeachDecor antes de moverse).
	FTransform Pose(FQuat::Identity, Recipe.Info.AnimPivot, FVector::OneVector);
	if (Recipe.Info.Anim != TNBeachProp::EAnim::None)
	{
		int32 AnimIndex = INDEX_NONE;
		if (const int32* Found = AnimRecipeByKey.Find(Key))
		{
			AnimIndex = *Found;
		}
		else
		{
			TNBeachDecorFieldTypes::FAnimRecipe& Anim = AnimRecipes.AddDefaulted_GetRef();
			Anim.Anim = static_cast<uint8>(Recipe.Info.Anim);
			Anim.Pivot = Recipe.Info.AnimPivot;
			Anim.Axis = Recipe.Info.AnimAxis;
			Anim.Amp = Recipe.Info.AnimAmp;
			Anim.Rate = Recipe.Info.AnimRate;
			Anim.bCastShadow = Recipe.Info.bCastShadow;
			Anim.Mesh = Recipe.Moving;
			AnimIndex = AnimRecipes.Num() - 1;
			AnimRecipeByKey.Add(Key, AnimIndex);
		}
		TNBeachDecorKit::FAnimState State;
		Pose = TNBeachDecorKit::AnimPose(Recipe.Info, 0.f, TNBeachDecorKit::AnimPhaseOf(Item.Seed), Item.Seed, State, []() { return false; });
		Item.AnimRecipe = AnimIndex;
		Item.AnimRange = TNBeachDecorKit::AnimRangeFor(Element, Item.Size);
		AnimItems.Add(ItemIndex);
	}
	Item.MovingRestXf = Pose * Item.BodyXf;
	Item.MovingBatch = BatchFor(Key | TNBeachDecorFieldDetail::MovingBit, Element, Recipe.Moving, true, false, false, Recipe.Info.bCastShadow);
	TNBeachDecorFieldTypes::FBatch& Moving = Batches[Item.MovingBatch];
	Moving.MaxSize = FMath::Max(Moving.MaxSize, Item.Size);
	Item.MovingInstance = Moving.Transforms.Num();
	Moving.Transforms.Add(Item.MovingRestXf);
	Moving.Owners.Add(ItemIndex);
}

int32 ATN_BeachDecorField::AcquireComp(uint32 BatchKey, uint8 Kind)
{
	const uint64 CompKey = (static_cast<uint64>(BatchKey) << 8) | Kind;
	if (const int32* Found = CompByKey.Find(CompKey))
	{
		if (Comps.IsValidIndex(*Found) && IsValid(Comps[*Found]))
		{
			return *Found;
		}
	}
	// Jerárquicas (HISM: culling por grupos y distancia por instancia) salvo la parte que se mueve, que cambia instancias
	// sueltas al empezar y acabar de moverse (ISM: sin árbol que rehacer).
	UInstancedStaticMeshComponent* Comp = Kind == TNBeachDecorFieldDetail::KindMoving
		? NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient | RF_DuplicateTransient)
		: NewObject<UHierarchicalInstancedStaticMeshComponent>(this, NAME_None, RF_Transient | RF_DuplicateTransient);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetupAttachment(FieldRoot);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetGenerateOverlapEvents(false);
	Comp->bAffectDistanceFieldLighting = false;
	// No se mueven (salvo esconder alguna instancia de la parte animada): las sombras virtuales no se invalidan por nada más.
	Comp->ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Rigid;
	if (Kind != TNBeachDecorFieldDetail::KindBody)
	{
		Comp->bDisableCollision = true;
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (Kind == TNBeachDecorFieldDetail::KindTwin)
	{
		// Copia que solo da sombra: no se dibuja en el pase principal ni en el de profundidad.
		Comp->SetRenderInMainPass(false);
		Comp->SetRenderInDepthPass(false);
		Comp->SetCastShadow(true);
	}
	Comp->RegisterComponent();
	const int32 Index = Comps.Add(Comp);
	CompByKey.Add(CompKey, Index);
	return Index;
}

void ATN_BeachDecorField::SetupBatchComps(int32 BatchIndex)
{
	TNBeachDecorFieldTypes::FBatch& Batch = Batches[BatchIndex];
	if (!Batch.Mesh || Batch.Transforms.Num() == 0)
	{
		return;
	}
	const bool bTiled = TNBeachProp::IsTiled(Batch.Element);
	// Lo grande da sombra siempre (y los tramos, que son largos); lo demás, con su copia de sombra hasta ShadowNearDistance.
	const double Reach = TNBeach::FootprintRadius(Batch.Element) * Batch.MaxSize;
	const bool bAlwaysShadow = Batch.bRecipeShadow && (bTiled || Reach >= ShadowBigRadius);
	const bool bNearShadow = Batch.bRecipeShadow && !bAlwaysShadow && bVisuals && !Batch.bMoving;
	// Lo pequeño deja de dibujarse lejos (como ATN_BeachDecor, con el tamaño mayor del lote); los tramos, nunca.
	const float Cull = bTiled ? 0.f : TNBeachDecorKit::CullDistanceFor(Batch.Element, Batch.MaxSize);

	const int32 CompIndex = AcquireComp(Batch.Key, Batch.bMoving ? TNBeachDecorFieldDetail::KindMoving : TNBeachDecorFieldDetail::KindBody);
	UInstancedStaticMeshComponent* Comp = Comps[CompIndex];
	// Reutilizado con la malla de arte de una ronda anterior: vuelve a la generada sin sus materiales (TNArt la pone luego).
	if (Comp->GetStaticMesh() && Comp->GetStaticMesh() != Batch.Mesh)
	{
		Comp->EmptyOverrideMaterials();
	}
	Comp->SetStaticMesh(Batch.Mesh);
	Comp->SetCastShadow(bAlwaysShadow);
	Comp->SetCullDistances(Cull > 0.f ? FMath::RoundToInt32(Cull * 0.85f) : 0, FMath::RoundToInt32(Cull));
	if (!Batch.bMoving)
	{
		// Antes de las instancias: cada una crea su cuerpo con la colisión del componente.
		TNBeachDecorKit::SetupCollision(Comp, Batch.bCollision, Batch.bBlocksCamera);
	}
	Comp->AddInstances(Batch.Transforms, false, false, false);
	TNArt::ApplyToInstances(Comp, TNBeachDecorFieldDetail::SlotOf(Batch));
	Batch.Comp = CompIndex;

	if (bNearShadow)
	{
		const float ShadowCull = Cull > 0.f ? FMath::Min(Cull, ShadowNearDistance) : ShadowNearDistance;
		const int32 TwinIndex = AcquireComp(Batch.Key, TNBeachDecorFieldDetail::KindTwin);
		UInstancedStaticMeshComponent* Twin = Comps[TwinIndex];
		if (Twin->GetStaticMesh() && Twin->GetStaticMesh() != Batch.Mesh)
		{
			Twin->EmptyOverrideMaterials();
		}
		Twin->SetStaticMesh(Batch.Mesh);
		Twin->SetCullDistances(FMath::RoundToInt32(ShadowCull * 0.8f), FMath::RoundToInt32(ShadowCull));
		Twin->AddInstances(Batch.Transforms, false, false, false);
		// La sombra, de la misma malla que se ve.
		TNArt::ApplyToInstances(Twin, TNBeachDecorFieldDetail::SlotOf(Batch));
		Batch.TwinComp = TwinIndex;
	}
}

void ATN_BeachDecorField::FinishBuild()
{
	Stage = TNBeachDecorFieldTypes::EStage::Done;
	PendingItems.Empty();
	AnimCheckClock = 0.f;
}

FTNBeachDecorStats ATN_BeachDecorField::GetStats() const
{
	FTNBeachDecorStats Stats;
	Stats.Items = Items.Num();
	for (const TNBeachDecorFieldTypes::FBatch& Batch : Batches)
	{
		const UInstancedStaticMeshComponent* Comp = Comps.IsValidIndex(Batch.Comp) ? Comps[Batch.Comp].Get() : nullptr;
		const int32 Count = IsValid(Comp) ? Comp->GetInstanceCount() : 0;
		if (Batch.bMoving)
		{
			Stats.MovingInstances += Count;
		}
		else
		{
			Stats.BodyInstances += Count;
			Stats.CollisionInstances += Batch.bCollision ? Count : 0;
		}
		Stats.ShadowInstances += IsValid(Comp) && Comp->CastShadow ? Count : 0;
		const UInstancedStaticMeshComponent* Twin = Comps.IsValidIndex(Batch.TwinComp) ? Comps[Batch.TwinComp].Get() : nullptr;
		Stats.ShadowTwinInstances += IsValid(Twin) ? Twin->GetInstanceCount() : 0;
	}
	for (const UInstancedStaticMeshComponent* Comp : Comps)
	{
		Stats.Components += IsValid(Comp) && Comp->GetInstanceCount() > 0 ? 1 : 0;
	}
	for (const TNBeachDecorFieldTypes::FAnimator& Anim : Animators)
	{
		Stats.Animators += Anim.Item != INDEX_NONE ? 1 : 0;
	}
	Stats.BuildMs = BuildSeconds * 1000.0;
	Stats.BuildFrames = BuildFrames;
	return Stats;
}

// ─────────────────────────────────────────────────────────────────────────────
// Partes animadas (solo en máquinas con pantalla y cerca de una cámara local)
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachDecorField::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UWorld* World = GetWorld();
	if (!bVisuals || !World || !World->IsGameWorld() || Stage != TNBeachDecorFieldTypes::EStage::Done)
	{
		return;
	}
	// Quién se mueve se decide cuatro veces por segundo; la pose, en cada fotograma.
	AnimCheckClock -= DeltaSeconds;
	if (AnimCheckClock <= 0.f)
	{
		AnimCheckClock = 0.25f;
		UpdateAnimators();
	}
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	for (int32 Slot = 0; Slot < Animators.Num(); ++Slot)
	{
		if (Animators[Slot].Item != INDEX_NONE)
		{
			PoseAnimator(Slot, Dt);
		}
	}
}

void ATN_BeachDecorField::UpdateAnimators()
{
	const UWorld* World = GetWorld();
	if (!World || AnimItems.Num() == 0)
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
	// Las más cercanas a una cámara dentro de su alcance, MaxAnimators como mucho.
	const FTransform FieldXf = GetActorTransform();
	TArray<TPair<double, int32>> Wanted;
	for (const int32 ItemIndex : AnimItems)
	{
		const TNBeachDecorFieldTypes::FItem& Item = Items[ItemIndex];
		if (Item.MovingInstance == INDEX_NONE)
		{
			continue;
		}
		const FVector At = FieldXf.TransformPosition(Item.BodyXf.GetLocation());
		double Best = TNumericLimits<double>::Max();
		for (const FVector& Camera : Cameras)
		{
			Best = FMath::Min(Best, FVector::DistSquared(Camera, At));
		}
		if (Best < FMath::Square(static_cast<double>(Item.AnimRange)))
		{
			Wanted.Emplace(Best, ItemIndex);
		}
	}
	Wanted.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	if (Wanted.Num() > MaxAnimators)
	{
		Wanted.SetNum(MaxAnimators);
	}
	TSet<int32> Keep;
	for (const TPair<double, int32>& Entry : Wanted)
	{
		Keep.Add(Entry.Value);
	}
	TSet<int32> Running;
	for (int32 Slot = 0; Slot < Animators.Num(); ++Slot)
	{
		const int32 ItemIndex = Animators[Slot].Item;
		if (ItemIndex == INDEX_NONE)
		{
			continue;
		}
		if (!Keep.Contains(ItemIndex))
		{
			StopAnimator(Slot);
			continue;
		}
		Running.Add(ItemIndex);
	}
	for (const TPair<double, int32>& Entry : Wanted)
	{
		if (!Running.Contains(Entry.Value))
		{
			StartAnimator(Entry.Value);
		}
	}
}

void ATN_BeachDecorField::StartAnimator(int32 ItemIndex)
{
	const TNBeachDecorFieldTypes::FItem& Item = Items[ItemIndex];
	if (!AnimRecipes.IsValidIndex(Item.AnimRecipe) || !Batches.IsValidIndex(Item.MovingBatch))
	{
		return;
	}
	int32 Slot = INDEX_NONE;
	for (int32 s = 0; s < Animators.Num(); ++s)
	{
		if (Animators[s].Item == INDEX_NONE)
		{
			Slot = s;
			break;
		}
	}
	if (Slot == INDEX_NONE)
	{
		Slot = Animators.AddDefaulted();
		AnimatorPool.SetNum(Animators.Num());
	}
	UStaticMeshComponent* Comp = AnimatorPool[Slot];
	if (!IsValid(Comp))
	{
		Comp = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient | RF_DuplicateTransient);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetupAttachment(FieldRoot);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetGenerateOverlapEvents(false);
		Comp->bAffectDistanceFieldLighting = false;
		Comp->RegisterComponent();
		AnimatorPool[Slot] = Comp;
	}
	const TNBeachDecorFieldTypes::FAnimRecipe& Recipe = AnimRecipes[Item.AnimRecipe];
	// El componente de la reserva cambia de receta: se quita la malla de arte de la anterior antes de prepararlo y se pone
	// la de esta después (Docs/Arte_Assets.md). Sin sustitutos no hace nada.
	TNArt::ApplyToComponent(Comp, NAME_None);
	Comp->SetStaticMesh(Recipe.Mesh);
	Comp->SetCastShadow(Recipe.bCastShadow);
	Comp->SetVisibility(true);
	TNArt::ApplyToComponent(Comp, TNBeachDecorKit::MovingSlot(Item.Element));
	TNBeachDecorFieldTypes::FAnimator& Anim = Animators[Slot];
	Anim.Item = ItemIndex;
	Anim.PoolIndex = Slot;
	Anim.Time = 0.f;
	Anim.ClamCycle = -1;
	Anim.bClamHeld = false;
	PoseAnimator(Slot, 0.f);
	// Su instancia quieta se esconde mientras la mueve el componente.
	const TNBeachDecorFieldTypes::FBatch& Batch = Batches[Item.MovingBatch];
	if (UInstancedStaticMeshComponent* Moving = Comps.IsValidIndex(Batch.Comp) ? Comps[Batch.Comp].Get() : nullptr)
	{
		// Con el ajuste de su malla de arte, si la tiene.
		TNArt::UpdateInstances(Moving, Item.MovingInstance, { TNBeachDecorFieldDetail::HiddenXf(Item.MovingRestXf) }, false, true, true);
	}
}

void ATN_BeachDecorField::StopAnimator(int32 AnimatorIndex)
{
	if (!Animators.IsValidIndex(AnimatorIndex))
	{
		return;
	}
	TNBeachDecorFieldTypes::FAnimator& Anim = Animators[AnimatorIndex];
	if (Items.IsValidIndex(Anim.Item))
	{
		// La instancia quieta vuelve a su sitio.
		const TNBeachDecorFieldTypes::FItem& Item = Items[Anim.Item];
		const TNBeachDecorFieldTypes::FBatch* Batch = Batches.IsValidIndex(Item.MovingBatch) ? &Batches[Item.MovingBatch] : nullptr;
		UInstancedStaticMeshComponent* Moving = Batch && Comps.IsValidIndex(Batch->Comp) ? Comps[Batch->Comp].Get() : nullptr;
		if (IsValid(Moving) && Item.MovingInstance != INDEX_NONE)
		{
			TNArt::UpdateInstances(Moving, Item.MovingInstance, { Item.MovingRestXf }, false, true, true);
		}
	}
	if (AnimatorPool.IsValidIndex(AnimatorIndex) && IsValid(AnimatorPool[AnimatorIndex]))
	{
		AnimatorPool[AnimatorIndex]->SetVisibility(false);
	}
	Anim.Item = INDEX_NONE;
}

void ATN_BeachDecorField::PoseAnimator(int32 Slot, float DeltaSeconds)
{
	TNBeachDecorFieldTypes::FAnimator& Anim = Animators[Slot];
	UStaticMeshComponent* Comp = AnimatorPool.IsValidIndex(Slot) ? AnimatorPool[Slot].Get() : nullptr;
	if (!IsValid(Comp) || !Items.IsValidIndex(Anim.Item))
	{
		return;
	}
	const TNBeachDecorFieldTypes::FItem& Item = Items[Anim.Item];
	if (!AnimRecipes.IsValidIndex(Item.AnimRecipe))
	{
		return;
	}
	Anim.Time += DeltaSeconds;
	TNBeachDecorKit::FAnimState State;
	State.ClamCycle = Anim.ClamCycle;
	State.bClamHeld = Anim.bClamHeld;
	const FTransform Pose = TNBeachDecorKit::AnimPose(TNBeachDecorFieldDetail::ToPropInfo(AnimRecipes[Item.AnimRecipe]), Anim.Time,
		TNBeachDecorKit::AnimPhaseOf(Item.Seed), Item.Seed, State, [this, &Item]() { return IsSomeoneOnTop(Item); });
	Anim.ClamCycle = State.ClamCycle;
	Anim.bClamHeld = State.bClamHeld;
	Comp->SetRelativeTransform(Pose * Item.BodyXf);
}

bool ATN_BeachDecorField::IsSomeoneOnTop(const TNBeachDecorFieldTypes::FItem& Item) const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	if (!GS)
	{
		return false;
	}
	const FVector Here = GetActorTransform().TransformPosition(Item.ItemXf.GetLocation());
	const double Reach = Item.Radius * 1.3 + 60.0;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		const APawn* Turtle = PS ? PS->GetPawn() : nullptr;
		if (!Turtle)
		{
			continue;
		}
		const FVector Rel = Turtle->GetActorLocation() - Here;
		if (FVector(Rel.X, Rel.Y, 0.0).SizeSquared() < Reach * Reach && Rel.Z > -50.0 && Rel.Z < 500.0)
		{
			return true;
		}
	}
	return false;
}
