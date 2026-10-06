#include "World/Beach/TN_BeachMine.h"
#include "World/Beach/TN_BeachCameraShake.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachMineSynth.h"
#include "World/Beach/TN_BeachNearby.h"
#include "World/Beach/TN_BeachSandWorm.h"
#include "World/Beach/TN_BeachStun.h"
#include "Core/TN_Log.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Player/TN_TurtleMovementComponent.h"
#include "Player/TortugaCharacter.h"
#include "Templates/Function.h"
#include "UObject/Package.h"
#include "TN_BeachTrapKit.h"

/**
 * Geometría de la mina con SizeScale 1 (cada mina escala sus componentes a su tamaño): montoncito de arena removida de
 * 1,28 m de radio con la mina medio enterrada en medio (plato de 70 cm de radio cuya tapa asoma 22 cm, o bote de 42 cm que
 * asoma 16), el pincho de la espoleta, el piloto, la tapa roja del parpadeo, la banderita de aviso a 1,9 m (girada con la
 * semilla) y el cráter (suelo chamuscado de 1,5 m, reborde de arena hasta 2,6 m, rayas de quemado y trozos hasta 3,2 m).
 * Las mallas son pocas y compartidas (una por aspecto y pieza): en una ronda hay decenas de minas.
 */
namespace TNBeachMineDetail
{
	using FBuffers = TNBeachTrapKit::FBuffers;

	/** Margen (cm) sobre el radio de la tapa para buscar quién la pisa: cabe la cápsula de una tortuga con holgura. */
	constexpr double StepReachMargin = 150.0;

	/** Aspecto de la mina. */
	struct FMineLook
	{
		FLinearColor Body;
		FLinearColor BodyDark;
		FLinearColor Plate;
		FLinearColor Band;
		FLinearColor Prong;
	};

	/** Plato verde oliva con franja amarilla (0), bote gris de tres pinchos (1), plato oxidado (2) o juguete caqui con botón rojo (3). */
	FMineLook LookOf(int32 Kind)
	{
		FMineLook L;
		switch (Kind)
		{
		case 1:
			L.Body = TNPlaygroundKit::Rgb(0x55595C, 0.4f);
			L.BodyDark = TNPlaygroundKit::Rgb(0x3C4043, 0.4f);
			L.Plate = TNPlaygroundKit::Rgb(0x6A6F73, 0.5f);
			L.Band = TNPlaygroundKit::Rgb(0xE8D27A, 0.1f);
			L.Prong = TNPlaygroundKit::Rgb(0x9AA0A6, 0.7f);
			break;
		case 2:
			L.Body = TNPlaygroundKit::Rgb(0x8A5A3A, 0.15f);
			L.BodyDark = TNPlaygroundKit::Rgb(0x5E3A26, 0.1f);
			L.Plate = TNPlaygroundKit::Rgb(0x9A6A44, 0.15f);
			L.Band = TNPlaygroundKit::Rgb(0x6E7B3C, 0.05f);
			L.Prong = TNPlaygroundKit::Rgb(0x6A4430, 0.3f);
			break;
		case 3:
			L.Body = TNPlaygroundKit::Rgb(0xC2AE7A, 0.15f);
			L.BodyDark = TNPlaygroundKit::Rgb(0x9A8A5E, 0.1f);
			L.Plate = TNPlaygroundKit::Rgb(0xD8342A, 0.3f);
			L.Band = TNPlaygroundKit::Rgb(0xF2E8CC, 0.1f);
			L.Prong = TNPlaygroundKit::Rgb(0xE0342E, 0.35f);
			break;
		default:
			L.Body = TNPlaygroundKit::Rgb(0x5E6B38, 0.1f);
			L.BodyDark = TNPlaygroundKit::Rgb(0x46512A, 0.1f);
			L.Plate = TNPlaygroundKit::Rgb(0x6E7B44, 0.1f);
			L.Band = TNPlaygroundKit::Rgb(0xE8D27A, 0.1f);
			L.Prong = TNPlaygroundKit::Rgb(0x9AA0A6, 0.7f);
			break;
		}
		return L;
	}

	/** Medidas de cada aspecto con SizeScale 1: alto de la tapa sobre la arena, sitio del piloto y radio de la tapa roja. */
	void MineSpots(int32 Kind, double& OutTop, FVector& OutLed, double& OutCoverR)
	{
		constexpr double R = 70.0;
		if (Kind == 1)
		{
			OutTop = 16.0;
			OutLed = FVector(-R * 0.6 * 0.62, 0.0, OutTop + 1.0);
			OutCoverR = R * 0.6 * 1.05;
			return;
		}
		OutTop = 22.0;
		OutLed = FVector(-R * 0.36, R * 0.2, OutTop + 1.0);
		OutCoverR = R * 1.03;
	}

	/** Estrella plana de cinco puntas (radio R) mirando hacia arriba en Center. */
	void AddFlatStar(FBuffers& B, const FVector& Center, double R, const FLinearColor& Color)
	{
		for (int32 k = 0; k < 10; ++k)
		{
			const double A0 = TNPlaygroundKit::KitTwoPi * k / 10.0;
			const double A1 = TNPlaygroundKit::KitTwoPi * (k + 1) / 10.0;
			const double R0 = (k % 2) ? R * 0.4 : R;
			const double R1 = (k % 2) ? R : R * 0.4;
			B.AddTri(Center, Center + FVector(FMath::Cos(A0) * R0, FMath::Sin(A0) * R0, 0.0), Center + FVector(FMath::Cos(A1) * R1, FMath::Sin(A1) * R1, 0.0),
				FVector::UpVector, Color);
		}
	}

