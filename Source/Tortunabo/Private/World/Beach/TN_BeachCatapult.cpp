#include "World/Beach/TN_BeachCatapult.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachTrapSynthComponent.h"
#include "Lobby/Playground/TN_PlaygroundSynthComponent.h"
#include "Core/TN_Log.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_ShellBody.h"
#include "Player/TN_ShellComponent.h"
#include "Player/TortugaCharacter.h"
#include "ProceduralMeshComponent.h"
#include "World/Beach/TN_BeachCatapultAim.h"
#include "World/Beach/TN_BeachStun.h"
#include "TN_BeachBoostKit.h"
#include "TN_BeachRideKit.h"
#include "TN_BeachSignKit.h"
#include "TN_BeachTrapKit.h"

/**
 * Geometría de la catapulta. El brazo (la cuchara con el cubito) se construye en el espacio de su eje: origen sobre el
 * tapón, X hacia el cubito, la cara de arriba del mango en Z = +grosor/2. El cazo queda en el extremo -X y va en su propia
 * malla, colgada de la bisagra del cuello (CrackX): así, al partirse, cae colgando. La piedra, el tapón, el palo que
 * sujeta y los banderines van en el espacio del marco (X hacia el mar, origen en la arena).
 */
namespace TNBeachCatapultDetail
{
	using FBuffers = TNBeachTrapKit::FBuffers;
	using FHulls = TNBeachTrapKit::FHulls;

	constexpr double HandleWidth = 90.0;
	constexpr double SwingSeconds = 0.16;
	constexpr double BounceEnd = 0.6;
	constexpr double HoldEnd = 1.1;
	constexpr double SettleSeconds = 0.3;
	constexpr double RatchetStep = 0.2;
	/** Colisión del brazo apagada durante el golpe (las bolas salen sin tocarlo). */
	constexpr double NoCollisionSeconds = 0.55;
	/**
	 * Temblor visual del aviso (grados sobre el eje; base + ganancia * aviso, con el aviso de 0 a 1). Es solo de las mallas, así
	 * que su desfase con la colisión se queda en unos centímetros a 5,5 m del eje: cabeceo hasta 0,65° (~6 cm en el cazo),
	 * balanceo hasta 2,5° (~5 cm en el borde del cazo) y guiñada hasta 0,5° (~5 cm de lado).
	 */
	constexpr double ShakePitchBase = 0.2;
	constexpr double ShakePitchGain = 0.45;
	constexpr double ShakeRollBase = 0.5;
	constexpr double ShakeRollGain = 2.0;
	constexpr double ShakeYawBase = 0.15;
	constexpr double ShakeYawGain = 0.35;
	/**
	 * Bola de caparazón en el cazo: por encima de esta velocidad (cm/s) no cuenta (una que pasa rodando no arma la catapulta;
	 * la que se acaba de meter aún se asienta). Margen del suelo del cazo (cm) para darla por dentro y por encima.
	 */
	constexpr double BowlBallMaxSpeed = 220.0;
	constexpr double BowlBallFloorBelow = 40.0;
	constexpr double BowlBallFloorAbove = 110.0;
	/** Giro con el que sale la bola lanzada, como el de UTN_ShellComponent::StartBody (rad/s, volteretas). */
	constexpr double BowlBallSpin = 7.0;
	/** Reserva Launch del árbitro mientras vuela: lo que tarda la caja en salir sola del caparazón como mucho (9 s) y un poco más. */
	constexpr double FlightClaimSeconds = 10.0;
	/** Un solo uso: al acabar el rebote el brazo se parte por el cuello del cazo. */
	constexpr double BreakSeconds = 0.62;
	/** Un solo uso: desde el disparo hasta que ya no se mueve nada (se apaga el Tick). */
	constexpr double SettledSeconds = 4.0;
	/**
	 * Puntería al final del arco de conchitas (SetLaunchTarget): solo si queda a menos de estos grados del morro de la cuchara
	 * (si no, el arco no es de su vuelo), y con la rapidez entre estas fracciones de LaunchSpeed.
	 */
	constexpr double AimMaxYawDeg = 35.0;
	constexpr double AimMinSpeedFraction = 0.6;
	constexpr double AimMaxSpeedFraction = 1.8;

	struct FCatapultArm
	{
		double Long = 750.0;
		double Short = 350.0;
		double Thick = 30.0;
		double BowlL = 300.0;
		double BowlHW = 110.0;
		double BowlDepth = 30.0;
		double BucketR = 95.0;
		double BucketH = 95.0;
		int32 Style = 0;
	};

	/** Cuello del cazo (X del eje) por donde se parte el brazo: pasado el cuello ensanchado. */
	double CrackXOf(const FCatapultArm& A)
	{
		return -A.Long + A.BowlL + 100.0;
	}

	/** Mueve en X lo construido (del espacio del eje al de la bisagra). */
	void ShiftX(FBuffers& B, double Dx)
	{
		for (FVector& V : B.Verts)
		{
			V.X += Dx;
		}
	}

	void ShiftX(FHulls& Hulls, double Dx)
	{
		for (TArray<FVector>& Hull : Hulls)
		{
			for (FVector& Pt : Hull)
			{
				Pt.X += Dx;
			}
		}
	}

	/** Contorno de un tramo de mango de largo Length y ancho Width, recto en -X (el corte) y redondeado en +X. */
	TArray<FVector2D> HalfStadium(double Length, double Width, int32 ArcSteps = 5)
	{
		TArray<FVector2D> Outline;
		const double Rad = 0.5 * Width;
		const double Straight = FMath::Max(0.0, 0.5 * Length - Rad);
		Outline.Add(FVector2D(-0.5 * Length, -Rad));
		for (int32 i = 0; i <= ArcSteps; ++i)
		{
			const double Ang = -0.5 * TNPlaygroundKit::KitPi + TNPlaygroundKit::KitPi * i / ArcSteps;
			Outline.Add(FVector2D(Straight + Rad * FMath::Cos(Ang), Rad * FMath::Sin(Ang)));
		}
		Outline.Add(FVector2D(-0.5 * Length, Rad));
		return Outline;
	}

	/** Contorno rectangular de Length x Width, centrado. */
	TArray<FVector2D> RectOutline(double Length, double Width)
	{
		return { FVector2D(-0.5 * Length, -0.5 * Width), FVector2D(0.5 * Length, -0.5 * Width), FVector2D(0.5 * Length, 0.5 * Width), FVector2D(-0.5 * Length, 0.5 * Width) };
	}

	/** Cazo: plato elíptico (borde a ras de la cara de arriba del mango) con su cara de abajo. */
	void AddBowl(FBuffers& B, const FCatapultArm& A, const FLinearColor& Col)
	{
		constexpr int32 Seg = 24;
		static const double Rings[5] = { 1.0, 0.85, 0.62, 0.34, 0.0 };
		const double Cx = -A.Long + 0.5 * A.BowlL;
		const double Top = 0.5 * A.Thick;
		const FLinearColor Inside = TNPlaygroundKit::Mix(Col, TNPlaygroundKit::Rgb(0xFFFFFF, Col.A), 0.18);
		TArray<FVector> In;
		TArray<FVector> Out;
		In.SetNum(5 * Seg);
		Out.SetNum(5 * Seg);
		for (int32 r = 0; r < 5; ++r)
		{
			for (int32 k = 0; k < Seg; ++k)
			{
				const double Phi = TNPlaygroundKit::KitTwoPi * k / Seg;
				const double X = Cx + 0.5 * A.BowlL * Rings[r] * FMath::Cos(Phi);
				const double Y = A.BowlHW * Rings[r] * FMath::Sin(Phi);
				const double Z = Top - A.BowlDepth * (1.0 - Rings[r] * Rings[r]);
				In[r * Seg + k] = FVector(X, Y, Z);
				Out[r * Seg + k] = FVector(X * 1.0, Y * 1.03, Z - 10.0);
			}
		}
		for (int32 r = 0; r < 4; ++r)
		{
			for (int32 k = 0; k < Seg; ++k)
			{
				const int32 K1 = (k + 1) % Seg;
				B.AddQuad(In[r * Seg + k], In[r * Seg + K1], In[(r + 1) * Seg + K1], In[(r + 1) * Seg + k], FVector::UpVector, (r % 2) ? Inside : TNPlaygroundKit::Shade(Inside, 0.96));
				B.AddQuad(Out[r * Seg + k], Out[r * Seg + K1], Out[(r + 1) * Seg + K1], Out[(r + 1) * Seg + k], -FVector::UpVector, TNPlaygroundKit::Shade(Col, 0.85));
			}
		}
		for (int32 k = 0; k < Seg; ++k)
		{
			const int32 K1 = (k + 1) % Seg;
			const double Am = TNPlaygroundKit::KitTwoPi * (k + 0.5) / Seg;
			B.AddQuad(In[k], In[K1], Out[K1], Out[k], FVector(FMath::Cos(Am), FMath::Sin(Am), 0.0), Col);
		}
	}

