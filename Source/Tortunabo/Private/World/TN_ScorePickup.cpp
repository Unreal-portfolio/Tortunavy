#include "World/TN_ScorePickup.h"
#include "World/TN_ScorePickupWakeSubsystem.h"
#include "Components/WidgetComponent.h"
#include "Core/TN_Log.h"
#include "Core/TN_CoopPlayerState.h"
#include "Player/TortugaCharacter.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "ProcMap/TN_ProcMapMeshKit.h"
#include "ProcMap/TN_ProcMapRuntimeMesh.h"
#include "ProcMap/TN_ProcMapAmbientFX.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto
// del bloque.
namespace TNScorePickupDetail
{
	/** Colores de la vieira de cada tamaño (M_ProcGlow: emisivo = color del vértice x 2). */
	struct FShellPalette
	{
		FLinearColor Hinge;
		FLinearColor Middle;
		FLinearColor Edge;
		FLinearColor Rim;
		FLinearColor Ears;
	};

	FShellPalette PaletteFor(TNScoreShells::ETier Tier)
	{
		switch (Tier)
		{
			// Pequeña: oro claro, más brillante (el brillo sencillo que la distingue).
			case TNScoreShells::ETier::Small:
				return { FLinearColor(0.75f, 0.45f, 0.12f), FLinearColor(0.95f, 0.78f, 0.32f), FLinearColor(1.f, 0.9f, 0.5f),
					FLinearColor(1.f, 0.86f, 0.42f), FLinearColor(0.8f, 0.5f, 0.15f) };
			// Grande: nácar turquesa con el borde casi blanco.
			case TNScoreShells::ETier::Big:
				return { FLinearColor(0.08f, 0.35f, 0.42f), FLinearColor(0.35f, 0.8f, 0.8f), FLinearColor(0.75f, 1.f, 0.95f),
					FLinearColor(0.7f, 0.95f, 0.9f), FLinearColor(0.1f, 0.4f, 0.45f) };
			// Reina: rosa y violeta con el borde dorado.
			case TNScoreShells::ETier::Grand:
				return { FLinearColor(0.4f, 0.06f, 0.32f), FLinearColor(0.85f, 0.3f, 0.7f), FLinearColor(1.f, 0.82f, 0.35f),
					FLinearColor(1.f, 0.78f, 0.3f), FLinearColor(0.5f, 0.1f, 0.4f) };
			// Normal: la concha dorada de siempre.
			default:
				return { FLinearColor(0.6f, 0.27f, 0.07f), FLinearColor(0.78f, 0.55f, 0.16f), FLinearColor(0.85f, 0.72f, 0.3f),
					FLinearColor(0.85f, 0.7f, 0.25f), FLinearColor(0.62f, 0.33f, 0.09f) };
		}
	}

