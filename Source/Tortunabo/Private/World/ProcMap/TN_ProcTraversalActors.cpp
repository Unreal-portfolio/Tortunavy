#include "World/ProcMap/TN_ProcTraversalActors.h"
#include "World/ProcMap/TN_ProcMapActorUtils.h"
#include "Art/TN_Art.h"
#include "Player/TortugaCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "TN_ProcMapMeshKit.h"
#include "TN_ProcMapRuntimeMesh.h"
#include "TN_ProcMapAmbientFX.h"
#include "Audio/TN_AmbientSynthComponent.h"

// Con nombre (no anónimo) para que su using no se filtre al resto del bloque unity.
namespace TNGeyserDetail
{
	using namespace TNProcMesh;

	/**
	 * Montículo de sínter del géiser (local, base en el suelo): terrazas concéntricas de depósito mineral
	 * (anaranjado de bacterias fuera, crema y blanco dentro), poza turquesa arriba, boca oscura con reborde
	 * y piedras alrededor. Bajo (34 cm): se sube sin saltar.
	 */
	void TNGeyserMound(FTNProcMeshBuffers& M, uint32 Seed)
	{
		const double Radii[4] = { 300.0, 238.0, 172.0, 110.0 };
		const double Tops[4] = { 9.0, 17.0, 25.0, 33.0 };
		const FLinearColor Cols[4] = { FLinearColor(0.84f, 0.5f, 0.2f), FLinearColor(0.9f, 0.76f, 0.48f), FLinearColor(0.92f, 0.9f, 0.83f), FLinearColor(0.83f, 0.83f, 0.8f) };
		double Z0 = -12.0;
		for (int32 k = 0; k < 4; ++k)
		{
			TNProcAddLathe(M, FVector::ZeroVector, { Z0, Tops[k] }, { Radii[k] * 1.04, Radii[k] }, 0.05, Seed + static_cast<uint32>(k) * 13u, Cols[k], 14, 0.0);
			Z0 = Tops[k] - 4.0;
		}
		TNProcAddLathe(M, FVector::ZeroVector, { 33.0, 34.5 }, { 96.0, 92.0 }, 0.03, Seed + 71u, FLinearColor(0.14f, 0.6f, 0.68f), 14, 0.0);
		TNProcAddCylinder(M, FVector(0.0, 0.0, 30.0), FVector(0.0, 0.0, 41.0), 44.0, 38.0, 10, FLinearColor(0.78f, 0.76f, 0.7f), false);
		TNProcAddCylinder(M, FVector(0.0, 0.0, 34.0), FVector(0.0, 0.0, 36.0), 36.0, 36.0, 10, FLinearColor(0.06f, 0.07f, 0.08f), true);
		for (int32 r = 0; r < 7; ++r)
		{
			const double A = TNProcMap::TwoPi * r / 7 + 0.4 * TNProcHashNoise(r, 3, Seed);
			const double D = 310.0 + 40.0 * TNProcHashNoise(r, 5, Seed);
			TNProcAddBoulder(M, FVector(FMath::Cos(A) * D, FMath::Sin(A) * D, -6.0), 22.0 + 14.0 * (0.5 + 0.5 * TNProcHashNoise(r, 7, Seed)), 30.0, Seed + static_cast<uint32>(r), FLinearColor(0.45f, 0.42f, 0.4f));
		}
	}