	/** Cubito de arena mojada atado en el extremo del mango, con su asa y una estrella (azul marino y dorada si bNavy). */
	void AddBucket(FBuffers& B, const FCatapultArm& A, uint32 Seed, bool bNavy)
	{
		const FLinearColor Toy = bNavy ? TNBeachBoostKit::Navy() : TNPlaygroundKit::ToyColor(static_cast<int32>(Seed % 7u), 0.25f);
		const double Bx = A.Short - 0.9 * A.BucketR;
		const double Base = 0.5 * A.Thick;
		const double TopZ = Base + A.BucketH;
		TNPlaygroundKit::AddFrustum(B, FVector(Bx, 0.0, Base), FVector(Bx, 0.0, TopZ), 0.82 * A.BucketR, A.BucketR, 18, Toy, TNPlaygroundKit::Shade(Toy, 0.85), true, false);
		// Reborde, arena mojada con marcas de dedos y el asa caída.
		const FLinearColor Rim = bNavy ? TNBeachBoostKit::Gold() : TNPlaygroundKit::Shade(Toy, 1.1);
		TNPlaygroundKit::AddFrustum(B, FVector(Bx, 0.0, TopZ - 10.0), FVector(Bx, 0.0, TopZ + 2.0), A.BucketR + 6.0, A.BucketR + 6.0, 18, Rim, Rim, true, true);
		TNPlaygroundKit::AddDisc(B, FVector(Bx, 0.0, TopZ - 6.0), FVector::UpVector, A.BucketR - 4.0, 18, TNBeachTrapKit::SandWet());
		for (int32 i = 0; i < 3; ++i)
		{
			const double Ang = TNPlaygroundKit::KitTwoPi * (i + 0.3) / 3.0;
			TNPlaygroundKit::AddEllipsoid(B, FVector(Bx + 35.0 * FMath::Cos(Ang), 35.0 * FMath::Sin(Ang), TopZ - 5.0), FVector::ForwardVector, FVector::RightVector,
				FVector::UpVector, FVector(12.0, 9.0, 3.0), 8, 3, TNBeachTrapKit::SandDeep());
		}
		TArray<FVector> Handle;
		for (int32 i = 0; i <= 8; ++i)
		{
			const double T = TNPlaygroundKit::KitPi * i / 8.0;
			Handle.Add(FVector(Bx, (A.BucketR + 8.0) * FMath::Cos(T), TopZ - 12.0 + 34.0 * FMath::Sin(T)));
		}
		TNPlaygroundKit::AddTube(B, Handle, { 5.0 }, 6, { bNavy ? TNBeachBoostKit::GoldDeep() : TNPlaygroundKit::Shade(Toy, 0.8) }, FVector::ForwardVector, false);
		if (bNavy)
		{
			TNBeachBoostKit::AddStar(B, FVector(Bx - 0.91 * A.BucketR - 2.0, 0.0, Base + 0.55 * A.BucketH), FVector(-1.0, 0.0, 0.1), FVector::UpVector, 30.0,
				TNBeachBoostKit::Gold());
		}
		else
		{
			TNPlaygroundKit::AddStarfish(B, FVector(Bx - 0.91 * A.BucketR, 0.0, Base + 0.55 * A.BucketH), FVector(-1.0, 0.0, 0.1), FVector::UpVector, 26.0, 3.0,
				TNPlaygroundKit::Rgb(0xFFF1A8, 0.3f));
		}
	}

	/**
	 * Cuchara: de plástico (0), de madera con vetas (1) o dos palos de polo atados con un vasito de yogur (2); dorada si
	 * bGold. Arm recibe el mango del corte al cubito; Bowl, el cazo con el cuello hasta el corte (las dos en el espacio del
	 * eje: el cazo se pasa luego al de la bisagra).
	 */
	void BuildArm(FBuffers& Arm, FBuffers& Bowl, const FCatapultArm& A, uint32 Seed, double CrackX, bool bGold)
	{
		const double Top = 0.5 * A.Thick;
		const double HandleX0 = -A.Long + 0.85 * A.BowlL;
		const double HandleX1 = A.Short;
		if (A.Style == 2)
		{
			const FLinearColor Wood = bGold ? TNPlaygroundKit::Rgb(0xFFD76A, 0.55f) : TNPlaygroundKit::Rgb(0xE2C08C, 0.05f);
			for (const double Side : { -1.0, 1.0 })
			{
				const FLinearColor Stick = TNPlaygroundKit::Shade(Wood, Side < 0.0 ? 1.0 : 0.94);
				const double ArmLen = HandleX1 + 20.0 - CrackX;
				const double BowlLen = CrackX - (HandleX0 - 20.0);
				TNPlaygroundKit::AddStick(Arm, FVector(CrackX + 0.5 * ArmLen, Side * 23.0, 0.0), FVector::ForwardVector, FVector::RightVector, ArmLen, 46.0, A.Thick, Stick);
				TNPlaygroundKit::AddStick(Bowl, FVector(CrackX - 0.5 * BowlLen, Side * 23.0, 0.0), FVector::ForwardVector, FVector::RightVector, BowlLen, 46.0, A.Thick, Stick);
			}
			// Gomas elásticas que atan los palos.
			const FLinearColor Band = TNPlaygroundKit::Rgb(0xFF5FA2, 0.2f);
			for (const double Bx : { HandleX0 + 60.0, -40.0, 0.5 * HandleX1 })
			{
				FBuffers& Into = Bx < CrackX ? Bowl : Arm;
				TNPlaygroundKit::AddAxisBox(Into, FVector(Bx, 0.0, Top + 2.0), FVector(7.0, 50.0, 2.5), Band);
				TNPlaygroundKit::AddAxisBox(Into, FVector(Bx, 0.0, -Top - 2.0), FVector(7.0, 50.0, 2.5), Band);
				for (const double Side : { -1.0, 1.0 })
				{
					TNPlaygroundKit::AddAxisBox(Into, FVector(Bx, Side * 49.0, 0.0), FVector(7.0, 2.5, Top + 3.0), Band);
				}
			}
			// Vasito de yogur por cazo (blanco con etiqueta de color; dorado en la potenciada).
			AddBowl(Bowl, A, bGold ? TNBeachBoostKit::Gold() : TNPlaygroundKit::Rgb(0xF7F3EA, 0.3f));
			const FLinearColor Label = bGold ? TNBeachBoostKit::Navy() : TNPlaygroundKit::ToyColor(static_cast<int32>((Seed >> 4) % 7u), 0.2f);
			const double Cx = -A.Long + 0.5 * A.BowlL;
			TNPlaygroundKit::AddFrustum(Bowl, FVector(Cx, 0.0, Top - A.BowlDepth - 12.0), FVector(Cx, 0.0, Top - 4.0), 0.72 * A.BowlHW, 0.98 * A.BowlHW, 20, Label,
				Label, false, false);
		}
		else
		{
			const FLinearColor Col = bGold ? TNBeachBoostKit::Gold()
				: (A.Style == 1 ? TNPlaygroundKit::Rgb(0xD9B07A, 0.05f) : TNPlaygroundKit::ToyColor(static_cast<int32>((Seed >> 2) % 7u), 0.35f));
			// Mango del corte al extremo (redondeado) y el trozo del cuello al corte.
			const double ArmLen = HandleX1 + 15.0 - CrackX;
			TNPlaygroundKit::AddSlab(Arm, FVector(CrackX + 0.5 * ArmLen, 0.0, 0.0), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, HalfStadium(ArmLen, HandleWidth),
				A.Thick, Col);
			const double StubLen = CrackX - (HandleX0 - 15.0);
			TNPlaygroundKit::AddSlab(Bowl, FVector(CrackX - 0.5 * StubLen, 0.0, 0.0), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, RectOutline(StubLen, HandleWidth),
				A.Thick, Col);
			// Cuello ensanchado hacia el cazo.
			const TArray<FVector2D> Neck = { FVector2D(HandleX0 - 40.0, -0.8 * A.BowlHW), FVector2D(HandleX0 + 120.0, -0.5 * HandleWidth),
				FVector2D(HandleX0 + 120.0, 0.5 * HandleWidth), FVector2D(HandleX0 - 40.0, 0.8 * A.BowlHW) };
			TNPlaygroundKit::AddSlab(Bowl, FVector(0.0, 0.0, 0.0), FVector::ForwardVector, FVector::RightVector, FVector::UpVector, Neck, A.Thick * 0.9, Col);
			if (A.Style == 1 && !bGold)
			{
				// Vetas de la madera (en el mango).
				for (int32 i = 0; i < 4; ++i)
				{
					const double Y = -30.0 + 20.0 * i;
					TNPlaygroundKit::AddAxisBox(Arm, FVector(0.5 * (CrackX + HandleX1) + 30.0 * TNBeachTrapKit::Hash01(i, 1, Seed), Y, Top + 0.6),
						FVector(0.32 * (HandleX1 - CrackX), 2.0, 0.5), TNPlaygroundKit::Shade(Col, 0.8));
				}
			}
			AddBowl(Bowl, A, Col);
		}
		AddBucket(Arm, A, Seed, bGold);
	}