	/** Arena removida y mina medio enterrada con su pincho (o sus tres pinchos). */
	void BuildMine(FBuffers& B, int32 Kind, const FMineLook& L, uint32 Seed)
	{
		constexpr double R = 70.0;
		double Top = 22.0;
		FVector LedAt = FVector::ZeroVector;
		double CoverR = R;
		MineSpots(Kind, Top, LedAt, CoverR);
		// Arena removida alrededor (de dentro afuera: la cara mira hacia arriba y, en el borde de dentro, a la mina).
		TNBeachTrapKit::AddLatheProfile(B, FVector::ZeroVector,
			{ FVector2D(R * 0.9, 4.0), FVector2D(R * 1.06, 13.0), FVector2D(90.0, 15.0), FVector2D(110.0, 8.0), FVector2D(128.0, -10.0) },
			{ TNBeachTrapKit::SandMark(), TNBeachTrapKit::SandMark(), TNBeachTrapKit::SandTop(), TNBeachTrapKit::SandSide() }, 12, 0.1, Seed);
		if (Kind == 1)
		{
			// Bote de tres pinchos: cilindro corto que asoma, franja y tres pinchos finos en triángulo.
			const double Rc = R * 0.6;
			TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, -30.0), FVector(0.0, 0.0, Top), Rc, Rc * 0.96, 12, L.Body, L.BodyDark, false, true);
			TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, Top - 7.0), FVector(0.0, 0.0, Top - 3.0), Rc * 1.02, Rc, 12, L.Band, L.Band, false, false);
			TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, Top), FVector(0.0, 0.0, Top + 6.0), Rc * 0.3, Rc * 0.25, 8, L.Plate, L.Plate, false, true);
			for (int32 k = 0; k < 3; ++k)
			{
				const double A = TNPlaygroundKit::KitTwoPi * k / 3.0 + 0.4;
				const FVector Foot(FMath::Cos(A) * Rc * 0.5, FMath::Sin(A) * Rc * 0.5, Top);
				const FVector Tip = Foot + FVector(FMath::Cos(A) * 3.0, FMath::Sin(A) * 3.0, 26.0);
				TNPlaygroundKit::AddRod(B, Foot, Tip, 2.4, 5, L.Prong, FVector::ForwardVector);
				TNPlaygroundKit::AddBall(B, Tip, 4.0, 6, L.Prong);
			}
			return;
		}
		// Plato: cuerpo bajo, franja, plato de presión con nervios y el pincho de la espoleta en medio.
		TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, -28.0), FVector(0.0, 0.0, Top - 6.0), R, R * 0.95, 16, L.Body, L.BodyDark, false, true);
		TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, Top - 14.0), FVector(0.0, 0.0, Top - 9.0), R * 0.975, R * 0.965, 16, L.Band, L.Band, false, false);
		TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, Top - 6.0), FVector(0.0, 0.0, Top), R * 0.56, R * 0.5, 14, L.Plate, L.Plate, false, true);
		for (int32 k = 0; k < 6; ++k)
		{
			const FTransform Xf(FQuat(FVector::UpVector, TNPlaygroundKit::KitTwoPi * k / 6.0 + 0.26));
			TNPlaygroundKit::AddXfBox(B, Xf, FVector(R * 0.74, 0.0, Top - 5.0), FVector(R * 0.15, 3.0, 2.5), L.BodyDark);
		}
		TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, Top - 1.0), FVector(0.0, 0.0, Top + 5.0), 9.0, 8.0, 8, L.BodyDark, L.BodyDark, false, true);
		TNPlaygroundKit::AddRod(B, FVector(0.0, 0.0, Top + 4.0), FVector(0.0, 0.0, Top + 22.0), 3.6, 6, L.Prong, FVector::ForwardVector);
		TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, Top + 22.0), FVector(0.0, 0.0, Top + 30.0), 3.6, 0.3, 6, L.Prong, L.Prong, true, false);
		if (Kind == 2)
		{
			// Percebes en la oxidada.
			for (int32 k = 0; k < 5; ++k)
			{
				const double A = TNPlaygroundKit::KitTwoPi * TNBeachTrapKit::Hash01(k, 3, Seed);
				const FVector Base(FMath::Cos(A) * R * 0.8, FMath::Sin(A) * R * 0.8, Top - 7.0);
				TNPlaygroundKit::AddFrustum(B, Base, Base + FVector(0.0, 0.0, 6.0), 5.5, 2.5, 5, TNPlaygroundKit::Rgb(0xD8D0BC), TNPlaygroundKit::Rgb(0x8A8270), false, true);
			}
		}
		else if (Kind == 3)
		{
			// Juguete: botón rojo en la punta y una estrella en el plato.
			TNPlaygroundKit::AddBall(B, FVector(0.0, 0.0, Top + 25.0), 7.0, 8, L.Prong);
			AddFlatStar(B, FVector(R * 0.28, -R * 0.1, Top + 0.8), 11.0, L.Band);
		}
	}

	/** Piloto rojo. */
	void BuildLed(FBuffers& B, const FVector& At)
	{
		TNPlaygroundKit::AddBall(B, At, 4.5, 8, TNPlaygroundKit::Rgb(0xFF2A1A, 0.3f));
	}

	/** Tapa roja del parpadeo: cubre el cuerpo que asoma y la base del pincho. */
	void BuildCover(FBuffers& B, double Top, double CoverR)
	{
		const FLinearColor Red = TNPlaygroundKit::Rgb(0xFF3020, 0.35f);
		TNPlaygroundKit::AddFrustum(B, FVector(0.0, 0.0, Top - 12.0), FVector(0.0, 0.0, Top + 2.0), CoverR, CoverR * 0.62, 16, Red, Red, false, true);
	}

	/** Banderita de aviso a 1,9 m hacia +X (el componente la gira): palo clavado y banderín rojo con franja blanca. */
	void BuildFlag(FBuffers& B)
	{
		const FVector Foot(190.0, 0.0, -20.0);
		const FVector PoleTop = Foot + FVector(0.06, -0.08, 1.0).GetSafeNormal() * 185.0;
		TNPlaygroundKit::AddRod(B, Foot, PoleTop, 3.2, 5, TNPlaygroundKit::Rgb(0xC8A86E), FVector::ForwardVector);
		const FVector FlagDir(FMath::Cos(1.4), FMath::Sin(1.4), 0.0);
		TNPlaygroundKit::AddPennant(B, PoleTop, FlagDir, 52.0, 34.0, TNPlaygroundKit::Rgb(0xE0342E, 0.1f));
		const FVector Side(-FlagDir.Y, FlagDir.X, 0.0);
		for (const double Sgn : { -1.0, 1.0 })
		{
			TNPlaygroundKit::AddPennant(B, PoleTop - FVector(0.0, 0.0, 9.0) + Side * (Sgn * 1.2), FlagDir, 30.0, 10.0, TNPlaygroundKit::Rgb(0xF6F2EA, 0.1f));
		}
		TNBeachTrapKit::AddLatheProfile(B, FVector(Foot.X, Foot.Y, 0.0), { FVector2D(0.0, 9.0), FVector2D(10.0, 7.0), FVector2D(22.0, -6.0) },
			{ TNBeachTrapKit::SandTop(), TNBeachTrapKit::SandSide() }, 7, 0.1, 0x5EEDu);
	}

	/** Cráter: suelo chamuscado, reborde de arena levantada, rayas de quemado y trozos de la carcasa. */
	void BuildCrater(FBuffers& B, const FMineLook& L, uint32 Seed)
	{
		const FLinearColor Scorch = TNPlaygroundKit::Rgb(0x2E2822);
		const FLinearColor Burnt = TNPlaygroundKit::Rgb(0x4A3E32);
		const FLinearColor Singed = TNPlaygroundKit::Rgb(0x8A7050);
		TNBeachTrapKit::AddLatheProfile(B, FVector::ZeroVector, { FVector2D(0.0, 3.0), FVector2D(70.0, 3.0), FVector2D(135.0, 4.0), FVector2D(150.0, 6.0) },
			{ Scorch, Burnt, Burnt }, 12, 0.1, Seed);
		TNBeachTrapKit::AddLatheProfile(B, FVector::ZeroVector,
			{ FVector2D(135.0, 2.0), FVector2D(165.0, 26.0), FVector2D(200.0, 22.0), FVector2D(238.0, 5.0), FVector2D(258.0, -10.0) },
			{ Burnt, Singed, TNBeachTrapKit::SandMark(), TNBeachTrapKit::SandSide() }, 14, 0.14, Seed + 1u);
		for (int32 k = 0; k < 8; ++k)
		{
			const double A = TNPlaygroundKit::KitTwoPi * (k + 0.3 * TNBeachTrapKit::Hash01(k, 9, Seed)) / 8.0;
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.0);
			const FVector Across(-Dir.Y, Dir.X, 0.0);
			const double R0 = 215.0;
			const double R1 = 270.0 + 50.0 * TNBeachTrapKit::Hash01(k, 10, Seed);
			B.AddTri(Dir * R0 + Across * 22.0 + FVector(0.0, 0.0, 6.0), Dir * R0 - Across * 22.0 + FVector(0.0, 0.0, 6.0), Dir * R1 + FVector(0.0, 0.0, 2.0),
				FVector::UpVector, Burnt);
		}
		for (int32 k = 0; k < 7; ++k)
		{
			// Trozos de la carcasa: dentro del cráter y por fuera del reborde.
			const double A = TNPlaygroundKit::KitTwoPi * TNBeachTrapKit::Hash01(k, 20, Seed);
			const bool bInside = k < 3;
			const double Rr = bInside ? 30.0 + 80.0 * TNBeachTrapKit::Hash01(k, 21, Seed) : 245.0 + 75.0 * TNBeachTrapKit::Hash01(k, 21, Seed);
			const FVector Axis(TNBeachTrapKit::Hash01(k, 22, Seed) - 0.5, TNBeachTrapKit::Hash01(k, 23, Seed) - 0.5, 1.0);
			const FTransform Xf(FQuat(Axis.GetSafeNormal(), TNPlaygroundKit::KitTwoPi * TNBeachTrapKit::Hash01(k, 24, Seed)), FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, bInside ? 8.0 : 5.0));
			TNPlaygroundKit::AddXfBox(B, Xf, FVector::ZeroVector, FVector(14.0, 9.0, 3.0), (k % 2) ? L.Body : L.BodyDark);
		}
		// El pincho, doblado y tirado.
		const double Ap = TNPlaygroundKit::KitTwoPi * TNBeachTrapKit::Hash01(30, 1, Seed);
		const FVector Bend(FMath::Cos(Ap) * 280.0, FMath::Sin(Ap) * 280.0, 4.0);
		TNPlaygroundKit::AddRod(B, Bend, Bend + FVector(FMath::Cos(Ap + 0.5), FMath::Sin(Ap + 0.5), 0.0) * 14.0, 3.6, 6, L.Prong, FVector::UpVector);
		TNPlaygroundKit::AddRod(B, Bend, Bend + FVector(FMath::Cos(Ap - 0.9), FMath::Sin(Ap - 0.9), 0.1) * 12.0, 3.6, 6, L.Prong, FVector::UpVector);
	}

	/** Piezas de la mina en la caché (con el aspecto en los bits bajos). */
	enum class EMinePart : uint32
	{
		Body,
		Led,
		Cover,
		Flag,
		Crater,
		Flash
	};

	/**
	 * Malla compartida de una pieza y aspecto: se construye la primera vez que se pide (en el paquete transitorio,
	 * RF_Transient | RF_DuplicateTransient) y la mantienen viva los componentes que la usan; si el recolector la suelta
	 * entre rondas, se vuelve a construir.
	 */
	UStaticMesh* SharedMesh(EMinePart Part, int32 Kind, TFunctionRef<void(FBuffers&)> Build)
	{
		static TMap<uint32, TWeakObjectPtr<UStaticMesh>> Cache;
		TWeakObjectPtr<UStaticMesh>& Entry = Cache.FindOrAdd((static_cast<uint32>(Part) << 4) | (static_cast<uint32>(Kind) & 0xFu));
		if (UStaticMesh* Found = Entry.Get())
		{
			return Found;
		}
		FBuffers B;
		Build(B);
		UStaticMesh* Mesh = B.IsEmpty() ? nullptr : TNPlaygroundKit::BuildMesh(GetTransientPackage(), B, TNPlaygroundKit::VertexColorMaterial());
		Entry = Mesh;
		return Mesh;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// ATN_BeachMine
// ─────────────────────────────────────────────────────────────────────────────

ATN_BeachMine::ATN_BeachMine()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.f);

	MineMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MineMesh"));
	MineMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(MineMesh);

	LedMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LedMesh"));
	LedMesh->SetupAttachment(MineMesh);
	TNBeachTrapKit::ConfigureVisual(LedMesh);
	LedMesh->SetCastShadow(false);

	BlinkMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BlinkMesh"));
	BlinkMesh->SetupAttachment(MineMesh);
	TNBeachTrapKit::ConfigureVisual(BlinkMesh);
	BlinkMesh->SetCastShadow(false);
	BlinkMesh->SetVisibility(false);

	FlagMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlagMesh"));
	FlagMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(FlagMesh);

	CraterMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CraterMesh"));
	CraterMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(CraterMesh);
	CraterMesh->SetVisibility(false);

	FlashMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlashMesh"));
	FlashMesh->SetupAttachment(GetRootComponent());
	TNBeachTrapKit::ConfigureVisual(FlashMesh);
	FlashMesh->SetCastShadow(false);
	FlashMesh->SetVisibility(false);

	FlashLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FlashLight"));
	FlashLight->SetupAttachment(GetRootComponent());
	FlashLight->SetRelativeLocation(FVector(0.0, 0.0, 120.0));
	FlashLight->SetIntensityUnits(ELightUnits::Lumens);
	FlashLight->SetIntensity(0.f);
	FlashLight->SetAttenuationRadius(1600.f);
	FlashLight->SetLightColor(FLinearColor(1.f, 0.62f, 0.3f));
	FlashLight->SetCastShadows(false);
	FlashLight->SetVisibility(false);
}