	/**
	 * Chorro del géiser (local, base en Z = 0, 100 cm de alto: se estira con el pulso): columna de agua abultada y algo
	 * retorcida, de 95 cm de radio en la base, con normales suaves y UV que suben por ella (el material de agua las hace
	 * correr hacia arriba). Transparente abajo y blanca y casi opaca arriba, rematada en una cúpula de espuma.
	 */
	void TNGeyserJet(FTNProcMeshBuffers& M)
	{
		constexpr int32 Around = 18;
		constexpr int32 Along = 22;
		auto Radius = [](double H, double A)
		{
			const double Taper = FMath::Lerp(1.0, 0.7, H);
			const double Bulge = 1.0 + 0.1 * FMath::Sin(H * 17.0 + A * 3.0) + 0.06 * FMath::Sin(A * 5.0 - H * 9.0);
			return 95.0 * Taper * Bulge;
		};
		auto ColorAt = [](double H)
		{
			const float T = static_cast<float>(FMath::SmoothStep(0.62, 1.0, H));
			FLinearColor C = TNProcLerpColor(FLinearColor(0.5f, 0.8f, 0.95f), FLinearColor(0.97f, 0.99f, 1.f), T);
			C.A = FMath::Lerp(0.5f, 0.95f, T);
			return C;
		};
		// Anillos (con la costura duplicada para las UV) y, arriba, la cúpula que cierra.
		const int32 Rings = Along + 4;
		for (int32 j = 0; j <= Rings; ++j)
		{
			const bool bDome = j > Along;
			const double H = bDome ? 1.0 : static_cast<double>(j) / Along;
			const double Dome = bDome ? static_cast<double>(j - Along) / 4.0 : 0.0;
			for (int32 i = 0; i <= Around; ++i)
			{
				const double A = TNProcMap::TwoPi * i / Around;
				const double R = Radius(H, A) * (bDome ? FMath::Cos(Dome * HALF_PI) : 1.0);
				const double Z = H * 100.0 + (bDome ? 14.0 * FMath::Sin(Dome * HALF_PI) : 0.0);
				M.Verts.Add(FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, Z));
				M.Normals.Add(FVector(FMath::Cos(A) * (1.0 - Dome), FMath::Sin(A) * (1.0 - Dome), 0.2 + Dome).GetSafeNormal());
				M.UVs.Add(FVector2D(2.0 * i / Around, 4.0 * (Z / 100.0)));
				M.Colors.Add(ColorAt(H));
			}
		}
		const int32 Row = Around + 1;
		for (int32 j = 0; j < Rings; ++j)
		{
			for (int32 i = 0; i < Around; ++i)
			{
				const int32 V00 = j * Row + i, V10 = V00 + 1, V01 = V00 + Row, V11 = V01 + 1;
				// Cara de fuera (convenio de FTNProcMeshBuffers::AddTri: se emite A, C, B con (B-A)x(C-A) hacia fuera).
				M.Tris.Append({ V00, V01, V10, V11, V10, V01 });
			}
		}
	}

	/**
	 * Espuma de dibujo animado: un racimo de bolas blancas de caras planas (Count alrededor de un anillo de radio Ring,
	 * más una central si bCenter), algo azuladas algunas.
	 */
	void TNGeyserFoam(FTNProcMeshBuffers& M, double Ring, int32 Count, double Size, uint32 Seed, bool bCenter)
	{
		auto Blob = [&](const FVector& C, double R, const FLinearColor& Col, uint32 S)
		{
			TNProcAddLathe(M, C, { -R * 0.8, -R * 0.45, 0.0, R * 0.45, R * 0.85 }, { R * 0.35, R * 0.85, R, R * 0.8, R * 0.25 }, 0.08, S, Col, 7, 0.0);
		};
		const FLinearColor White(0.97f, 0.98f, 1.f);
		const FLinearColor Shade(0.82f, 0.91f, 0.97f);
		if (bCenter) { Blob(FVector(0.0, 0.0, Size * 0.35), Size * 1.15, White, Seed); }
		for (int32 k = 0; k < Count; ++k)
		{
			const double A = TNProcMap::TwoPi * k / Count + 0.35 * TNProcHashNoise(k, 1, Seed);
			const double R = Size * (0.75 + 0.3 * (0.5 + 0.5 * TNProcHashNoise(k, 2, Seed)));
			const double Z = Size * 0.25 * TNProcHashNoise(k, 3, Seed);
			Blob(FVector(FMath::Cos(A) * Ring, FMath::Sin(A) * Ring, Z), R, (k % 3 == 0) ? Shade : White, Seed + static_cast<uint32>(k) * 7u);
		}
	}

	/** Envolvente del chorro en un ciclo (0-1): sube de golpe, se sostiene temblando, baja y queda borboteando. */
	float TNGeyserEnvelope(float Phase)
	{
		if (Phase < 0.16f) { return FMath::Pow(FMath::SmoothStep(0.f, 0.16f, Phase), 0.7f); }
		if (Phase < 0.55f) { return 1.f - 0.07f * (0.5f + 0.5f * FMath::Sin(Phase * 60.f)); }
		if (Phase < 0.8f) { return 1.f - FMath::SmoothStep(0.55f, 0.8f, Phase); }
		return 0.f;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Géiser
// ─────────────────────────────────────────────────────────────────────────────

ATN_ProcGeyser::ATN_ProcGeyser()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Trigger = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(Root);
	Trigger->InitCapsuleSize(190.f, 160.f);
	Trigger->SetRelativeLocation(FVector(0.f, 0.f, 140.f));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	BaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
	BaseMesh->SetupAttachment(Root);
	BaseMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BaseMesh->SetRelativeScale3D(FVector(3.2f, 3.2f, 0.25f));
	BaseMesh->SetRelativeLocation(FVector(0.f, 0.f, 5.f));

	ColumnMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ColumnMesh"));
	ColumnMesh->SetupAttachment(Root);
	ColumnMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ColumnMesh->SetRelativeScale3D(FVector(1.2f, 1.2f, 3.f));
	ColumnMesh->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
	ColumnMesh->SetCastShadow(false);

	if (Cylinder.Succeeded())
	{
		BaseMesh->SetStaticMesh(Cylinder.Object);
		ColumnMesh->SetStaticMesh(Cylinder.Object);
	}

	FoamCapMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FoamCapMesh"));
	FoamBaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FoamBaseMesh"));
	for (UStaticMeshComponent* Foam : { FoamCapMesh.Get(), FoamBaseMesh.Get() })
	{
		Foam->SetupAttachment(Root);
		Foam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Foam->SetCastShadow(false);
	}

	SprayVFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("SprayVFX"));
	SprayVFX->SetupAttachment(Root);
	SprayVFX->bAutoActivate = true;
}