	/** Cascos del brazo (espacio del eje): el cazo con su cuello hasta el corte en Bowl; el mango y el cubito en Arm. */
	void BuildArmHulls(FHulls& Arm, FHulls& Bowl, const FCatapultArm& A, double CrackX)
	{
		const double Top = 0.5 * A.Thick;
		const double Floor = Top - A.BowlDepth;
		const double X0 = -A.Long;
		const double X1 = -A.Long + A.BowlL;
		Bowl.Add(TNPlaygroundKit::HullAxisBox(FVector(0.5 * (X0 + X1), 0.0, Floor - 7.0), FVector(0.5 * (X1 - X0) - 12.0, A.BowlHW - 12.0, 7.0)));
		Bowl.Add(TNPlaygroundKit::HullAxisBox(FVector(X0 + 6.0, 0.0, 0.5 * (Floor - 14.0 + Top)), FVector(12.0, A.BowlHW, 0.5 * (Top - Floor + 14.0))));
		for (const double Side : { -1.0, 1.0 })
		{
			Bowl.Add(TNPlaygroundKit::HullAxisBox(FVector(0.5 * (X0 + X1), Side * (A.BowlHW - 9.0), 0.5 * (Floor - 14.0 + Top)),
				FVector(0.5 * (X1 - X0), 9.0, 0.5 * (Top - Floor + 14.0))));
		}
		Bowl.Add(TNPlaygroundKit::HullAxisBox(FVector(0.5 * (X1 - 20.0 + CrackX), 0.0, 0.0), FVector(0.5 * (CrackX - X1 + 20.0), 0.5 * HandleWidth, Top)));
		Arm.Add(TNPlaygroundKit::HullAxisBox(FVector(0.5 * (CrackX + A.Short), 0.0, 0.0), FVector(0.5 * (A.Short - CrackX), 0.5 * HandleWidth, Top)));
		Arm.Add(TNPlaygroundKit::HullCylinder(FVector(A.Short - 0.9 * A.BucketR, 0.0, Top), A.BucketH, 0.82 * A.BucketR, A.BucketR, 12));
	}

	/** Astillas que asoman del corte del mango hacia el cazo (espacio del eje), del color del material roto. */
	void BuildSplinters(FBuffers& B, const FCatapultArm& A, double CrackX, uint32 Seed, const FLinearColor& Col)
	{
		const double Across = A.Style == 2 ? 92.0 : HandleWidth - 10.0;
		for (int32 i = 0; i < 10; ++i)
		{
			const double Len = 22.0 + 42.0 * TNBeachTrapKit::Hash01(i, 1, Seed);
			const double Y = (TNBeachTrapKit::Hash01(i, 2, Seed) - 0.5) * Across;
			const double Z = (TNBeachTrapKit::Hash01(i, 3, Seed) - 0.5) * A.Thick * 0.8;
			const double Yaw = 180.0 + (TNBeachTrapKit::Hash01(i, 4, Seed) - 0.5) * 50.0;
			const double Tilt = (TNBeachTrapKit::Hash01(i, 5, Seed) - 0.5) * 40.0;
			const FTransform Xf(FRotator(Tilt, Yaw, 0.0), FVector(CrackX + 4.0, Y, Z));
			TNPlaygroundKit::AddXfBox(B, Xf, FVector(0.5 * Len, 0.0, 0.0), FVector(0.5 * Len, 2.0 + 2.5 * TNBeachTrapKit::Hash01(i, 6, Seed), 1.6),
				(i % 3 == 0) ? TNPlaygroundKit::Shade(Col, 0.8) : Col);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachCatapult
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachCatapult::ATN_BeachCatapult()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(3.f);

	Frame = CreateDefaultSubobject<USceneComponent>(TEXT("Frame"));
	Frame->SetupAttachment(GetRootComponent());

	BaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
	BaseMesh->SetupAttachment(Frame);
	TNBeachTrapKit::ConfigureVisual(BaseMesh);

	BaseCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BaseCollision"));
	BaseCollision->SetupAttachment(Frame);
	TNBeachTrapKit::ConfigureSolid(BaseCollision, false);

	ArmPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ArmPivot"));
	ArmPivot->SetupAttachment(Frame);

	ArmMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ArmMesh"));
	ArmMesh->SetupAttachment(ArmPivot);
	TNBeachTrapKit::ConfigureVisual(ArmMesh);
	// Se mueve por sí sola con el temblor del aviso (ApplyVisualShake): un componente estático no puede.
	ArmMesh->SetMobility(EComponentMobility::Movable);

	ArmCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ArmCollision"));
	ArmCollision->SetupAttachment(ArmPivot);
	TNBeachTrapKit::ConfigureSolid(ArmCollision, false);

	BowlHinge = CreateDefaultSubobject<USceneComponent>(TEXT("BowlHinge"));
	BowlHinge->SetupAttachment(ArmPivot);

	BowlMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BowlMesh"));
	BowlMesh->SetupAttachment(BowlHinge);
	TNBeachTrapKit::ConfigureVisual(BowlMesh);
	BowlMesh->SetMobility(EComponentMobility::Movable);

	BowlCollision = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BowlCollision"));
	BowlCollision->SetupAttachment(BowlHinge);
	TNBeachTrapKit::ConfigureSolid(BowlCollision, false);

	SplinterMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SplinterMesh"));
	SplinterMesh->SetupAttachment(ArmPivot);
	TNBeachTrapKit::ConfigureVisual(SplinterMesh);
	SplinterMesh->SetVisibility(false);

	PropPivot = CreateDefaultSubobject<USceneComponent>(TEXT("PropPivot"));
	PropPivot->SetupAttachment(Frame);
	PropMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PropMesh"));
	PropMesh->SetupAttachment(PropPivot);
	TNBeachTrapKit::ConfigureVisual(PropMesh);

	FlagGreen = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlagGreen"));
	FlagGreen->SetupAttachment(Frame);
	TNBeachTrapKit::ConfigureVisual(FlagGreen);
	FlagRed = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlagRed"));
	FlagRed->SetupAttachment(Frame);
	TNBeachTrapKit::ConfigureVisual(FlagRed);
	FlagRed->SetVisibility(false);

	// Cartel de madera (sin colisión): tabla con el icono, cinta de «rota» y rótulo.
	SignPivot = CreateDefaultSubobject<USceneComponent>(TEXT("SignPivot"));
	SignPivot->SetupAttachment(Frame);
	SignMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SignMesh"));
	SignMesh->SetupAttachment(SignPivot);
	TNBeachTrapKit::ConfigureVisual(SignMesh);
	SignCross = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SignCross"));
	SignCross->SetupAttachment(SignPivot);
	TNBeachTrapKit::ConfigureVisual(SignCross);
	SignCross->SetVisibility(false);
	SignText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("SignText"));
	SignText->SetupAttachment(SignPivot);
	TNBeachSignKit::ConfigureText(SignText);
}

void ATN_BeachCatapult::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachCatapult, ArmedAt);
	DOREPLIFETIME(ATN_BeachCatapult, FiredAt);
}

