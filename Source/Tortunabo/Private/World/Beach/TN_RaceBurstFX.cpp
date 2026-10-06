// Efectos puntuales locales de los objetos de carrera (onda del silbato, nubecilla de humo, estallido de estrellas y
// destellos dorados): cada máquina con pantalla los crea al recibir el aviso del servidor y se destruyen solos.

#include "World/Beach/TN_RaceBurstFX.h"
#include "TN_BeachEnemyKit.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto del
// bloque.
namespace TNRaceBurstFXDetail
{
	using FBurstBuffers = TNProcMesh::FTNProcMeshBuffers;

	// ── Onda del silbato ──────────────────────────────────────────────────────────────────────────────────────────────

	/** Radio final que se acepta (cm). */
	constexpr float WhistleMinRadius = 400.f;
	constexpr float WhistleMaxRadius = 20000.f;
	/**
	 * El primer anillo llega al radio final en WhistleExpandSeconds (sale disparado y frena); el segundo sale
	 * WhistleSecondDelay más tarde, tarda WhistleSecondSeconds y se queda en WhistleSecondReach del radio.
	 */
	constexpr float WhistleExpandSeconds = 0.8f;
	constexpr float WhistleSecondDelay = 0.1f;
	constexpr float WhistleSecondSeconds = 1.f;
	constexpr float WhistleSecondReach = 0.82f;
	/** Pasado este tiempo la onda se ha apagado del todo (el actor vive un poco más por si acaso). */
	constexpr float WhistleTotalSeconds = 1.7f;
	constexpr float WhistleLifeSeconds = 2.4f;
	/** Cuánto sube el plano de la onda sobre el suelo (cm) y cuánto quedan los pies bajo el centro de una tortuga. */
	constexpr float WhistleLiftCm = 30.f;
	constexpr float TurtleFeetBelowCm = 88.f;
	/** Opacidad máxima del primer anillo, del segundo y de la cúpula (los anillos van de 0 a 1 en el alfa del vértice). */
	constexpr float WhistleRingOpacity = 0.95f;
	constexpr float WhistleSecondOpacity = 0.6f;
	constexpr float WhistleDomeOpacity = 0.37f;

	// ── Los demás ─────────────────────────────────────────────────────────────────────────────────────────────────────

	/** Los efectos pequeños no se crean con la cámara local más lejos de esto (cm) y admiten estos factores de tamaño. */
	constexpr float SmallFxMaxDistance = 15000.f;
	constexpr float SmallFxMinScale = 0.25f;
	constexpr float SmallFxMaxScale = 4.f;
	/** Vida del actor de cada efecto pequeño (s) y tiempo mínimo antes de poder destruirse aunque no quede nada vivo. */
	constexpr float PoofLifeSeconds = 3.f;
	constexpr float PoofMinSeconds = 0.5f;
	constexpr float StarPopLifeSeconds = 2.6f;
	constexpr float StarPopFlashSeconds = 0.7f;
	constexpr float SparkleLifeSeconds = 2.2f;
	constexpr float SparkleFlashSeconds = 0.6f;
	/** La estrella sube esto por encima del sitio (cm, por el tamaño) y gira estos grados por segundo. */
	constexpr float StarLiftCm = 45.f;
	constexpr float StarSpinDegrees = 55.f;

	inline float FxEaseOut(float X)
	{
		const float Inverse = 1.f - FMath::Clamp(X, 0.f, 1.f);
		return 1.f - Inverse * Inverse * Inverse;
	}

	inline float FxSmooth(float X)
	{
		const float Clamped = FMath::Clamp(X, 0.f, 1.f);
		return Clamped * Clamped * (3.f - 2.f * Clamped);
	}