void ATN_BeachMine::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_BeachMine, TriggeredAt);
	DOREPLIFETIME(ATN_BeachMine, ExplodedAt);
}

bool ATN_BeachMine::IsArmedAt(double Now) const
{
	if (TriggeredAt > ExplodedAt)
	{
		return false;
	}
	if (ExplodedAt < 0.f)
	{
		return true;
	}
	return RearmSeconds > 0.f && Now >= static_cast<double>(ExplodedAt) + RearmSeconds;
}

bool ATN_BeachMine::IsTickBusy() const
{
	return TNBeachMineTick::IsBusy(TriggeredAt, ExplodedAt, RearmSeconds, TNBeachTrapKit::ServerNow(GetWorld()));
}

void ATN_BeachMine::OnTickWakeChanged(bool bAwake)
{
	// Dormida, el piloto se queda apagado (no encendido a medio destello).
	if (!bAwake && LedMesh && LedMesh->IsVisible())
	{
		LedMesh->SetVisibility(false);
	}
}

void ATN_BeachMine::WakeForNews()
{
	// Dormida lejos y con noticias del servidor (pisada, explosión o el estado al llegar): un fotograma al menos para
	// dibujarlo; si no hay nadie cerca ni nada en marcha, el subsistema la vuelve a dormir.
	if (bHasScreen && !IsActorTickEnabled())
	{
		SetActorTickEnabled(true);
	}
}