void ATN_ProcGeyser::BeginPlay()
{
	using namespace TNGeyserDetail;
	Super::BeginPlay();
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ATN_ProcGeyser::OnTriggerOverlap);
	PulseTime = FMath::FRand() * 3.f;

	// Low-poly propio: montículo de sínter (con colisión: se sube como un escalón), columna de agua que pulsa (material
	// de agua que corre hacia arriba) y espuma arriba y en la boca.
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcFoliage.M_ProcFoliage"));
	UMaterialInterface* WaterMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcCascade.M_ProcCascade"));
	const uint32 Seed = static_cast<uint32>(GetTypeHash(GetActorLocation()));
	FTNProcMeshBuffers Mound, Jet, Cap, Ring;
	TNGeyserMound(Mound, Seed);
	TNGeyserJet(Jet);
	TNGeyserFoam(Cap, 62.0, 6, 48.0, Seed + 11u, true);
	TNGeyserFoam(Ring, 128.0, 11, 40.0, Seed + 23u, false);
	UStaticMesh* MoundMesh = Mat ? TNProcRuntimeMesh::MakeStaticMesh(this, Mound, Mat, true, 0.f, 1.f, 0.f) : nullptr;
	UStaticMesh* JetMesh = WaterMat ? TNProcRuntimeMesh::MakeStaticMesh(this, Jet, WaterMat, false, 0.f, 1.f, -2.f)
		: (Mat ? TNProcRuntimeMesh::MakeStaticMesh(this, Jet, Mat, false, 0.f, 1.f, 0.f) : nullptr);
	UStaticMesh* CapMesh = Mat ? TNProcRuntimeMesh::MakeStaticMesh(this, Cap, Mat, false, 0.f, 1.f, 0.f) : nullptr;
	UStaticMesh* RingMesh = Mat ? TNProcRuntimeMesh::MakeStaticMesh(this, Ring, Mat, false, 0.f, 1.f, 0.f) : nullptr;
	if (MoundMesh && JetMesh)
	{
		BaseMesh->SetStaticMesh(MoundMesh);
		BaseMesh->SetRelativeScale3D(FVector::OneVector);
		BaseMesh->SetRelativeLocation(FVector::ZeroVector);
		BaseMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		BaseMesh->SetCollisionProfileName(TEXT("BlockAll"));
		ColumnMesh->SetStaticMesh(JetMesh);
		ColumnMesh->SetRelativeLocation(FVector(0.f, 0.f, 30.f));
		FoamCapMesh->SetStaticMesh(CapMesh);
		FoamBaseMesh->SetStaticMesh(RingMesh);
		FoamBaseMesh->SetRelativeLocation(FVector(0.f, 0.f, 36.f));
		// Mallas de arte (Docs/Arte_Assets.md), ya con la colisión y el sitio de cada componente: van de hijas y siguen al
		// chorro y a la espuma cuando el Tick los estira y los mueve.
		TNArt::ApplyToComponent(BaseMesh, TN_ART("ProcMap.Geyser.Mound"));
		TNArt::ApplyToComponent(ColumnMesh, TN_ART("ProcMap.Geyser.Jet"));
		TNArt::ApplyToComponent(FoamCapMesh, TN_ART("ProcMap.Geyser.FoamCap"));
		TNArt::ApplyToComponent(FoamBaseMesh, TN_ART("ProcMap.Geyser.FoamRing"));
	}
	else
	{
		TNProcActors::Tint(BaseMesh, FLinearColor(0.18f, 0.16f, 0.14f));
		TNProcActors::Tint(ColumnMesh, FLinearColor(0.55f, 0.8f, 1.f));
	}

	// Efectos (el orden fija el índice: Launch y Tick los usan). 0 gotas que saltan de lo alto y caen alrededor;
	// 1 bruma que sube; 2 salpicaduras en la boca; 3 espuma en lo alto; 4 espuma en la boca; 5 gotas que suben
	// pegadas a la columna. Los de arriba siguen la altura del chorro (Tick).
	const FVector Base = GetActorLocation();
	TNAmbientFX::FEmitterDesc Spray;
	Spray.Shape = TNAmbientFX::EShape::Drop;
	Spray.Color = FLinearColor(0.75f, 0.92f, 1.f);
	Spray.MaxParticles = 120;
	Spray.Rate = 70.f;
	Spray.SpawnRadius = 70.f;
	Spray.Speed = 520.f;
	Spray.SpeedJitter = 0.35f;
	Spray.Spread = 0.95f;
	Spray.Drag = 0.08f;
	Spray.LifeMin = 1.2f;
	Spray.LifeMax = 2.1f;
	Spray.SizeStart = 17.f;
	Spray.SizeEnd = 10.f;
	TNAmbientFX::AddEmitter(this, Spray, Base + FVector(0.f, 0.f, JetHigh));
	TNAmbientFX::FEmitterDesc Mist;
	Mist.Shape = TNAmbientFX::EShape::Puff;
	Mist.bSoft = true;
	Mist.bCloud = true;
	Mist.Color = FLinearColor(0.95f, 0.97f, 1.f);
	Mist.Alpha = 0.18f;
	Mist.MaxParticles = 24;
	Mist.Rate = 4.f;
	Mist.SpawnRadius = 170.f;
	Mist.SpawnHeight = 120.f;
	Mist.Speed = 80.f;
	Mist.Spread = 0.6f;
	Mist.Gravity = 0.f;
	Mist.Buoyancy = 45.f;
	Mist.Drag = 0.4f;
	Mist.LifeMin = 3.f;
	Mist.LifeMax = 5.f;
	Mist.SizeStart = 110.f;
	Mist.SizeEnd = 280.f;
	Mist.WakeDistance = 22000.f;
	TNAmbientFX::AddEmitter(this, Mist, Base + FVector(0.f, 0.f, 80.f));
	TNAmbientFX::FEmitterDesc Splash;
	Splash.Shape = TNAmbientFX::EShape::Drop;
	Splash.Color = FLinearColor(0.85f, 0.95f, 1.f);
	Splash.MaxParticles = 60;
	Splash.Rate = 34.f;
	Splash.SpawnRadius = 110.f;
	Splash.Speed = 380.f;
	Splash.Spread = 0.95f;
	Splash.LifeMin = 0.5f;
	Splash.LifeMax = 0.9f;
	Splash.SizeStart = 11.f;
	Splash.SizeEnd = 7.f;
	TNAmbientFX::AddEmitter(this, Splash, Base + FVector(0.f, 0.f, 45.f));
	TNAmbientFX::FEmitterDesc TopFoam;
	TopFoam.Shape = TNAmbientFX::EShape::Puff;
	TopFoam.bSoft = true;
	TopFoam.bCloud = true;
	TopFoam.Color = FLinearColor::White;
	TopFoam.Alpha = 0.75f;
	TopFoam.MaxParticles = 44;
	TopFoam.Rate = 30.f;
	TopFoam.SpawnRadius = 100.f;
	TopFoam.Speed = 170.f;
	TopFoam.Spread = 0.9f;
	TopFoam.Gravity = -260.f;
	TopFoam.Drag = 0.6f;
	TopFoam.LifeMin = 0.6f;
	TopFoam.LifeMax = 1.1f;
	TopFoam.SizeStart = 90.f;
	TopFoam.SizeEnd = 210.f;
	TNAmbientFX::AddEmitter(this, TopFoam, Base + FVector(0.f, 0.f, JetHigh));
	TNAmbientFX::FEmitterDesc BaseFoam;
	BaseFoam.Shape = TNAmbientFX::EShape::Puff;
	BaseFoam.bSoft = true;
	BaseFoam.bCloud = true;
	BaseFoam.Color = FLinearColor::White;
	BaseFoam.Alpha = 0.65f;
	BaseFoam.MaxParticles = 30;
	BaseFoam.Rate = 14.f;
	BaseFoam.SpawnRadius = 150.f;
	BaseFoam.Speed = 60.f;
	BaseFoam.Spread = 0.9f;
	BaseFoam.Gravity = 0.f;
	BaseFoam.Buoyancy = 10.f;
	BaseFoam.Drag = 0.8f;
	BaseFoam.LifeMin = 1.f;
	BaseFoam.LifeMax = 1.8f;
	BaseFoam.SizeStart = 60.f;
	BaseFoam.SizeEnd = 150.f;
	TNAmbientFX::AddEmitter(this, BaseFoam, Base + FVector(0.f, 0.f, 42.f));
	TNAmbientFX::FEmitterDesc Rise;
	Rise.Shape = TNAmbientFX::EShape::Drop;
	Rise.Color = FLinearColor(0.8f, 0.94f, 1.f);
	Rise.MaxParticles = 60;
	Rise.Rate = 40.f;
	Rise.SpawnRadius = 80.f;
	Rise.Speed = 1500.f;
	Rise.SpeedJitter = 0.2f;
	Rise.Spread = 0.05f;
	Rise.Gravity = -600.f;
	Rise.Drag = 0.f;
	Rise.LifeMin = 0.45f;
	Rise.LifeMax = 0.7f;
	Rise.SizeStart = 13.f;
	Rise.SizeEnd = 9.f;
	TNAmbientFX::AddEmitter(this, Rise, Base + FVector(0.f, 0.f, 60.f));

	WaterSound = UTN_AmbientSynthComponent::AttachWaterSound(this, 1.f, true);
}