	/**
	 * M_ProcFXHard (translúcido sin luz como M_ProcFXSoft pero sin su fundido por profundidad de 80 cm, que dejaba a medio ver
	 * lo que roza el suelo; lo crea Scripts/create_poop_decal.py). Null si aún no existe: se usa el suave.
	 */
	inline UMaterialInterface* BurstHardMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (!Cached.IsValid())
		{
			Cached = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcFXHard.M_ProcFXHard"), nullptr, LOAD_NoWarn);
		}
		return Cached.Get();
	}

	/** Pone en Comp el material dinámico de un efecto (el duro si existe, el suave si no) y lo devuelve. */
	inline UMaterialInstanceDynamic* BurstSetupMid(UStaticMeshComponent* Comp)
	{
		if (!Comp)
		{
			return nullptr;
		}
		if (UMaterialInterface* Hard = BurstHardMaterial())
		{
			Comp->CreateDynamicMaterialInstance(0, Hard);
		}
		return TNBeachKit::SoftMID(Comp);
	}

	/** Pieza de malla suave del efecto, enganchada a la raíz del actor (sin sombra, sin colisión, translúcida por delante). */
	inline UStaticMeshComponent* BurstAddPart(AActor* Host, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Comp = TNBeachKit::AddPart(Host, Host ? Host->GetRootComponent() : nullptr, Mesh, FVector::ZeroVector, false);
		if (Comp)
		{
			Comp->SetTranslucentSortPriority(3);
			BurstSetupMid(Comp);
		}
		return Comp;
	}

	/**
	 * Cota del suelo bajo Where: una traza. Si no cuadra con una tortuga
	 * encima (plataforma, hueco sin cubrir), los pies de una tortuga que estuviera en Where.
	 */
	inline float WhistleGroundZ(UWorld* World, const FVector& Where)
	{
		const float FeetZ = static_cast<float>(Where.Z) - TurtleFeetBelowCm;
		float Found = FeetZ;
		bool bFound = false;
		if (World)
		{
			FHitResult Hit;
			FCollisionQueryParams Params(FName(TEXT("TNRaceBurstGround")), false);
			if (World->LineTraceSingleByChannel(Hit, Where + FVector(0.0, 0.0, 200.0), Where - FVector(0.0, 0.0, 1500.0), ECC_WorldStatic, Params))
			{
				Found = static_cast<float>(Hit.ImpactPoint.Z);
				bFound = true;
			}
		}
		if (!bFound || Found > static_cast<float>(Where.Z) + 30.f || Found < static_cast<float>(Where.Z) - 600.f)
		{
			return FeetZ;
		}
		return Found;
	}

	// ── Mallas ────────────────────────────────────────────────────────────────────────────────────────────────────────

	/** Cuadrilátero A-B-C-D con las dos caras visibles (Hint es una de las normales) y el alfa por vértice que da AlphaOf. */
	template <typename TAlphaFn>
	inline void BurstAddQuadBothSides(FBurstBuffers& M, const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Hint,
		const FLinearColor& Color, TAlphaFn AlphaOf)
	{
		TNBeachKit::AddTriAlpha(M, A, B, C, Hint, Color, AlphaOf);
		TNBeachKit::AddTriAlpha(M, A, C, D, Hint, Color, AlphaOf);
		TNBeachKit::AddTriAlpha(M, A, B, C, -Hint, Color, AlphaOf);
		TNBeachKit::AddTriAlpha(M, A, C, D, -Hint, Color, AlphaOf);
	}

	/**
	 * Anillo de la onda, de radio 100 en el plano XY: una pared vertical (Z de -100 a 100, opaca hacia el centro de la altura y
	 * transparente arriba y abajo, así se ve en cualquier ladera) y una corona plana fina en el suelo (94 a 100, dura por fuera
	 * y blanda por dentro). Se escala en cada fotograma: XY con el radio y Z con la altura de la pared.
	 */
	inline void BurstBuildWhistleRing(FBurstBuffers& M, const FLinearColor& WallColor, const FLinearColor& FlatColor)
	{
		constexpr int32 Segments = 48;
		const double Rows[3] = { -100.0, 15.0, 100.0 };
		const auto WallAlpha = [](const FVector& P)
		{
			return P.Z <= 15.0 ? static_cast<float>(FMath::Clamp((P.Z + 100.0) / 115.0, 0.0, 1.0)) : static_cast<float>(FMath::Clamp((100.0 - P.Z) / 85.0, 0.0, 1.0));
		};
		const auto FlatAlpha = [](const FVector& P)
		{
			return static_cast<float>(FMath::Clamp((P.Size2D() - 94.0) / 6.0, 0.0, 1.0));
		};
		for (int32 Slice = 0; Slice < Segments; ++Slice)
		{
			const double AngleA = TNProcMap::TwoPi * Slice / Segments;
			const double AngleB = TNProcMap::TwoPi * (Slice + 1) / Segments;
			const double AngleMid = 0.5 * (AngleA + AngleB);
			const FVector Outward(FMath::Cos(AngleMid), FMath::Sin(AngleMid), 0.0);
			for (int32 Row = 0; Row < 2; ++Row)
			{
				const FVector A(FMath::Cos(AngleA) * 100.0, FMath::Sin(AngleA) * 100.0, Rows[Row]);
				const FVector B(FMath::Cos(AngleB) * 100.0, FMath::Sin(AngleB) * 100.0, Rows[Row]);
				const FVector C(FMath::Cos(AngleB) * 100.0, FMath::Sin(AngleB) * 100.0, Rows[Row + 1]);
				const FVector D(FMath::Cos(AngleA) * 100.0, FMath::Sin(AngleA) * 100.0, Rows[Row + 1]);
				BurstAddQuadBothSides(M, A, B, C, D, Outward, WallColor, WallAlpha);
			}
			const FVector InnerA(FMath::Cos(AngleA) * 94.0, FMath::Sin(AngleA) * 94.0, 0.0);
			const FVector InnerB(FMath::Cos(AngleB) * 94.0, FMath::Sin(AngleB) * 94.0, 0.0);
			const FVector OuterA(FMath::Cos(AngleA) * 100.0, FMath::Sin(AngleA) * 100.0, 0.0);
			const FVector OuterB(FMath::Cos(AngleB) * 100.0, FMath::Sin(AngleB) * 100.0, 0.0);
			BurstAddQuadBothSides(M, InnerA, OuterA, OuterB, InnerB, FVector::UpVector, FlatColor, FlatAlpha);
		}
	}

	/** Anillo crema (primero, cálido) o pálido (segundo, más frío). */
	inline UStaticMesh* WhistleRingMesh(bool bSecond)
	{
		return TNBeachKit::CachedMesh(bSecond ? TEXT("Race.Burst.WhistleRingB") : TEXT("Race.Burst.WhistleRingA"), [bSecond](FBurstBuffers& M)
		{
			if (bSecond)
			{
				BurstBuildWhistleRing(M, FLinearColor(0.86f, 0.95f, 1.f), FLinearColor(0.92f, 0.98f, 1.f));
			}
			else
			{
				BurstBuildWhistleRing(M, FLinearColor(1.f, 0.93f, 0.72f), FLinearColor(1.f, 0.97f, 0.86f));
			}
		}, TNBeachKit::EBeachMeshMat::SoftVertexAlpha);
	}

	/**
	 * Cúpula baja de radio 100 y alto 100 (Z de 0 a 100), con las dos caras: casi transparente en la cima y más densa hacia el
	 * borde, así se ve como una cortina que avanza con el anillo.
	 */
	inline UStaticMesh* WhistleDomeMesh()
	{
		return TNBeachKit::CachedMesh(TEXT("Race.Burst.WhistleDome"), [](FBurstBuffers& M)
		{
			constexpr int32 Segments = 32;
			constexpr int32 Bands = 5;
			const double PolarDegrees[Bands + 1] = { 0.0, 30.0, 50.0, 66.0, 78.0, 90.0 };
			const FLinearColor Tint(1.f, 0.96f, 0.82f);
			const auto DomeAlpha = [](const FVector& P)
			{
				const double Reach = FMath::Clamp(P.Size2D() / 100.0, 0.0, 1.0);
				return static_cast<float>(0.04 + 0.96 * FMath::Pow(Reach, 2.4));
			};
			const auto DomePoint = [](double Azimuth, double PolarDeg)
			{
				const double Polar = FMath::DegreesToRadians(PolarDeg);
				return FVector(FMath::Cos(Azimuth) * FMath::Sin(Polar) * 100.0, FMath::Sin(Azimuth) * FMath::Sin(Polar) * 100.0, FMath::Cos(Polar) * 100.0);
			};
			for (int32 Slice = 0; Slice < Segments; ++Slice)
			{
				const double AzimuthA = TNProcMap::TwoPi * Slice / Segments;
				const double AzimuthB = TNProcMap::TwoPi * (Slice + 1) / Segments;
				const double AzimuthMid = 0.5 * (AzimuthA + AzimuthB);
				for (int32 Band = 0; Band < Bands; ++Band)
				{
					const FVector A = DomePoint(AzimuthA, PolarDegrees[Band]);
					const FVector B = DomePoint(AzimuthB, PolarDegrees[Band]);
					const FVector C = DomePoint(AzimuthB, PolarDegrees[Band + 1]);
					const FVector D = DomePoint(AzimuthA, PolarDegrees[Band + 1]);
					const FVector Outward(FMath::Cos(AzimuthMid), FMath::Sin(AzimuthMid), 0.6);
					BurstAddQuadBothSides(M, A, B, C, D, Outward, Tint, DomeAlpha);
				}
			}
		}, TNBeachKit::EBeachMeshMat::SoftVertexAlpha);
	}

	/**
	 * Estrella de cuatro puntas en el plano YZ mirando a +X (una pieza que se orienta hacia la cámara): un halo blando dorado
	 * detrás, la estrella grande (radio 100) y otra más pequeña y clara girada 45 grados por delante. Dos caras.
	 */
	inline UStaticMesh* StarFlashMesh()
	{
		return TNBeachKit::CachedMesh(TEXT("Race.Burst.Star4"), [](FBurstBuffers& M)
		{
			const FVector Toward(1.0, 0.0, 0.0);
			const auto StarPoint = [](double Angle, double Radius, double Depth)
			{
				return FVector(Depth, FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius);
			};
			const auto HaloAlpha = [](const FVector& P)
			{
				const double Reach = FMath::Sqrt(P.Y * P.Y + P.Z * P.Z);
				return static_cast<float>(0.55 * FMath::Clamp(1.0 - Reach / 85.0, 0.0, 1.0));
			};
			const auto StarAlpha = [](const FVector& P)
			{
				const double Reach = FMath::Sqrt(P.Y * P.Y + P.Z * P.Z);
				return static_cast<float>(FMath::Clamp(1.0 - 0.5 * Reach / 100.0, 0.35, 1.0));
			};

			const FLinearColor HaloColor(1.f, 0.82f, 0.32f);
			constexpr int32 HaloSegments = 24;
			const FVector HaloCenter = StarPoint(0.0, 0.0, -2.0);
			for (int32 Slice = 0; Slice < HaloSegments; ++Slice)
			{
				const double AngleA = UE_DOUBLE_TWO_PI * Slice / HaloSegments;
				const double AngleB = UE_DOUBLE_TWO_PI * (Slice + 1) / HaloSegments;
				const FVector RimA = StarPoint(AngleA, 85.0, -2.0);
				const FVector RimB = StarPoint(AngleB, 85.0, -2.0);
				TNBeachKit::AddTriAlpha(M, HaloCenter, RimA, RimB, Toward, HaloColor, HaloAlpha);
				TNBeachKit::AddTriAlpha(M, HaloCenter, RimA, RimB, -Toward, HaloColor, HaloAlpha);
			}

			const auto AddStar = [&M, &StarPoint, &StarAlpha, &Toward](double Outer, double Inner, double Depth, double Offset, const FLinearColor& Color)
			{
				const FVector Center = StarPoint(0.0, 0.0, Depth);
				for (int32 Vertex = 0; Vertex < 8; ++Vertex)
				{
					const double AngleA = Offset + UE_DOUBLE_PI * 0.25 * Vertex;
					const double AngleB = AngleA + UE_DOUBLE_PI * 0.25;
					const FVector PointA = StarPoint(AngleA, (Vertex & 1) ? Inner : Outer, Depth);
					const FVector PointB = StarPoint(AngleB, (Vertex & 1) ? Outer : Inner, Depth);
					TNBeachKit::AddTriAlpha(M, Center, PointA, PointB, Toward, Color, StarAlpha);
					TNBeachKit::AddTriAlpha(M, Center, PointA, PointB, -Toward, Color, StarAlpha);
				}
			};
			AddStar(100.0, 21.0, 0.0, 0.0, FLinearColor(1.f, 0.9f, 0.5f));
			AddStar(58.0, 15.0, 1.5, UE_DOUBLE_PI * 0.25, FLinearColor(1.f, 0.98f, 0.85f));
		}, TNBeachKit::EBeachMeshMat::SoftVertexAlpha);
	}

	/** Coloca un componente de la onda con su radio y la altura de su pared (cm) y su opacidad; con radio u opacidad 0, lo esconde. */
	inline void PlaceWave(UStaticMeshComponent* Comp, double RadiusCm, double HeightCm, float Opacity)
	{
		if (!Comp)
		{
			return;
		}
		const bool bShow = RadiusCm > 5.0 && Opacity > 0.01f;
		if (Comp->IsVisible() != bShow)
		{
			Comp->SetVisibility(bShow);
		}
		if (bShow)
		{
			Comp->SetRelativeScale3D(FVector(RadiusCm / 100.0, RadiusCm / 100.0, HeightCm / 100.0));
			TNBeachKit::SetOpacity(TNBeachKit::SoftMID(Comp), Opacity);
		}
	}

	/** Mitad de la altura de la pared del anillo (cm): baja y ancha de cerca, algo más alta a lo lejos para que se lea. */
	inline double WaveHalfHeight(double RadiusCm)
	{
		return FMath::Clamp(120.0 + 0.05 * RadiusCm, 120.0, 420.0);
	}

	/** Altura de la cúpula (cm). */
	inline double WaveDomeHeight(double RadiusCm)
	{
		return 150.0 + 0.1 * RadiusCm;
	}
}