void ATN_BeachMine::ApplySpec()
{
	using namespace TNBeachMineDetail;
	SizeK = FMath::Clamp(static_cast<double>(Spec.SizeScale), 0.5, 1.6);
	CapRadius = 70.0 * SizeK;
	const uint32 MineSeed = TNBeachTrapKit::SeedOf(Spec.Seed, 31u);
	const int32 Kind = static_cast<int32>(MineSeed % 4u);
	double Top1 = 22.0;
	FVector LedAt = FVector::ZeroVector;
	double CoverR = 70.0;
	MineSpots(Kind, Top1, LedAt, CoverR);
	CapTop = Top1 * SizeK;
	LedPhase = static_cast<float>(TNBeachTrapKit::Hash01(4, 4, MineSeed));
	const UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_DedicatedServer)
	{
		// Sin pantalla no hace falta ninguna malla: la mina no tiene colisión.
		return;
	}
	// Las mallas son de SizeScale 1 y compartidas: cada mina escala sus componentes. Piezas de arte (Docs/Arte_Assets.md) salvo el
	// destello de la explosión, que es un efecto.
	const FMineLook Look = LookOf(Kind);
	const uint32 KindSeed = 0x51u + static_cast<uint32>(Kind) * 977u;
	const FVector Scale3(SizeK);
	TNArt::SetMesh(MineMesh, SharedMesh(EMinePart::Body, Kind, [Kind, &Look, KindSeed](FBuffers& B) { BuildMine(B, Kind, Look, KindSeed); }), TN_ART("Beach.Mine.Body"));
	MineMesh->SetRelativeScale3D(Scale3);
	TNArt::SetMesh(LedMesh, SharedMesh(EMinePart::Led, Kind, [&LedAt](FBuffers& B) { BuildLed(B, LedAt); }), TN_ART("Beach.Mine.Led"));
	TNArt::SetMesh(BlinkMesh, SharedMesh(EMinePart::Cover, Kind, [Top1, CoverR](FBuffers& B) { BuildCover(B, Top1, CoverR); }), TN_ART("Beach.Mine.Cover"));
	// Banderita de aviso en algo menos de la mitad, girada con la semilla.
	const bool bFlag = TNBeachTrapKit::Hash01(9, 1, MineSeed) < 0.45;
	TNArt::SetMesh(FlagMesh, bFlag ? SharedMesh(EMinePart::Flag, 0, [](FBuffers& B) { BuildFlag(B); }) : nullptr, TN_ART("Beach.Mine.Flag"));
	FlagMesh->SetRelativeTransform(FTransform(FQuat(FVector::UpVector, TNPlaygroundKit::KitTwoPi * TNBeachTrapKit::Hash01(1, 2, MineSeed)), FVector::ZeroVector, Scale3));
	TNArt::SetMesh(CraterMesh, SharedMesh(EMinePart::Crater, Kind, [&Look, KindSeed](FBuffers& B) { BuildCrater(B, Look, KindSeed + 17u); }), TN_ART("Beach.Mine.Crater"));
	CraterMesh->SetRelativeTransform(FTransform(FQuat(FVector::UpVector, TNPlaygroundKit::KitTwoPi * TNBeachTrapKit::Hash01(2, 3, MineSeed)), FVector::ZeroVector, Scale3));
	CraterMesh->SetVisibility(ExplodedAt >= 0.f);
	FlashMesh->SetStaticMesh(SharedMesh(EMinePart::Flash, 0, [](FBuffers& B) { TNPlaygroundKit::AddBall(B, FVector::ZeroVector, 100.0, 10, TNPlaygroundKit::Rgb(0xFFF0A0)); }));
	FlashMesh->SetRelativeLocation(FVector(0.0, 0.0, CapTop));
	UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Mina %s: aspecto %d, tapa a %.0f cm, radio %.0f cm, %s bandera."), *GetName(), Kind, CapTop, CapRadius,
		bFlag ? TEXT("con") : TEXT("sin"));
}

