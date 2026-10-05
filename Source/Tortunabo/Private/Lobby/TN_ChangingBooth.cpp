#include "Lobby/TN_ChangingBooth.h"
#include "Art/TN_Art.h"
#include "Core/TN_Log.h"
#include "Player/MP_GamePlayerController.h"
#include "Player/TortugaCharacter.h"
#include "Settings/TN_LanguageSettings.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "../World/ProcMap/TN_ProcMapRuntimeMesh.h"

namespace TNBoothDetail
{
	/**
	 * Media botella boca abajo: la mitad del culo, cortada y puesta sobre el corte. El culo hace de techo y la puerta
	 * es el tapón (una chapa de corona). La puerta mira a +X; su centro y su radio se miden sobre la pared (arco y
	 * altura).
	 */
	constexpr double WallR = 150.0;
	constexpr double WallTop = 305.0;
	constexpr double DoorR = 88.0;
	constexpr double DoorZ = 114.0;
	/** Cara de la chapa, por fuera de la pared (la falda rizada va de la cara a la pared). */
	constexpr double CapFaceR = WallR + 9.0;
	constexpr int32 Seg = 40;
	constexpr int32 CapFlutes = 21;
	/** Etiqueta de refresco alrededor de la botella, por encima de la puerta. */
	constexpr double LabelBottom = 244.0;
	constexpr double LabelTop = 288.0;
	/** Suelo propio de tarima (tablones y alfombra redonda): tapa el suelo del nivel, sea cual sea. */
	constexpr double FloorTop = 6.0;
	/** Culo petaloide de las botellas de plástico de litro y medio: cinco pies redondos y valles estrechos entre ellos. */
	constexpr int32 Feet = 5;
	constexpr double FootHeight = 70.0;
	constexpr double ValleyHeight = 30.0;
	constexpr float DoorOpenYaw = -108.f;
	constexpr float HopDelay = 0.4f;

	FLinearColor Pal(uint32 Hex)
	{
		return FLinearColor(TNProcRuntimeMesh::SRGBToLinear(((Hex >> 16) & 255) / 255.f), TNProcRuntimeMesh::SRGBToLinear(((Hex >> 8) & 255) / 255.f),
			TNProcRuntimeMesh::SRGBToLinear((Hex & 255) / 255.f), 1.f);
	}

	/** Punto de un cilindro de radio R: S = arco desde el centro de la puerta (+X), Z = altura. */
	FVector OnCylinder(double R, double S, double Z)
	{
		const double A = S / R;
		return FVector(R * FMath::Cos(A), R * FMath::Sin(A), Z);
	}

	FVector RadialOut(const FVector& P)
	{
		return FVector(P.X, P.Y, 0.0).GetSafeNormal();
	}

	/**
	 * Altura del techo sobre WallTop. U: 1 en el borde y 0 en el centro; Theta: ángulo alrededor del eje (un pie
	 * centrado sobre la puerta). Los pies suben casi en vertical desde el borde, se redondean arriba y bajan hacia el
	 * centro, donde se juntan con los valles.
	 */
	double RoofHeight(double UIn, double Theta)
	{
		const double U = FMath::Clamp(UIn, 0.0, 1.0);
		const double Valley = ValleyHeight * (1.0 - FMath::Pow(U, 2.2));
		double Foot = 0.0;
		if (U >= 0.55) { Foot = FMath::Pow(FMath::Sin((1.0 - U) / 0.45 * UE_DOUBLE_HALF_PI), 0.6); }
		else { const double T = (0.55 - U) / 0.55; Foot = 1.0 - 0.58 * T * T; }
		Foot *= FootHeight;
		// W = 1 en el centro del pie y 0 en el fondo del valle (pies anchos, valles estrechos).
		const double V = 0.5 * (1.0 - FMath::Cos(Feet * Theta));
		const double W = 1.0 - V * V * V;
		return Valley + W * FMath::Max(0.0, Foot - Valley);
	}

	/** Bisagra de la chapa (a la izquierda vista desde fuera, por fuera de la falda): el giro negativo la abre. */
	FVector HingePoint()
	{
		const FVector H = OnCylinder(CapFaceR, -DoorR - 10.0, 0.0);
		return FVector(H.X, H.Y, 0.0);
	}
}