ATN_RaceBurstFX::ATN_RaceBurstFX()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// Local a cada máquina: no se replica (el servidor avisa con un multicast y cada una crea el suyo).
	bReplicates = false;
	SetReplicateMovement(false);
	SetCanBeDamaged(false);
	SetActorEnableCollision(false);
	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SceneRoot->SetMobility(EComponentMobility::Movable);
	SetRootComponent(SceneRoot);
}

void ATN_RaceBurstFX::SpawnLocal(UWorld* World, ETNRaceBurst Kind, const FVector& Where, float Size)
{
	using namespace TNRaceBurstFXDetail;
	if (!World || !World->IsGameWorld() || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer || Kind >= ETNRaceBurst::Count)
	{
		return;
	}

	FVector Placement = Where;
	float Clamped = 1.f;
	if (Kind == ETNRaceBurst::WhistleWave)
	{
		// El plano de la onda, sobre el suelo bajo Where; Size es el radio final.
		Clamped = FMath::Clamp(Size, WhistleMinRadius, WhistleMaxRadius);
		Placement.Z = WhistleGroundZ(World, Where) + WhistleLiftCm;
	}
	else
	{
		Clamped = FMath::Clamp(Size, SmallFxMinScale, SmallFxMaxScale);
		// Un destello a 200 m no se ve: solo se crea cerca de la cámara local.
		FVector View = FVector::ZeroVector;
		if (TNBeachKit::LocalCamera(World, View) && FVector::DistSquared(View, Where) > FMath::Square(static_cast<double>(SmallFxMaxDistance)))
		{
			return;
		}
	}

	const FTransform PlacedAt(FRotator::ZeroRotator, Placement);
	ATN_RaceBurstFX* Fx = World->SpawnActorDeferred<ATN_RaceBurstFX>(ATN_RaceBurstFX::StaticClass(), PlacedAt, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Fx)
	{
		return;
	}
	Fx->BurstKind = Kind;
	Fx->BurstSize = Clamped;
	Fx->FinishSpawning(PlacedAt);
}