void ATN_BeachMine::BeginPlay()
{
	Super::BeginPlay();
	const UWorld* World = GetWorld();
	bHasScreen = World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;
	// Lo que ya pasó antes de llegar (pisada o explosión) no se oye: se ve el estado.
	BeepsPlayed = 1000;
	if (!bHasScreen)
	{
		return;
	}
	Voice = UTN_BeachMineSynthComponent::AttachTo(this, GetActorLocation() + FVector(0.0, 0.0, 40.0), 1500.f, 10000.f);
}

void ATN_BeachMine::EnsureFX()
{
	// Las partículas se crean con la primera explosión: la mayoría de las minas de la ronda no llegan a explotar.
	if (bFXReady || !bHasScreen)
	{
		return;
	}
	bFXReady = true;
	Fire.Init(this, ETNTrapBurstShape::Blob, TNPlaygroundKit::Rgb(0xFFA23A), 24);
	Fire.SetMotion(150.f, 5.f, 150.f, 30.f, 0.16f, 0.32f);
	Dust.Init(this, ETNTrapBurstShape::Blob, TNPlaygroundKit::Rgb(0xE2C890), 48);
	Dust.SetMotion(-250.f, 2.6f, 90.f, 280.f, 0.9f, 1.8f);
	Clods.Init(this, ETNTrapBurstShape::Blob, TNPlaygroundKit::Rgb(0xC9A76C), 32);
	Clods.SetMotion(-1500.f, 0.35f, 32.f, 18.f, 0.9f, 1.5f);
	Chips.Init(this, ETNTrapBurstShape::Chip, TNPlaygroundKit::Rgb(0x55602D, 0.1f), 12);
	Chips.SetMotion(-1300.f, 0.5f, 38.f, 24.f, 1.f, 1.6f);
	Smoke.Init(this, ETNTrapBurstShape::Blob, TNPlaygroundKit::Rgb(0x5E5852), 14);
	Smoke.SetMotion(80.f, 0.9f, 80.f, 260.f, 2.2f, 3.6f);
}