	/**
	 * Concha de vieira de unos 70 cm, de pie en el plano XZ (se ve de frente desde ±Y): valvas abombadas por las dos
	 * caras con costillas en abanico, borde festoneado y orejetas en la charnela, con los colores de su tamaño. Una sola
	 * malla en caché por tamaño para todas las conchas.
	 */
	UStaticMesh* ShellCoinMesh(TNScoreShells::ETier Tier)
	{
		static TWeakObjectPtr<UStaticMesh> Cached[TNScoreShells::NumTiers];
		const int32 Index = FMath::Clamp(static_cast<int32>(Tier), 0, TNScoreShells::NumTiers - 1);
		if (Cached[Index].IsValid()) { return Cached[Index].Get(); }
		UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcGlow.M_ProcGlow"));
		if (!Mat) { Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcFoliage.M_ProcFoliage")); }
		if (!Mat) { return nullptr; }

		const FShellPalette Pal = PaletteFor(Tier);
		TNProcMesh::FTNProcMeshBuffers M;
		constexpr int32 Fan = 18;
		constexpr int32 Rings = 5;
		constexpr int32 Ribs = 9;
		const double Spread = FMath::DegreesToRadians(80.0);
		const FVector Hinge(0.0, 0.0, -26.0);
		auto EdgeR = [&](double A)
		{
			const double Rib = FMath::Abs(FMath::Sin((A / Spread + 1.0) * 0.5 * Ribs * PI));
			return 34.0 * (1.0 + 0.07 * Rib);
		};
		auto Point = [&](int32 i, int32 j, double Side)
		{
			const double A = FMath::Lerp(-Spread, Spread, static_cast<double>(i) / Fan);
			const double T = static_cast<double>(j) / Rings;
			const double R = EdgeR(A) * T;
			const double Rib = FMath::Abs(FMath::Sin((A / Spread + 1.0) * 0.5 * Ribs * PI));
			const double Bulge = (7.5 * (1.0 - T * T) + 1.6 * Rib * T) * Side;
			return Hinge + FVector(FMath::Sin(A) * R, Bulge, FMath::Cos(A) * R);
		};
		auto ColorAt = [&](int32 i, int32 j)
		{
			const float T = static_cast<float>(j) / Rings;
			FLinearColor C = FMath::Lerp(Pal.Hinge, Pal.Middle, T);
			if (((i + 1) / 2) % 2 == 0) { C *= 0.82f; }
			if (j == Rings) { C = Pal.Edge; }
			return C;
		};
		for (const double Side : { 1.0, -1.0 })
		{
			const FVector Out(0.0, Side, 0.0);
			for (int32 j = 0; j < Rings; ++j)
			{
				for (int32 i = 0; i < Fan; ++i)
				{
					M.AddQuad(Point(i, j, Side), Point(i + 1, j, Side), Point(i + 1, j + 1, Side), Point(i, j + 1, Side), Out, ColorAt(i, j + 1));
				}
			}
		}
		// Canto: une las dos valvas por el borde festoneado.
		for (int32 i = 0; i < Fan; ++i)
		{
			const double Am = FMath::Lerp(-Spread, Spread, (i + 0.5) / Fan);
			const FVector Radial(FMath::Sin(Am), 0.0, FMath::Cos(Am));
			M.AddQuad(Point(i, Rings, 1.0), Point(i + 1, Rings, 1.0), Point(i + 1, Rings, -1.0), Point(i, Rings, -1.0), Radial, Pal.Rim);
		}
		// Orejetas de la charnela.
		for (const double Sx : { -1.0, 1.0 })
		{
			const FVector A = Hinge + FVector(Sx * 3.0, 0.0, 2.0);
			const FVector B = Hinge + FVector(Sx * 17.0, 0.0, 6.0);
			const FVector C = Hinge + FVector(Sx * 14.0, 0.0, -7.0);
			const FVector D = Hinge + FVector(Sx * 3.0, 0.0, -6.0);
			for (const double Side : { 1.0, -1.0 })
			{
				const FVector Off(0.0, 2.5 * Side, 0.0);
				M.AddQuad(A + Off, B + Off, C + Off, D + Off, FVector(0.0, Side, 0.0), Pal.Ears);
			}
		}
		UStaticMesh* Mesh = TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), M, Mat, false, 0.f, 1.f, 0.f);
		if (Mesh) { Mesh->AddToRoot(); }
		Cached[Index] = Mesh;
		return Mesh;
	}

	/** Alto de la columna de luz de las grandes y las reinas (cm, desde el suelo), su halo (escala de 1 m) y su luz. */
	constexpr double BeamHeight(TNScoreShells::ETier Tier) { return Tier == TNScoreShells::ETier::Grand ? 1300.0 : 900.0; }
	constexpr float HaloScale(TNScoreShells::ETier Tier) { return Tier == TNScoreShells::ETier::Grand ? 2.4f : 1.8f; }
	constexpr float LightLumens(TNScoreShells::ETier Tier) { return Tier == TNScoreShells::ETier::Grand ? 2200.f : 1200.f; }
	constexpr float LightRadius(TNScoreShells::ETier Tier) { return Tier == TNScoreShells::ETier::Grand ? 750.f : 500.f; }

	/**
	 * Columna de luz translúcida (M_ProcFXSoft) que sube desde el suelo y se desvanece hacia arriba, para ver de lejos
	 * las grandes y las reinas: cono de 10 lados, con las dos caras, y el alfa del vértice de 0,32-0,42 abajo a 0 arriba.
	 */
	UStaticMesh* BeamMesh(TNScoreShells::ETier Tier)
	{
		static TWeakObjectPtr<UStaticMesh> Cached[TNScoreShells::NumTiers];
		const int32 Index = FMath::Clamp(static_cast<int32>(Tier), 0, TNScoreShells::NumTiers - 1);
		if (Cached[Index].IsValid()) { return Cached[Index].Get(); }
		UMaterialInterface* Mat = TNAmbientFX::MaterialFor(true);
		if (!Mat) { return nullptr; }
		const bool bGrand = Tier == TNScoreShells::ETier::Grand;
		const double Height = BeamHeight(Tier);
		const double RadiusBottom = bGrand ? 34.0 : 26.0;
		const double RadiusTop = bGrand ? 14.0 : 10.0;
		const float AlphaBottom = bGrand ? 0.42f : 0.32f;
		const FLinearColor Color = FMath::Lerp(TNScoreShells::GlowColor(Tier), FLinearColor::White, 0.3f);
		TNProcMesh::FTNProcMeshBuffers M;
		constexpr int32 Sides = 10;
		constexpr int32 Bands = 6;
		for (int32 r = 0; r < Bands; ++r)
		{
			const double Z0 = Height * r / Bands;
			const double Z1 = Height * (r + 1) / Bands;
			const double Ra = FMath::Lerp(RadiusBottom, RadiusTop, static_cast<double>(r) / Bands);
			const double Rb = FMath::Lerp(RadiusBottom, RadiusTop, static_cast<double>(r + 1) / Bands);
			for (int32 s = 0; s < Sides; ++s)
			{
				const double A0 = 2.0 * PI * s / Sides;
				const double A1 = 2.0 * PI * (s + 1) / Sides;
				const FVector D0(FMath::Cos(A0), FMath::Sin(A0), 0.0);
				const FVector D1(FMath::Cos(A1), FMath::Sin(A1), 0.0);
				const FVector P00 = D0 * Ra + FVector(0.0, 0.0, Z0);
				const FVector P10 = D1 * Ra + FVector(0.0, 0.0, Z0);
				const FVector P11 = D1 * Rb + FVector(0.0, 0.0, Z1);
				const FVector P01 = D0 * Rb + FVector(0.0, 0.0, Z1);
				const FVector Outward = (D0 + D1).GetSafeNormal();
				M.AddQuad(P00, P10, P11, P01, Outward, Color);
				M.AddQuad(P00, P10, P11, P01, -Outward, Color);
			}
		}
		for (int32 i = 0; i < M.Verts.Num(); ++i)
		{
			const double U = FMath::Clamp(M.Verts[i].Z / Height, 0.0, 1.0);
			M.Colors[i].A = AlphaBottom * static_cast<float>(FMath::Pow(1.0 - U, 1.3));
		}
		// Alfa de los propios buffers (FixedAlpha = -2): la opacidad del material suave.
		UStaticMesh* Mesh = TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), M, Mat, false, 0.f, 1.f, -2.f);
		if (Mesh) { Mesh->AddToRoot(); }
		Cached[Index] = Mesh;
		return Mesh;
	}

	/** Destellos alrededor de la concha (copos brillantes que suben despacio). */
	void AddSparkles(AActor* Owner, const FLinearColor& Color, int32 MaxParticles, float Rate, float Radius, float Height, float Size, float Wake)
	{
		TNAmbientFX::FEmitterDesc Sparkle;
		Sparkle.Shape = TNAmbientFX::EShape::Flake;
		Sparkle.bSoft = true;
		Sparkle.Color = Color;
		Sparkle.Alpha = 0.9f;
		Sparkle.MaxParticles = MaxParticles;
		Sparkle.Rate = Rate;
		Sparkle.SpawnRadius = Radius;
		Sparkle.SpawnHeight = Height;
		Sparkle.Speed = 25.f;
		Sparkle.Spread = 1.f;
		Sparkle.Gravity = 0.f;
		Sparkle.Buoyancy = 20.f;
		Sparkle.LifeMin = 0.5f;
		Sparkle.LifeMax = 0.9f;
		Sparkle.SizeStart = Size;
		Sparkle.SizeEnd = 4.f;
		Sparkle.WakeDistance = Wake;
		TNAmbientFX::AddEmitter(Owner, Sparkle, Owner->GetActorLocation() + FVector(0.f, 0.f, 5.f));
	}
}