void ATN_RaceBurstFX::BeginPlay()
{
	using namespace TNRaceBurstFXDetail;
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer)
	{
		Destroy();
		return;
	}
	switch (BurstKind)
	{
		case ETNRaceBurst::WhistleWave:
			SetLifeSpan(WhistleLifeSeconds);
			BuildWhistle();
			break;
		case ETNRaceBurst::Poof:
			SetLifeSpan(PoofLifeSeconds);
			BuildPoof();
			break;
		case ETNRaceBurst::StarPop:
			SetLifeSpan(StarPopLifeSeconds);
			BuildStarPop();
			break;
		case ETNRaceBurst::Sparkle:
			SetLifeSpan(SparkleLifeSeconds);
			BuildSparkle();
			break;
		default:
			Destroy();
			return;
	}
	// Todo en su sitio desde el primer fotograma (la estrella mira a la cámara, la onda empieza escondida).
	FVector View = GetActorLocation();
	TNBeachKit::LocalCamera(GetWorld(), View);
	TickWhistle();
	TickStar(View);
}

// ─────────────────────────────────────────────────────────────────────────────
// Construcción
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RaceBurstFX::BuildWhistle()
{
	using namespace TNRaceBurstFXDetail;
	WaveRingA = BurstAddPart(this, WhistleRingMesh(false));
	WaveRingB = BurstAddPart(this, WhistleRingMesh(true));
	WaveDome = BurstAddPart(this, WhistleDomeMesh());
}

