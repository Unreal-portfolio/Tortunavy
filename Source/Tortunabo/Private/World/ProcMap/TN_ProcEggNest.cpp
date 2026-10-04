#include "World/ProcMap/TN_ProcEggNest.h"
#include "Art/TN_Art.h"
#include "Game/TN_ProcMapGameMode.h"
#include "Player/TortugaCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Engine/StaticMesh.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "Core/TN_Log.h"
#include "World/TN_PlaceholderArt.h"
#include "World/TN_PlaceholderArtMeshes.h"
#include "Lobby/TN_CastleKit.h"

namespace
{
	constexpr int32 NumNestEggs = 8;

	/**
	 * Base de cada huevo de la pila (cm, locales): cinco abajo, hundidos en el nido, dos encima en los huecos y uno en
	 * la cima. Los huevos tienen el origen en su base.
	 */
	const FVector& NestEggSpot(int32 Index)
	{
		static const FVector Spots[NumNestEggs] = {
			FVector(0.0, 0.0, 12.0), FVector(62.0, 0.0, 10.0), FVector(-62.0, 0.0, 10.0), FVector(0.0, 62.0, 10.0),
			FVector(0.0, -62.0, 10.0), FVector(31.0, 31.0, 60.0), FVector(-31.0, -31.0, 60.0), FVector(0.0, 0.0, 108.0) };
		return Spots[FMath::Clamp(Index, 0, NumNestEggs - 1)];
	}

	/** Inclinación de cada huevo: los del aro de abajo se abren hacia fuera; el giro reparte las bandas de color. */
	FRotator NestEggRotation(int32 Index)
	{
		const FVector& Spot = NestEggSpot(Index);
		if (Index == 0 || Index >= 5)
		{
			return FRotator(Index == NumNestEggs - 1 ? 0.0 : 6.0, Index * 37.0, 0.0);
		}
		// Con el +X local mirando hacia fuera, un pitch negativo baja el +X: la punta del huevo se abre hacia fuera.
		const double Out = FMath::RadiansToDegrees(FMath::Atan2(Spot.Y, Spot.X));
		return FRotator(-12.0, Out, 0.0);
	}

	/** Huevo entero de la pila del lobby (perfil de TNCastleKit), cerrado por abajo y por arriba, con su banda de color. */
	void BuildNestEgg(TNProcMesh::FTNProcMeshBuffers& B, const FLinearColor& Shell, const FLinearColor& Accent)
	{
		const TArray<double> Zs(TNCastleKit::EggZ(), TNCastleKit::EggProfileNum);
		const TArray<double> Rs(TNCastleKit::EggR(), TNCastleKit::EggProfileNum);
		constexpr int32 Segments = 16;
		constexpr int32 AccentEvery = 3;
		TNCastleKit::AddRevolution(B, FVector::ZeroVector, Zs, Rs, Segments, Shell, true, Accent, AccentEvery);
		const FVector Bottom(0.0, 0.0, Zs[0]);
		const FVector Rim(0.0, 0.0, Zs.Last());
		const FVector Top(0.0, 0.0, Zs.Last() + 3.0);
		for (int32 k = 0; k < Segments; ++k)
		{
			const double A0 = TNProcMap::TwoPi * k / Segments;
			const double A1 = TNProcMap::TwoPi * (k + 1) / Segments;
			const FVector Dir0(FMath::Cos(A0), FMath::Sin(A0), 0.0);
			const FVector Dir1(FMath::Cos(A1), FMath::Sin(A1), 0.0);
			B.AddTri(Bottom, Bottom + Dir0 * Rs[0], Bottom + Dir1 * Rs[0], FVector(0.0, 0.0, -1.0), Shell);
			B.AddTri(Top, Rim + Dir0 * Rs.Last(), Rim + Dir1 * Rs.Last(), FVector(0.0, 0.0, 1.0), Shell);
		}
	}

