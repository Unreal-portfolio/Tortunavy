#include "Vehicles/TN_BuggyLookComponent.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyCosmetics.h"
#include "Vehicles/TN_BuggyMath.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "TN_BuggyArt.h"
#include "TN_BuggyTurretMesh.h"
#include "World/ProcMap/TN_ProcMapRuntimeMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"

namespace TNBuggyLookDetail
{
	using TNBuggyArt::EPiece;

	/** Antena: inclinación (grados) por cm/s² de aceleración y por cm/s de velocidad, tope, muelle y amortiguación. */
	constexpr float LeanPerAccel = 0.0035f;
	constexpr float LeanPerSpeed = 0.0032f;
	constexpr float MaxLeanDeg = 15.f;
	constexpr float Spring = 70.f;
	constexpr float Damping = 6.5f;

	/** Material de color de vértice de la torreta del escaparate (el mismo que usa ATN_Buggy). */
	const TCHAR* const VertexColorMaterialPath = TEXT("/Game/Cosmetics/Materials/M_CosmeticVertexColor.M_CosmeticVertexColor");

	const TCHAR* PieceComponentName(int32 Index)
	{
		static const TCHAR* const Names[] = { TEXT("BuggyChassis"), TEXT("BuggyCockpit"), TEXT("BuggyShell"), TEXT("BuggyHead"), TEXT("BuggyFenders"),
			TEXT("BuggyTail"), TEXT("BuggyRear"), TEXT("BuggyExtras") };
		return Index >= 0 && Index < UE_ARRAY_COUNT(Names) ? Names[Index] : TEXT("BuggyPiece");
	}
}

UTN_BuggyLookComponent::UTN_BuggyLookComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(false);
}

void UTN_BuggyLookComponent::UseVehicleParts(UStaticMeshComponent* InBody, const TArray<UStaticMeshComponent*>& InTires)
{
	bVehicleParts = true;
	StockBody = InBody;
	Wheels.Reset();
	for (UStaticMeshComponent* Tire : InTires)
	{
		Wheels.Add(Tire);
		if (Tire && !StockTireMesh) { StockTireMesh = Tire->GetStaticMesh(); }
	}
}

void UTN_BuggyLookComponent::SetStudio(const FLightingChannels& Channels)
{
	bStudio = true;
	StudioChannels = Channels;
	TArray<UPrimitiveComponent*> Parts;
	GetPrimitives(Parts);
	for (UPrimitiveComponent* Part : Parts) { if (UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Part)) { SetupPart(Mesh); } }
}

void UTN_BuggyLookComponent::SetupPart(UStaticMeshComponent* Part) const
{
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetGenerateOverlapEvents(false);
	Part->SetCanEverAffectNavigation(false);
	Part->SetCastShadow(true);
	if (bStudio)
	{
		Part->SetVisibleInSceneCaptureOnly(true);
		Part->LightingChannels = StudioChannels;
	}
}

UStaticMeshComponent* UTN_BuggyLookComponent::MakePart(const TCHAR* Name, USceneComponent* Parent)
{
	AActor* Owner = GetOwner();
	UObject* Outer = Owner ? static_cast<UObject*>(Owner) : static_cast<UObject*>(this);
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Outer, MakeUniqueObjectName(Outer, UStaticMeshComponent::StaticClass(), Name), RF_Transient);
	Part->SetupAttachment(Parent);
	SetupPart(Part);
	if (IsRegistered()) { Part->RegisterComponent(); }
	return Part;
}

void UTN_BuggyLookComponent::EnsureParts()
{
	using namespace TNBuggyArt;
	if (BodyPieces.Num() == NumBodyPieces) { return; }
	BodyPieces.Reset();
	for (int32 i = 0; i < NumBodyPieces; ++i)
	{
		BodyPieces.Add(MakePart(TNBuggyLookDetail::PieceComponentName(i), this));
	}
	if (!bVehicleParts)
	{
		// Escaparate: el buggy de serie entero, con sus ruedas (las derechas giradas, como en ATN_Buggy) y su torreta.
		StockBody = MakePart(TEXT("BuggyStockBody"), this);
		StockBody->SetStaticMesh(Stock::BodyMesh());
		StockTireMesh = Stock::TireMesh();
		const FVector Spots[] = { Frame::FrontWheel * FVector(1.0, -1.0, 1.0), Frame::FrontWheel, Frame::RearWheel * FVector(1.0, -1.0, 1.0), Frame::RearWheel };
		for (int32 i = 0; i < 4; ++i)
		{
			UStaticMeshComponent* Wheel = MakePart(TEXT("BuggyWheel"), this);
			Wheel->SetRelativeLocationAndRotation(Spots[i], FRotator(0.0, i % 2 == 1 ? 180.0 : 0.0, 0.0));
			Wheels.Add(Wheel);
		}
		BuildStudioTurret();
	}
	AntennaPivot = NewObject<USceneComponent>(GetOwner() ? static_cast<UObject*>(GetOwner()) : static_cast<UObject*>(this),
		MakeUniqueObjectName(GetOwner(), USceneComponent::StaticClass(), TEXT("BuggyAntennaPivot")), RF_Transient);
	AntennaPivot->SetupAttachment(this);
	if (IsRegistered()) { AntennaPivot->RegisterComponent(); }
	Antenna = MakePart(TEXT("BuggyAntenna"), AntennaPivot);
}