void ATN_BeachCatapult::ApplySpec()
{
	using namespace TNBeachCatapultDetail;
	const double Fit = TNBeachTrapKit::FitRadius(Spec.Element, Spec.SizeScale);
	const uint32 Seed = TNBeachTrapKit::SeedOf(Spec.Seed, 83u);
	bBoosted = (Spec.Flags & TNBeach::FlagBoosted) != 0;

	// Brazo: el cazo y el cubito tienen medidas de tortuga (no bajan de lo que cabe una); el resto escala con la huella.
	FCatapultArm A;
	A.Style = static_cast<int32>(Seed % 3u);
	const double Length = FMath::Clamp(1.22 * Fit, 820.0, 1150.0);
	A.Long = 0.68 * Length;
	A.Short = 0.32 * Length;
	A.Thick = 30.0;
	A.BowlL = FMath::Clamp(0.27 * Length, 250.0, 320.0);
	A.BowlHW = FMath::Clamp(0.1 * Length, 100.0, 120.0);
	A.BowlDepth = 30.0;
	// Cubito bajo y ancho: desde el mango se llega de un salto a su arena (~1,1 m por encima).
	A.BucketR = FMath::Clamp(0.08 * Length, 75.0, 92.0);
	A.BucketH = 70.0;
	LongArm = A.Long;
	ShortArm = A.Short;
	ArmThick = A.Thick;
	BowlLength = A.BowlL;
	BowlHalfWidth = A.BowlHW;
	BowlDepth = A.BowlDepth;
	BucketRadius = A.BucketR;
	BucketHeight = A.BucketH;
	CrackX = CrackXOf(A);

	// Fulcro: piedra y tapón de garrafa; el brazo se apoya encima.
	const double StoneH = FMath::Clamp(0.16 * Fit, 110.0, 145.0);
	const double CapH = 90.0;
	const double CapR = 78.0;
	PivotZ = StoneH + CapH + 0.5 * A.Thick;
	const double PivotX = FMath::Clamp(0.2 * Fit, 110.0, 190.0);

	// Reposo: el fondo del cazo toca la arena. Disparada: la cara de abajo del extremo del mango da en la arena.
	const double BowlCx = -(A.Long - 0.5 * A.BowlL);
	const double BowlBottom = 0.5 * A.Thick - A.BowlDepth - 10.0;
	double Theta = 0.2;
	for (int32 i = 0; i < 4; ++i)
	{
		Theta = FMath::Asin(FMath::Clamp((PivotZ + BowlBottom * FMath::Cos(Theta) - 2.0) / -BowlCx, 0.0, 0.9));
	}
	RestDeg = FMath::RadiansToDegrees(Theta);
	FiredDeg = -FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp((PivotZ - 0.5 * A.Thick - 4.0) / A.Short, 0.0, 0.95)));
	// Partida: el mango se queda con el cubito en la arena (FiredDeg) y el cazo cuelga ~72° por debajo de la horizontal.
	DangleDeg = 72.0 - FiredDeg;

	// Hacia su X si ya mira al mar (el reparto le deja libre el arco de salto por ahí); si no, hacia el mar.
	FrameYawDeg = TNBeachRideKit::LaunchYawInActor(this);
	Frame->SetRelativeRotation(FRotator(0.0, FrameYawDeg, 0.0));
	ArmPivot->SetRelativeLocationAndRotation(FVector(PivotX, 0.0, PivotZ), FRotator(RestDeg, 0.0, 0.0));
	BowlHinge->SetRelativeLocationAndRotation(FVector(CrackX, 0.0, 0.0), FRotator::ZeroRotator);

	TNBeachTrapKit::FBuffers Arm;
	TNBeachTrapKit::FBuffers Bowl;
	BuildArm(Arm, Bowl, A, Seed, CrackX, bBoosted);
	ShiftX(Bowl, -CrackX);
	TNBeachTrapKit::SetMesh(ArmMesh, this, Arm, TN_ART("Beach.Catapult.Arm"));
	TNBeachTrapKit::SetMesh(BowlMesh, this, Bowl, TN_ART("Beach.Catapult.Bowl"));
	TNBeachTrapKit::FHulls ArmHulls;
	TNBeachTrapKit::FHulls BowlHulls;
	BuildArmHulls(ArmHulls, BowlHulls, A, CrackX);
	ShiftX(BowlHulls, -CrackX);
	ArmCollision->SetCollisionConvexMeshes(ArmHulls);
	BowlCollision->SetCollisionConvexMeshes(BowlHulls);
	TNBeachTrapKit::FBuffers Splinters;
	const FLinearColor Raw = bBoosted ? TNPlaygroundKit::Rgb(0xFFE9A8, 0.3f)
		: (A.Style == 0 ? TNPlaygroundKit::Rgb(0xF4F1EA, 0.2f) : TNPlaygroundKit::Rgb(0xF2D9A8, 0.05f));
	BuildSplinters(Splinters, A, CrackX, Seed, Raw);
	TNBeachTrapKit::SetMesh(SplinterMesh, this, Splinters, TN_ART("Beach.Catapult.Splinters"));

	// Piedra, tapón con estrías y una cuna para el brazo, arena removida.
	TNBeachTrapKit::FBuffers Base;
	TNBeachTrapKit::FHulls BaseHulls;
	const double StoneBaseR = CapR + 60.0;
	const double StoneTopR = CapR + 22.0;
	const TArray<FVector2D> StoneProfile = { FVector2D(0.0, StoneH), FVector2D(StoneTopR, StoneH), FVector2D(StoneTopR + 22.0, StoneH - 30.0),
		FVector2D(StoneBaseR, 30.0), FVector2D(StoneBaseR + 12.0, -25.0) };
	const TArray<FLinearColor> StoneCols = { TNBeachTrapKit::RockTone(0), TNBeachTrapKit::RockTone(2), TNBeachTrapKit::RockTone(1), TNBeachTrapKit::RockTone(0) };
	TNBeachTrapKit::AddLatheProfile(Base, FVector(PivotX, 0.0, 0.0), StoneProfile, StoneCols, 11, 0.07, Seed);
	BaseHulls.Add(TNPlaygroundKit::HullCylinder(FVector(PivotX, 0.0, -25.0), StoneH + 25.0, StoneBaseR, StoneTopR, 12));
	static const uint32 CapHex[4] = { 0xE63946, 0x1D7FD1, 0x2BB673, 0xF4F1EA };
	const FLinearColor CapCol = bBoosted ? TNBeachBoostKit::Navy() : TNPlaygroundKit::Rgb(CapHex[(Seed >> 5) % 4u], 0.35f);
	const FLinearColor RibCol = bBoosted ? TNBeachBoostKit::Gold() : TNPlaygroundKit::Shade(CapCol, 0.9);
	TNPlaygroundKit::AddFrustum(Base, FVector(PivotX, 0.0, StoneH - 4.0), FVector(PivotX, 0.0, StoneH + CapH), CapR, CapR - 3.0, 24, CapCol,
		TNPlaygroundKit::Shade(CapCol, 1.08), false, true);
	for (int32 i = 0; i < 24; ++i)
	{
		const double Ang = 360.0 * i / 24.0;
		TNPlaygroundKit::AddXfBox(Base, FTransform(FRotator(0.0, Ang, 0.0), FVector(PivotX, 0.0, StoneH)), FVector(CapR, 0.0, 0.5 * CapH), FVector(4.0, 5.0, 0.5 * CapH - 6.0),
			RibCol);
	}
	for (const double Side : { -1.0, 1.0 })
	{
		TNPlaygroundKit::AddAxisBox(Base, FVector(PivotX, Side * (0.5 * HandleWidth + 14.0), StoneH + CapH + 16.0), FVector(22.0, 10.0, 16.0), TNPlaygroundKit::Shade(CapCol, 0.8));
	}
	BaseHulls.Add(TNPlaygroundKit::HullCylinder(FVector(PivotX, 0.0, StoneH - 4.0), CapH + 4.0, CapR, CapR - 3.0, 14));
	// Arena removida donde golpean el cazo y el extremo del mango.
	TNPlaygroundKit::AddDisc(Base, FVector(PivotX + BowlCx * FMath::Cos(Theta), 0.0, 1.5), FVector::UpVector, A.BowlHW + 40.0, 16, TNBeachTrapKit::SandWet());
	TNPlaygroundKit::AddDisc(Base, FVector(PivotX + A.Short * FMath::Cos(FMath::DegreesToRadians(FiredDeg)), 0.0, 1.5), FVector::UpVector, A.BucketR + 50.0, 16,
		TNBeachTrapKit::SandMark());

	// Banderines en un palillo clavado en la piedra (verde: lista; rojo: armada o recargando).
	const FVector PoleBase(PivotX - 20.0, -(StoneTopR - 6.0), StoneH - 10.0);
	const FVector PoleTop = PoleBase + FVector(0.0, 0.0, 190.0);
	TNPlaygroundKit::AddRod(Base, PoleBase, PoleTop, 6.0, 6, TNPlaygroundKit::Rgb(0xE8D2A6), FVector::ForwardVector);
	if (bBoosted)
	{
		// Potenciada: al otro lado, la bandera de Tortunavy, y una guirnalda de banderines de palo a palo por encima del eje.
		const FVector NavyFoot(PivotX - 20.0, StoneTopR - 6.0, StoneH - 10.0);
		TNBeachBoostKit::AddNavyFlag(Base, NavyFoot, 260.0, FVector(-1.0, 0.4, 0.0), 120.0, 80.0, Seed);
		TNBeachBoostKit::AddBunting(Base, PoleTop, NavyFoot + FVector(0.0, 0.0, 240.0), 28.0, 26.0, Seed);
	}
	TNBeachTrapKit::SetMesh(BaseMesh, this, Base, TN_ART("Beach.Catapult.Base"));
	BaseCollision->SetCollisionConvexMeshes(BaseHulls);
	TNBeachTrapKit::FBuffers Green;
	TNPlaygroundKit::AddPennant(Green, PoleTop, FVector(-1.0, 0.0, 0.0), 95.0, 60.0, TNPlaygroundKit::Rgb(0x3DDC97, 0.2f));
	TNBeachTrapKit::SetMesh(FlagGreen, this, Green, TN_ART("Beach.Catapult.FlagReady"));
	TNBeachTrapKit::FBuffers Red;
	TNPlaygroundKit::AddPennant(Red, PoleTop, FVector(-1.0, 0.0, 0.0), 95.0, 60.0, TNPlaygroundKit::Rgb(0xFF4B3E, 0.2f));
	TNBeachTrapKit::SetMesh(FlagRed, this, Red, TN_ART("Beach.Catapult.FlagArmed"));

	// Palo de polo de pie bajo el extremo del mango, con su montoncito de arena.
	const double PropArmX = A.Short - 2.0 * A.BucketR - 10.0;
	const double PropX = PivotX + PropArmX * FMath::Cos(Theta);
	const double PropLen = PivotZ + PropArmX * FMath::Sin(Theta) - 0.5 * A.Thick * FMath::Cos(Theta);
	PropPivot->SetRelativeLocationAndRotation(FVector(PropX, 0.0, 0.0), FRotator::ZeroRotator);
	TNBeachTrapKit::FBuffers Prop;
	TNPlaygroundKit::AddStick(Prop, FVector(0.0, 0.0, 0.5 * PropLen), FVector::UpVector, FVector::RightVector, PropLen + 20.0, 48.0, 12.0,
		TNPlaygroundKit::Rgb(0xE9CC98, 0.05f));
	TNBeachTrapKit::SetMesh(PropMesh, this, Prop, TN_ART("Beach.Catapult.Prop"));

	// Cartel por el lado por el que se llega (-X del marco): a medio brazo largo, a un lado (fuera del cazo, la piedra y los
	// banderines; el arco va hacia +X) y girado 20° hacia el centro para leerse al venir de frente.
	SignSide = TNBeachSignKit::SideOf(Spec.Seed);
	SignYawDeg = SignSide * 20.0;
	SignPivot->SetRelativeLocationAndRotation(FVector(PivotX - 0.5 * A.Long, SignSide * (FMath::Max(A.BowlHW, StoneBaseR) + 175.0), 0.0),
		FRotator(0.0, SignYawDeg, 0.0));
	SignPivot->SetRelativeScale3D(FVector::OneVector);
	TNBeachTrapKit::FBuffers Sign;
	TNBeachSignKit::BuildSign(Sign, TNBeachSignKit::EIcon::Arc, bBoosted, Seed);
	TNBeachTrapKit::SetMesh(SignMesh, this, Sign, TN_ART("Beach.Catapult.Sign"));
	TNBeachTrapKit::FBuffers Cross;
	TNBeachSignKit::BuildBrokenCross(Cross);
	TNBeachTrapKit::SetMesh(SignCross, this, Cross, TN_ART("Beach.Sign.BrokenCross"));
	SignCross->SetVisibility(false);
	TNBeachSignKit::SetText(SignText, NSLOCTEXT("TNBeach", "CatapultSign", "¡CATAPULTA!"), TNBeachSignKit::TextColor(bBoosted));
	bSignShowsBroken = false;
	SignGlowApplied = -1.f;

	// Si se rehace con el brazo ya partido, que el Tick vuelva a poner la pose.
	SetActorTickEnabled(true);
	UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Catapulta %s: brazo de %.0f cm, fulcro a %.0f, %.0f° en reposo y %.0f° disparada, estilo %d%s%s."), *GetName(),
		A.Long + A.Short, PivotZ, RestDeg, FiredDeg, A.Style, bBoosted ? TEXT(", potenciada") : TEXT(""), bSingleUse ? TEXT(", un solo uso") : TEXT(""));
}