ATN_ScorePickup::ATN_ScorePickup()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	// Quietas: basta con mirar su estado una vez por segundo (bActive y ScoreValue se fuerzan al cambiar). Hay cientos
	// en el mapa procedural y más de 200 en la playa.
	SetNetUpdateFrequency(1.f);
	// Relevantes a 200 m (antes, siempre y en todo el mapa: el anfitrión las miraba todas para cada cliente cada segundo) y
	// dormidas: su estado solo cambia al cogerlas, reaparecer o cambiar de valor, y cada cambio llama a ForceNetUpdate, que
	// las despierta y lo manda. La recogida es del servidor; el estallido va por el PlayerState.
	bAlwaysRelevant = false;
	SetNetCullDistanceSquared(FMath::Square(20000.f));
	NetDormancy = DORM_DormantAll;

	PickupMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupMesh"));
	SetRootComponent(PickupMesh);
	PickupMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PickupMesh->SetIsReplicated(false);

	CollectSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollectSphere"));
	CollectSphere->SetupAttachment(PickupMesh);
	CollectSphere->SetSphereRadius(60.f);
	CollectSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollectSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollectSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	ShellMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShellMesh"));
	ShellMesh->SetupAttachment(PickupMesh);
	ShellMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShellMesh->SetCastShadow(false);
	ShellMesh->SetIsReplicated(false);
}