void ATN_ProcGeyser::Tick(float DeltaTime)
{
	using namespace TNGeyserDetail;
	Super::Tick(DeltaTime);

	// Chorro (solo visual, local): sube de golpe hasta JetHigh, se sostiene temblando, baja y queda borboteando a
	// JetLow. La corona de espuma y los efectos de arriba van con la cima; los de abajo, al ritmo del chorro.
	PulseTime += DeltaTime;
	const float Phase = FMath::Frac(PulseTime / FMath::Max(1.f, CycleSeconds));
	const float Surge = TNGeyserEnvelope(Phase);
	if (WaterSound) { WaterSound->SetIntensity(Surge); }
	const float Height = FMath::Lerp(JetLow, JetHigh, Surge) + 25.f * FMath::Sin(PulseTime * 9.f);
	const float Width = (bShaft ? 1.25f : 1.f) * (0.9f + 0.25f * Surge + 0.05f * FMath::Sin(PulseTime * 13.f));
	ColumnMesh->SetRelativeScale3D(FVector(Width, Width, Height / 100.f));
	const FVector Top(0.f, 0.f, 30.f + Height);
	if (FoamCapMesh)
	{
		const float CapScale = Width * (0.8f + 0.45f * Surge + 0.06f * FMath::Sin(PulseTime * 11.f));
		FoamCapMesh->SetRelativeLocation(Top - FVector(0.f, 0.f, 10.f));
		FoamCapMesh->SetRelativeScale3D(FVector(CapScale, CapScale, CapScale * 0.8f));
	}
	if (FoamBaseMesh)
	{
		const float RingScale = 0.9f + 0.2f * Surge + 0.04f * FMath::Sin(PulseTime * 7.f);
		FoamBaseMesh->SetRelativeScale3D(FVector(RingScale, RingScale, 0.8f + 0.4f * Surge));
	}
	const FVector Base = GetActorLocation();
	const float Rates[6] = { 0.25f + 0.75f * Surge, 0.5f + 0.5f * Surge, 0.4f + 0.6f * Surge, 0.3f + 0.7f * Surge, 1.f, 0.15f + 0.85f * Surge };
	for (int32 k = 0; k < 6; ++k)
	{
		if (TNAmbientFX::FEmitter* E = TNAmbientFX::GetEmitter(this, k))
		{
			E->RateScale = Rates[k];
			if (k == 0 || k == 3) { E->Origin = Base + Top; }
		}
	}
	TNAmbientFX::TickOwner(this, DeltaTime);

	// Tiro de la torre: quien ya ha pasado el forjado va hacia el aterrizaje (cayendo en él).
	if (bShaft && InShaft.Num() > 0 && GetWorld())
	{
		const double Now = GetWorld()->GetTimeSeconds();
		for (auto It = InShaft.CreateIterator(); It; ++It)
		{
			ACharacter* Character = It.Key().Get();
			UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
			if (!Move || Now - It.Value() > 6.0) { It.RemoveCurrent(); continue; }
			const float Half = Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
			const FVector P = Character->GetActorLocation();
			if (P.Z - Half < Hole.Z + 60.f) { continue; }
			const float Gravity = FMath::Max(1.f, -Move->GetGravityZ());
			const FVector Land = Target + FVector(0.f, 0.f, Half + 30.f);
			const float Vz = Move->Velocity.Z;
			const float T = (Vz + FMath::Sqrt(FMath::Max(0.f, Vz * Vz + 2.f * Gravity * (P.Z - Land.Z)))) / Gravity;
			Character->LaunchCharacter(FVector(Land.X - P.X, Land.Y - P.Y, 0.f) / FMath::Max(0.25f, T), true, false);
			It.RemoveCurrent();
		}
	}
}