void ATN_BeachMine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (HasAuthority())
	{
		const double ServerTime = TNBeachTrapKit::ServerNow(World);
		if (TriggeredAt > ExplodedAt)
		{
			if (ServerTime >= static_cast<double>(TriggeredAt) + FuseSeconds)
			{
				Explode(ServerTime);
			}
		}
		else if (IsArmedAt(ServerTime))
		{
			CheckStep(ServerTime);
		}
	}
	if (bHasScreen)
	{
		UpdateVisuals(Clock.Advance(World, DeltaSeconds), DeltaSeconds);
	}
}

void ATN_BeachMine::CheckStep(double Now)
{
	const FTransform ActorXf = GetActorTransform();
	// Solo las que están al alcance de la tapa (en planta, con margen para la cápsula y la escala del actor).
	TArray<ACharacter*> Near;
	TNBeachNearby::Gather(GetWorld(), ActorXf.GetLocation(), (CapRadius + TNBeachMineDetail::StepReachMargin) * ActorXf.GetMaximumAxisScale(), Near);
	for (ACharacter* Walker : Near)
	{
		ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Walker);
		if (!TNBeachTrapKit::IsFreeTurtle(Turtle))
		{
			continue;
		}
		const UCapsuleComponent* Capsule = Turtle->GetCapsuleComponent();
		if (!Capsule)
		{
			continue;
		}
		// Los pies sobre la tapa (o cayendo encima): cerca en planta y a la altura de la tapa.
		const FVector Local = ActorXf.InverseTransformPosition(Capsule->GetComponentLocation());
		const double Reach = CapRadius + Capsule->GetScaledCapsuleRadius() * 0.45;
		const double Feet = Local.Z - Capsule->GetScaledCapsuleHalfHeight();
		if (FVector2D(Local.X, Local.Y).SizeSquared() > Reach * Reach || Feet > CapTop + 45.0 || Feet < -60.0)
		{
			continue;
		}
		TriggeredAt = static_cast<float>(Now);
		Stepper = Turtle;
		ForceNetUpdate();
		// En el servidor escucha OnRep no corre solo: clic y parpadeo del anfitrión.
		OnRep_TriggeredAt();
		UE_LOG(LogTortunabo, Verbose, TEXT("[Playa] Mina %s: la pisa %s."), *GetName(), *Turtle->GetName());
		return;
	}
}

bool ATN_BeachMine::TriggerForTest()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return false;
	}
	const double Now = TNBeachTrapKit::ServerNow(World);
	if (!IsArmedAt(Now))
	{
		return false;
	}
	TriggeredAt = static_cast<float>(Now);
	ForceNetUpdate();
	OnRep_TriggeredAt();
	return true;
}