ATN_ChangingBooth::ATN_ChangingBooth()
{
	using namespace TNBoothDetail;
	PrimaryActorTick.bCanEverTick = true;
	NetDormancy = DORM_Awake;
	bAlwaysRelevant = true;
	CooldownSeconds = 0.8f;
	PromptText = NSLOCTEXT("Tortunabo", "BoothPrompt", "Entrar al probador");

	// Hitbox de interacción (invisible) delante de la puerta.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Mesh && Cylinder.Succeeded())
	{
		Mesh->SetStaticMesh(Cylinder.Object);
		Mesh->SetRelativeLocation(FVector(WallR + 50.0, 0.0, 60.0));
		Mesh->SetRelativeScale3D(FVector(1.6f, 1.6f, 1.2f));
		// Tampoco se ve en el editor: el nivel se enseña tal cual se juega.
		Mesh->SetVisibility(false);
		Mesh->SetHiddenInGame(true);
	}
	if (PromptWidgetComponent)
	{
		PromptWidgetComponent->SetUsingAbsoluteScale(true);
		PromptWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, 170.f));
	}

	Bottle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bottle"));
	Bottle->SetupAttachment(SceneRoot);
	Bottle->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	DoorHinge = CreateDefaultSubobject<USceneComponent>(TEXT("DoorHinge"));
	DoorHinge->SetupAttachment(Bottle);
	DoorHinge->SetRelativeLocation(HingePoint());

	Door = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Door"));
	Door->SetupAttachment(DoorHinge);
	Door->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Paredes: cuatro cajas giradas 45° forman un octógono de la altura de la botella (una cápsula tan ancha se
	// estrecharía abajo y dejaría meter los pies).
	for (int32 k = 0; k < 4; ++k)
	{
		UBoxComponent* Side = CreateDefaultSubobject<UBoxComponent>(*FString::Printf(TEXT("Walls%d"), k));
		Side->SetupAttachment(SceneRoot);
		Side->InitBoxExtent(FVector(WallR, WallR * 0.4142, (WallTop + 60.0) * 0.5));
		Side->SetRelativeLocation(FVector(0.0, 0.0, (WallTop + 60.0) * 0.5));
		Side->SetRelativeRotation(FRotator(0.0, 45.0 * k, 0.0));
		Side->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
		Side->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Walls.Add(Side);
	}

	// "La cámara se aleja": vista desde fuera, de tres cuartos, con la botella entera.
	ViewCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ViewCamera"));
	ViewCamera->SetupAttachment(SceneRoot);
	const FVector CamPos(780.0, -350.0, 300.0);
	ViewCamera->SetRelativeLocation(CamPos);
	ViewCamera->SetRelativeRotation((FVector(30.0, 0.0, 195.0) - CamPos).Rotation());
	ViewCamera->SetFieldOfView(62.f);
}

void ATN_ChangingBooth::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATN_ChangingBooth, Occupant);
}

void ATN_ChangingBooth::BeginPlay()
{
	Super::BeginPlay();
	BuildMeshes();
	BuildLabel();
	HideBlockout();
	LanguageHandle = TNLanguage::OnApplied().AddUObject(this, &ATN_ChangingBooth::HandleLanguageApplied);
}

void ATN_ChangingBooth::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TNLanguage::OnApplied().Remove(LanguageHandle);
	LanguageHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

void ATN_ChangingBooth::HandleLanguageApplied()
{
	BuildLabel();
}

void ATN_ChangingBooth::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildMeshes();
	BuildLabel();
}

void ATN_ChangingBooth::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
#if WITH_EDITOR
	// Al abrir el nivel en el editor, la botella (malla transitoria) llega vacía: se rehace para verla sin jugar.
	if (!IsTemplate() && GetWorld() && GetWorld()->WorldType == EWorldType::Editor && Bottle && !Bottle->GetStaticMesh())
	{
		BuildMeshes();
		BuildLabel();
	}
#endif
}

bool ATN_ChangingBooth::CanInteract(APawn* Interactor) const
{
	return Occupant == nullptr && Super::CanInteract(Interactor);
}

FVector ATN_ChangingBooth::GetInteractionPoint() const
{
	using namespace TNBoothDetail;
	return GetActorTransform().TransformPosition(FVector(WallR + 60.0, 0.0, 0.0));
}