void ATN_ScorePickup::BeginPlay()
{
	Super::BeginPlay();

	// Aspecto del tamaño (la concha que gira, salvo que arte haya puesto una malla propia en PickupMesh) y radio de
	// recogida. En los clientes ScoreValue ya ha llegado con el actor.
	ApplyTierLook();

	if (HasAuthority())
	{
		CollectSphere->OnComponentBeginOverlap.AddDynamic(this, &ATN_ScorePickup::OnSphereOverlap);
		// Quien ya estaba dentro al aparecer (TNShells, reaparición) no da evento de entrada: se mira en el siguiente
		// fotograma.
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (!bActive || !CollectSphere || IsActorBeingDestroyed()) { return; }
			TArray<AActor*> Inside;
			CollectSphere->GetOverlappingActors(Inside, APawn::StaticClass());
			for (AActor* Other : Inside)
			{
				if (!bActive || IsActorBeingDestroyed()) { break; }
				OnSphereOverlap(CollectSphere, Other, nullptr, 0, false, FHitResult());
			}
		}));
	}
	else if (CollectSphere)
	{
		// La recogida la decide el servidor: en los clientes la esfera sobra (menos consultas de física).
		CollectSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		// Sin pantalla no hay nada que animar.
		bAnimates = false;
		SetActorTickEnabled(false);
	}
	else if (UTN_ScorePickupWakeSubsystem* Wake = GetWorld() ? GetWorld()->GetSubsystem<UTN_ScorePickupWakeSubsystem>() : nullptr)
	{
		// Dormida hasta que la cámara se acerque: el subsistema mira las distancias de todas de una vez.
		Wake->Register(this);
		bWakeRegistered = true;
	}
	else
	{
		// Sin subsistema (un mundo que no es de juego): siempre despierta, como antes.
		SetAwake(true);
	}
}