void ATN_BeachCatapult::BeginPlay()
{
	Super::BeginPlay();
	const FVector Mid = ArmPivot->GetComponentLocation();
	Voice = UTN_BeachTrapSynthComponent::AttachTo(this, Mid, 800.f, bBoosted ? 5000.f : 3500.f);
	Toy = UTN_PlaygroundSynthComponent::AttachTo(this, Mid, 800.f, bBoosted ? 5000.f : 3500.f);
	Dust.Init(this, ETNTrapBurstShape::Blob, TNBeachTrapKit::SandTop(), 32);
	Dust.SetMotion(-500.f, 2.f, 40.f, 12.f, 0.4f, 0.9f);
	Chips.Init(this, ETNTrapBurstShape::Chip, bBoosted ? TNPlaygroundKit::Rgb(0xFFE9A8) : TNPlaygroundKit::Rgb(0xE9CC98), 24);
	Chips.SetMotion(-980.f, 0.6f, 20.f, 8.f, 0.5f, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Consultas
// ─────────────────────────────────────────────────────────────────────────────

bool ATN_BeachCatapult::IsLoaded(double ServerTime) const
{
	if (FiredAt < 0.f)
	{
		return true;
	}
	return !bSingleUse && ServerTime - static_cast<double>(FiredAt) >= ReloadSeconds;
}

double ATN_BeachCatapult::ArmPitchAt(double Now) const
{
	using namespace TNBeachCatapultDetail;
	if (FiredAt < 0.f)
	{
		return RestDeg;
	}
	const double T = Now - static_cast<double>(FiredAt);
	if (T < 0.0)
	{
		return RestDeg;
	}
	if (T < SwingSeconds)
	{
		const double U = T / SwingSeconds;
		return FMath::Lerp(RestDeg, FiredDeg, U * U);
	}
	if (T < BounceEnd)
	{
		// Rebote contra la arena: dos botes que se apagan.
		const double B = T - SwingSeconds;
		return FiredDeg + 9.0 * FMath::Exp(-B / 0.14) * FMath::Abs(FMath::Sin(TNPlaygroundKit::KitPi * B / 0.17));
	}
	if (bSingleUse)
	{
		// Partida: el mango se queda tumbado con el cubito en la arena.
		return FiredDeg;
	}
	if (T >= ReloadSeconds)
	{
		return RestDeg;
	}
	if (T < HoldEnd)
	{
		return FiredDeg;
	}
	const double ReturnEnd = ReloadSeconds - SettleSeconds;
	if (T < ReturnEnd)
	{
		// Vuelve despacio, a golpes de carraca.
		const double U = (T - HoldEnd) / FMath::Max(0.1, ReturnEnd - HoldEnd);
		const double Steps = 14.0;
		const double Stepped = (FMath::FloorToDouble(U * Steps) + TNPlaygroundKit::Smooth01(0.0, 0.35, FMath::Frac(U * Steps))) / Steps;
		return FMath::Lerp(FiredDeg, RestDeg, Stepped);
	}
	const double S = T - ReturnEnd;
	return RestDeg - 2.5 * FMath::Exp(-S / 0.1) * FMath::Sin(TNPlaygroundKit::KitPi * S / 0.12);
}

double ATN_BeachCatapult::BowlPitchAt(double Now) const
{
	using namespace TNBeachCatapultDetail;
	if (!bSingleUse || FiredAt < 0.f)
	{
		return 0.0;
	}
	const double U = Now - static_cast<double>(FiredAt) - BreakSeconds;
	if (U <= 0.0)
	{
		return 0.0;
	}
	// Se parte y cae colgando de las astillas: se pasa un poco, rebota y se queda meciéndose cada vez menos.
	return DangleDeg * (1.0 - FMath::Exp(-U / 0.14) * FMath::Cos(TNPlaygroundKit::KitTwoPi * U / 0.55));
}

int32 ATN_BeachCatapult::WhereOnArm(const ACharacter* Character) const
{
	const UPrimitiveComponent* Floor = Character ? Character->GetMovementBase() : nullptr;
	if (!Floor || (Floor != ArmCollision.Get() && Floor != BowlCollision.Get()))
	{
		return 0;
	}
	// En el espacio del eje (la bisagra del cazo no gira mientras la catapulta está entera).
	const FVector Feet = TNBeachRideKit::FeetIn(ArmPivot->GetComponentTransform(), Character);
	if (Feet.X < -LongArm + BowlLength + 10.0 && FMath::Abs(Feet.Y) < BowlHalfWidth + 25.0)
	{
		return 1;
	}
	if (Feet.X > ShortArm - 2.0 * BucketRadius && Feet.Z > 0.5 * ArmThick + BucketHeight - 40.0)
	{
		return 3;
	}
	return 2;
}

void ATN_BeachCatapult::SetLaunchTarget(const FVector& WorldTarget)
{
	LaunchTarget = WorldTarget;
	bHasLaunchTarget = true;
}

FVector ATN_BeachCatapult::LaunchVelocity(float Fraction, const FVector& From) const
{
	using namespace TNBeachCatapultDetail;
	// Con el arco de conchitas (las normales): hacia su final, con la rapidez justa para caer en él frenando en el aire.
	const UWorld* World = GetWorld();
	if (bHasLaunchTarget && !bBoosted && World)
	{
		const FVector Fwd = Frame->GetForwardVector().GetSafeNormal2D();
		const FVector ToTarget = LaunchTarget - From;
		const FVector Dir = ToTarget.GetSafeNormal2D();
		if (!Dir.IsNearlyZero() && FVector::DotProduct(Dir, Fwd) >= FMath::Cos(FMath::DegreesToRadians(AimMaxYawDeg)))
		{
			const double PitchRad = FMath::DegreesToRadians(static_cast<double>(LaunchPitch));
			const double Gravity = FMath::Max(1.0, -static_cast<double>(World->GetGravityZ()));
			const double Speed = TNBeachCatapultAim::SpeedToReach(ToTarget.Size2D(), ToTarget.Z, PitchRad, TNBeachCatapultAim::ShellBallLinearDamping,
				Gravity, AimMinSpeedFraction * LaunchSpeed, AimMaxSpeedFraction * LaunchSpeed);
			const FVector Unit = Dir * FMath::Cos(PitchRad) + FVector::UpVector * FMath::Sin(PitchRad);
			return Unit * (Speed * Fraction * FMath::FRandRange(0.98f, 1.02f));
		}
	}
	// Hacia el mar (X del marco) con desvío y elevación al azar (servidor); la potenciada, más fuerte y más recta.
	const float Spread = bBoosted ? BoostedDeviationDeg : DeviationDeg;
	const float Yaw = FMath::FRandRange(-Spread, Spread);
	const float Pitch = (bBoosted ? BoostedLaunchPitch : LaunchPitch) + FMath::FRandRange(bBoosted ? -1.5f : -3.f, bBoosted ? 1.5f : 3.f);
	const float Speed = (bBoosted ? BoostedLaunchSpeed : LaunchSpeed) * Fraction * FMath::FRandRange(bBoosted ? 0.97f : 0.95f, bBoosted ? 1.03f : 1.05f);
	const FVector Local = FRotator(Pitch, Yaw, 0.f).Vector();
	return Frame->GetComponentTransform().TransformVectorNoScale(Local) * Speed;
}

ATN_ShellBody* ATN_BeachCatapult::BowlBallOf(const ACharacter* Character) const
{
	using namespace TNBeachCatapultDetail;
	const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Character);
	if (!IsValid(Turtle) || Turtle->IsDead() || Turtle->IsKnockedDown() || !Turtle->IsInShell())
	{
		return nullptr;
	}
	// Como IsFreeRider/IsFreeTurtle, pero dentro del caparazón: sin aturdir, sin sujetar un enemigo ni un gusano, sin que la
	// recoloque la tormenta o la red de seguridad y sin que la lleve nadie.
	if (TNBeach::IsTurtleStunned(Turtle) || TNBeach::IsTurtleRelocating(Turtle) || ATN_BeachEnemy::IsTurtleHeld(Turtle) || ATN_BeachSandWorm::IsBeingEaten(Turtle))
	{
		return nullptr;
	}
	const UTN_CarryComponent* Carry = Turtle->GetCarryComponent();
	if (Carry && (Carry->GetCarrier() != nullptr || Carry->IsCarrying()))
	{
		return nullptr;
	}
	// Su cuerpo físico (solo existe con la tortuga suelta; a la que llevan no lo tiene).
	const UTN_ShellComponent* Shell = Turtle->GetShellComponent();
	ATN_ShellBody* Body = Shell ? Shell->GetBody() : nullptr;
	const UBoxComponent* Box = Body ? Body->GetBox() : nullptr;
	if (!Box || !Box->IsSimulatingPhysics())
	{
		return nullptr;
	}
	// Quieta: la que pasa rodando a toda velocidad no arma la catapulta.
	if (Box->GetComponentVelocity().SizeSquared() > FMath::Square(BowlBallMaxSpeed))
	{
		return nullptr;
	}
	// Dentro del cazo, apoyada en su suelo (el centro de la caja, en el espacio del eje: el cazo es lo que queda a -X).
	const FVector Local = ArmPivot->GetComponentTransform().InverseTransformPosition(Box->GetComponentLocation());
	const double Floor = 0.5 * ArmThick - BowlDepth;
	if (Local.X > -LongArm + BowlLength + 10.0 || Local.X < -LongArm - 30.0 || FMath::Abs(Local.Y) > BowlHalfWidth + 15.0
		|| Local.Z < Floor - BowlBallFloorBelow || Local.Z > Floor + BowlBallFloorAbove)
	{
		return nullptr;
	}
	return Body;
}

bool ATN_BeachCatapult::LaunchBowlBall(ATortugaCharacter* Turtle, ATN_ShellBody* Body, const FVector& Velocity)
{
	using namespace TNBeachCatapultDetail;
	UTN_ShellComponent* Shell = Turtle ? Turtle->GetShellComponent() : nullptr;
	UBoxComponent* Box = Body ? Body->GetBox() : nullptr;
	if (!Turtle || !Turtle->HasAuthority() || !Shell || !Box || Shell->GetBody() != Body)
	{
		return false;
	}
	// Lanzada: no se sale en el aire (ForceExitShell, al pararse la caja o caer al agua, la desbloquea). InitBody, pública, es
	// lo que hace falta para que la caja salga sola al pararse (y reinicia su edad para el límite de 9 s); no crea otra caja.
	Shell->SetExitLocked(true);
	Body->InitBody(Turtle, true);
	// La velocidad va a la caja del servidor y la réplica de física la lleva a los clientes, igual que cualquier empujón.
	const FVector Right = FRotator(0.0, Velocity.Rotation().Yaw, 0.0).RotateVector(FVector::RightVector);
	Box->SetPhysicsLinearVelocity(Velocity);
	Box->SetPhysicsAngularVelocityInRadians(Right * BowlBallSpin);
	Turtle->ForceNetUpdate();
	return true;
}

void ATN_BeachCatapult::BeginFlight(ACharacter* Turtle)
{
	using namespace TNBeachCatapultDetail;
	UWorld* World = GetWorld();
	if (!Turtle || !World)
	{
		return;
	}
	// Lanzada por el aire en su bola: el árbitro la da por lanzada (Launch), así la tormenta no la patea como si rodara ella
	// misma por gusto. (La red de seguridad solo mira bajo la arena y en una patada de la tormenta, no aquí.)
	TNBeach::ClaimTurtle(Turtle, TNBeach::ETNBeachMover::Launch, static_cast<float>(FlightClaimSeconds));
	Flights.Add(Turtle, World->GetTimeSeconds() + FlightClaimSeconds);
	SetActorTickEnabled(true);
}

void ATN_BeachCatapult::TickFlights()
{
	if (Flights.IsEmpty() || !GetWorld())
	{
		return;
	}
	const double WorldNow = GetWorld()->GetTimeSeconds();
	for (auto It = Flights.CreateIterator(); It; ++It)
	{
		ACharacter* Walker = It.Key().Get();
		const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Walker);
		if (Turtle && Turtle->IsInShell() && WorldNow < It.Value())
		{
			continue;
		}
		// Ya aterrizó (salió del caparazón), se cayó del mundo o se pasó de tiempo.
		if (Walker)
		{
			TNBeach::ReleaseTurtle(Walker, TNBeach::ETNBeachMover::Launch);
		}
		It.RemoveCurrent();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Servidor
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachCatapult::ServerTick(double Now)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// Las lanzadas siguen reservadas hasta que aterrizan, aunque la catapulta ya esté gastada.
	TickFlights();
	if (IsSpent())
	{
		// Gastada: ya no se arma ni dispara en toda la ronda.
		Riders.Reset();
		if (ArmedAt >= 0.f)
		{
			ArmedAt = -1.f;
			ForceNetUpdate();
		}
		return;
	}
	const FVector PivotAt = ArmPivot->GetComponentLocation();
	const double Reach = LongArm + 600.0;
	int32 InBowl = 0;
	int32 OnHandle = 0;
	bool bKick = false;
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ACharacter* Walker = *It;
		if (!IsValid(Walker))
		{
			continue;
		}
		const bool bNear = FVector::DistSquared2D(Walker->GetActorLocation(), PivotAt) < Reach * Reach;
		if (!bNear)
		{
			Riders.Remove(Walker);
			continue;
		}
		FRider& Rider = Riders.FindOrAdd(Walker);
		int32 Where = TNBeachRideKit::IsFreeRider(Walker) ? WhereOnArm(Walker) : 0;
		if (Where == 0)
		{
			// Metida en su caparazón y quieta en el cazo: pasajera como cualquiera. Con el aviso en marcha se la sujeta quieta
			// (velocidad a cero en cada tic del servidor: ni la empuja otra tortuga que entre ni resbala por el cazo inclinado).
			if (ATN_ShellBody* Ball = BowlBallOf(Walker))
			{
				Where = 1;
				if (ArmedAt >= 0.f)
				{
					if (UBoxComponent* Box = Ball->GetBox())
					{
						Box->SetPhysicsLinearVelocity(FVector::ZeroVector);
						Box->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
					}
				}
			}
		}
		// Llega al cubito (el punto más alto): dispara. Sin pedir velocidad de caída: desde el mango se sube de un salto de
		// ~1,1 m y se aterriza en la cima del salto, casi sin velocidad.
		if (Where == 3 && !Rider.bOnBucket)
		{
			bKick = true;
		}
		Rider.bOnBucket = Where == 3;
		if (Where == 1)
		{
			++InBowl;
		}
		else if (Where == 2)
		{
			++OnHandle;
		}
	}
	for (auto It = Riders.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	if (!IsLoaded(Now) || !TNBeachRideKit::IsRaceLive(this))
	{
		if (ArmedAt >= 0.f)
		{
			ArmedAt = -1.f;
			ForceNetUpdate();
		}
		return;
	}
	// Pisar el cubito dispara si hay a quién lanzar. Sin nadie en el cazo ni en el mango la gastaba vacía: quien jugaba sola y
	// lo pisaba la rompía y se hundía con el cubito, sin salir lanzada (#257). Para salir lanzada, al cazo.
	if (bKick && InBowl + OnHandle > 0)
	{
		Fire(Now);
		return;
	}
	if (InBowl > 0)
	{
		EmptySince = -1.0;
		if (ArmedAt < 0.f)
		{
			ArmedAt = static_cast<float>(Now);
			ForceNetUpdate();
		}
		else if (Now >= static_cast<double>(ArmedAt) + WarnSeconds)
		{
			Fire(Now);
		}
	}
	else if (ArmedAt >= 0.f)
	{
		// Se ha bajado del cazo: se desarma si sigue vacío un momento.
		if (EmptySince < 0.0)
		{
			EmptySince = Now;
		}
		else if (Now - EmptySince > 0.35)
		{
			ArmedAt = -1.f;
			EmptySince = -1.0;
			ForceNetUpdate();
		}
	}
}

void ATN_BeachCatapult::Fire(double Now)
{
	FiredAt = static_cast<float>(Now);
	ArmedAt = -1.f;
	EmptySince = -1.0;
	// Sin colisión en el golpe: las bolas nacen dentro del cazo.
	ArmCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	bArmCollisionOn = false;
	SetBowlCollision(false);
	int32 Launched = 0;
	int32 LaunchedBalls = 0;
	for (auto It = Riders.CreateIterator(); It; ++It)
	{
		ACharacter* Walker = It.Key().Get();
		if (!Walker)
		{
			continue;
		}
		if (!TNBeachRideKit::IsFreeRider(Walker))
		{
			// La bola de caparazón quieta en el cazo sale igual que una de pie (la colisión ya está apagada: nace sin tropezar).
			ATortugaCharacter* BallTurtle = Cast<ATortugaCharacter>(Walker);
			if (ATN_ShellBody* Ball = BowlBallOf(Walker))
			{
				const UBoxComponent* BallBox = Ball->GetBox();
				const FVector From = BallBox ? BallBox->GetComponentLocation() : Walker->GetActorLocation();
				if (LaunchBowlBall(BallTurtle, Ball, LaunchVelocity(1.f, From)))
				{
					BeginFlight(Walker);
					++Launched;
					++LaunchedBalls;
				}
			}
			continue;
		}
		const int32 Where = WhereOnArm(Walker);
		if (Where != 1 && Where != 2)
		{
			continue;
		}
		if (TNBeachRideKit::LaunchAsBall(Cast<ATortugaCharacter>(Walker), LaunchVelocity(Where == 1 ? 1.f : HandleLaunchFraction, Walker->GetActorLocation())))
		{
			BeginFlight(Walker);
			++Launched;
		}
	}
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] Catapulta %s%s dispara: %d lanzadas (%d ya en su bola)%s."), *GetName(), bBoosted ? TEXT(" potenciada") : TEXT(""), Launched,
		LaunchedBalls, bSingleUse ? TEXT("; queda partida") : TEXT(""));
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick y efectos
// ─────────────────────────────────────────────────────────────────────────────