	/**
	 * Nido de arena (cm, origen en el suelo): montículo bajo con el hueco más oscuro, dos vueltas de paja y dos estrellas
	 * y dos vieiras en la arena de alrededor.
	 */
	void BuildNestBase(TNProcMesh::FTNProcMeshBuffers& B)
	{
		using TNCastleKit::Pal;
		const FVector Up(0.0, 0.0, 1.0);
		TNProcMesh::TNProcAddCylinder(B, FVector(0.0, 0.0, -8.0), FVector(0.0, 0.0, 14.0), 155.0, 128.0, 24, Pal(0xF0D49A));
		TNProcMesh::TNProcAddCylinder(B, FVector(0.0, 0.0, 13.0), FVector(0.0, 0.0, 15.0), 104.0, 98.0, 24, Pal(0xD9B474));
		constexpr int32 Straws = 18;
		for (int32 Layer = 0; Layer < 2; ++Layer)
		{
			const double Z = 16.0 + Layer * 9.0;
			const double Radius = 116.0 - Layer * 6.0;
			for (int32 n = 0; n < Straws; ++n)
			{
				const double A = TNProcMap::TwoPi * (n + Layer * 0.5) / Straws;
				const FVector P0(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius, Z);
				const FVector P1(FMath::Cos(A + 0.55) * (Radius - 8.0), FMath::Sin(A + 0.55) * (Radius - 8.0), Z + 5.0 + 3.0 * (n % 2));
				B.AddBeam(P0, P1, 4.5, ((n + Layer) % 2) ? Pal(0xD9B45A) : Pal(0xC49A45));
			}
		}
		for (int32 i = 0; i < 4; ++i)
		{
			const double A = TNProcMap::TwoPi * i / 4.0 + 0.6;
			const FVector P(FMath::Cos(A) * 185.0, FMath::Sin(A) * 185.0, 0.0);
			if (i % 2 == 0)
			{
				TNCastleKit::AddStarfish(B, P, 24.0, A, Pal(i == 0 ? 0xFF8A70 : 0xFFB077));
			}
			else
			{
				TNCastleKit::AddScallop(B, P + Up * 3.0, Up, FVector(FMath::Cos(A), FMath::Sin(A), 0.0), 22.0, Pal(0xFFE0C2));
			}
		}
	}

	/** Huevo Index de la pila con la cáscara Shell (lineal), compartido entre todas las pilas por cáscara y banda. */
	UStaticMesh* NestEggMesh(int32 Index, const FLinearColor& Shell)
	{
		const uint32 AccentHex = TNCastleKit::EggAccent(Index);
		const FLinearColor ShellOpaque(Shell.R, Shell.G, Shell.B, 1.f);
		const FString Key = FString::Printf(TEXT("EggNest.Egg.%.3f_%.3f_%.3f.%06X"), ShellOpaque.R, ShellOpaque.G, ShellOpaque.B, AccentHex);
		return TNPlaceholderArt::CachedArtMesh(Key, [&ShellOpaque, AccentHex](TNProcMesh::FTNProcMeshBuffers& B)
		{
			BuildNestEgg(B, ShellOpaque, TNCastleKit::Pal(AccentHex));
		}, TNPlaceholderArt::MatteAlpha);
	}
}

ATN_ProcEggNest::ATN_ProcEggNest()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(Root);
	Trigger->InitSphereRadius(420.f);
	Trigger->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));

	// Sin mallas en el constructor: el arte lo pone BuildCodeArt (o NestMeshOverride) en BeginPlay (#50).
	NestBase = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NestBase"));
	NestBase->SetupAttachment(Root);
	NestBase->SetMobility(EComponentMobility::Movable);
	NestBase->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NestBase->SetCanEverAffectNavigation(false);

	// Colisión de la peana: un cilindro invisible bajo el nido. La malla de código del nido no trae colisión simple.
	PedestalCollision = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PedestalCollision"));
	PedestalCollision->SetupAttachment(Root);
	PedestalCollision->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	PedestalCollision->SetHiddenInGame(true);
	PedestalCollision->SetCastShadow(false);
	PedestalCollision->SetRelativeScale3D(FVector(2.2f, 2.2f, 0.25f));
	PedestalCollision->SetRelativeLocation(FVector(0.f, 0.f, 10.f));
	if (Cylinder.Succeeded()) { PedestalCollision->SetStaticMesh(Cylinder.Object); }

	for (int32 i = 0; i < NumNestEggs; ++i)
	{
		UStaticMeshComponent* Egg = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Egg%d"), i));
		Egg->SetupAttachment(Root);
		Egg->SetMobility(EComponentMobility::Movable);
		Egg->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Egg->SetCanEverAffectNavigation(false);
		Egg->SetRelativeLocationAndRotation(NestEggSpot(i), NestEggRotation(i));
		Eggs.Add(Egg);
	}

	// Colisión de los huevos: un cono invisible sobre la peana que los envuelve. Con una esfera por huevo, la tortuga
	// podía quedarse encajada entre ellos sin suelo andable; la pendiente del cono la hace resbalar hasta la peana.
	EggsCollision = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EggsCollision"));
	EggsCollision->SetupAttachment(Root);
	EggsCollision->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	EggsCollision->SetHiddenInGame(true);
	EggsCollision->SetCastShadow(false);
	EggsCollision->SetRelativeLocation(FVector(0.f, 0.f, 105.f));
	EggsCollision->SetRelativeScale3D(FVector(1.7f, 1.7f, 1.65f));
	if (Cone.Succeeded()) { EggsCollision->SetStaticMesh(Cone.Object); }
}