void ATN_ScorePickup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bWakeRegistered)
	{
		if (UTN_ScorePickupWakeSubsystem* Wake = GetWorld() ? GetWorld()->GetSubsystem<UTN_ScorePickupWakeSubsystem>() : nullptr)
		{
			Wake->Unregister(this);
		}
		bWakeRegistered = false;
	}
	TNAmbientFX::RemoveOwner(this);
	Super::EndPlay(EndPlayReason);
}

void ATN_ScorePickup::SetAwake(bool bAwake)
{
	bNearView = bAwake;
	SetActorTickEnabled(bAwake && bAnimates);
	// El WidgetComponent del Blueprint (si lo hay) tampoco se redibuja de lejos.
	TInlineComponentArray<UWidgetComponent*> Widgets(this);
	for (UWidgetComponent* Widget : Widgets)
	{
		Widget->SetComponentTickEnabled(bAwake);
	}
}

void ATN_ScorePickup::ClearTierExtras()
{
	TNAmbientFX::RemoveOwner(this);
	if (HaloMesh) { HaloMesh->DestroyComponent(); HaloMesh = nullptr; }
	if (BeamMesh) { BeamMesh->DestroyComponent(); BeamMesh = nullptr; }
	if (GlowLight) { GlowLight->DestroyComponent(); GlowLight = nullptr; }
}

void ATN_ScorePickup::ApplyTierLook()
{
	const TNScoreShells::ETier ShellTier = GetShellTier();
	const uint8 TierIndex = static_cast<uint8>(ShellTier);
	if (CollectSphere) { CollectSphere->SetSphereRadius(TNScoreShells::CollectRadius(ShellTier)); }
	if (AppliedTier == TierIndex) { return; }
	AppliedTier = TierIndex;
	ClearTierExtras();

	// La concha de código salvo que arte haya puesto una malla propia en PickupMesh (la de ayuda del motor, el signo de
	// interrogación, no cuenta).
	const UStaticMesh* Current = PickupMesh ? PickupMesh->GetStaticMesh() : nullptr;
	const bool bPlaceholder = !Current || Current->GetPathName().StartsWith(TEXT("/Engine/"));
	UStaticMesh* Shell = bPlaceholder ? TNScorePickupDetail::ShellCoinMesh(ShellTier) : nullptr;
	if (!Shell || !ShellMesh)
	{
		bAnimates = false;
		SetActorTickEnabled(false);
		if (ShellMesh) { ShellMesh->SetVisibility(false); }
		return;
	}
	bAnimates = GetNetMode() != NM_DedicatedServer;
	SetActorTickEnabled(bNearView && bAnimates);
	const float Lift = TNScoreShells::MeshLift(ShellTier);
	PickupMesh->SetVisibility(false, false);
	ShellMesh->SetVisibility(true);
	ShellMesh->SetStaticMesh(Shell);
	ShellMesh->SetRelativeScale3D(FVector(TNScoreShells::MeshScale(ShellTier)));
	ShellMesh->SetRelativeLocation(FVector(0.f, 0.f, Lift));
	ShellMesh->SetCullDistance(TNScoreShells::CullDistance(ShellTier));
	SpinTime = FMath::FRand() * 10.f;
	if (GetNetMode() == NM_DedicatedServer) { return; }

	// Adornos: las pequeñas, solo el brillo del material; las normales, sus destellos dorados de siempre; las grandes y
	// las reinas, destellos de su color, halo, columna de luz y luz.
	const FLinearColor Glow = TNScoreShells::GlowColor(ShellTier);
	switch (ShellTier)
	{
		case TNScoreShells::ETier::Normal:
			TNScorePickupDetail::AddSparkles(this, FLinearColor(1.f, 0.92f, 0.55f), 14, 7.f, 60.f, 90.f, 14.f, 6000.f);
			break;
		case TNScoreShells::ETier::Big:
			TNScorePickupDetail::AddSparkles(this, Glow, 20, 10.f, 85.f, 140.f, 17.f, 25000.f);
			break;
		case TNScoreShells::ETier::Grand:
			TNScorePickupDetail::AddSparkles(this, Glow, 22, 11.f, 105.f, 170.f, 19.f, 25000.f);
			TNScorePickupDetail::AddSparkles(this, FLinearColor(1.f, 0.85f, 0.4f), 12, 6.f, 70.f, 150.f, 15.f, 25000.f);
			break;
		default:
			break;
	}
	if (ShellTier != TNScoreShells::ETier::Big && ShellTier != TNScoreShells::ETier::Grand) { return; }

	if (UStaticMesh* HaloShape = TNAmbientFX::ShapeMesh(TNAmbientFX::EShape::Puff, Glow, true, 0.2f, true))
	{
		HaloMesh = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		HaloMesh->SetupAttachment(PickupMesh);
		HaloMesh->SetStaticMesh(HaloShape);
		HaloMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		HaloMesh->SetCastShadow(false);
		HaloMesh->SetRelativeLocation(FVector(0.f, 0.f, Lift));
		HaloMesh->SetRelativeScale3D(FVector(TNScorePickupDetail::HaloScale(ShellTier)));
		HaloMesh->RegisterComponent();
	}
	if (UStaticMesh* BeamShape = TNScorePickupDetail::BeamMesh(ShellTier))
	{
		BeamMesh = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		BeamMesh->SetupAttachment(PickupMesh);
		BeamMesh->SetStaticMesh(BeamShape);
		BeamMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BeamMesh->SetCastShadow(false);
		// Desde el suelo (la concha flota a Hover sobre él).
		BeamMesh->SetRelativeLocation(FVector(0.f, 0.f, -static_cast<float>(TNScoreShells::Hover)));
		BeamMesh->RegisterComponent();
	}
	GlowLight = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
	GlowLight->SetupAttachment(PickupMesh);
	GlowLight->SetRelativeLocation(FVector(0.f, 0.f, Lift));
	GlowLight->SetIntensityUnits(ELightUnits::Lumens);
	GlowLight->SetIntensity(TNScorePickupDetail::LightLumens(ShellTier));
	GlowLight->SetAttenuationRadius(TNScorePickupDetail::LightRadius(ShellTier));
	GlowLight->SetLightColor(Glow);
	GlowLight->SetCastShadows(false);
	GlowLight->RegisterComponent();
}