void ATN_ChangingBooth::BuildMeshes()
{
	using namespace TNBoothDetail;
	UMaterialInterface* VertexColorMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Cosmetics/Materials/M_CosmeticVertexColor.M_CosmeticVertexColor"));
	const FLinearColor Glass = Pal(0x6CCDB5);
	const FLinearColor GlassDark = Pal(0x3E9C88);
	const FLinearColor GlassLight = Pal(0xB4F0E0);
	const FLinearColor GlassEdge = Pal(0xCFF8EC);

	TNProcMesh::FTNProcMeshBuffers B;
	// Quad con normal por vértice: el vidrio curvo (pared, techo y etiqueta) se ve liso, sin facetas. Se orienta como
	// AddTri, con la cara frontal hacia la media de las normales.
	auto SmoothQuad = [&B](const FVector (&P)[4], const FVector (&N)[4], const FLinearColor& Color)
	{
		const int32 Base = B.Verts.Num();
		for (int32 j = 0; j < 4; ++j)
		{
			B.Verts.Add(P[j]);
			B.Normals.Add(N[j]);
			B.UVs.Add(FVector2D(P[j].X + P[j].Z, P[j].Y + P[j].Z) / 400.0);
			B.Colors.Add(Color);
		}
		const FVector Hint = N[0] + N[1] + N[2] + N[3];
		auto Tri = [&B, Base, &Hint](int32 A, int32 Bi, int32 C)
		{
			const FVector Face = FVector::CrossProduct(B.Verts[Base + Bi] - B.Verts[Base + A], B.Verts[Base + C] - B.Verts[Base + A]);
			if (Face.SizeSquared() < 1e-6) { return; }
			const bool bFlip = FVector::DotProduct(Face, Hint) < 0.0;
			B.Tris.Add(Base + A);
			B.Tris.Add(Base + (bFlip ? Bi : C));
			B.Tris.Add(Base + (bFlip ? C : Bi));
		};
		Tri(0, 1, 2);
		Tri(0, 2, 3);
	};
	// Vidrio: por fuera con su color y por dentro, más oscuro, con las normales al revés.
	auto GlassQuad = [&SmoothQuad, GlassDark](const FVector (&P)[4], const FVector (&N)[4], const FLinearColor& Color)
	{
		SmoothQuad(P, N, Color);
		const FVector In[4] = { -N[0], -N[1], -N[2], -N[3] };
		SmoothQuad(P, In, GlassDark * 0.8f);
	};

	// ── Media botella: la pared recta, subdividida para recortar el hueco de la puerta.
	TArray<FVector2D> Profile = { FVector2D(WallR, 12.0) };
	for (int32 i = 1; i <= 14; ++i) { Profile.Add(FVector2D(WallR, 12.0 + (WallTop - 12.0) * i / 14.0)); }
	auto RingPoint = [](const FVector2D& P, int32 K) { const double A = TNProcMap::TwoPi * K / Seg; return FVector(P.X * FMath::Cos(A), P.X * FMath::Sin(A), P.Y); };
	for (int32 i = 0; i + 1 < Profile.Num(); ++i)
	{
		for (int32 k = 0; k < Seg; ++k)
		{
			const FVector Q[4] = { RingPoint(Profile[i], k), RingPoint(Profile[i], k + 1), RingPoint(Profile[i + 1], k + 1), RingPoint(Profile[i + 1], k) };
			const FVector Mid = (Q[0] + Q[1] + Q[2] + Q[3]) * 0.25;
			double Ang = TNProcMap::TwoPi * (k + 0.5) / Seg;
			if (Ang > PI) { Ang -= TNProcMap::TwoPi; }
			const double Arc = Ang * FVector(Mid.X, Mid.Y, 0.0).Size();
			if (FMath::Square(Arc) + FMath::Square(Mid.Z - DoorZ) < FMath::Square(DoorR - 4.0)) { continue; }
			FLinearColor Col = Glass;
			if (k % 8 == 2 || k % 8 == 3) { Col = TNProcMesh::TNProcLerpColor(Col, GlassLight, 0.55f); }
			const FVector N[4] = { RadialOut(Q[0]), RadialOut(Q[1]), RadialOut(Q[2]), RadialOut(Q[3]) };
			GlassQuad(Q, N, Col);
		}
	}
	// ── Techo: el culo petaloide (cinco pies y sus valles) en una rejilla polar, con más anillos cerca del borde,
	// donde el pie sube casi en vertical. Normales de la superficie por diferencias centradas; los pies, más claros
	// cuanto más altos.
	{
		constexpr int32 RoofRings = 18;
		constexpr int32 RoofSeg = Seg * 2;
		auto RoofPoint = [](int32 Ring, int32 K)
		{
			const double U = FMath::Cos(UE_DOUBLE_HALF_PI * Ring / RoofRings);
			const double Theta = TNProcMap::TwoPi * K / RoofSeg;
			return FVector(WallR * U * FMath::Cos(Theta), WallR * U * FMath::Sin(Theta), WallTop + RoofHeight(U, Theta));
		};
		auto RoofNormal = [](const FVector& Pt)
		{
			auto H = [](double X, double Y) { return RoofHeight(FVector2D(X, Y).Size() / WallR, FMath::Atan2(Y, X)); };
			constexpr double E = 0.5;
			return FVector(-(H(Pt.X + E, Pt.Y) - H(Pt.X - E, Pt.Y)) / (2.0 * E), -(H(Pt.X, Pt.Y + E) - H(Pt.X, Pt.Y - E)) / (2.0 * E), 1.0).GetSafeNormal();
		};
		for (int32 i = 0; i < RoofRings; ++i)
		{
			for (int32 k = 0; k < RoofSeg; ++k)
			{
				const FVector Q[4] = { RoofPoint(i, k), RoofPoint(i, k + 1), RoofPoint(i + 1, k + 1), RoofPoint(i + 1, k) };
				const FVector N[4] = { RoofNormal(Q[0]), RoofNormal(Q[1]), RoofNormal(Q[2]), RoofNormal(Q[3]) };
				const double MidZ = (Q[0].Z + Q[1].Z + Q[2].Z + Q[3].Z) * 0.25;
				const float Height = static_cast<float>(FMath::Clamp((MidZ - WallTop) / FootHeight, 0.0, 1.0));
				GlassQuad(Q, N, TNProcMesh::TNProcLerpColor(Glass, GlassLight, 0.12f + 0.4f * Height));
			}
		}
	}
	// ── Etiqueta de refresco alrededor de la botella: cantos oscuros, franjas naranjas y el centro crema, donde van
	// las letras (BuildLabel).
	{
		const FLinearColor LabelEdge = Pal(0xC4520A);
		const FLinearColor LabelOrange = Pal(0xFF8A1E);
		const FLinearColor LabelCream = Pal(0xFFF4DC);
		const double Zs[6] = { LabelBottom, LabelBottom + 2.0, LabelBottom + 7.0, LabelTop - 7.0, LabelTop - 2.0, LabelTop };
		const FLinearColor Bands[5] = { LabelEdge, LabelOrange, LabelCream, LabelOrange, LabelEdge };
		constexpr double LabelR = WallR + 1.2;
		for (int32 k = 0; k < Seg; ++k)
		{
			for (int32 b = 0; b < 5; ++b)
			{
				const FVector Q[4] = { RingPoint(FVector2D(LabelR, Zs[b]), k), RingPoint(FVector2D(LabelR, Zs[b]), k + 1),
					RingPoint(FVector2D(LabelR, Zs[b + 1]), k + 1), RingPoint(FVector2D(LabelR, Zs[b + 1]), k) };
				const FVector N[4] = { RadialOut(Q[0]), RadialOut(Q[1]), RadialOut(Q[2]), RadialOut(Q[3]) };
				SmoothQuad(Q, N, Bands[b]);
			}
			// Grosor del papel arriba y abajo.
			for (const double Z : { LabelBottom, LabelTop })
			{
				const FVector A = RingPoint(FVector2D(WallR, Z), k), Bv = RingPoint(FVector2D(WallR, Z), k + 1);
				const FVector C = RingPoint(FVector2D(LabelR, Z), k + 1), D = RingPoint(FVector2D(LabelR, Z), k);
				B.AddQuad(A, Bv, C, D, FVector(0.0, 0.0, Z > (LabelBottom + LabelTop) * 0.5 ? 1.0 : -1.0), LabelEdge);
			}
		}
	}
	// Canto del corte a ras de suelo (vidrio grueso, más claro).
	for (int32 k = 0; k < Seg; ++k)
	{
		const FVector R0 = RingPoint(FVector2D(WallR + 5.0, 0.0), k), R1 = RingPoint(FVector2D(WallR + 5.0, 0.0), k + 1);
		const FVector T0 = RingPoint(FVector2D(WallR + 5.0, 14.0), k), T1 = RingPoint(FVector2D(WallR + 5.0, 14.0), k + 1);
		const FVector I0 = RingPoint(FVector2D(WallR - 1.0, 14.0), k), I1 = RingPoint(FVector2D(WallR - 1.0, 14.0), k + 1);
		B.AddQuad(R0, R1, T1, T0, RadialOut((R0 + T1) * 0.5), GlassEdge);
		B.AddQuad(T0, T1, I1, I0, FVector::UpVector, GlassEdge * 1.05f);
	}
	// Tarima de madera dentro de la botella: tablones que se cortan contra el círculo, canto hasta el suelo y una
	// alfombra redonda en el centro. Queda por encima del suelo del nivel (arena o cuadrícula) y lo tapa entero.
	{
		const FLinearColor PlankA = Pal(0xB9804B);
		const FLinearColor PlankB = Pal(0xA56E3E);
		const FLinearColor PlankEdge = Pal(0x7A4E2A);
		const FLinearColor Rug = Pal(0x3FB8AF);
		const FLinearColor RugRim = Pal(0xFFF2D4);
		constexpr double FloorR = WallR - 3.0;
		constexpr double PlankW = 24.0;
		const int32 Planks = FMath::CeilToInt32(2.0 * FloorR / PlankW);
		for (int32 j = 0; j < Planks; ++j)
		{
			const double Y0 = -FloorR + PlankW * j;
			const double Y1 = FMath::Min(FloorR, Y0 + PlankW);
			// Largo del tablón: el de la cuerda más corta de sus dos bordes, para no salirse del círculo.
			const double Half = FMath::Sqrt(FMath::Max(0.0, FloorR * FloorR - FMath::Max(Y0 * Y0, Y1 * Y1)));
			if (Half < 4.0) { continue; }
			const double Gap = 0.8;
			B.AddQuad(FVector(-Half, Y0 + Gap, FloorTop), FVector(Half, Y0 + Gap, FloorTop), FVector(Half, Y1 - Gap, FloorTop),
				FVector(-Half, Y1 - Gap, FloorTop), FVector::UpVector, (j % 2) ? PlankA : PlankB);
			B.AddQuad(FVector(-Half, Y0, FloorTop - 0.6), FVector(Half, Y0, FloorTop - 0.6), FVector(Half, Y1, FloorTop - 0.6),
				FVector(-Half, Y1, FloorTop - 0.6), FVector::UpVector, PlankEdge);
		}
		for (int32 k = 0; k < Seg; ++k)
		{
			const FVector E0 = RingPoint(FVector2D(FloorR, FloorTop), k), E1 = RingPoint(FVector2D(FloorR, FloorTop), k + 1);
			const FVector G0 = RingPoint(FVector2D(FloorR, 0.0), k), G1 = RingPoint(FVector2D(FloorR, 0.0), k + 1);
			B.AddQuad(G0, G1, E1, E0, -RadialOut((G0 + E1) * 0.5), PlankEdge);
			B.AddTri(FVector(0.0, 0.0, FloorTop - 0.7), E0, E1, FVector::UpVector, PlankEdge);
			const FVector R0 = RingPoint(FVector2D(58.0, FloorTop + 0.6), k), R1 = RingPoint(FVector2D(58.0, FloorTop + 0.6), k + 1);
			const FVector Q0 = RingPoint(FVector2D(50.0, FloorTop + 0.7), k), Q1 = RingPoint(FVector2D(50.0, FloorTop + 0.7), k + 1);
			B.AddQuad(Q0, Q1, R1, R0, FVector::UpVector, RugRim);
			B.AddTri(FVector(0.0, 0.0, FloorTop + 0.7), Q0, Q1, FVector::UpVector, Rug);
		}
	}
	// Boca de vidrio grueso alrededor del hueco (tapa los dientes del recorte) con canto hacia dentro y hacia fuera.
	constexpr int32 LipSeg = 40;
	const FVector Axis = OnCylinder(WallR, 0.0, DoorZ);
	for (int32 j = 0; j < LipSeg; ++j)
	{
		const double P0 = TNProcMap::TwoPi * j / LipSeg, P1 = TNProcMap::TwoPi * (j + 1) / LipSeg;
		auto LipPt = [](double R, double Phi, double Radius) { return OnCylinder(Radius, R * FMath::Cos(Phi), DoorZ + R * FMath::Sin(Phi)); };
		const FVector I0 = LipPt(DoorR - 22.0, P0, WallR + 4.0), I1 = LipPt(DoorR - 22.0, P1, WallR + 4.0);
		const FVector O0 = LipPt(DoorR + 7.0, P0, WallR + 4.0), O1 = LipPt(DoorR + 7.0, P1, WallR + 4.0);
		B.AddQuad(I0, I1, O1, O0, RadialOut((I0 + O1) * 0.5), GlassEdge);
		const FVector O0b = LipPt(DoorR + 7.0, P0, WallR), O1b = LipPt(DoorR + 7.0, P1, WallR);
		B.AddQuad(O0, O1, O1b, O0b, (O0 + O1) * 0.5 - Axis, GlassLight);
		const FVector J0 = LipPt(DoorR - 22.0, P0, WallR - 10.0), J1 = LipPt(DoorR - 22.0, P1, WallR - 10.0);
		B.AddQuad(I0, I1, J1, J0, Axis - (I0 + J1) * 0.5, GlassLight * 0.85f);
	}
	TNArt::SetMesh(Bottle, TNProcRuntimeMesh::MakeStaticMesh(this, B, VertexColorMat), TN_ART("Lobby.Booth.Bottle"));

	// ── Puerta: el tapón, una chapa de corona roja con su estrella (en el espacio de la bisagra) ──
	TNProcMesh::FTNProcMeshBuffers D;
	const FVector Hinge = HingePoint();
	auto CapPt = [&Hinge](double Radius, double S, double Z) { return OnCylinder(Radius, S, Z) - Hinge; };
	const FLinearColor CapRed = Pal(0xD7263D);
	const FLinearColor CapRedDark = Pal(0xA3162B);
	const FLinearColor CapCream = Pal(0xFFF2D4);
	const FLinearColor CapGold = Pal(0xFFCB3D);
	const FLinearColor Liner = Pal(0xE9DCC0);
	const double Radii[5] = { 0.0, 0.34, 0.62, 0.78, 1.0 };
	constexpr int32 CapSeg = 42;
	for (int32 r = 0; r < 4; ++r)
	{
		for (int32 j = 0; j < CapSeg; ++j)
		{
			const double P0 = TNProcMap::TwoPi * j / CapSeg, P1 = TNProcMap::TwoPi * (j + 1) / CapSeg;
			const double Ra = Radii[r] * DoorR, Rb = Radii[r + 1] * DoorR;
			// La cara abomba un poco hacia fuera en el centro, como una chapa.
			const double Da = 5.0 * (1.0 - Radii[r] * Radii[r]), Db = 5.0 * (1.0 - Radii[r + 1] * Radii[r + 1]);
			const FVector A = CapPt(CapFaceR + Da, Ra * FMath::Cos(P0), DoorZ + Ra * FMath::Sin(P0));
			const FVector Bv = CapPt(CapFaceR + Da, Ra * FMath::Cos(P1), DoorZ + Ra * FMath::Sin(P1));
			const FVector C = CapPt(CapFaceR + Db, Rb * FMath::Cos(P1), DoorZ + Rb * FMath::Sin(P1));
			const FVector Dv = CapPt(CapFaceR + Db, Rb * FMath::Cos(P0), DoorZ + Rb * FMath::Sin(P0));
			const FVector Out = RadialOut((A + C) * 0.5 + Hinge);
			D.AddQuad(A, Bv, C, Dv, Out, r == 2 ? CapCream : CapRed);
			// Forro de dentro (se ve con la puerta abierta).
			const FVector Back = -Out * 8.0;
			D.AddQuad(A + Back, Bv + Back, C + Back, Dv + Back, -Out, Liner);
		}
	}
	// Estrella dorada en relieve en el centro de la chapa.
	{
		const FVector Center = CapPt(CapFaceR + 6.5, 0.0, DoorZ);
		const FVector Out = RadialOut(Center + Hinge);
		TArray<FVector> Star;
		for (int32 i = 0; i < 10; ++i)
		{
			const double A = HALF_PI + PI * i / 5.0;
			const double R = (i % 2) ? 12.0 : 30.0;
			Star.Add(CapPt(CapFaceR + 6.5, R * FMath::Cos(A), DoorZ + R * FMath::Sin(A)));
		}
		for (int32 i = 0; i < 10; ++i)
		{
			const FVector& S0 = Star[i];
			const FVector& S1 = Star[(i + 1) % 10];
			D.AddTri(Center + Out * 1.5, S0, S1, Out, CapGold);
			D.AddQuad(S0, S1, S1 - Out * 2.0, S0 - Out * 2.0, (S0 + S1) * 0.5 - Center, CapGold * 0.8f);
		}
	}
	// Falda rizada de la chapa (21 pliegues) desde la cara hasta la pared, y borde de la cara.
	constexpr int32 Crimp = CapFlutes * 2;
	for (int32 j = 0; j < Crimp; ++j)
	{
		const double P0 = TNProcMap::TwoPi * j / Crimp, P1 = TNProcMap::TwoPi * (j + 1) / Crimp;
		const double R0 = DoorR + ((j % 2) ? 9.0 : 2.0), R1 = DoorR + (((j + 1) % 2) ? 9.0 : 2.0);
		const FVector F0 = CapPt(CapFaceR, R0 * FMath::Cos(P0), DoorZ + R0 * FMath::Sin(P0));
		const FVector F1 = CapPt(CapFaceR, R1 * FMath::Cos(P1), DoorZ + R1 * FMath::Sin(P1));
		const FVector W0 = CapPt(WallR + 1.0, (R0 + 5.0) * FMath::Cos(P0), DoorZ + (R0 + 5.0) * FMath::Sin(P0));
		const FVector W1 = CapPt(WallR + 1.0, (R1 + 5.0) * FMath::Cos(P1), DoorZ + (R1 + 5.0) * FMath::Sin(P1));
		const FVector Radial = RadialOut(F0 + Hinge);
		const double PhiMid = (P0 + P1) * 0.5;
		const FVector EdgeOut = FVector(0.0, 0.0, FMath::Sin(PhiMid)) + FVector(-Radial.Y, Radial.X, 0.0) * FMath::Cos(PhiMid);
		D.AddQuad(F0, F1, W1, W0, EdgeOut, (j % 2) ? CapRedDark : CapRed * 1.08f);
		const FVector E0 = CapPt(CapFaceR, DoorR * FMath::Cos(P0), DoorZ + DoorR * FMath::Sin(P0));
		const FVector E1 = CapPt(CapFaceR, DoorR * FMath::Cos(P1), DoorZ + DoorR * FMath::Sin(P1));
		D.AddQuad(E0, E1, F1, F0, Radial, CapRed);
	}
	TNArt::SetMesh(Door, TNProcRuntimeMesh::MakeStaticMesh(this, D, VertexColorMat), TN_ART("Lobby.Booth.Door"));
}