void ATN_ProcGeyser::OnTriggerOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (ACharacter* Character = Cast<ACharacter>(OtherActor))
	{
		if (TNProcActors::SimulatesMovement(Character))
		{
			Launch(Character);
		}
	}
}

void ATN_ProcGeyser::Launch(ACharacter* Character)
{
	UCharacterMovementComponent* Move = Character->GetCharacterMovement();
	if (!Move || !GetWorld())
	{
		return;
	}

	// Antirrebote: un overlap por pisada.
	const double Now = GetWorld()->GetTimeSeconds();
	if (const double* Last = LastLaunchTime.Find(Character))
	{
		if (Now - *Last < 0.6) { return; }
	}
	LastLaunchTime.Add(Character, Now);

	// Parábola que pasa por un ápice por encima del punto más alto y cae en Target.
	const float Gravity = FMath::Max(1.f, -Move->GetGravityZ());
	const FVector Start = Character->GetActorLocation();
	const float HalfHeight = Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	const FVector Land = Target + FVector(0.f, 0.f, HalfHeight + 30.f);
	const float Apex = FMath::Max(Start.Z, Land.Z) + ApexExtra;
	const float Vz = FMath::Sqrt(2.f * Gravity * (Apex - Start.Z));
	const float TUp = Vz / Gravity;
	const float TDown = FMath::Sqrt(FMath::Max(0.f, 2.f * (Apex - Land.Z) / Gravity));
	const FVector Flat(Land.X - Start.X, Land.Y - Start.Y, 0.f);
	FVector Velocity = Flat / FMath::Max(0.1f, TUp + TDown) + FVector(0.f, 0.f, Vz);
	if (bShaft)
	{
		// Por el tiro: en vertical, llegando al centro del hueco justo al pasar el forjado; arriba, Tick lo lleva al
		// aterrizaje.
		const float ZCross = Hole.Z + HalfHeight + 20.f;
		const float Disc = Vz * Vz - 2.f * Gravity * (ZCross - Start.Z);
		const float TCross = Disc > 0.f ? (Vz - FMath::Sqrt(Disc)) / Gravity : TUp;
		Velocity = FVector(Hole.X - Start.X, Hole.Y - Start.Y, 0.f) / FMath::Max(0.2f, TCross) + FVector(0.f, 0.f, Vz);
		InShaft.Add(Character, Now);
	}

	if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Character))
	{
		Turtle->SetFallImmuneUntilLanded();
	}
	Character->LaunchCharacter(Velocity, true, true);

	PlayLaunchBurst();
}