void ATN_RaceBurstFX::BuildPoof()
{
	const float Scale = BurstSize;
	const FVector Base = GetActorLocation();
	const uint32 Seed = static_cast<uint32>(GetTypeHash(Base)) | 1u;

	// Humo: bolas blancas-arena que se hinchan mientras se disipan.
	TNAmbientFX::FEmitterDesc SmokeDesc = TNBeachKit::MakeDesc(TNAmbientFX::EShape::Puff, FLinearColor(0.93f, 0.88f, 0.76f), true, 0.8f, 20, 0.f,
		170.f * Scale, -20.f, 0.55f, 0.95f, 50.f * Scale, 190.f * Scale);
	SmokeDesc.SpawnRadius = 25.f * Scale;
	SmokeDesc.SpawnHeight = 30.f * Scale;
	SmokeDesc.Spread = 1.7f;
	SmokeDesc.Drag = 1.8f;
	TNBeachKit::InitEmitter(FXMain, this, SmokeDesc, Seed);
	TNBeachKit::BurstAt(FXMain, Base, FVector::UpVector, 13);

	// Unas estrellitas que saltan de la nubecilla.
	TNAmbientFX::FEmitterDesc StarDesc = TNBeachKit::MakeDesc(TNAmbientFX::EShape::Ember, FLinearColor(1.f, 0.88f, 0.4f), true, 0.95f, 12, 0.f,
		340.f * Scale, -420.f, 0.5f, 0.85f, 28.f * Scale, 4.f * Scale);
	StarDesc.SpawnRadius = 15.f * Scale;
	StarDesc.SpawnHeight = 25.f * Scale;
	StarDesc.Spread = 1.5f;
	TNBeachKit::InitEmitter(FXExtra, this, StarDesc, Seed + 7u);
	TNBeachKit::BurstAt(FXExtra, Base + FVector(0.0, 0.0, 20.0 * Scale), FVector::UpVector, 8);
}