void ATN_BeachMine::Explode(double Now)
{
	UWorld* World = GetWorld();
	ExplodedAt = static_cast<float>(Now);
	ForceNetUpdate();
	const FVector Center = GetActorTransform().TransformPosition(FVector(0.0, 0.0, CapTop * 0.5));
	const FVector Back = GetBackDirection();
	const FVector Across(-Back.Y, Back.X, 0.0);
	const double Blast = BlastRadius * SizeK;
	const double Reach = FMath::Max(Blast + 50.0, static_cast<double>(PushRadius) * SizeK);
	const ATortugaCharacter* StepperTurtle = Stepper.Get();
	int32 Launched = 0;
	int32 Shoved = 0;
	for (TActorIterator<ATortugaCharacter> It(World); It; ++It)
	{
		ATortugaCharacter* Turtle = *It;
		// Ni a la que sujeta un enemigo (la bola y su boca se pelearían por ella) ni a la que se come un gusano.
		if (!IsValid(Turtle) || Turtle->IsDead() || ATN_BeachEnemy::IsTurtleHeld(Turtle) || ATN_BeachSandWorm::IsBeingEaten(Turtle))
		{
			continue;
		}
		const FVector Rel = Turtle->GetActorLocation() - Center;
		const double Dist = FVector2D(Rel.X, Rel.Y).Size();
		if (Dist > Reach || FMath::Abs(Rel.Z) > 500.0)
		{
			continue;
		}
		if (Dist <= Blast || (Turtle == StepperTurtle && Dist <= Blast * 2.0))
		{
			// En bola hacia atrás y arriba, con algo de lado según dónde estuviera (si la pisan dos, no caen juntas).
			const double SideOff = FMath::Clamp(FVector::DotProduct(Rel, Across) / FMath::Max(1.0, Blast), -1.0, 1.0);
			const FVector Launch = Back * LaunchBack + Across * (SideOff * 160.0) + FVector::UpVector * LaunchUp;
			TNBeach::StunTurtle(Turtle, StunSeconds, Launch);
			++Launched;
		}
		else if (TNBeachTrapKit::IsFreeTurtle(Turtle))
		{
			// Empujón hacia fuera (algo hacia atrás: a quien va delante no le da un acelerón), más flojo cuanto más lejos.
			const double Near = 1.0 - FMath::Clamp((Dist - Blast) / FMath::Max(1.0, Reach - Blast), 0.0, 1.0);
			FVector Away = FVector(Rel.X, Rel.Y, 0.0).GetSafeNormal();
			Away = (Away.IsNearlyZero() ? Back : (Away + Back * 0.6)).GetSafeNormal2D();
			// Lo decide el servidor y, si la mueve un cliente, lo estrena su dueño en su siguiente movimiento y el servidor en ese
			// mismo (LaunchFromServer): sin corrección (#18). Un LaunchCharacter del servidor le llegaba al dueño como
			// corrección de 12 a 85 cm; repetirlo en el dueño al llegar un multicast lo empujaba dos veces.
			const FVector Push = Away * (PushSpeed * (0.35 + 0.65 * Near)) + FVector::UpVector * (PushUp * (0.4 + 0.6 * Near));
			UTN_TurtleMovementComponent::LaunchFromServer(Turtle, Push);
			++Shoved;
		}
	}
	Stepper.Reset();
	// Explosión del anfitrión (en el servidor escucha OnRep no corre solo).
	OnRep_ExplodedAt();
	UE_LOG(LogTortunabo, Log, TEXT("[Playa] Mina %s: explota (%d en bola, %d empujadas)."), *GetName(), Launched, Shoved);
}

FVector ATN_BeachMine::GetBackDirection() const
{
	FVector Sea = FVector::ZeroVector;
	if (Sea.IsNearlyZero())
	{
		Sea = GetActorForwardVector();
		Sea.Z = 0.0;
	}
	const FVector Back = -Sea.GetSafeNormal();
	return Back.IsNearlyZero() ? FVector(-1.0, 0.0, 0.0) : Back;
}

void ATN_BeachMine::OnRep_TriggeredAt()
{
	WakeForNews();
	if (!bHasScreen || TriggeredAt < 0.f)
	{
		return;
	}
	const double Now = TNBeachTrapKit::ServerNow(GetWorld());
	if (Now - TriggeredAt > FuseSeconds + 0.3f)
	{
		// Llega tarde: sin clic ni pitidos.
		BeepsPlayed = 1000;
		return;
	}
	BeepsPlayed = 0;
	PlayClickFX();
}

void ATN_BeachMine::OnRep_ExplodedAt()
{
	WakeForNews();
	if (!bHasScreen || ExplodedAt < 0.f)
	{
		return;
	}
	const double Now = TNBeachTrapKit::ServerNow(GetWorld());
	if (Now - ExplodedAt > 1.5)
	{
		return;
	}
	PlayExplosionFX();
}

void ATN_BeachMine::PlayClickFX()
{
	const FVector CapAt = GetActorTransform().TransformPosition(FVector(0.0, 0.0, CapTop));
	if (Voice)
	{
		Voice->Play(ETNBeachMineSound::Click, FMath::FRandRange(0.95f, 1.08f), 1.f);
	}
	ClickText.Show(this, NSLOCTEXT("TNBeach", "MineClick", "¡clic!"), FColor(255, 250, 235), CapAt + FVector(0.0, 0.0, 170.0), 75.f);
}

void ATN_BeachMine::PlayExplosionFX()
{
	EnsureFX();
	const float K = static_cast<float>(SizeK);
	const FVector At = GetActorTransform().TransformPosition(FVector(0.0, 0.0, CapTop * 0.6));
	FlashAge = 0.f;
	bRearmFXPending = RearmSeconds > 0.f;
	Fire.Burst(At, 18, FVector::UpVector, 1000.f * K, 1.35f, 30.f * K);
	Dust.Burst(At, 34, FVector::UpVector, 700.f * K, 1.25f, 90.f * K);
	Clods.Burst(At, 24, FVector::UpVector, 1350.f * K, 0.8f, 40.f * K);
	Chips.Burst(At, 10, FVector::UpVector, 1200.f * K, 1.f, 20.f * K);
	Smoke.Burst(At + FVector(0.0, 0.0, 60.0), 12, FVector::UpVector, 180.f, 0.7f, 110.f * K);
	BoomText.Show(this, NSLOCTEXT("TNBeach", "MineBoom", "¡BUM!"), FColor(255, 140, 40), At + FVector(0.0, 0.0, 260.0), 220.f);
	if (Voice)
	{
		Voice->Play(ETNBeachMineSound::Boom, FMath::FRandRange(0.92f, 1.08f), 1.f);
		Voice->Play(ETNBeachMineSound::Debris, FMath::FRandRange(0.9f, 1.1f), 0.8f);
	}
	UTN_BeachCameraShake::Kick(this, At, 0.8f, 700.f, 3200.f);
}