void ATN_ProcGeyser::PlayLaunchBurst()
{
	// Estallido al lanzar: el chorro vuelve a arrancar, gotas y espuma arriba y en la boca y una bocanada de bruma.
	PulseTime -= FMath::Frac(PulseTime / FMath::Max(1.f, CycleSeconds)) * FMath::Max(1.f, CycleSeconds);
	if (TNAmbientFX::FEmitter* E = TNAmbientFX::GetEmitter(this, 0)) { TNAmbientFX::Burst(*E, 50); }
	if (TNAmbientFX::FEmitter* E = TNAmbientFX::GetEmitter(this, 1)) { TNAmbientFX::Burst(*E, 5); }
	if (TNAmbientFX::FEmitter* E = TNAmbientFX::GetEmitter(this, 4)) { TNAmbientFX::Burst(*E, 12); }
	if (TNAmbientFX::FEmitter* E = TNAmbientFX::GetEmitter(this, 5)) { TNAmbientFX::Burst(*E, 30); }

	if (LaunchSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, LaunchSound, GetActorLocation());
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Tobogán
// ─────────────────────────────────────────────────────────────────────────────

ATN_ProcSlideZone::ATN_ProcSlideZone()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

bool ATN_ProcSlideZone::FindFlowAt(const FVector& Point, FVector& OutFlow) const
{
	for (int32 Index = 0; Index < Segments.Num() && Index < SegmentDirs.Num(); ++Index)
	{
		const UBoxComponent* Seg = Segments[Index];
		if (!Seg)
		{
			continue;
		}
		const FVector Local = Seg->GetComponentTransform().InverseTransformPositionNoScale(Point);
		const FVector Extent = Seg->GetUnscaledBoxExtent();
		if (FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z + 200.f)
		{
			OutFlow = SegmentDirs[Index];
			return true;
		}
	}
	return false;
}

bool ATN_ProcSlideZone::GetTop(FVector& OutTop, FVector& OutFlow) const
{
	if (Segments.Num() == 0 || !Segments[0] || SegmentDirs.Num() == 0)
	{
		return false;
	}
	const UBoxComponent* First = Segments[0];
	OutFlow = SegmentDirs[0];
	// El tramo va centrado entre sus dos puntos y 120 cm por encima: su principio es el labio.
	OutTop = First->GetComponentLocation() - OutFlow * First->GetUnscaledBoxExtent().X - FVector(0.f, 0.f, 120.f);
	return true;
}

bool ATN_ProcSlideZone::GetPool(FVector& OutCenter, float& OutRadius) const
{
	OutCenter = PoolCenterWorld;
	OutRadius = PoolRadiusCm;
	return PoolRadiusCm > 0.f;
}

void ATN_ProcSlideZone::InitFromPoints(const TArray<FVector>& Points, float Width, const FVector& PoolCenter, float PoolRadius, const FVector& Impact)
{
	PoolCenterWorld = PoolCenter;
	PoolRadiusCm = PoolRadius;
	for (UBoxComponent* Seg : Segments)
	{
		if (Seg) { Seg->DestroyComponent(); }
	}
	Segments.Reset();
	SegmentDirs.Reset();

	for (int32 i = 0; i + 1 < Points.Num(); ++i)
	{
		const FVector A = Points[i];
		const FVector B = Points[i + 1];
		const FVector Dir = (B - A).GetSafeNormal();
		if (Dir.IsNearlyZero()) { continue; }

		UBoxComponent* Seg = NewObject<UBoxComponent>(this);
		Seg->SetupAttachment(Root);
		Seg->SetBoxExtent(FVector((B - A).Size() * 0.5f + 60.f, Width * 0.5f + 100.f, 220.f));
		Seg->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Seg->SetCollisionResponseToAllChannels(ECR_Ignore);
		Seg->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		Seg->SetGenerateOverlapEvents(true);
		Seg->RegisterComponent();
		Seg->SetWorldLocationAndRotation((A + B) * 0.5f + FVector(0.f, 0.f, 120.f), Dir.Rotation());
		Seg->OnComponentBeginOverlap.AddDynamic(this, &ATN_ProcSlideZone::OnSegmentOverlap);
		Segments.Add(Seg);
		SegmentDirs.Add(Dir);
	}

	// Efectos: en el labio (se ve dónde empieza la cascada) y en la poza (dónde acaba), a la cota del agua.
	TNAmbientFX::RemoveOwner(this);
	if (Points.Num() >= 2)
	{
		const FVector Lip = Points[0];
		const FVector LipFlow = (Points[1] - Points[0]).GetSafeNormal2D();
		const FVector Flow = (Points.Last() - Points[Points.Num() - 2]).GetSafeNormal2D();

		// 0 Labio: espuma que se asoma y cae con el agua.
		TNAmbientFX::FEmitterDesc LipFoam;
		LipFoam.Shape = TNAmbientFX::EShape::Puff;
		LipFoam.bSoft = true;
		LipFoam.bCloud = true;
		LipFoam.Color = FLinearColor::White;
		LipFoam.Alpha = 0.6f;
		LipFoam.MaxParticles = 30;
		LipFoam.Rate = 14.f;
		LipFoam.SpawnRadius = Width * 0.32f;
		LipFoam.Direction = (LipFlow + FVector::UpVector * 0.2f).GetSafeNormal();
		LipFoam.Speed = 140.f;
		LipFoam.Spread = 0.5f;
		LipFoam.Gravity = -300.f;
		LipFoam.Drag = 0.6f;
		LipFoam.LifeMin = 0.8f;
		LipFoam.LifeMax = 1.3f;
		LipFoam.SizeStart = 45.f;
		LipFoam.SizeEnd = 110.f;
		TNAmbientFX::AddEmitter(this, LipFoam, Lip + FVector(0.f, 0.f, 35.f));
		// 1 Labio: gotitas que saltan por delante.
		TNAmbientFX::FEmitterDesc LipSpray;
		LipSpray.Shape = TNAmbientFX::EShape::Drop;
		LipSpray.Color = FLinearColor(0.85f, 0.95f, 1.f);
		LipSpray.MaxParticles = 40;
		LipSpray.Rate = 24.f;
		LipSpray.SpawnRadius = Width * 0.3f;
		LipSpray.Direction = (LipFlow + FVector::UpVector * 0.6f).GetSafeNormal();
		LipSpray.Speed = 260.f;
		LipSpray.Spread = 0.5f;
		LipSpray.LifeMin = 0.6f;
		LipSpray.LifeMax = 0.9f;
		LipSpray.SizeStart = 9.f;
		LipSpray.SizeEnd = 6.f;
		TNAmbientFX::AddEmitter(this, LipSpray, Lip + FVector(0.f, 0.f, 30.f));

		// 2 Poza: salpicaduras donde cae el agua.
		TNAmbientFX::FEmitterDesc Splash;
		Splash.Shape = TNAmbientFX::EShape::Drop;
		Splash.Color = FLinearColor(0.82f, 0.94f, 1.f);
		Splash.MaxParticles = 70;
		Splash.Rate = 40.f;
		Splash.SpawnRadius = Width * 0.3f;
		Splash.Direction = (FVector::UpVector * 1.3f + Flow).GetSafeNormal();
		Splash.Speed = 460.f;
		Splash.Spread = 0.7f;
		Splash.LifeMin = 0.7f;
		Splash.LifeMax = 1.3f;
		Splash.SizeStart = 12.f;
		Splash.SizeEnd = 7.f;
		TNAmbientFX::AddEmitter(this, Splash, Impact + FVector(0.f, 0.f, 20.f));
		// 3 Poza: espuma que se abre desde el impacto.
		TNAmbientFX::FEmitterDesc Foam;
		Foam.Shape = TNAmbientFX::EShape::Puff;
		Foam.bSoft = true;
		Foam.bCloud = true;
		Foam.Color = FLinearColor::White;
		Foam.Alpha = 0.65f;
		Foam.MaxParticles = 30;
		Foam.Rate = 10.f;
		Foam.SpawnRadius = Width * 0.25f;
		Foam.Direction = Flow;
		Foam.Speed = 90.f;
		Foam.Spread = 1.f;
		Foam.Gravity = 0.f;
		Foam.Drag = 0.5f;
		Foam.LifeMin = 2.f;
		Foam.LifeMax = 3.f;
		Foam.SizeStart = 50.f;
		Foam.SizeEnd = 160.f;
		TNAmbientFX::AddEmitter(this, Foam, Impact + FVector(0.f, 0.f, 8.f));
		// 4 Poza: bruma.
		TNAmbientFX::FEmitterDesc Mist;
		Mist.Shape = TNAmbientFX::EShape::Puff;
		Mist.bSoft = true;
		Mist.bCloud = true;
		Mist.Color = FLinearColor(0.92f, 0.96f, 1.f);
		Mist.Alpha = 0.25f;
		Mist.MaxParticles = 18;
		Mist.Rate = 3.5f;
		Mist.SpawnRadius = Width * 0.4f;
		Mist.Speed = 60.f;
		Mist.Spread = 0.6f;
		Mist.Gravity = 0.f;
		Mist.Buoyancy = 30.f;
		Mist.Drag = 0.4f;
		Mist.LifeMin = 3.f;
		Mist.LifeMax = 4.5f;
		Mist.SizeStart = 110.f;
		Mist.SizeEnd = 320.f;
		Mist.WakeDistance = 20000.f;
		TNAmbientFX::AddEmitter(this, Mist, Impact + FVector(0.f, 0.f, 60.f));
		// 5 Poza: ondas que se abren desde donde cae el agua hasta el borde y se hunden al final.
		TNAmbientFX::FEmitterDesc Ripples;
		Ripples.Shape = TNAmbientFX::EShape::Ring;
		Ripples.bSoft = true;
		Ripples.Color = FLinearColor(0.93f, 0.98f, 1.f);
		Ripples.Alpha = 0.55f;
		Ripples.MaxParticles = 8;
		Ripples.Rate = 2.4f;
		Ripples.SpawnRadius = 25.f;
		Ripples.Direction = -FVector::UpVector;
		Ripples.Speed = 5.f;
		Ripples.SpeedJitter = 0.f;
		Ripples.Spread = 0.f;
		Ripples.Gravity = -4.f;
		Ripples.Drag = 0.f;
		Ripples.LifeMin = 2.2f;
		Ripples.LifeMax = 2.6f;
		Ripples.SizeStart = 60.f;
		// Hasta pasado el centro de la poza sin salirse mucho por el lado de la cascada.
		Ripples.SizeEnd = 2.f * FMath::Max(150.f, PoolRadius - 0.5f * static_cast<float>(FVector::Dist2D(PoolCenter, Impact)));
		Ripples.WakeDistance = 14000.f;
		TNAmbientFX::AddEmitter(this, Ripples, Impact + FVector(0.f, 0.f, 3.f));

		// Rumor de la cascada, donde cae el agua (se oye unos 40 m).
		const FVector FallAt = Impact + FVector(0.f, 0.f, 150.f);
		if (UTN_AmbientSynthComponent* Fall = FindComponentByClass<UTN_AmbientSynthComponent>()) { Fall->SetWorldLocation(FallAt); }
		else { UTN_AmbientSynthComponent::AttachWaterSoundAt(this, FallAt, FMath::Clamp(Width / 1500.f, 0.7f, 1.3f), false); }
	}
}

void ATN_ProcSlideZone::OnSegmentOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(OtherActor))
	{
		if (TNProcActors::SimulatesMovement(Turtle))
		{
			Turtle->SetFallImmuneUntilLanded();
		}
	}
}