void ATN_BeachCatapult::OnRep_Shot()
{
	// Todo sale de las horas en TickVisuals; aquí solo se asegura que el tick corre.
	SetActorTickEnabled(true);
}

void ATN_BeachCatapult::SetBowlCollision(bool bOn)
{
	if (bOn != bBowlCollisionOn)
	{
		bBowlCollisionOn = bOn;
		BowlCollision->SetCollisionEnabled(bOn ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
}

void ATN_BeachCatapult::ApplyVisualShake(double Now, double Warn)
{
	using namespace TNBeachCatapultDetail;
	if (Warn < 0.0)
	{
		if (bMeshShaken)
		{
			// Una última vez al acabar el aviso (o al disparar): las mallas vuelven exactas a su sitio sobre las colisiones.
			bMeshShaken = false;
			ArmMesh->SetRelativeTransform(FTransform::Identity);
			BowlMesh->SetRelativeTransform(FTransform::Identity);
		}
		return;
	}
	bMeshShaken = true;
	const FRotator Shake((ShakePitchBase + ShakePitchGain * Warn) * FMath::Sin(Now * 63.0), (ShakeYawBase + ShakeYawGain * Warn) * FMath::Sin(Now * 47.0 + 1.3),
		(ShakeRollBase + ShakeRollGain * Warn) * FMath::Sin(Now * 71.0 + 0.6));
	const FTransform ShakeXf(Shake);
	// El brazo tiembla sobre el eje. El cazo cuelga de la bisagra del cuello, no del eje: para que gire igual sobre el eje
	// hay que llevar el temblor al espacio de la bisagra (aplicar la bisagra, el temblor y la bisagra inversa).
	ArmMesh->SetRelativeTransform(ShakeXf);
	const FTransform HingeRel = BowlHinge->GetRelativeTransform();
	BowlMesh->SetRelativeTransform(HingeRel * ShakeXf * HingeRel.Inverse());
}

void ATN_BeachCatapult::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = Clock.Advance(GetWorld(), DeltaSeconds);
	if (HasAuthority())
	{
		ServerTick(TNBeachTrapKit::ServerNow(GetWorld()));
	}
	TickVisuals(Now, DeltaSeconds);
	const bool bDustLive = Dust.Tick(DeltaSeconds);
	const bool bChipsLive = Chips.Tick(DeltaSeconds);
	const bool bPopLive = Pop.Tick(DeltaSeconds, GetWorld());
	const bool bBreakPopLive = BreakPop.Tick(DeltaSeconds, GetWorld());
	// Gastada y quieta: nada que animar hasta la ronda siguiente (OnRep_Shot o ApplySpec lo vuelven a encender).
	// (Con lanzadas en vuelo, el servidor sigue con el Tick para soltar su reserva del árbitro al aterrizar.)
	if (IsSpent() && Now - static_cast<double>(FiredAt) > TNBeachCatapultDetail::SettledSeconds && !bDustLive && !bChipsLive && !bPopLive && !bBreakPopLive
		&& Flights.IsEmpty())
	{
		SetActorTickEnabled(false);
	}
}

void ATN_BeachCatapult::TickSign(double Now, float DeltaSeconds, bool bBroken)
{
	using namespace TNBeachCatapultDetail;
	// Rótulo según el estado (también para quien llega tarde y la ve ya partida).
	if (bBroken != bSignShowsBroken)
	{
		bSignShowsBroken = bBroken;
		SignCross->SetVisibility(bBroken);
		TNBeachSignKit::SetText(SignText, bBroken ? NSLOCTEXT("TNBeach", "CatapultSignBroken", "¡ROTA!") : NSLOCTEXT("TNBeach", "CatapultSign", "¡CATAPULTA!"),
			TNBeachSignKit::TextColor(bBoosted, bBroken));
		SignGlowApplied = -1.f;
	}
	if (bBroken)
	{
		// Con el crujido de la rotura, el cartel se tuerce hacia fuera y hacia atrás (se pasa un poco y se asienta).
		const double U = Now - static_cast<double>(FiredAt) - BreakSeconds;
		const double Lean = U <= 0.0 ? 0.0 : 1.0 - FMath::Exp(-U / 0.12) * FMath::Cos(TNPlaygroundKit::KitTwoPi * U / 0.6);
		SignPivot->SetRelativeRotation(FRotator(-12.0 * Lean, SignYawDeg + 6.0 * SignSide * Lean, 24.0 * SignSide * Lean));
		SignPivot->SetRelativeScale3D(FVector::OneVector);
		return;
	}
	// Entera: un botecito al acercarse la tortuga de esta máquina y el rótulo más claro mientras está cerca.
	float Stretch = 0.f;
	float Sway = 0.f;
	TNBeachSignKit::TickSignAnim(DeltaSeconds, GetWorld(), SignPivot->GetComponentLocation(), SignAge, bSignNear, SignGlow, Stretch, Sway);
	const bool bMoving = FMath::Abs(Stretch) > 0.0005f || FMath::Abs(Sway) > 0.01f;
	if (bMoving || bSignMoving)
	{
		// Una última vez al pararse, para dejarlo recto.
		SignPivot->SetRelativeRotation(FRotator(0.0, SignYawDeg, bMoving ? Sway : 0.f));
		SignPivot->SetRelativeScale3D(FVector(1.0, 1.0, bMoving ? 1.0 + Stretch : 1.0));
		bSignMoving = bMoving;
	}
	TNBeachSignKit::ApplySignGlow(SignText, TNBeachSignKit::TextColor(bBoosted), SignGlow, SignGlowApplied);
}

void ATN_BeachCatapult::TickVisuals(double Now, float DeltaSeconds)
{
	using namespace TNBeachCatapultDetail;
	const double Fired = static_cast<double>(FiredAt);
	const double T = FiredAt >= 0.f ? Now - Fired : 1e6;
	// FiredAt va en float: en el servidor, en el mismo fotograma del disparo, T puede salir un pelo negativo y la colisión
	// del brazo y del cazo volvía a encenderse con las bolas recién nacidas dentro (lanzamientos torcidos o flojos).
	const bool bShot = FiredAt >= 0.f && (T >= -0.05 || HasAuthority());
	const bool bBroken = bSingleUse && bShot;
	const bool bLoaded = IsLoaded(Now);
	const bool bArmed = !bBroken && ArmedAt >= 0.f && Now >= static_cast<double>(ArmedAt);
	const double Warn = bArmed ? FMath::Clamp((Now - static_cast<double>(ArmedAt)) / FMath::Max(0.05, static_cast<double>(WarnSeconds)), 0.0, 1.0) : 0.0;

	// Brazo: el eje (y con él las colisiones del brazo y del cazo) solo sigue el cabeceo real. El temblor del aviso NO se
	// suma aquí: mover las colisiones a 10 Hz desplazaba el suelo bajo la tortuga o la bola de caparazón que espera en el
	// cazo y la despedía antes del disparo. Tiembla solo la malla visible (ApplyVisualShake). Partido, el cazo cuelga de su bisagra.
	ArmPivot->SetRelativeRotation(FRotator(ArmPitchAt(Now), 0.0, 0.0));
	BowlHinge->SetRelativeRotation(FRotator(BowlPitchAt(Now), 0.0, 0.0));
	const bool bShowSplinters = bSingleUse && bShot && T >= BreakSeconds;
	if (SplinterMesh->IsVisible() != bShowSplinters)
	{
		SplinterMesh->SetVisibility(bShowSplinters);
	}

	// Colisión del brazo apagada durante el golpe; la del cazo, para siempre al partirse.
	const bool bWantCollision = !(bShot && T < NoCollisionSeconds);
	if (bWantCollision != bArmCollisionOn)
	{
		bArmCollisionOn = bWantCollision;
		ArmCollision->SetCollisionEnabled(bWantCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
	SetBowlCollision(bWantCollision && !bBroken);

	// Palo que sujeta: tiembla con el aviso, sale volando al disparar y (si recarga) se pone de pie al final.
	double PropPitch = 0.0;
	double PropRoll = bArmed ? (2.0 + 4.0 * Warn) * FMath::Sin(Now * 47.0) : 0.0;
	if (bShot && (bSingleUse || T < ReloadSeconds))
	{
		const double Down = T < 0.35 ? TNPlaygroundKit::Smooth01(0.0, 0.35, T) : 1.0;
		const double Up = bSingleUse ? 0.0 : TNPlaygroundKit::Smooth01(ReloadSeconds - 0.55, ReloadSeconds - 0.1, T);
		PropPitch = -84.0 * Down * (1.0 - Up) + (T > 0.35 && T < 0.6 ? 5.0 * FMath::Sin(TNPlaygroundKit::KitPi * (T - 0.35) / 0.25) : 0.0);
		PropRoll = 0.0;
	}
	PropPivot->SetRelativeRotation(FRotator(PropPitch, 0.0, PropRoll));

	if (GetNetMode() == NM_DedicatedServer)
	{
		LastVisualNow = Now;
		return;
	}
	TickSign(Now, DeltaSeconds, bBroken);
	ApplyVisualShake(Now, bArmed && bLoaded ? Warn : -1.0);

	// Banderín: verde lista; rojo parpadeando armada; rojo fijo recargando; ninguno con el brazo partido (arrancado).
	const bool bShowRed = !bBroken && (!bLoaded || (bArmed && FMath::Frac(Now * 4.0) < 0.5));
	const bool bShowGreen = !bBroken && !bShowRed;
	if (FlagGreen->IsVisible() != bShowGreen)
	{
		FlagGreen->SetVisibility(bShowGreen);
	}
	if (FlagRed->IsVisible() != bShowRed)
	{
		FlagRed->SetVisibility(bShowRed);
	}

	const double Before = LastVisualNow;
	auto Crossed = [Before, Now](double At) { return At >= 0.0 && Before < At && Now >= At && Now - At < 1.0; };
	const FTransform ArmXf = ArmPivot->GetComponentTransform();
	const FVector BowlAt = BowlHinge->GetComponentTransform().TransformPosition(FVector(-LongArm + 0.5 * BowlLength - CrackX, 0.0, 40.0));
	const FVector BucketAt = ArmXf.TransformPosition(FVector(ShortArm - 0.9 * BucketRadius, 0.0, 0.0));

	// Aviso: crujidos cada vez más seguidos y agudos.
	if (bArmed && bLoaded)
	{
		if (Crossed(static_cast<double>(ArmedAt)))
		{
			Pop.Show(this, NSLOCTEXT("TNBeach", "CatapultWarn", "¡AGÁRRATE!"), FColor(255, 214, 90), BowlAt + FVector(0.0, 0.0, 220.0), 110.f);
		}
		CreakTimer -= DeltaSeconds;
		if (CreakTimer <= 0.f && Voice)
		{
			Voice->TriggerSoundAt(ETNBeachTrapSound::Creak, PropPivot->GetComponentLocation() + FVector(0.0, 0.0, 150.0), 1.f + 0.5f * static_cast<float>(Warn),
				0.6f + 0.5f * static_cast<float>(Warn));
			CreakTimer = static_cast<float>(FMath::Lerp(0.32, 0.1, Warn));
		}
	}
	else
	{
		CreakTimer = 0.f;
	}

	// Disparo: el palo se parte, el cubito cae y la cuchara da la vuelta. La potenciada, con fanfarria y más golpe.
	if (Crossed(Fired))
	{
		const FVector PropTop = PropPivot->GetComponentLocation() + FVector(0.0, 0.0, 120.0);
		Chips.Burst(PropTop, 10, FVector::UpVector, 420.f, 1.2f, 30.f);
		if (Voice)
		{
			Voice->TriggerSoundAt(ETNBeachTrapSound::Crack, PropTop, FMath::FRandRange(1.1f, 1.25f), 1.f);
		}
		if (Toy)
		{
			Toy->TriggerSound(ETNPlaygroundSound::Whoosh, bBoosted ? 0.62f : 0.8f, bBoosted ? 1.3f : 1.f);
		}
		if (bBoosted)
		{
			Pop.Show(this, NSLOCTEXT("TNBeach", "CatapultFireBoosted", "¡ZAAAS!"), FColor(255, 215, 60), BowlAt + FVector(0.0, 0.0, 320.0), 230.f);
			TNBeachBoostKit::PlayFanfareNear(this, BowlAt, 9000.f, 1.f);
			if (Voice)
			{
				Voice->TriggerSoundAt(ETNBeachTrapSound::Twang, BowlAt, 0.5f, 1.3f);
			}
		}
		else
		{
			Pop.Show(this, NSLOCTEXT("TNBeach", "CatapultFire", "¡ZAS!"), FColor(255, 170, 70), BowlAt + FVector(0.0, 0.0, 300.0), 150.f);
		}
	}
	if (Crossed(Fired + SwingSeconds))
	{
		Dust.Burst(BucketAt, bBoosted ? 26 : 16, FVector::UpVector, bBoosted ? 520.f : 380.f, 1.3f, static_cast<float>(BucketRadius));
		if (Voice)
		{
			Voice->TriggerSoundAt(ETNBeachTrapSound::Thud, BucketAt, FMath::FRandRange(0.8f, 0.9f) * (bBoosted ? 0.8f : 1.f), bBoosted ? 1.5f : 1.2f);
			Voice->TriggerSoundAt(ETNBeachTrapSound::Twang, BowlAt, FMath::FRandRange(0.7f, 0.8f), 0.9f);
		}
		if (Toy)
		{
			Toy->TriggerSound(ETNPlaygroundSound::Bonk, 0.7f, 1.f);
		}
		if (bBoosted)
		{
			UTN_BeachCameraShake::Kick(this, BucketAt, 0.7f, 900.f, 4200.f);
		}
		else
		{
			UTN_BeachCameraShake::Kick(this, BucketAt, 0.4f, 600.f, 2600.f);
		}
	}

	// Un solo uso: el brazo se parte por el cuello del cazo, que cae colgando de las astillas.
	if (bSingleUse && Crossed(Fired + BreakSeconds))
	{
		const FVector CrackAt = ArmXf.TransformPosition(FVector(CrackX, 0.0, 0.0));
		Chips.Burst(CrackAt, 16, FVector::UpVector, 360.f, 1.4f, 40.f);
		if (Voice)
		{
			Voice->TriggerSoundAt(ETNBeachTrapSound::Crack, CrackAt, FMath::FRandRange(0.7f, 0.8f), 1.3f);
			Voice->TriggerSoundAt(ETNBeachTrapSound::Creak, CrackAt, 0.6f, 0.8f);
		}
		BreakPop.Show(this, NSLOCTEXT("TNBeach", "CatapultBreak", "¡CRAC!"), FColor(255, 120, 90), CrackAt + FVector(0.0, 0.0, 170.0), 130.f);
	}
	if (bSingleUse && Crossed(Fired + BreakSeconds + 0.3))
	{
		const FVector HangAt = BowlHinge->GetComponentLocation();
		if (Voice)
		{
			Voice->TriggerSoundAt(ETNBeachTrapSound::Creak, HangAt, 0.45f, 0.7f);
		}
		Dust.Burst(HangAt - FVector(0.0, 0.0, FMath::Max(0.0, HangAt.Z - GetActorLocation().Z - 40.0)), 6, FVector::UpVector, 160.f, 1.2f, 50.f);
	}

	// Recarga (sin bSingleUse): carraca a cada paso y el cazo que se asienta.
	if (!bSingleUse && FiredAt >= 0.f && T > HoldEnd && T < ReloadSeconds - SettleSeconds)
	{
		if (Now - LastRatchetAt >= RatchetStep || LastRatchetAt > Now)
		{
			LastRatchetAt = Now;
			if (Voice)
			{
				Voice->TriggerSoundAt(ETNBeachTrapSound::Plink, ArmPivot->GetComponentLocation(), 1.9f, 0.3f);
			}
		}
	}
	if (!bSingleUse && Crossed(Fired + ReloadSeconds) && Voice)
	{
		Voice->TriggerSoundAt(ETNBeachTrapSound::Clack, BowlAt, 1.2f, 0.7f);
		Dust.Burst(BowlAt - FVector(0.0, 0.0, 40.0), 6, FVector::UpVector, 200.f, 1.2f, 60.f);
	}
	LastVisualNow = Now;
}