void ATN_ChangingBooth::BuildLabel()
{
	using namespace TNBoothDetail;
	// Una letra por componente, girada hacia fuera en su punto de la curva: el texto queda impreso en la etiqueta en vez
	// de flotar delante de la botella. Se lee de izquierda a derecha desde fuera (de +Y a -Y).
	// Las letras de una construcción anterior (el editor vuelve a construir al mover la botella) se quitan.
	for (UTextRenderComponent* Old : LabelLetters)
	{
		if (Old) { Old->DestroyComponent(); }
	}
	LabelLetters.Reset();
	// Sin pantalla no hay etiqueta; el servidor dedicado tampoco carga la botella (sin colisión, #658).
	if (IsRunningDedicatedServer() || !Bottle) { return; }
	const FString Text = NSLOCTEXT("Tortunabo", "BoothLabel", "PROBADOR").ToString();
	constexpr double Radius = WallR + 2.4;
	constexpr double Tracking = 2.5;
	const double MidZ = (LabelBottom + LabelTop) * 0.5;
	TArray<double> Widths;
	for (const TCHAR Ch : Text)
	{
		UTextRenderComponent* Letter = NewObject<UTextRenderComponent>(this, NAME_None, RF_Transient);
		Letter->SetupAttachment(Bottle);
		Letter->SetHorizontalAlignment(EHTA_Center);
		Letter->SetVerticalAlignment(EVRTA_TextCenter);
		Letter->SetWorldSize(24.f);
		Letter->SetTextRenderColor(FColor(16, 32, 66));
		Letter->SetText(FText::AsCultureInvariant(FString(1, &Ch)));
		Letter->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Letter->RegisterComponent();
		LabelLetters.Add(Letter);
		Widths.Add(FMath::Max(4.0, static_cast<double>(Letter->GetTextLocalSize().Y)));
	}
	double Total = Tracking * FMath::Max(0, Widths.Num() - 1);
	for (const double W : Widths) { Total += W; }
	double S = Total * 0.5;
	for (int32 i = 0; i < LabelLetters.Num(); ++i)
	{
		const double Center = S - Widths[i] * 0.5;
		S -= Widths[i] + Tracking;
		const double A = Center / Radius;
		LabelLetters[i]->SetRelativeLocationAndRotation(FVector(Radius * FMath::Cos(A), Radius * FMath::Sin(A), MidZ),
			FRotator(0.0, FMath::RadiansToDegrees(A), 0.0));
	}
}