void ATN_BeachMine::PlayRearmFX()
{
	EnsureFX();
	const FVector At = GetActorTransform().TransformPosition(FVector(0.0, 0.0, CapTop));
	Dust.Burst(At, 8, FVector::UpVector, 260.f, 1.2f, 60.f * static_cast<float>(SizeK));
	if (Voice)
	{
		Voice->Play(ETNBeachMineSound::Rearm, 1.f, 0.9f);
	}
}

void ATN_BeachMine::UpdateVisuals(double Now, float DeltaSeconds)
{
	const bool bFuse = TriggeredAt > ExplodedAt;
	const bool bBlown = ExplodedAt >= 0.f && !bFuse;
	// Cuánto asoma: 1 armada; 0 volada; subiendo en los últimos 0,6 s antes de rearmarse.
	double Rise = 1.0;
	if (bBlown)
	{
		Rise = RearmSeconds > 0.f ? 1.0 - FMath::Clamp((static_cast<double>(ExplodedAt) + RearmSeconds - Now) / 0.6, 0.0, 1.0) : 0.0;
	}
	const bool bShown = Rise > 0.001;
	if (MineMesh->IsVisible() != bShown)
	{
		MineMesh->SetVisibility(bShown);
	}
	// Hundida con el clic; al rearmarse sube desde la arena con un botecito.
	double Z = 0.0;
	if (bFuse)
	{
		Z = -5.0 * SizeK;
	}
	else if (Rise < 1.0)
	{
		const double Eased = FMath::InterpEaseOut(0.0, 1.0, static_cast<float>(Rise), 2.f);
		Z = -(CapTop + 45.0 * SizeK) * (1.0 - Eased) + 6.0 * SizeK * FMath::Sin(Rise * UE_DOUBLE_PI);
	}
	if (!FMath::IsNearlyEqual(MineMesh->GetRelativeLocation().Z, Z, 0.05))
	{
		MineMesh->SetRelativeLocation(FVector(0.0, 0.0, Z));
	}

	bool bBlinkOn = false;
	bool bLedOn = false;
	if (bFuse)
	{
		// Parpadeo y pitidos cada vez más deprisa (de 5 a 17 por segundo con la mecha de 0,4 s).
		const double T = FMath::Max(0.0, Now - static_cast<double>(TriggeredAt));
		const double Fuse = FMath::Max(0.05, static_cast<double>(FuseSeconds));
		const double Phase = 5.0 * T + 6.0 * T * T / Fuse;
		bBlinkOn = FMath::Frac(Phase) < 0.5;
		bLedOn = true;
		const int32 Cycle = FMath::FloorToInt32(Phase);
		if (Cycle >= BeepsPlayed && T < Fuse + 0.05)
		{
			BeepsPlayed = Cycle + 1;
			if (Voice)
			{
				Voice->Play(ETNBeachMineSound::Beep, 1.f + 0.45f * static_cast<float>(FMath::Min(1.0, T / Fuse)), 0.9f);
			}
		}
	}
	else if (Rise >= 1.0)
	{
		// Armada: el piloto da un destello corto cada 1,6 s.
		bLedOn = FMath::Frac(Now / 1.6 + LedPhase) < 0.08;
	}
	if (BlinkMesh->IsVisible() != bBlinkOn)
	{
		BlinkMesh->SetVisibility(bBlinkOn);
	}
	const bool bLedShown = bLedOn && bShown;
	if (LedMesh->IsVisible() != bLedShown)
	{
		LedMesh->SetVisibility(bLedShown);
	}
	const bool bCrater = ExplodedAt >= 0.f;
	if (CraterMesh->IsVisible() != bCrater)
	{
		CraterMesh->SetVisibility(bCrater);
	}
	if (bRearmFXPending && bBlown && Rise >= 1.0)
	{
		bRearmFXPending = false;
		PlayRearmFX();
	}

	// Fogonazo: la bola crece en 0,07 s y se apaga en 0,16; la luz, en 0,28.
	if (FlashAge < 1.f)
	{
		FlashAge += DeltaSeconds;
		const float Grow = FlashAge < 0.07f ? FlashAge / 0.07f : FMath::Max(0.f, 1.f - (FlashAge - 0.07f) / 0.16f);
		const bool bFlashOn = Grow > 0.01f;
		if (FlashMesh->IsVisible() != bFlashOn)
		{
			FlashMesh->SetVisibility(bFlashOn);
		}
		if (bFlashOn)
		{
			FlashMesh->SetRelativeScale3D(FVector(2.4 * SizeK * Grow));
		}
		const float Glow = FMath::Square(FMath::Max(0.f, 1.f - FlashAge / 0.28f));
		const bool bLit = Glow > 0.001f;
		if (FlashLight->IsVisible() != bLit)
		{
			FlashLight->SetVisibility(bLit);
		}
		if (bLit)
		{
			FlashLight->SetIntensity(60000.f * Glow);
		}
	}
	if (bFXReady)
	{
		Fire.Tick(DeltaSeconds);
		Dust.Tick(DeltaSeconds);
		Clods.Tick(DeltaSeconds);
		Chips.Tick(DeltaSeconds);
		Smoke.Tick(DeltaSeconds);
	}
	ClickText.Tick(DeltaSeconds, GetWorld());
	BoomText.Tick(DeltaSeconds, GetWorld());
}