void ATN_ProcEggNest::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_ProcEggNest, NestOrder);
	DOREPLIFETIME(ATN_ProcEggNest, PathProgress);
	DOREPLIFETIME(ATN_ProcEggNest, bActivated);
}

void ATN_ProcEggNest::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &ATN_ProcEggNest::OnTriggerOverlap);
	}
	if (NestMeshOverride)
	{
		NestBase->SetStaticMesh(NestMeshOverride);
		NestBase->SetRelativeScale3D(FVector(1.f));
		for (UStaticMeshComponent* Egg : Eggs) { Egg->SetVisibility(false); }
		// La malla definitiva trae su propia colisión.
		NestBase->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		PedestalCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		EggsCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	else
	{
		BuildCodeArt();
	}
	ApplyVisual();
	// Mallas de arte (Docs/Arte_Assets.md): van de hijas y heredan la escala de cada componente.
	TNArt::ApplyToComponent(NestBase, TN_ART("ProcMap.Nest.Base"));
	for (UStaticMeshComponent* Egg : Eggs) { TNArt::ApplyToComponent(Egg, TN_ART("ProcMap.Nest.Egg")); }
}

void ATN_ProcEggNest::BuildCodeArt()
{
	if (GetNetMode() == NM_DedicatedServer || !TNPlaceholderArt::NeedsCodeArt(NestBase))
	{
		return;
	}
	UStaticMesh* Base = TNPlaceholderArt::CachedArtMesh(TEXT("EggNest.Base"), [](TNProcMesh::FTNProcMeshBuffers& B)
	{
		BuildNestBase(B);
	}, TNPlaceholderArt::MatteAlpha);
	if (!Base)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[EggNest] %s: no se ha podido construir el nido; la pila sale sin arte."), *GetName());
		return;
	}
	NestBase->SetStaticMesh(Base);
	NestBase->SetRelativeTransform(FTransform::Identity);
	const FVector EggScale(CodeArtEggScale);
	for (int32 i = 0; i < Eggs.Num(); ++i)
	{
		if (UStaticMeshComponent* Egg = Eggs[i])
		{
			Egg->SetRelativeTransform(FTransform(NestEggRotation(i), NestEggSpot(i), EggScale));
		}
	}
	bCodeArt = true;
}

FTransform ATN_ProcEggNest::GetRespawnTransform(int32 Slot) const
{
	const float Angle = (Slot % 8) * (UE_PI / 4.f);
	const FVector Offset(FMath::Cos(Angle) * 260.f, FMath::Sin(Angle) * 260.f, 120.f);
	return FTransform(GetActorRotation(), GetActorLocation() + Offset);
}

void ATN_ProcEggNest::GatherWorldNests(UWorld* World, TArray<ATN_ProcEggNest*>& OutNests)
{
	OutNests.Reset();
	if (!World)
	{
		return;
	}
	for (TActorIterator<ATN_ProcEggNest> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			OutNests.Add(*It);
		}
	}
}

ATN_ProcEggNest* ATN_ProcEggNest::PickRespawnNest(TConstArrayView<ATN_ProcEggNest*> Nests, int32 ReachedOrder, float MinProgress)
{
	ATN_ProcEggNest* Best = nullptr;
	for (ATN_ProcEggNest* Nest : Nests)
	{
		if (!IsValid(Nest) || Nest->GetNestOrder() > ReachedOrder || Nest->GetPathProgress() < MinProgress)
		{
			continue;
		}
		if (!Best || Nest->GetNestOrder() > Best->GetNestOrder())
		{
			Best = Nest;
		}
	}
	return Best;
}

void ATN_ProcEggNest::MarkActivated()
{
	if (!HasAuthority() || bActivated) { return; }
	bActivated = true;
	OnRep_Activated();
}

void ATN_ProcEggNest::OnRep_Activated()
{
	ApplyVisual();
	if (bActivated)
	{
		if (ActivateSound) { UGameplayStatics::PlaySoundAtLocation(this, ActivateSound, GetActorLocation()); }
		OnNestActivated();
	}
}

void ATN_ProcEggNest::ApplyVisual()
{
	if (!bCodeArt)
	{
		return;
	}
	const FLinearColor& Shell = bActivated ? ActiveColor : IdleColor;
	for (int32 i = 0; i < Eggs.Num(); ++i)
	{
		if (UStaticMeshComponent* Egg = Eggs[i])
		{
			Egg->SetStaticMesh(NestEggMesh(i, Shell));
		}
	}
}

void ATN_ProcEggNest::OnTriggerOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(OtherActor);
	if (!Turtle || Turtle->IsDead()) { return; }
	APlayerController* PC = Cast<APlayerController>(Turtle->GetController());
	if (!PC) { return; }
	if (ATN_ProcMapGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ATN_ProcMapGameMode>() : nullptr)
	{
		GM->NotifyEggNestReached(PC, this);
	}
}