void ATN_ChangingBooth::HideBlockout()
{
	// Las piezas de la maqueta que esta botella sustituye (la botella gris y la puerta de prueba) se esconden.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Other = *It;
		if (!Other || Other == this) { continue; }
		const FString ClassName = Other->GetClass()->GetName();
		const double Dist = FVector::Dist2D(Other->GetActorLocation(), GetActorLocation());
		const bool bBottle = ClassName.Contains(TEXT("VestidorBotella")) && Dist < 200.0;
		const bool bDoor = ClassName.Contains(TEXT("ShellDoor")) && Dist < 450.0;
		if (bBottle || bDoor)
		{
			Other->SetActorHiddenInGame(true);
			Other->SetActorEnableCollision(false);
		}
	}
}

void ATN_ChangingBooth::OnInteracted_Implementation(APawn* Interactor)
{
	Super::OnInteracted_Implementation(Interactor);
	if (!HasAuthority() || !Interactor || Occupant) { return; }
	AMP_GamePlayerController* PC = Cast<AMP_GamePlayerController>(Interactor->GetController());
	if (!PC) { return; }

	Occupant = Interactor;
	OnRep_Occupant();
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Probador] %s entra en %s."), *GetNameSafe(Interactor), *GetName());
	PC->ClientOpenBooth(this);
}