void ATN_ScorePickup::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!ShellMesh || !ShellMesh->IsVisible()) { return; }
	const TNScoreShells::ETier ShellTier = GetShellTier();

	// Lejos de la cámara local el Tick está apagado (UTN_ScorePickupWakeSubsystem); esto es solo por si acaso.
	if (!bNearView) { return; }

	// Giro de moneda y balanceo suave; los destellos, al paso.
	SpinTime += DeltaTime;
	const float Turns = ShellTier == TNScoreShells::ETier::Normal ? SpinTurnsPerSecond : TNScoreShells::SpinTurns(ShellTier);
	const float Lift = TNScoreShells::MeshLift(ShellTier) + (ShellTier == TNScoreShells::ETier::Small ? 7.f : 12.f) * FMath::Sin(SpinTime * 3.f);
	ShellMesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, Lift), FRotator(0.f, FMath::Fmod(SpinTime * Turns * 360.f, 360.f), 0.f));
	// Halo que late y luz que respira (grandes y reinas).
	const float Breath = FMath::Sin(SpinTime * 2.2f);
	if (HaloMesh)
	{
		HaloMesh->SetRelativeLocationAndRotation(FVector(0.f, 0.f, Lift), FRotator(0.f, FMath::Fmod(SpinTime * 20.f, 360.f), 0.f));
		HaloMesh->SetRelativeScale3D(FVector(TNScorePickupDetail::HaloScale(ShellTier) * (1.f + 0.08f * Breath)));
	}
	if (GlowLight) { GlowLight->SetIntensity(TNScorePickupDetail::LightLumens(ShellTier) * (0.85f + 0.15f * Breath)); }
	TNAmbientFX::TickOwner(this, DeltaTime);
}