void UTN_BuggyLookComponent::BuildStudioTurret()
{
	UMaterialInterface* VertexColor = LoadObject<UMaterialInterface>(nullptr, TNBuggyLookDetail::VertexColorMaterialPath, nullptr, LOAD_NoWarn);
	if (!VertexColor) { return; }
	using FBuild = void (*)(TNProcMesh::FTNProcMeshBuffers&);
	const FBuild Builders[] = { &TNBuggyTurretMesh::BuildRing, &TNBuggyTurretMesh::BuildMount, &TNBuggyTurretMesh::BuildGun, &TNBuggyTurretMesh::BuildBarrel };
	const FVector Pivot = ATN_Buggy::GunnerSeatLocal + FVector(0.0, 0.0, UTN_BuggyTurretComponent::PivotAboveSeatCm);
	for (const FBuild Build : Builders)
	{
		TNProcMesh::FTNProcMeshBuffers Buffers;
		Build(Buffers);
		UStaticMeshComponent* Part = MakePart(TEXT("BuggyTurret"), this);
		Part->SetRelativeLocation(Pivot);
		Part->SetStaticMesh(TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), Buffers, VertexColor));
		TurretParts.Add(Part);
	}
}

bool UTN_BuggyLookComponent::ApplyLook(const FTN_BuggyLook& InLook, int32 InTeamIndex, bool bForce)
{
	using namespace TNBuggyArt;
	if (GetNetMode() == NM_DedicatedServer) { return false; }
	const FTN_BuggyLook Clean = TNBuggyCosmetics::Sanitize(InLook);
	const FString Key = FString::Printf(TEXT("%s|%d"), *TNBuggyCosmetics::LookKey(Clean), InTeamIndex);
	if (!bForce && Key == AppliedKey) { return false; }
	EnsureParts();

	const FTNBuggyModelInfo& Model = TNBuggyCosmetics::ResolveModel(Clean.ModelId);
	const FTNBuggyPaintInfo& Paint = TNBuggyCosmetics::ResolvePaint(Clean.PaintId);
	const bool bStock = Model.Style == ETNBuggyBodyStyle::Stock;
	const FLinearColor TeamColor = TNBuggy::TeamColor(InTeamIndex);
	UMaterialInterface* PaintMat = PaintMaterial();
	if (PaintMat)
	{
		if (!PaintMID || PaintMID->Parent != PaintMat) { PaintMID = UMaterialInstanceDynamic::Create(PaintMat, this); }
		ApplyPaint(PaintMID, Paint, TeamColor);
	}
	// Sin M_BuggyPaint: tortugas con la pintura horneada en el color de vértice y el de serie con la skin del equipo.
	const FTNBuggyPaintInfo* Bake = PaintMat ? nullptr : &Paint;
	bUsesTeamSkin = bStock && (Clean.PaintId.IsNone() || !PaintMat);

	const auto SetPiece = [this, &Model, Bake](UStaticMeshComponent* Part, EPiece Piece, bool bShow)
	{
		if (!Part) { return; }
		UStaticMesh* Mesh = bShow ? GetPieceMesh(Model.Style, Piece, Bake) : nullptr;
		Part->SetStaticMesh(Mesh);
		Part->SetVisibility(Mesh != nullptr);
		if (Mesh) { Part->SetMaterial(0, PaintMID && !Bake ? static_cast<UMaterialInterface*>(PaintMID) : nullptr); }
	};
	for (int32 i = 0; i < BodyPieces.Num(); ++i) { SetPiece(BodyPieces[i], static_cast<EPiece>(i), !bStock); }

	if (bStock)
	{
		// El de serie: su carrocería y sus neumáticos, con la skin del equipo (la de ATN_Buggy o la del escaparate) o la
		// pintura de la tienda con sus zonas.
		UMaterialInterface* StockMat = nullptr;
		if (!bUsesTeamSkin)
		{
			if (!StockPaintMID || StockPaintMID->Parent != PaintMat) { StockPaintMID = UMaterialInstanceDynamic::Create(PaintMat, this); }
			ApplyPaint(StockPaintMID, Paint, TeamColor, true);
			StockMat = StockPaintMID;
		}
		else if (!bVehicleParts)
		{
			UMaterialInterface* Skin = Stock::Skin(InTeamIndex);
			if (Skin && (!StudioSkinMID || StudioSkinMID->Parent != Skin)) { StudioSkinMID = UMaterialInstanceDynamic::Create(Skin, this); }
			if (StudioSkinMID && InTeamIndex >= 0) { StudioSkinMID->SetVectorParameterValue(ATN_Buggy::TintParameterName, TeamColor); }
			StockMat = StudioSkinMID;
		}
		if (StockBody)
		{
			StockBody->SetVisibility(true);
			if (StockMat) { StockBody->SetMaterial(0, StockMat); }
		}
		for (UStaticMeshComponent* Wheel : Wheels)
		{
			if (!Wheel) { continue; }
			Wheel->SetStaticMesh(StockTireMesh);
			if (StockMat) { Wheel->SetMaterial(0, StockMat); }
		}
	}
	else
	{
		// Una tortuga: la carrocería de serie se esconde (sus sockets siguen sentando a las tortugas) y las ruedas son las suyas.
		if (StockBody) { StockBody->SetVisibility(false); }
		for (UStaticMeshComponent* Wheel : Wheels) { SetPiece(Wheel, EPiece::Wheel, true); }
	}
	// La antena con el banderín del equipo: en las tortugas siempre; en el de serie, si la pintura tapa el color del equipo.
	SetPiece(Antenna, EPiece::Antenna, !bUsesTeamSkin);
	if (AntennaPivot) { AntennaPivot->SetRelativeLocation(AntennaMount(Model.Style)); }

	Look = Clean;
	TeamIndex = InTeamIndex;
	AppliedKey = Key;
	AppliedStyle = static_cast<uint8>(Model.Style);
	return true;
}