void ATN_ProcSlideZone::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TNAmbientFX::TickOwner(this, DeltaTime);

	for (int32 i = 0; i < Segments.Num(); ++i)
	{
		UBoxComponent* Seg = Segments[i];
		if (!Seg) { continue; }
		TArray<AActor*> Overlapping;
		Seg->GetOverlappingActors(Overlapping, ACharacter::StaticClass());
		for (AActor* Actor : Overlapping)
		{
			ACharacter* Character = Cast<ACharacter>(Actor);
			if (!Character || !TNProcActors::SimulatesMovement(Character)) { continue; }
			if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
			{
				// Empuje ladera abajo: la pendiente ya no es caminable, esto le da ritmo de tobogán.
				Move->AddImpulse(SegmentDirs[i] * BoostAcceleration * DeltaTime, true);
			}
			if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Character))
			{
				Turtle->SetFallImmuneUntilLanded();
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Volumen de muerte
// ─────────────────────────────────────────────────────────────────────────────

ATN_ProcKillVolume::ATN_ProcKillVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	SetRootComponent(Box);
	Box->SetBoxExtent(FVector(200.f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Box->SetGenerateOverlapEvents(true);
	Box->SetHiddenInGame(true);
}

void ATN_ProcKillVolume::BeginPlay()
{
	Super::BeginPlay();
	Box->OnComponentBeginOverlap.AddDynamic(this, &ATN_ProcKillVolume::OnBoxOverlap);
}

void ATN_ProcKillVolume::SetExtent(const FVector& Extent)
{
	Box->SetBoxExtent(Extent);
}

void ATN_ProcKillVolume::OnBoxOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!TNProcActors::IsServerWorld(this))
	{
		return;
	}
	if (ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(OtherActor))
	{
		Turtle->RequestKill(this);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Meta
// ─────────────────────────────────────────────────────────────────────────────

void ATN_ProcFinishVolume::SetExtent(const FVector& Extent)
{
	if (TriggerBox)
	{
		TriggerBox->SetBoxExtent(Extent);
	}
}
