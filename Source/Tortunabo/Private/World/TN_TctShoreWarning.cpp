#include "World/TN_TctShoreWarning.h"
#include "Core/TN_ProjectMaterials.h"
#include "Game/TN_TctGameState.h"
#include "Game/TN_TctRules.h"
#include "ProcMap/TN_ProcMapRuntimeMesh.h"
#include "ProcMap/TN_TctShoreMeshes.h"
#include "World/ProcMap/TN_ProcMapMath.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"

namespace TNTctShoreWarningDetail
{
	const TCHAR* FoliageMaterialPath = TEXT("/Game/ProcMap/Materials/M_ProcFoliage.M_ProcFoliage");
	const TCHAR* LampMaterialPath = TEXT("/Game/ProcMap/Materials/MI_ProcSea.MI_ProcSea");
	/** En cuántos pasos va apareciendo el aviso (cada paso rehace las instancias: pocos y espaciados). */
	constexpr int32 Stages = 12;
	/** Color verde tóxico de las lámparas (el del agua venenosa). */
	const FLinearColor LampColor(0.55f, 1.f, 0.12f);
}

ATN_TctShoreWarning::ATN_TctShoreWarning()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = false;
	SetReplicatingMovement(false);
	SetCanBeDamaged(false);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

UHierarchicalInstancedStaticMeshComponent* ATN_TctShoreWarning::MakeInstances(UStaticMesh* Mesh, UMaterialInterface* Material, float CullEnd, bool bShadow)
{
	if (!Mesh)
	{
		return nullptr;
	}
	UHierarchicalInstancedStaticMeshComponent* Comp = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
	Comp->SetupAttachment(RootComponent);
	Comp->SetStaticMesh(Mesh);
	if (Material)
	{
		Comp->SetMaterial(0, Material);
	}
	// Se atraviesa todo: son marcas, no estorbos.
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetGenerateOverlapEvents(false);
	Comp->SetCastShadow(bShadow);
	Comp->bAffectDistanceFieldLighting = false;
	Comp->bEvaluateWorldPositionOffset = false;
	Comp->SetCullDistances(FMath::RoundToInt(CullEnd * 0.8f), FMath::RoundToInt(CullEnd));
	Comp->RegisterComponent();
	return Comp;
}

bool ATN_TctShoreWarning::Init(TSharedPtr<TNTctScenery::FHeightField> InField, const TArray<TNTctScenery::FKeepOut>& InKeepOuts, uint32 InSeed)
{
	using namespace TNTctMesh;
	Field = InField;
	KeepOuts = InKeepOuts;
	Seed = InSeed;
	if (!Field.IsValid() || !Field->IsValid() || IsRunningDedicatedServer() || !FApp::CanEverRender())
	{
		return false;
	}
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TNTctShoreWarningDetail::FoliageMaterialPath);
	if (!Material) { Material = TNMaterials::VertexColor(); }
	auto Make = [this, Material](TFunctionRef<void(TNProcMesh::FTNProcMeshBuffers&)> Build) -> UStaticMesh*
	{
		TNProcMesh::FTNProcMeshBuffers Buffers;
		Build(Buffers);
		// La paleta de los props es la de las mallas procedurales: se decodifica una vez más, como en el generador.
		for (FLinearColor& Col : Buffers.Colors)
		{
			Col = FLinearColor(TNProcRuntimeMesh::SRGBToLinear(Col.R), TNProcRuntimeMesh::SRGBToLinear(Col.G), TNProcRuntimeMesh::SRGBToLinear(Col.B), Col.A);
		}
		UStaticMesh* Mesh = TNProcRuntimeMesh::MakeStaticMesh(this, Buffers, Material, false);
		if (Mesh) { Meshes.Add(Mesh); }
		return Mesh;
	};
	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		const uint32 MeshSeed = TNProcMap::HashCell(0x920B0Au, Variant, 1);
		Foam[Variant] = MakeInstances(Make([Variant, MeshSeed](TNProcMesh::FTNProcMeshBuffers& B) { BuildFoam(B, Variant, MeshSeed); }), Material, 7000.f, false);
		Algae[Variant] = MakeInstances(Make([Variant, MeshSeed](TNProcMesh::FTNProcMeshBuffers& B) { BuildAlgae(B, Variant, MeshSeed + 7u); }), Material, 6000.f, true);
	}
	Posts = MakeInstances(Make([](TNProcMesh::FTNProcMeshBuffers& B) { BuildPost(B); }), Material, 16000.f, true);

	// Las lámparas de los postes: una esfera pequeña y translúcida del verde del agua, que se enciende al acercarse la subida.
	UMaterialInterface* Sea = LoadObject<UMaterialInterface>(nullptr, TNTctShoreWarningDetail::LampMaterialPath);
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sea && Sphere)
	{
		LampMaterial = UMaterialInstanceDynamic::Create(Sea, this);
		if (LampMaterial)
		{
			LampMaterial->SetVectorParameterValue(TEXT("Color"), TNTctShoreWarningDetail::LampColor);
			LampMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.f);
		}
		Lamps = MakeInstances(Sphere, LampMaterial, 16000.f, false);
	}
	SetActorTickEnabled(true);
	return true;
}

void ATN_TctShoreWarning::DebugSet(float TargetZ, float InProgress, float SecondsLeft)
{
	bDebug = true;
	DebugTargetZ = TargetZ;
	DebugProgress = InProgress;
	DebugSecondsLeft = SecondsLeft;
}