void ATN_RaceBurstFX::BuildStarPop()
{
	using namespace TNRaceBurstFXDetail;
	const float Scale = BurstSize;
	const FVector Base = GetActorLocation();
	const uint32 Seed = static_cast<uint32>(GetTypeHash(Base)) | 1u;

	// Chispas doradas en todas direcciones (más hacia arriba) que caen en arco.
	TNAmbientFX::FEmitterDesc GoldDesc = TNBeachKit::MakeDesc(TNAmbientFX::EShape::Ember, FLinearColor(1.f, 0.8f, 0.28f), true, 1.f, 36, 0.f,
		640.f * Scale, -560.f, 0.45f, 0.95f, 32.f * Scale, 3.f * Scale);
	GoldDesc.SpawnRadius = 14.f * Scale;
	GoldDesc.SpawnHeight = 20.f * Scale;
	GoldDesc.Spread = 2.4f;
	GoldDesc.Drag = 0.9f;
	TNBeachKit::InitEmitter(FXMain, this, GoldDesc, Seed);
	TNBeachKit::BurstAt(FXMain, Base + FVector(0.0, 0.0, StarLiftCm * 0.5 * Scale), FVector::UpVector, 30);

	// Chispas claras, más lentas y que suben menos.
	TNAmbientFX::FEmitterDesc CreamDesc = TNBeachKit::MakeDesc(TNAmbientFX::EShape::Ember, FLinearColor(1.f, 0.96f, 0.78f), true, 0.9f, 16, 0.f,
		300.f * Scale, -160.f, 0.6f, 1.1f, 22.f * Scale, 2.f * Scale);
	CreamDesc.SpawnRadius = 10.f * Scale;
	CreamDesc.SpawnHeight = 30.f * Scale;
	CreamDesc.Spread = 3.f;
	CreamDesc.Drag = 0.8f;
	TNBeachKit::InitEmitter(FXExtra, this, CreamDesc, Seed + 7u);
	TNBeachKit::BurstAt(FXExtra, Base, FVector::UpVector, 12);

	StarFlash = BurstAddPart(this, StarFlashMesh());
}