FVector UTN_BuggyLookComponent::GetExhaustLocal(const FVector& Default) const
{
	return TNBuggyArt::ExhaustLocal(static_cast<ETNBuggyBodyStyle>(AppliedStyle), Default);
}

void UTN_BuggyLookComponent::GetPrimitives(TArray<UPrimitiveComponent*>& Out) const
{
	for (UStaticMeshComponent* Part : BodyPieces) { if (Part) { Out.Add(Part); } }
	if (!bVehicleParts)
	{
		if (StockBody) { Out.Add(StockBody); }
		for (UStaticMeshComponent* Wheel : Wheels) { if (Wheel) { Out.Add(Wheel); } }
	}
	for (UStaticMeshComponent* Part : TurretParts) { if (Part) { Out.Add(Part); } }
	if (Antenna) { Out.Add(Antenna); }
}

void UTN_BuggyLookComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!AntennaPivot || !Antenna || !Antenna->IsVisible() || GetNetMode() == NM_DedicatedServer) { return; }
	if (!bStudio && !Antenna->WasRecentlyRendered(0.5f))
	{
		bHasLastLocation = false;
		return;
	}
	UpdateAntenna(FMath::Min(DeltaTime, 0.05f));
}

void UTN_BuggyLookComponent::UpdateAntenna(float DeltaTime)
{
	using namespace TNBuggyLookDetail;
	AntennaTime += DeltaTime;
	const FVector Location = GetComponentLocation();
	if (!bHasLastLocation || DeltaTime <= KINDA_SMALL_NUMBER)
	{
		LastLocation = Location;
		LastVelocity = FVector::ZeroVector;
		bHasLastLocation = true;
		return;
	}
	FVector Velocity = (Location - LastLocation) / DeltaTime;
	// Una reaparición o un enderezado teletransportan el chasis: no es un acelerón.
	if (Velocity.SizeSquared() > FMath::Square(8000.f)) { Velocity = LastVelocity; }
	const FVector Accel = (Velocity - LastVelocity) / DeltaTime;
	LastLocation = Location;
	LastVelocity = Velocity;

	const FTransform& Xf = GetComponentTransform();
	const FVector LocalAccel = Xf.InverseTransformVectorNoScale(Accel);
	const FVector LocalVelocity = Xf.InverseTransformVectorNoScale(Velocity);
	// Cabeceo positivo: la varilla se va hacia atrás (al acelerar y con el viento); alabeo: hacia el lado contrario al giro.
	const float Idle = 1.6f * FMath::Sin(AntennaTime * 2.3f) + 0.8f * FMath::Sin(AntennaTime * 5.1f);
	const FVector2D Target(
		FMath::Clamp(static_cast<float>(LocalAccel.X) * LeanPerAccel + static_cast<float>(LocalVelocity.X) * LeanPerSpeed + Idle, -MaxLeanDeg, MaxLeanDeg),
		FMath::Clamp(static_cast<float>(LocalAccel.Y) * LeanPerAccel + Idle * 0.5f, -MaxLeanDeg, MaxLeanDeg));
	LeanSpeed += ((Target - Lean) * Spring - LeanSpeed * Damping) * DeltaTime;
	Lean += LeanSpeed * DeltaTime;
	Lean.X = FMath::Clamp(Lean.X, -MaxLeanDeg, MaxLeanDeg);
	Lean.Y = FMath::Clamp(Lean.Y, -MaxLeanDeg, MaxLeanDeg);
	AntennaPivot->SetRelativeRotation(FRotator(Lean.X, 0.0, Lean.Y));
}