void ATN_TctShoreWarning::Evaluate(float& OutTargetZ, float& OutProgress, float& OutSecondsLeft) const
{
	OutTargetZ = 0.f;
	OutProgress = 0.f;
	OutSecondsLeft = 1.0e6f;
	if (bDebug)
	{
		OutTargetZ = DebugTargetZ;
		OutProgress = DebugProgress;
		OutSecondsLeft = DebugSecondsLeft;
		return;
	}
	const UWorld* World = GetWorld();
	const ATN_TctGameState* State = World ? World->GetGameState<ATN_TctGameState>() : nullptr;
	FTNTctNextRise Next;
	if (State && State->GetNextRise(Next))
	{
		OutProgress = TNTctRules::ShoreWarnProgress(Next);
		OutTargetZ = TNTctRules::ShoreWarnTargetZ(Next);
		OutSecondsLeft = Next.bRising ? 0.f : Next.SecondsLeft;
	}
}

void ATN_TctShoreWarning::ClearMarks()
{
	for (UHierarchicalInstancedStaticMeshComponent* Comp : { Foam[0].Get(), Foam[1].Get(), Algae[0].Get(), Algae[1].Get(), Posts.Get(), Lamps.Get() })
	{
		if (IsValid(Comp))
		{
			Comp->ClearInstances();
		}
	}
	NumShown = 0;
	if (LampMaterial)
	{
		LampMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.f);
	}
}

void ATN_TctShoreWarning::ShowMarks(float Fraction)
{
	using namespace TNTctScenery;
	ClearMarks();
	const int32 Count = FMath::Clamp(FMath::CeilToInt(Marks.Num() * Fraction), 0, Marks.Num());
	// Más grandes según se acerca la subida: la orilla se va llenando y creciendo.
	const double Grow = 0.55 + 0.45 * Fraction;
	TArray<FTransform> FoamXf[2], AlgaeXf[2], PostXf, LampXf;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FShoreMark& Mark = Marks[Index];
		const FRotator Yaw(0.0, Mark.YawDeg, 0.0);
		switch (Mark.Kind)
		{
		case ShoreKindFoam:
			FoamXf[Mark.Variant & 1].Add(FTransform(Yaw, Mark.Location, FVector(Mark.Scale * Grow)));
			break;
		case ShoreKindAlgae:
			AlgaeXf[Mark.Variant & 1].Add(FTransform(Yaw, Mark.Location, FVector(Mark.Scale * Grow)));
			break;
		default:
			PostXf.Add(FTransform(Yaw, Mark.Location));
			LampXf.Add(FTransform(FRotator::ZeroRotator, Mark.Location + FVector(0.0, 0.0, TNTctMesh::PostLampHeight), FVector(0.17)));
			break;
		}
	}
	for (int32 Variant = 0; Variant < 2; ++Variant)
	{
		if (IsValid(Foam[Variant]) && FoamXf[Variant].Num() > 0) { Foam[Variant]->AddInstances(FoamXf[Variant], false, true); }
		if (IsValid(Algae[Variant]) && AlgaeXf[Variant].Num() > 0) { Algae[Variant]->AddInstances(AlgaeXf[Variant], false, true); }
	}
	if (IsValid(Posts) && PostXf.Num() > 0) { Posts->AddInstances(PostXf, false, true); }
	if (IsValid(Lamps) && LampXf.Num() > 0) { Lamps->AddInstances(LampXf, false, true); }
	NumShown = Count;
}

void ATN_TctShoreWarning::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	float Target = 0.f, SecondsLeft = 0.f;
	Evaluate(Target, Progress, SecondsLeft);
	if (Progress <= 0.f)
	{
		if (NumShown > 0 || Stage >= 0)
		{
			ClearMarks();
		}
		Stage = -1;
		return;
	}
	if (!Field.IsValid())
	{
		return;
	}
	if (!FMath::IsNearlyEqual(Target, MarksZ, 0.5f))
	{
		// Otro escalón: otra orilla.
		Marks = TNTctScenery::PlanShoreMarks(*Field, Target, KeepOuts, Seed);
		MarksZ = Target;
		Stage = -1;
	}
	const int32 NewStage = FMath::Clamp(FMath::CeilToInt(Progress * TNTctShoreWarningDetail::Stages), 1, TNTctShoreWarningDetail::Stages);
	if (NewStage != Stage)
	{
		Stage = NewStage;
		ShowMarks(static_cast<float>(NewStage) / static_cast<float>(TNTctShoreWarningDetail::Stages));
	}
	// Las lámparas: un latido tranquilo y, en los últimos segundos, cada vez más deprisa y más fuerte; con el agua subiendo, fijas.
	if (LampMaterial && NumShown > 0)
	{
		const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		const bool bRising = SecondsLeft <= 0.f;
		const float Warn = TNTctPoisonDefaults::WarnSeconds;
		const float Hurry = bRising ? 1.f : 1.f - FMath::Clamp(SecondsLeft / Warn, 0.f, 1.f);
		const float Rate = 1.2f + 8.f * Hurry;
		const float Pulse = bRising ? 1.f : 0.5f + 0.5f * FMath::Sin(Time * Rate * 2.f);
		LampMaterial->SetScalarParameterValue(TEXT("Opacity"), (0.1f + 0.2f * Progress + 0.4f * Hurry) * (0.4f + 0.6f * Pulse));
	}
}