void ATN_RaceBurstFX::BuildSparkle()
{
	using namespace TNRaceBurstFXDetail;
	const float Scale = BurstSize;
	const FVector Base = GetActorLocation();
	const uint32 Seed = static_cast<uint32>(GetTypeHash(Base)) | 1u;

	// Destellos dorados cortos que suben y se apagan.
	TNAmbientFX::FEmitterDesc SparkDesc = TNBeachKit::MakeDesc(TNAmbientFX::EShape::Ember, FLinearColor(1.f, 0.84f, 0.34f), true, 1.f, 24, 0.f,
		170.f * Scale, 0.f, 0.35f, 0.75f, 24.f * Scale, 2.f * Scale);
	SparkDesc.SpawnRadius = 45.f * Scale;
	SparkDesc.SpawnHeight = 70.f * Scale;
	SparkDesc.Spread = 2.f;
	SparkDesc.Buoyancy = 70.f;
	SparkDesc.Drag = 1.2f;
	TNBeachKit::InitEmitter(FXMain, this, SparkDesc, Seed);
	TNBeachKit::BurstAt(FXMain, Base, FVector::UpVector, 14);

	StarFlash = BurstAddPart(this, StarFlashMesh());
}

// ─────────────────────────────────────────────────────────────────────────────
// Animación
// ─────────────────────────────────────────────────────────────────────────────

void ATN_RaceBurstFX::TickWhistle()
{
	using namespace TNRaceBurstFXDetail;
	if (BurstKind != ETNRaceBurst::WhistleWave)
	{
		return;
	}
	const double Reach = static_cast<double>(BurstSize);

	// Primer anillo: sale disparado y frena al llegar al radio; aparece de golpe y se apaga en la segunda mitad.
	const float ExpandA = FMath::Clamp(BurstAge / WhistleExpandSeconds, 0.f, 1.f);
	const double RadiusA = Reach * FxEaseOut(ExpandA);
	const float FadeInA = FMath::Clamp(BurstAge / 0.05f, 0.f, 1.f);
	const float FadeOutA = 1.f - FxSmooth((BurstAge - 0.45f) / 0.6f);
	PlaceWave(WaveRingA, RadiusA, WaveHalfHeight(RadiusA), WhistleRingOpacity * FadeInA * FadeOutA);

	// Segundo anillo: más lento, más frío y más bajo.
	const float ExpandB = FMath::Clamp((BurstAge - WhistleSecondDelay) / WhistleSecondSeconds, 0.f, 1.f);
	const double RadiusB = Reach * WhistleSecondReach * FxEaseOut(ExpandB);
	const float FadeInB = FMath::Clamp((BurstAge - WhistleSecondDelay) / 0.08f, 0.f, 1.f);
	const float FadeOutB = 1.f - FxSmooth((BurstAge - 0.6f) / 0.7f);
	PlaceWave(WaveRingB, RadiusB, WaveHalfHeight(RadiusB) * 0.8, WhistleSecondOpacity * FadeInB * FadeOutB);

	// Cúpula baja translúcida: sigue al primer anillo y se apaga antes.
	const float FadeInD = FMath::Clamp(BurstAge / 0.1f, 0.f, 1.f);
	const float FadeOutD = 1.f - FxSmooth((BurstAge - 0.25f) / 0.9f);
	PlaceWave(WaveDome, RadiusA, WaveDomeHeight(RadiusA), WhistleDomeOpacity * FadeInD * FadeOutD);
}