void ATN_ChangingBooth::ReleaseOccupant(APawn* Pawn)
{
	if (!HasAuthority() || !Pawn || Pawn != Occupant) { return; }
	Occupant = nullptr;
	OnRep_Occupant();
	ForceNetUpdate();
	UE_LOG(LogTortunabo, Log, TEXT("[Probador] %s sale de %s."), *GetNameSafe(Pawn), *GetName());
}

void ATN_ChangingBooth::OnRep_Occupant()
{
	using namespace TNBoothDetail;
	WobbleTime = 1.2f;

	// Solo mueve a la tortuga quien la controla: el servidor (autoridad) y su cliente (predicción), a la vez.
	const FRotator Facing(0.f, GetActorRotation().Yaw, 0.f);
	auto Controls = [this](const APawn* P) { return P && (HasAuthority() || P->IsLocallyControlled()); };

	if (Occupant && Controls(Occupant))
	{
		// Dentro: centrada y mirando a la puerta; ignora las paredes mientras esté aquí.
		Occupant->MoveIgnoreActorAdd(this);
		if (ACharacter* Char = Cast<ACharacter>(Occupant.Get())) { Char->GetCharacterMovement()->StopMovementImmediately(); }
		Occupant->SetActorLocationAndRotation(GetActorTransform().TransformPosition(FVector(0.0, 0.0, 96.0)), Facing, false, nullptr, ETeleportType::TeleportPhysics);
	}
	APawn* Leaving = (!Occupant && PreviousOccupant.IsValid()) ? PreviousOccupant.Get() : nullptr;
	// Dentro, la cabeza mira al frente y no a su cámara (su dueño ve la del probador), en todas las máquinas.
	if (PreviousOccupant.Get() != Occupant.Get())
	{
		if (ATortugaCharacter* Before = Cast<ATortugaCharacter>(PreviousOccupant.Get())) { Before->SetHeadLookSuppressed(false); }
		if (ATortugaCharacter* Inside = Cast<ATortugaCharacter>(Occupant.Get())) { Inside->SetHeadLookSuppressed(true); }
	}
	PreviousOccupant = Occupant.Get();
	if (Leaving && Controls(Leaving) && GetWorld())
	{
		// Sale de un saltito por la puerta cuando ya se ha abierto un poco, y luego vuelve a chocar con la botella.
		TWeakObjectPtr<APawn> WeakPawn(Leaving);
		TWeakObjectPtr<ATN_ChangingBooth> WeakBooth(this);
		FTimerHandle HopTimer;
		GetWorldTimerManager().SetTimer(HopTimer, FTimerDelegate::CreateLambda([WeakPawn, WeakBooth, Facing]()
		{
			APawn* P = WeakPawn.Get();
			ATN_ChangingBooth* Booth = WeakBooth.Get();
			if (!P || !Booth) { return; }
			P->SetActorRotation(Facing);
			if (ACharacter* Char = Cast<ACharacter>(P)) { Char->LaunchCharacter(Facing.Vector() * 540.f + FVector(0.f, 0.f, 420.f), true, true); }
			FTimerHandle RestoreTimer;
			Booth->GetWorldTimerManager().SetTimer(RestoreTimer, FTimerDelegate::CreateLambda([WeakPawn, WeakBooth]()
			{
				if (APawn* Q = WeakPawn.Get()) { Q->MoveIgnoreActorRemove(WeakBooth.Get()); }
			}), 1.1f, false);
		}), HopDelay, false);
	}
}