void ATN_ScorePickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_ScorePickup, bActive);
	DOREPLIFETIME(ATN_ScorePickup, ScoreValue);
}

void ATN_ScorePickup::SetScoreValue(int32 NewValue)
{
	const int32 Clamped = FMath::Max(1, NewValue);
	if (Clamped == ScoreValue) { return; }
	ScoreValue = Clamped;
	if (HasActorBegunPlay())
	{
		ApplyTierLook();
		// Ya replicada y dormida: despierta del todo para mandar el valor nuevo (antes de empezar va con la primera réplica).
		if (HasAuthority() && NetDormancy > DORM_Awake) { SetNetDormancy(DORM_Awake); }
	}
	ForceNetUpdate();
}

void ATN_ScorePickup::OnRep_ScoreValue()
{
	// Con el actor recién llegado lo aplica BeginPlay; aquí, si cambia después.
	if (HasActorBegunPlay()) { ApplyTierLook(); }
}

// ── Recogida ───────────────────────────────────────────────────────────────────

void ATN_ScorePickup::OnSphereOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority() || !bActive) { return; }

	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->GetController()) { return; }

	// Solo jugadores vivos
	ATN_CoopPlayerState* PS = Pawn->GetPlayerState<ATN_CoopPlayerState>();
	if (!PS || !PS->IsAliveAndPlaying()) { return; }

	// Sumar puntos (server-auth). AddRaceScore difunde OnRaceScoreChanged también en
	// el host del listen-server, cuyo OnRep no dispara → su HUD se refresca en vivo.
	PS->AddRaceScore(ScoreValue);
	// Para el término de conchas de la puntuación final del Coop (#789): solo cuenta, no suma más puntos.
	PS->CollectedShellPoints += ScoreValue;
	PS->ForceNetUpdate();
	// Estallido (destello, chispas y «¡plin!») en todas las máquinas y, en la del jugador, las conchas que vuelan a su
	// contador. Va por el PlayerState, que no se destruye: el multicast de esta concha se perdería con ella.
	const TNScoreShells::ETier ShellTier = GetShellTier();
	const FVector Where = ShellMesh && ShellMesh->IsVisible() ? ShellMesh->GetComponentLocation()
		: GetActorLocation() + FVector(0.f, 0.f, TNScoreShells::MeshLift(ShellTier));
	PS->MulticastScoreShellCollected(FVector_NetQuantize10(Where), static_cast<uint8>(ShellTier), ScoreValue);
	UE_LOG(LogTortunabo, Log, TEXT("[ScorePickup] %s recogió %d puntos → total %d"),
		*GetNameSafe(Pawn), ScoreValue, PS->RaceScore);

	// Desactivar
	bActive = false;
	ApplyActiveState(false);
	MulticastPickedUp(Pawn);

	if (bRespawn)
	{
		if (NetDormancy > DORM_Awake) { SetNetDormancy(DORM_Awake); }
		ForceNetUpdate();
		FTimerDelegate RespawnDelegate;
		RespawnDelegate.BindUObject(this, &ATN_ScorePickup::Respawn);
		GetWorldTimerManager().SetTimer(RespawnTimerHandle, RespawnDelegate, RespawnSeconds, false);
	}
	else
	{
		Destroy();
	}
}

void ATN_ScorePickup::MulticastPickedUp_Implementation(APawn* Collector)
{
	OnPickedUp(Collector);
}

void ATN_ScorePickup::Respawn()
{
	bActive = true;
	ApplyActiveState(true);
	// Dormida desde que se cogió: despierta del todo para mandar que vuelve (solo las que reaparecen, en el cooperativo).
	if (NetDormancy > DORM_Awake) { SetNetDormancy(DORM_Awake); }
	ForceNetUpdate();
}

void ATN_ScorePickup::ApplyActiveState(bool bNowActive)
{
	SetActorHiddenInGame(!bNowActive);
	SetActorEnableCollision(bNowActive);
}

void ATN_ScorePickup::OnRep_bActive()
{
	ApplyActiveState(bActive);
}