void ATN_RaceBurstFX::TickStar(const FVector& View)
{
	using namespace TNRaceBurstFXDetail;
	if (!StarFlash)
	{
		return;
	}
	const bool bPop = BurstKind == ETNRaceBurst::StarPop;
	const float Progress = BurstAge / (bPop ? StarPopFlashSeconds : SparkleFlashSeconds);
	if (Progress >= 1.f)
	{
		if (StarFlash->IsVisible())
		{
			StarFlash->SetVisibility(false);
		}
		return;
	}
	float Grow = 1.f;
	float Opacity = 1.f;
	if (bPop)
	{
		// Se abre de golpe hasta algo más que su tamaño y se apaga desde el primer cuarto.
		Grow = FMath::Lerp(0.25f, 1.2f, FxEaseOut(Progress * 2.4f));
		Opacity = 1.f - FxSmooth((Progress - 0.25f) / 0.7f);
	}
	else
	{
		// Titila: una estrella pequeña que sube y baja de brillo mientras se encoge.
		Grow = 0.55f * (1.f - 0.45f * Progress);
		Opacity = FMath::Sin(Progress * PI) * (0.75f + 0.25f * FMath::Sin(BurstAge * 42.f));
	}
	const FVector StarAt = GetActorLocation() + FVector(0.0, 0.0, static_cast<double>(StarLiftCm) * BurstSize);
	const FVector ToView = View - StarAt;
	const FQuat Face = ToView.IsNearlyZero() ? FQuat::Identity : FRotationMatrix::MakeFromX(ToView).ToQuat();
	const FQuat Spin(FVector::ForwardVector, FMath::DegreesToRadians(static_cast<double>(BurstAge * StarSpinDegrees)));
	const double StarScale = static_cast<double>(BurstSize * Grow);
	if (!StarFlash->IsVisible())
	{
		StarFlash->SetVisibility(true);
	}
	StarFlash->SetWorldTransform(FTransform(Face * Spin, StarAt, FVector(StarScale)));
	TNBeachKit::SetOpacity(TNBeachKit::SoftMID(StarFlash), FMath::Max(0.f, Opacity));
}

bool ATN_RaceBurstFX::AreEmittersIdle() const
{
	return !TNBeachKit::AnyAlive(FXMain) && !FXMain.bAwake && !TNBeachKit::AnyAlive(FXExtra) && !FXExtra.bAwake;
}

void ATN_RaceBurstFX::Tick(float DeltaSeconds)
{
	using namespace TNRaceBurstFXDetail;
	Super::Tick(DeltaSeconds);
	BurstAge += DeltaSeconds;

	FVector View = GetActorLocation();
	TNBeachKit::LocalCamera(GetWorld(), View);
	switch (BurstKind)
	{
		case ETNRaceBurst::WhistleWave:
			TickWhistle();
			break;
		case ETNRaceBurst::StarPop:
		case ETNRaceBurst::Sparkle:
			TickStar(View);
			break;
		default:
			break;
	}
	TNBeachKit::TickEmitterIfBusy(FXMain, DeltaSeconds, View);
	TNBeachKit::TickEmitterIfBusy(FXExtra, DeltaSeconds, View);

	// Se va solo cuando ha pasado lo suyo y no queda ninguna partícula viva (SetLifeSpan es la red de seguridad).
	float MinimumSeconds = PoofMinSeconds;
	if (BurstKind == ETNRaceBurst::WhistleWave)
	{
		MinimumSeconds = WhistleTotalSeconds;
	}
	else if (BurstKind == ETNRaceBurst::StarPop)
	{
		MinimumSeconds = StarPopFlashSeconds;
	}
	else if (BurstKind == ETNRaceBurst::Sparkle)
	{
		MinimumSeconds = SparkleFlashSeconds;
	}
	if (BurstAge >= MinimumSeconds && AreEmittersIdle())
	{
		Destroy();
	}
}