void ATN_ChangingBooth::Tick(float DeltaSeconds)
{
	using namespace TNBoothDetail;
	Super::Tick(DeltaSeconds);

	// Si quien estaba dentro se ha ido (desconexión, viaje), la puerta se vuelve a abrir.
	if (HasAuthority() && Occupant && (!IsValid(Occupant) || !Occupant->GetController()))
	{
		Occupant = nullptr;
		OnRep_Occupant();
		ForceNetUpdate();
	}

	const float Target = Occupant ? 0.f : 1.f;
	DoorOpen = FMath::FInterpConstantTo(DoorOpen, Target, DeltaSeconds, 1.9f);
	DoorHinge->SetRelativeRotation(FRotator(0.f, DoorOpenYaw * FMath::SmoothStep(0.f, 1.f, DoorOpen), 0.f));

	// Meneo de la botella: al cerrar/abrir y, mientras alguien se cambia, un traqueteo de vez en cuando.
	if (Occupant)
	{
		ShuffleClock += DeltaSeconds;
		if (ShuffleClock > 2.3f) { ShuffleClock = 0.f; WobbleTime = FMath::Max(WobbleTime, 0.7f); }
	}
	float Squash = 0.f;
	if (WobbleTime > 0.f)
	{
		WobbleTime = FMath::Max(0.f, WobbleTime - DeltaSeconds);
		Squash = 0.035f * FMath::Sin(WobbleTime * 22.f) * FMath::Min(1.f, WobbleTime);
	}
	if (Bottle) { Bottle->SetRelativeScale3D(FVector(1.f + Squash, 1.f + Squash, 1.f - Squash * 0.8f)); }
}
