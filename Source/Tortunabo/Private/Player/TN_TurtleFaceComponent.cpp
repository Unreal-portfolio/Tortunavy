#include "Player/TN_TurtleFaceComponent.h"
#include "Core/TN_ProjectMaterials.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/TN_CoopGameState.h"
#include "Core/TN_CosmeticLook.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Player/MP_GamePlayerController.h"
#include "Player/TN_StaminaComponent.h"
#include "Player/TortugaCharacter.h"
#include "ProceduralMeshComponent.h"
#include "ReferenceSkeleton.h"
#include "Voice/ProximityVoiceComponent.h"

/** Lo que la cara quiere hacer en un fotograma (antes de suavizar). Boca y ojos: parámetros de M_TurtleBody. */
struct FTNTurtleFaceGoal
{
	float Mouth = 0.3f;
	float Smile = 1.f;
	float Tired = 0.f;
	float Blush = 0.f;
	bool bSqueeze = false;
	/** Gotas de sudor a la vez (0-2) y cada cuánto sale una (s). */
	int32 Sweat = 0;
	float SweatPeriod = 1.6f;
	/** Lengua: cuánto sale (0 dentro, 1 fuera), su largo fuera de la boca (unidades de la malla) y el lado de la boca
	 *  (-1 derecha de la tortuga, 0 centro, 1 izquierda). */
	float TongueOut = 0.f;
	float TongueLen = 0.f;
	float TongueSide = 0.f;
	/** Forma de reposo (espacio de la malla, x para el lado izquierdo; se refleja con el lado): dirección del primer
	 *  tramo al salir de la boca y del último; los de en medio se reparten entre las dos. */
	FVector TongueStart = FVector(0.0, 0.95, -0.3);
	FVector TongueEnd = FVector(0.0, 0.55, -0.85);
	/** Rigidez de la forma (multiplica TongueShapeStiffness), gravedad (multiplica TongueGravity), aleteo al viento
	 *  (0-1,5), jadeo y meneo de lado a lado. */
	float TongueStiff = 1.f;
	float TongueGravity = 1.f;
	float TongueFlap = 0.f;
	float TonguePant = 0.f;
	float TongueWag = 0.f;
};

namespace TNTurtleFaceDetail
{
	TAutoConsoleVariable<int32> CVarFaceMood(TEXT("tn.Face.Mood"), -1,
		TEXT("Cara 3D de las tortugas: fuerza el ánimo (0 feliz, 1 cansada, 2 jadeando, 3 tumbada); -1 = el real."));
	TAutoConsoleVariable<int32> CVarFaceTongue(TEXT("tn.Face.Tongue"), -1,
		TEXT("Lengua 3D: fuerza el modo (0 dentro, 1 al viento como al esprintar, 2 colgando como al jadear, 3 asomando la punta); -1 = el real."));
	TAutoConsoleVariable<int32> CVarFaceTalk(TEXT("tn.Face.Talk"), 0, TEXT("Cara 3D: 1 = todas las tortugas mueven la boca como si hablaran."));

	/** Hueso al que se engancha todo (la cabeza de TotugaDemo_Rig). */
	const FName HeadBoneName(TEXT("Head"));

	/**
	 * Medidas de TotugaDemo_Rig (espacio de la malla antes del skinning: mira a +Y, arriba +Z, su izquierda +X). El hueco
	 * de la boca está bajo la nariz, en |x| < 1,7 y z 41,3-43,9, con el fondo en y ~ 10 y el borde en y ~ 13,7; la lengua
	 * nace dentro del hueco, cerca del borde y un poco por debajo del centro (así no se dobla tanto al pasar el labio).
	 */
	const FVector TongueRootCenter(0.0, 12.8, 42.1);
	constexpr float TongueRootSide = 0.7f;

	/** Anillos de la malla de la lengua y vértices por anillo. */
	constexpr int32 TongueRings = 12;
	constexpr int32 TongueRingVerts = 16;
	/** Ángulos (grados) de los vértices de cada anillo; el primero (90) es el surco de arriba, con los vecinos cerca. */
	const float TongueRingAngles[TongueRingVerts] = { 90.f, 101.f, 128.f, 155.f, 180.f, 205.f, 232.f, 256.f, 270.f, 284.f, 308.f, 335.f, 0.f, 25.f, 52.f, 79.f };

	/**
	 * Cara vista de frente: la y de la superficie más adelantada (cuerpo y ojos, sin el casco) en z = 32..47 (filas) y
	 * |x| = 0..10 (columnas), medida sobre la malla; -99 = por ahí no hay cabeza. La lengua no se mete detrás. El hueco de
	 * la boca va tapado (con la altura del labio): si no, el viento la volvería a meter dentro. Los dos primeros puntos de
	 * la cadena (la raíz y el que sale del hueco) no chocan.
	 */
	constexpr int32 SurfRows = 16;
	constexpr int32 SurfCols = 11;
	constexpr float SurfZ0 = 32.f;
	const float FaceSurfaceTable[SurfRows][SurfCols] =
	{
		{ 7.0f, 6.8f, 6.5f, 5.8f, 5.1f, 4.7f, 3.9f, 3.9f, 4.2f, 4.4f, 4.7f },          // z = 32
		{ 6.9f, 6.6f, 6.2f, 5.4f, 5.0f, 4.9f, 4.8f, 4.7f, 4.7f, 4.7f, 4.7f },          // z = 33
		{ 6.7f, 6.3f, 5.8f, 5.0f, 4.8f, 4.6f, 4.5f, 4.3f, 4.1f, 3.1f, -99.f },         // z = 34
		{ 7.2f, 6.5f, 5.4f, 4.3f, 3.9f, 3.6f, -99.f, -99.f, -99.f, -99.f, -99.f },     // z = 35
		{ 8.3f, 7.8f, 4.7f, 3.4f, 1.8f, -99.f, -99.f, -99.f, -99.f, -99.f, -99.f },    // z = 36
		{ 11.7f, 11.4f, 11.1f, 10.6f, -99.f, -99.f, -99.f, -99.f, -99.f, -99.f, -99.f }, // z = 37
		{ 13.0f, 13.0f, 12.8f, 12.5f, 10.7f, 9.8f, -99.f, -99.f, -99.f, -99.f, -99.f },  // z = 38
		{ 13.7f, 13.7f, 13.6f, 13.2f, 12.5f, 11.3f, 10.0f, -99.f, -99.f, -99.f, -99.f }, // z = 39
		{ 14.2f, 14.1f, 14.0f, 13.6f, 13.0f, 12.1f, 10.8f, 8.8f, -99.f, -99.f, -99.f },  // z = 40
		{ 13.5f, 13.9f, 14.0f, 13.7f, 13.1f, 12.3f, 11.2f, 10.0f, -99.f, -99.f, -99.f }, // z = 41
		{ 13.8f, 13.8f, 13.8f, 13.7f, 13.2f, 12.4f, 11.3f, 9.9f, -99.f, -99.f, -99.f },  // z = 42 (hueco tapado)
		{ 13.5f, 13.5f, 13.2f, 13.7f, 13.3f, 12.5f, 11.4f, 9.8f, -99.f, -99.f, -99.f },  // z = 43 (hueco tapado)
		{ 17.0f, 16.5f, 13.8f, 13.9f, 13.4f, 12.6f, 11.4f, 10.7f, -99.f, -99.f, -99.f }, // z = 44
		{ 17.3f, 17.2f, 15.6f, 14.1f, 13.5f, 12.7f, 11.9f, 11.3f, 9.7f, -99.f, -99.f },  // z = 45
		{ 17.5f, 17.2f, 16.5f, 13.9f, 13.4f, 12.3f, 12.1f, 11.5f, 10.2f, -99.f, -99.f }, // z = 46
		{ 17.4f, 16.9f, 13.9f, 13.6f, 13.1f, 12.1f, 11.9f, 11.3f, 9.8f, -99.f, -99.f },  // z = 47
	};

	/** Superficie de la cara en (X, Z) con interpolación bilineal; -99 si no hay cara (o falta alguna esquina). */
	float FaceSurfaceY(float X, float Z)
	{
		const float Ax = FMath::Abs(X);
		const float Fz = Z - SurfZ0;
		if (Ax > SurfCols - 1 || Fz < 0.f || Fz > SurfRows - 1) { return -99.f; }
		const int32 C0 = FMath::Min(static_cast<int32>(Ax), SurfCols - 2);
		const int32 R0 = FMath::Min(static_cast<int32>(Fz), SurfRows - 2);
		const float Tx = Ax - C0;
		const float Tz = Fz - R0;
		const float A = FaceSurfaceTable[R0][C0];
		const float B = FaceSurfaceTable[R0][C0 + 1];
		const float C = FaceSurfaceTable[R0 + 1][C0];
		const float D = FaceSurfaceTable[R0 + 1][C0 + 1];
		if (FMath::Min(FMath::Min(A, B), FMath::Min(C, D)) < -50.f) { return -99.f; }
		return FMath::Lerp(FMath::Lerp(A, B, Tx), FMath::Lerp(C, D, Tx), Tz);
	}

	/**
	 * Saca un punto (espacio de la malla) que se ha metido en la cara: lo lleva a Margin por fuera de la superficie
	 * siguiendo su normal (en las mejillas, hacia el lado; delante, hacia delante). Solo si está cerca de la superficie.
	 */
	bool PushOutOfFace(FVector& Local, float Margin)
	{
		const float X = static_cast<float>(Local.X);
		const float Z = static_cast<float>(Local.Z);
		const float Surface = FaceSurfaceY(X, Z);
		if (Surface < -50.f) { return false; }
		const float Gap = static_cast<float>(Local.Y) - Surface - Margin;
		if (Gap >= 0.f) { return false; }
		const float X0 = FaceSurfaceY(X - 0.5f, Z);
		const float X1 = FaceSurfaceY(X + 0.5f, Z);
		const float Z0 = FaceSurfaceY(X, Z - 0.5f);
		const float Z1 = FaceSurfaceY(X, Z + 0.5f);
		const float SlopeX = FMath::Min(X0, X1) > -50.f ? X1 - X0 : 0.f;
		const float SlopeZ = FMath::Min(Z0, Z1) > -50.f ? Z1 - Z0 : 0.f;
		const FVector Grad(-SlopeX, 1.0, -SlopeZ);
		const double Grad2 = Grad.SizeSquared();
		if (Gap / FMath::Sqrt(static_cast<float>(Grad2)) < -4.f) { return false; }
		Local -= Grad * (Gap / Grad2);
		return true;
	}

	/** Hermite 0..1 entre E0 y E1 (sirve también con E0 > E1). */
	float Smooth01(float E0, float E1, float X)
	{
		const float T = FMath::Clamp((X - E0) / (E1 - E0), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	/** Color sRGB 0xRRGGBB en lineal (lo que llega al material por el color de vértice de la malla procedural). */
	FLinearColor Lin(uint32 Hex, float Alpha)
	{
		FLinearColor Out = FLinearColor::FromSRGBColor(FColor((Hex >> 16) & 255, (Hex >> 8) & 255, Hex & 255));
		Out.A = Alpha;
		return Out;
	}

	FVector CatmullRom(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, float T)
	{
		const float T2 = T * T;
		const float T3 = T2 * T;
		return 0.5f * ((2.f * P1) + (P2 - P0) * T + (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * T2 + (3.f * P1 - P0 - 3.f * P2 + P3) * T3);
	}

	/**
	 * Triángulo con la cara frontal hacia fuera (la media de las normales de sus vértices). En UE la cara frontal de
	 * (A, B, C) es la de normal (C-A)x(B-A) (ver TN_PlaygroundMeshKit.h).
	 */
	void AddOrientedTri(TArray<int32>& Tris, const TArray<FVector>& Verts, const TArray<FVector>& Normals, int32 IA, int32 IB, int32 IC)
	{
		const FVector Front = FVector::CrossProduct(Verts[IC] - Verts[IA], Verts[IB] - Verts[IA]);
		const bool bFlip = FVector::DotProduct(Front, Normals[IA] + Normals[IB] + Normals[IC]) < 0.0;
		Tris.Add(IA);
		Tris.Add(bFlip ? IC : IB);
		Tris.Add(bFlip ? IB : IC);
	}

	/**
	 * Material de las piezas de la cara: M_TurtleFaceParts (color de vértice; el alfa es lo mojado: más brillo). Si
	 * todavía no se ha creado, M_CosmeticVertexColor con el alfa a 0 (mate: ahí el alfa sería metal).
	 */
	UMaterialInterface* FacePartsMaterial(bool& bOutWetAlpha)
	{
		if (UMaterialInterface* Parts = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Cosmetics/Materials/M_TurtleFaceParts.M_TurtleFaceParts"), nullptr, LOAD_NoWarn))
		{
			bOutWetAlpha = true;
			return Parts;
		}
		bOutWetAlpha = false;
		return TNMaterials::VertexColor();
	}

	/**
	 * Gota de sudor de dibujo (unidades de la malla, punta hacia +Z): media esfera abajo y un cono tangente hasta la punta,
	 * celeste con la base más azul y un brillo blanco arriba y delante.
	 */
	void BuildSweatDrop(UProceduralMeshComponent& Drop, bool bWet)
	{
		const float R = 1.15f;
		const float TipZ = 2.3f * R;
		const float TangentAngle = FMath::Asin(R / TipZ);
		const float ConeBaseZ = R * FMath::Sin(TangentAngle);
		const float ConeBaseR = R * FMath::Cos(TangentAngle);
		constexpr int32 Around = 12;
		// Anillos: cuatro en la esfera (ángulo desde el ecuador) y dos en el cono (fracción hacia la punta).
		struct FRing { float Z; float Radius; float NormalAngle; };
		TArray<FRing> Profile;
		for (const float Deg : { -65.f, -35.f, -8.f })
		{
			const float A = FMath::DegreesToRadians(Deg);
			Profile.Add({ R * FMath::Sin(A), R * FMath::Cos(A), A });
		}
		Profile.Add({ ConeBaseZ, ConeBaseR, TangentAngle });
		for (const float F : { 0.36f, 0.72f })
		{
			Profile.Add({ FMath::Lerp(ConeBaseZ, TipZ, F), ConeBaseR * (1.f - F), TangentAngle });
		}

		const float WetA = bWet ? 0.9f : 0.f;
		const FLinearColor Base = Lin(0x8FD8FF, WetA);
		const FLinearColor Deep = Lin(0x4FA9E6, WetA);
		const FLinearColor Shine = Lin(0xFFFFFF, WetA);
		const FVector HighlightDir = FVector(0.0, 0.55, 0.84).GetSafeNormal();
		TArray<FVector> Verts;
		TArray<FVector> Normals;
		TArray<FLinearColor> Colors;
		auto Push = [&Verts, &Normals, &Colors, &Base, &Deep, &Shine, &HighlightDir, R](const FVector& Pos, const FVector& Nrm)
		{
			FLinearColor Col = FMath::Lerp(Base, Deep, Smooth01(-0.2f * R, -0.9f * R, static_cast<float>(Pos.Z)));
			Col = FMath::Lerp(Col, Shine, 0.9f * Smooth01(0.72f, 0.9f, static_cast<float>(FVector::DotProduct(Nrm, HighlightDir))));
			Verts.Add(Pos);
			Normals.Add(Nrm);
			Colors.Add(Col);
		};
		for (const FRing& Ring : Profile)
		{
			for (int32 j = 0; j < Around; ++j)
			{
				const float Phi = 2.f * PI * j / Around;
				const FVector Radial(FMath::Cos(Phi), FMath::Sin(Phi), 0.0);
				Push(Radial * Ring.Radius + FVector(0.0, 0.0, Ring.Z),
					(Radial * FMath::Cos(Ring.NormalAngle) + FVector(0.0, 0.0, FMath::Sin(Ring.NormalAngle))).GetSafeNormal());
			}
		}
		const int32 Bottom = Verts.Num();
		Push(FVector(0.0, 0.0, -R), FVector(0.0, 0.0, -1.0));
		const int32 Tip = Verts.Num();
		Push(FVector(0.0, 0.0, TipZ), FVector(0.0, 0.0, 1.0));

		TArray<int32> Tris;
		for (int32 r = 0; r + 1 < Profile.Num(); ++r)
		{
			for (int32 j = 0; j < Around; ++j)
			{
				const int32 A = r * Around + j;
				const int32 B = r * Around + (j + 1) % Around;
				const int32 C = (r + 1) * Around + (j + 1) % Around;
				const int32 D = (r + 1) * Around + j;
				AddOrientedTri(Tris, Verts, Normals, A, B, C);
				AddOrientedTri(Tris, Verts, Normals, A, C, D);
			}
		}
		const int32 Last = (Profile.Num() - 1) * Around;
		for (int32 j = 0; j < Around; ++j)
		{
			AddOrientedTri(Tris, Verts, Normals, Bottom, j, (j + 1) % Around);
			AddOrientedTri(Tris, Verts, Normals, Tip, Last + j, Last + (j + 1) % Around);
		}
		const TArray<FVector2D> NoUVs;
		const TArray<FProcMeshTangent> NoTangents;
		Drop.CreateMeshSection_LinearColor(0, Verts, Tris, Normals, NoUVs, Colors, NoTangents, false);
	}

	UProceduralMeshComponent* MakePart(AActor* OwnerActor, USceneComponent* Parent)
	{
		UProceduralMeshComponent* Part = NewObject<UProceduralMeshComponent>(OwnerActor, NAME_None, RF_Transient);
		Part->SetupAttachment(Parent);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(false);
		Part->bUseAsyncCooking = true;
		Part->RegisterComponent();
		return Part;
	}

	// ── Lenguas de cada situación ────────────────────────────────────────────

	/**
	 * Al viento por un lado de la boca (sprint, panzazo, modo loco): sale por la comisura, se abre hacia el lado y el
	 * viento la lleva hacia atrás por la mejilla, aleteando.
	 */
	void SetWindTongue(FTNTurtleFaceGoal& Goal, float Length, float Side, float Flap)
	{
		Goal.TongueOut = 1.f;
		Goal.TongueLen = Length;
		Goal.TongueSide = Side;
		Goal.TongueStart = FVector(0.35, 0.9, -0.25);
		Goal.TongueEnd = FVector(0.8, -0.3, -0.55);
		Goal.TongueStiff = 1.f;
		Goal.TongueGravity = 0.8f;
		Goal.TongueFlap = Flap;
		Goal.TonguePant = 0.f;
	}

	/** Colgando por delante de la barbilla, blandita, sube y baja con el jadeo (Breath 0..1). */
	void SetHangTongue(FTNTurtleFaceGoal& Goal, float Length, float Breath)
	{
		Goal.TongueOut = 0.86f + 0.14f * Breath;
		Goal.TongueLen = Length;
		Goal.TongueSide = 0.f;
		Goal.TongueStart = FVector(0.0, 0.95, -0.3);
		Goal.TongueEnd = FVector(0.0, 0.15, -1.0);
		Goal.TongueStiff = 0.3f;
		Goal.TongueGravity = 1.2f;
		Goal.TongueFlap = 0.f;
		Goal.TonguePant = 1.f;
	}

	/** Solo la punta, un momento, meneándose (Clock en segundos). */
	void SetBlepTongue(FTNTurtleFaceGoal& Goal, float Clock)
	{
		Goal.TongueOut = 1.f;
		Goal.TongueLen = 1.7f;
		Goal.TongueSide = 0.f;
		Goal.TongueStart = FVector(0.0, 0.95, -0.3);
		Goal.TongueEnd = FVector(0.0, 0.55, -0.85);
		Goal.TongueStiff = 3.5f;
		Goal.TongueGravity = 0.3f;
		Goal.TongueFlap = 0.f;
		Goal.TonguePant = 0.f;
		Goal.TongueWag = 0.22f * FMath::Sin(Clock * 11.f);
		Goal.Mouth = FMath::Max(Goal.Mouth, 0.4f);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Ciclo de vida
// ─────────────────────────────────────────────────────────────────────────────

UTN_TurtleFaceComponent::UTN_TurtleFaceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Después de la animación, la física (ragdoll) y el movimiento: la cabeza ya está donde se va a dibujar.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetIsReplicatedByDefault(false);
	for (int32 k = 0; k < ChainNum; ++k)
	{
		ChainPos[k] = FVector::ZeroVector;
		ChainVel[k] = FVector::ZeroVector;
	}
}

void UTN_TurtleFaceComponent::BeginPlay()
{
	Super::BeginPlay();
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false);
		return;
	}
	if (const AActor* OwnerActor = GetOwner()) { PrevYaw = static_cast<float>(OwnerActor->GetActorRotation().Yaw); }
	BlepTimer = FMath::FRandRange(6.f, 12.f);
	GaspTimer = FMath::FRandRange(3.f, 5.f);
	BindChat();
}

void UTN_TurtleFaceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ATN_CoopGameState* GS = ChatSource.Get())
	{
		GS->OnQuickChatReceived.RemoveDynamic(this, &UTN_TurtleFaceComponent::HandleQuickChat);
	}
	ChatSource.Reset();
	DestroyParts();
	Super::EndPlay(EndPlayReason);
}

bool UTN_TurtleFaceComponent::EnsureParts(USkeletalMeshComponent* Body)
{
	using namespace TNTurtleFaceDetail;
	if (bPartsReady) { return true; }
	if (bPartsTried || !Body) { return false; }
	const USkinnedAsset* Asset = Body->GetSkinnedAsset();
	if (!Asset) { return false; }
	bPartsTried = true;
	// Solo la tortuga de demo: el hueco de la boca y las medidas de la cara son los de esa malla.
	if (!UTN_CosmeticLook::IsDemoTurtle(Body)) { return false; }
	const FReferenceSkeleton& RefSkeleton = Asset->GetRefSkeleton();
	const int32 HeadIndex = RefSkeleton.FindBoneIndex(HeadBoneName);
	if (HeadIndex == INDEX_NONE) { return false; }

	// Hueso de la cabeza en la postura de referencia (espacio de la malla): el ancla lo deshace, así que sus hijos se
	// colocan en coordenadas de la malla y siguen a la cabeza animada (como el casco en UTN_CosmeticLook::AttachHelmet).
	FTransform HeadRef = FTransform::Identity;
	for (int32 Index = HeadIndex; Index != INDEX_NONE; Index = RefSkeleton.GetParentIndex(Index))
	{
		HeadRef = HeadRef * RefSkeleton.GetRefBonePose()[Index];
	}
	AActor* OwnerActor = GetOwner();
	FaceRoot = NewObject<USceneComponent>(OwnerActor, NAME_None, RF_Transient);
	FaceRoot->SetupAttachment(Body, HeadBoneName);
	FaceRoot->RegisterComponent();
	FaceRoot->SetRelativeTransform(HeadRef.Inverse());

	bool bWet = false;
	UMaterialInterface* PartsMaterial = FacePartsMaterial(bWet);
	bWetAlpha = bWet;

	TongueMesh = MakePart(OwnerActor, FaceRoot);
	RebuildTongueMesh(4.f, true);
	TongueMesh->SetMaterial(0, PartsMaterial);
	TongueMesh->SetVisibility(false);

	for (int32 d = 0; d < 2; ++d)
	{
		UProceduralMeshComponent* Drop = MakePart(OwnerActor, FaceRoot);
		BuildSweatDrop(*Drop, bWetAlpha);
		Drop->SetMaterial(0, PartsMaterial);
		Drop->SetVisibility(false);
		SweatDrops.Add(Drop);
	}
	bPartsReady = true;
	return true;
}

void UTN_TurtleFaceComponent::DestroyParts()
{
	for (UProceduralMeshComponent* Drop : SweatDrops)
	{
		if (Drop) { Drop->DestroyComponent(); }
	}
	SweatDrops.Reset();
	if (TongueMesh)
	{
		TongueMesh->DestroyComponent();
		TongueMesh = nullptr;
	}
	if (FaceRoot)
	{
		FaceRoot->DestroyComponent();
		FaceRoot = nullptr;
	}
	bPartsReady = false;
	bChainValid = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Chat rápido y voz
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TurtleFaceComponent::BindChat()
{
	if (ChatSource.IsValid()) { return; }
	UWorld* World = GetWorld();
	ATN_CoopGameState* GS = World ? World->GetGameState<ATN_CoopGameState>() : nullptr;
	if (!GS) { return; }
	GS->OnQuickChatReceived.AddUniqueDynamic(this, &UTN_TurtleFaceComponent::HandleQuickChat);
	ChatSource = GS;
}

void UTN_TurtleFaceComponent::HandleQuickChat(const FTN_QuickChatEntry& Entry)
{
	// El multicast del GameState llega a todas las máquinas a la vez que el bocadillo del HUD.
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerState* PS = Pawn ? Pawn->GetPlayerState() : nullptr;
	if (!PS || PS->GetPlayerId() != Entry.SenderPlayerId) { return; }
	// Lo largo que es la frase (el catálogo lo tiene el mando local de cada máquina).
	int32 Chars = 18;
	const UWorld* World = GetWorld();
	if (const AMP_GamePlayerController* PC = World ? Cast<AMP_GamePlayerController>(World->GetFirstPlayerController()) : nullptr)
	{
		FText SenderName;
		FText Message;
		UTexture2D* Icon = nullptr;
		if (PC->ResolveQuickChatDisplayData(Entry, SenderName, Message, Icon)) { Chars = Message.ToString().Len(); }
	}
	// Unas 14 letras por segundo, sin pasar del bocadillo (4,5 s).
	TalkFor(FMath::Clamp(0.4f + 0.07f * Chars, 1.f, 4.2f));
}

void UTN_TurtleFaceComponent::TalkFor(float Seconds)
{
	TalkLeft = FMath::Max(TalkLeft, FMath::Clamp(Seconds, 0.f, 8.f));
}

void UTN_TurtleFaceComponent::UpdateTalk(float Dt)
{
	using namespace TNTurtleFaceDetail;
	// La voz de proximidad la añade el mando al poseer: se busca de vez en cuando hasta que aparece.
	VoiceSearchTimer -= Dt;
	if (!Voice.IsValid() && VoiceSearchTimer <= 0.f)
	{
		VoiceSearchTimer = 1.f;
		Voice = GetOwner() ? GetOwner()->FindComponentByClass<UProximityVoiceComponent>() : nullptr;
	}
	TalkLeft = FMath::Max(0.f, TalkLeft - Dt);
	const bool bTalking = TalkLeft > 0.f || (Voice.IsValid() && Voice->IsHeardSpeaking()) || CVarFaceTalk.GetValueOnGameThread() > 0;
	float Target = 0.f;
	if (bTalking)
	{
		SyllableTime += Dt;
		if (SyllableTime >= SyllableLength)
		{
			// Sílaba nueva: de 0,09 a 0,17 s abriendo de un tercio a del todo; a veces, una pausa corta entre palabras.
			SyllableTime = 0.f;
			SyllableLength = FMath::FRandRange(0.09f, 0.17f);
			SyllablePeak = FMath::FRand() < 0.16f ? 0.f : FMath::FRandRange(0.35f, 1.f);
		}
		Target = SyllablePeak * FMath::Sin(PI * FMath::Clamp(SyllableTime / SyllableLength, 0.f, 1.f));
	}
	else
	{
		SyllableTime = SyllableLength;
	}
	TalkLevel = FMath::FInterpTo(TalkLevel, Target, Dt, 25.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Ánimo y objetivos de la cara
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TurtleFaceComponent::UpdateMood(const ATortugaCharacter& Turtle)
{
	using namespace TNTurtleFaceDetail;
	ETNTurtleFaceMood Next = ETNTurtleFaceMood::Happy;
	if (Turtle.IsKnockedDown() || Turtle.IsDead())
	{
		Next = ETNTurtleFaceMood::Down;
	}
	else if (Turtle.IsInShell())
	{
		Next = ETNTurtleFaceMood::Shell;
	}
	else if (const UTN_StaminaComponent* Stamina = Turtle.GetStaminaComponent())
	{
		// Los mismos umbrales (con margen para no parpadear) que las caras del HUD: CurrentStamina y bIsExhausted se
		// replican a todos.
		const float Energy = FMath::Clamp(Stamina->GetCurrentStamina() / FMath::Max(1.f, Stamina->GetMaxStamina()), 0.f, 1.f);
		const bool bWasPanting = Mood == ETNTurtleFaceMood::Panting;
		const bool bWasTired = Mood == ETNTurtleFaceMood::Tired || bWasPanting;
		if (Stamina->IsExhausted() || Energy < (bWasPanting ? 0.3f : 0.22f)) { Next = ETNTurtleFaceMood::Panting; }
		else if (Energy < (bWasTired ? 0.6f : 0.5f)) { Next = ETNTurtleFaceMood::Tired; }
	}
	const int32 Forced = CVarFaceMood.GetValueOnGameThread();
	if (Forced >= 0 && Forced <= 3) { Next = static_cast<ETNTurtleFaceMood>(Forced); }
	if (Next == ETNTurtleFaceMood::Down && Mood != ETNTurtleFaceMood::Down) { DownSide = FMath::FRand() < 0.5f ? -1.f : 1.f; }
	Mood = Next;
}

void UTN_TurtleFaceComponent::BuildGoal(const ATortugaCharacter& Turtle, float Dt, FTNTurtleFaceGoal& Goal)
{
	using namespace TNTurtleFaceDetail;
	const UTN_StaminaComponent* Stamina = Turtle.GetStaminaComponent();
	const float Speed = static_cast<float>(Turtle.GetVelocity().Size2D());
	const float WalkRef = Stamina ? FMath::Max(100.f, Stamina->GetWalkSpeed()) : 450.f;
	const bool bSprint = Stamina && Stamina->IsSprinting() && Speed > WalkRef * 0.6f;
	const bool bDive = Turtle.IsDiving();
	const int32 Emote = Turtle.GetActiveEmoteIndex();
	const float Wind = FMath::Clamp(Speed / 800.f, 0.f, 1.3f);

	// Giro (grados por segundo, suavizado): en las curvas la lengua se va al lado de fuera.
	const float Yaw = static_cast<float>(Turtle.GetActorRotation().Yaw);
	YawRate = FMath::FInterpTo(YawRate, FMath::FindDeltaAngleDegrees(PrevYaw, Yaw) / Dt, Dt, 6.f);
	PrevYaw = Yaw;

	// Jadeo: unas 2,4 respiraciones por segundo.
	if (Mood == ETNTurtleFaceMood::Panting) { BreathPhase = FMath::Fmod(BreathPhase + Dt * 2.f * PI * 2.4f, 2.f * PI); }
	const float Breath = 0.5f + 0.5f * FMath::Sin(BreathPhase);

	// 1. Cara de base según el ánimo (las del HUD: feliz, cansada, jadeando y mareada).
	switch (Mood)
	{
	case ETNTurtleFaceMood::Tired:
		Goal.Mouth = 0.1f;
		Goal.Smile = 0.f;
		Goal.Tired = 0.5f;
		Goal.Blush = 0.35f;
		Goal.Sweat = 1;
		Goal.SweatPeriod = 1.7f;
		break;
	case ETNTurtleFaceMood::Panting:
		Goal.Mouth = 0.78f + 0.22f * Breath;
		Goal.Smile = 0.f;
		Goal.Tired = 1.f;
		Goal.Blush = 0.85f;
		Goal.Sweat = 2;
		Goal.SweatPeriod = 1.15f;
		SetHangTongue(Goal, PantTongueLength, Breath);
		break;
	case ETNTurtleFaceMood::Down:
		// Noqueada: ojos en espiral (TickEyes), boca torcida y la lengua cayendo floja por un lado.
		Goal.Mouth = 0.45f;
		Goal.Smile = 0.f;
		Goal.TongueOut = 1.f;
		Goal.TongueLen = PantTongueLength;
		Goal.TongueSide = DownSide;
		Goal.TongueStart = FVector(0.6, 0.8, -0.2);
		Goal.TongueEnd = FVector(0.4, 0.0, -1.0);
		Goal.TongueStiff = 0.08f;
		Goal.TongueGravity = 1.6f;
		break;
	default:
		break;
	}
	const bool bFree = Mood != ETNTurtleFaceMood::Down && Mood != ETNTurtleFaceMood::Shell;

	// 2. Al viento: esprintando o en pleno panzazo, la lengua sale por un lado y aletea (más cuanto más deprisa).
	bool bWindTongue = false;
	if (bFree && (bSprint || bDive))
	{
		SetWindTongue(Goal, SprintTongueLength, SideGoal, FMath::Clamp(Wind * Wind, 0.2f, 1.5f));
		Goal.Mouth = FMath::Max(Goal.Mouth, 0.6f);
		Goal.Smile *= 0.4f;
		bWindTongue = true;
	}

	// 3. Emotes con cara propia.
	if (bFree)
	{
		switch (Emote)
		{
		case 0: // WAZAAA: boca abierta del todo y la lengua fuera, de lado a lado.
			Goal.TongueOut = 1.f;
			Goal.TongueLen = SprintTongueLength * 0.85f;
			Goal.TongueSide = 0.f;
			Goal.TongueStart = FVector(0.0, 1.0, -0.12);
			Goal.TongueEnd = FVector(0.0, 0.9, -0.45);
			Goal.TongueStiff = 1.4f;
			Goal.TongueGravity = 0.5f;
			Goal.TongueFlap = 0.f;
			Goal.TongueWag = 0.55f * FMath::Sin(FaceClock * 9.f);
			Goal.Mouth = 1.f;
			Goal.Smile = 0.3f;
			break;
		case 1: // HAPPIE: sonrisa enorme.
			Goal.Mouth = 0.85f;
			Goal.Smile = 1.f;
			break;
		case 8: // Modo loco: la lengua al aire, cambiando de lado.
			SetWindTongue(Goal, SprintTongueLength, FMath::Sin(FaceClock * 5.2f) >= 0.f ? 1.f : -1.f, 0.8f);
			Goal.Mouth = 0.8f;
			Goal.Smile = 0.5f;
			bWindTongue = true;
			break;
		case 9: // Fiesta (el clip de gritar): boca a gritos y ojos apretados.
			Goal.Mouth = 0.75f + 0.25f * FMath::Abs(FMath::Sin(FaceClock * 7.f));
			Goal.Smile = 0.2f;
			Goal.bSqueeze = true;
			Goal.TongueOut = 0.f;
			break;
		default:
			break;
		}
	}

	// Lado de la lengua al viento: al empezar, uno al azar; en una curva cerrada, el de fuera (girar a la derecha
	// —guiñada que sube— la lanza a su izquierda, +X de la malla).
	if (bWindTongue && Emote != 8)
	{
		if (!bWasSideTongue) { SideGoal = FMath::FRand() < 0.5f ? -1.f : 1.f; }
		if (FMath::Abs(YawRate) > 80.f) { SideGoal = YawRate > 0.f ? 1.f : -1.f; }
		Goal.TongueSide = SideGoal;
	}
	bWasSideTongue = bWindTongue;

	// 4. Quieta y contenta: de vez en cuando asoma la punta de la lengua un segundo.
	const bool bCalm = Mood == ETNTurtleFaceMood::Happy && Speed < 40.f && Emote < 0 && TalkLevel < 0.05f && TalkLeft <= 0.f && !bWindTongue;
	if (bIdleBlep && bCalm)
	{
		BlepTimer -= Dt;
		if (BlepTimer <= 0.f)
		{
			BlepLeft = 1.1f;
			BlepTimer = FMath::FRandRange(8.f, 16.f);
		}
	}
	else
	{
		BlepTimer = FMath::Max(BlepTimer, 4.f);
	}
	if (BlepLeft > 0.f)
	{
		BlepLeft = bCalm ? BlepLeft - Dt : 0.f;
		if (bCalm) { SetBlepTongue(Goal, FaceClock); }
	}

	// 5. Ojos apretados («>_<»): al agotarse del todo y, jadeando, a ratos, como quien coge aire.
	const bool bExhausted = Stamina && Stamina->IsExhausted();
	if (bExhausted && !bWasExhausted && bFree) { SqueezeLeft = 0.9f; }
	bWasExhausted = bExhausted;
	if (Mood == ETNTurtleFaceMood::Panting)
	{
		GaspTimer -= Dt;
		if (GaspTimer <= 0.f)
		{
			SqueezeLeft = FMath::Max(SqueezeLeft, 0.45f);
			GaspTimer = FMath::FRandRange(3.5f, 6.5f);
		}
	}
	SqueezeLeft = FMath::Max(0.f, SqueezeLeft - Dt);
	Goal.bSqueeze = (Goal.bSqueeze || SqueezeLeft > 0.f) && bFree;

	// 6. Hablando (chat rápido o voz): la boca se abre y se cierra por sílabas encima de la cara que tenga.
	if (TalkLevel > 0.01f && Mood != ETNTurtleFaceMood::Shell)
	{
		Goal.Mouth = FMath::Max(Goal.Mouth * 0.6f, 0.08f) + TalkLevel * 0.62f;
		Goal.Smile *= 0.5f;
	}

	// Pruebas: forzar la lengua desde la consola.
	switch (CVarFaceTongue.GetValueOnGameThread())
	{
	case 0: Goal.TongueOut = 0.f; break;
	case 1: SetWindTongue(Goal, SprintTongueLength, Goal.TongueSide != 0.f ? Goal.TongueSide : 1.f, FMath::Max(1.f, Wind * Wind)); break;
	case 2: SetHangTongue(Goal, PantTongueLength, Breath); break;
	case 3: SetBlepTongue(Goal, FaceClock); break;
	default: break;
	}

	// Con la lengua fuera, la boca abierta; metida en el caparazón, sin cara ni lengua.
	if (Goal.TongueOut > 0.2f) { Goal.Mouth = FMath::Max(Goal.Mouth, 0.5f); }
	if (Mood == ETNTurtleFaceMood::Shell)
	{
		Goal.TongueOut = 0.f;
		Goal.Sweat = 0;
	}
	Goal.Mouth = FMath::Clamp(Goal.Mouth, 0.f, 1.f);
	Goal.Smile = FMath::Clamp(Goal.Smile, 0.f, 1.f);
}

void UTN_TurtleFaceComponent::ApplyFaceMaterial(USkeletalMeshComponent* Body, float Dt, const FTNTurtleFaceGoal& Goal)
{
	TiredShown = FMath::FInterpTo(TiredShown, Goal.Tired, Dt, 5.f);
	BlushShown = FMath::FInterpTo(BlushShown, Goal.Blush, Dt, 2.5f);
	MouthShown = FMath::FInterpTo(MouthShown, Goal.Mouth, Dt, 18.f);
	SmileShown = FMath::FInterpTo(SmileShown, Goal.Smile, Dt, 8.f);

	UMaterialInstanceDynamic* MID = UTN_CosmeticLook::GetBodyMaterial(Body);
	if (!MID) { return; }
	if (MID != FaceMID.Get())
	{
		// Aspecto nuevo (ApplyLook crea otra instancia): se vuelve a escribir todo.
		FaceMID = MID;
		TiredApplied = SqueezeApplied = MouthApplied = SmileApplied = BlushApplied = -1.f;
	}
	auto Write = [MID](const TCHAR* Param, float NewValue, float& LastValue, float Epsilon)
	{
		if (FMath::Abs(NewValue - LastValue) > Epsilon)
		{
			LastValue = NewValue;
			MID->SetScalarParameterValue(Param, NewValue);
		}
	};
	// En espiral (noqueada) el cansancio no se pinta: TickEyes pone EyeDizzy.
	const bool bDizzyFace = Mood == ETNTurtleFaceMood::Down;
	Write(TEXT("EyeTired"), bDizzyFace ? 0.f : TiredShown, TiredApplied, 0.01f);
	Write(TEXT("EyeSqueeze"), Goal.bSqueeze ? 1.f : 0.f, SqueezeApplied, 0.5f);
	Write(TEXT("MouthOpen"), MouthShown, MouthApplied, 0.01f);
	Write(TEXT("MouthSmile"), SmileShown, SmileApplied, 0.01f);
	Write(TEXT("FaceBlush"), BlushShown, BlushApplied, 0.01f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lengua
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TurtleFaceComponent::UpdateTongue(float Dt, const FTNTurtleFaceGoal& Goal, bool bRender)
{
	if (!TongueMesh || !FaceRoot) { return; }
	TongueOut = FMath::FInterpTo(TongueOut, Goal.TongueOut, Dt, Goal.TongueOut > TongueOut ? 9.f : 11.f);
	if (Goal.TongueOut > 0.01f) { TongueLen = FMath::FInterpTo(TongueLen, Goal.TongueLen, Dt, 6.f); }
	TongueSide = FMath::FInterpTo(TongueSide, Goal.TongueSide, Dt, 4.5f);
	TongueStiffShown = FMath::FInterpTo(TongueStiffShown, Goal.TongueStiff, Dt, 4.f);
	const float LengthUnits = TongueLen * TongueOut;
	const bool bShow = bRender && LengthUnits > 0.35f;
	if (TongueMesh->IsVisible() != bShow) { TongueMesh->SetVisibility(bShow); }
	if (!bShow)
	{
		// Al volver a salir, la cadena se recoloca en su forma de reposo.
		bChainValid = false;
		return;
	}
	SimulateTongue(Dt, LengthUnits, Goal);
	RebuildTongueMesh(LengthUnits, false);
}

void UTN_TurtleFaceComponent::ResetTongueChain(const FTransform& FaceToWorld, const FVector& RootFace, const FVector& StartFace, const FVector& EndFace, float SegmentWorld)
{
	// En su forma de reposo y moviéndose ya con la tortuga (si no, el primer fotograma tiraría de ella hacia atrás).
	const FVector StartVelocity = GetOwner() ? GetOwner()->GetVelocity() : FVector::ZeroVector;
	ChainPos[0] = FaceToWorld.TransformPosition(RootFace);
	ChainVel[0] = StartVelocity;
	for (int32 k = 1; k < ChainNum; ++k)
	{
		const float Along = static_cast<float>(k - 1) / (ChainNum - 2);
		const FVector Dir = FaceToWorld.TransformVectorNoScale(FMath::Lerp(StartFace, EndFace, static_cast<double>(Along)).GetSafeNormal(UE_SMALL_NUMBER, StartFace));
		ChainPos[k] = ChainPos[k - 1] + Dir * SegmentWorld;
		ChainVel[k] = StartVelocity;
	}
	LastRootWorld = ChainPos[0];
	bChainValid = true;
}

void UTN_TurtleFaceComponent::SimulateTongue(float Dt, float LengthUnits, const FTNTurtleFaceGoal& Goal)
{
	using namespace TNTurtleFaceDetail;
	// Espacio de la cara = el de la malla en su postura de referencia, pegado a la cabeza animada.
	const FTransform FaceToWorld = FaceRoot->GetComponentTransform();
	const float UnitScale = FMath::Max(0.01f, static_cast<float>(FaceToWorld.GetScale3D().GetAbsMax()));
	const float SegWorld = FMath::Max(0.05f, LengthUnits * UnitScale / (ChainNum - 1));

	// Raíz dentro del hueco de la boca (al centro o hacia una comisura) y forma de reposo: del primer tramo (la
	// salida) al último, reflejada según el lado; el meneo mueve la salida de lado a lado.
	const FVector RootFace = TongueRootCenter + FVector(TongueRootSide * TongueSide, 0.0, 0.0);
	const FVector StartFace = FVector(Goal.TongueStart.X * TongueSide + Goal.TongueWag, Goal.TongueStart.Y, Goal.TongueStart.Z).GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0, 1.0, 0.0));
	const FVector EndFace = FVector(Goal.TongueEnd.X * TongueSide + 0.5 * Goal.TongueWag, Goal.TongueEnd.Y, Goal.TongueEnd.Z).GetSafeNormal(UE_SMALL_NUMBER, StartFace);
	const FVector RootWorld = FaceToWorld.TransformPosition(RootFace);
	const FVector UpWorld = FaceToWorld.GetUnitAxis(EAxis::Z);
	// Primera vez o teletransporte (reaparecer, viaje): se recoloca en reposo.
	if (!bChainValid || FVector::DistSquared(RootWorld, LastRootWorld) > FMath::Square(150.0))
	{
		ResetTongueChain(FaceToWorld, RootFace, StartFace, EndFace, SegWorld);
	}
	FVector RestDir[ChainNum];
	RestDir[0] = FaceToWorld.TransformVectorNoScale(StartFace);
	for (int32 k = 1; k < ChainNum; ++k)
	{
		const float Along = static_cast<float>(k - 1) / (ChainNum - 2);
		RestDir[k] = FaceToWorld.TransformVectorNoScale(FMath::Lerp(StartFace, EndFace, static_cast<double>(Along)).GetSafeNormal(UE_SMALL_NUMBER, StartFace));
	}

	// Pasos de ~1/120 s: estable a cualquier ritmo de fotogramas.
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(Dt * 120.f), 1, 4);
	const float H = Dt / Steps;
	const FVector GravityAcc(0.0, 0.0, -980.0 * TongueGravity * Goal.TongueGravity);
	const float Damp = FMath::Exp(-4.f * H);
	// Rigideces como muelles (1/s²) aplicados como fracción de corrección por paso (k·h²): la salida de la boca, firme;
	// el resto, más blando hacia la punta.
	const float RootAlpha = FMath::Clamp(5000.f * H * H, 0.f, 0.9f);
	const float ShapeK = TongueShapeStiffness * TongueStiffShown;
	// Aleteo: onda que corre de la raíz a la punta (arriba y abajo), más rápida y fuerte cuanto más viento.
	const float FlapAmp = 1000.f * TongueFlap * Goal.TongueFlap;
	const float FlapOmega = 24.f + 8.f * Goal.TongueFlap;
	const float PantAmp = 1400.f * Goal.TonguePant * FMath::Sin(BreathPhase);

	FVector Pred[ChainNum];
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		FlapPhase = FMath::Fmod(FlapPhase + H * FlapOmega, 2.f * PI * 64.f);
		// La raíz va con la cabeza (interpolada dentro del fotograma). La cara con la que se choca es la de este mismo
		// instante: la del final del fotograma, retrasada lo que aún le falta a la raíz (si no, a la carrera todo parece
		// metido en la cara y la lengua sale disparada hacia delante).
		const double Blend = static_cast<double>(Step + 1) / Steps;
		Pred[0] = FMath::Lerp(LastRootWorld, RootWorld, Blend);
		const FVector Lag = (RootWorld - LastRootWorld) * (1.0 - Blend);
		for (int32 k = 1; k < ChainNum; ++k)
		{
			const float Along = static_cast<float>(k) / (ChainNum - 1);
			// Gravedad, rozamiento con el aire (la velocidad de la carrera la echa hacia atrás), aleteo y el bombeo del jadeo.
			FVector Acc = GravityAcc - ChainVel[k] * TongueAirDrag;
			if (FlapAmp > 0.f)
			{
				Acc += UpWorld * (FlapAmp * FMath::Pow(Along, 1.3f) * FMath::Sin(FlapPhase - 0.45f * k));
			}
			Acc += UpWorld * (PantAmp * Along);
			ChainVel[k] = ((ChainVel[k] + Acc * H) * Damp).GetClampedToMaxSize(4000.0);
			Pred[k] = ChainPos[k] + ChainVel[k] * H;
		}
		// Forma: cada tramo quiere apuntar a su dirección de reposo (muelles de ángulo).
		for (int32 k = 1; k < ChainNum; ++k)
		{
			const float Along = static_cast<float>(k - 1) / (ChainNum - 2);
			const float Alpha = k == 1 ? RootAlpha : FMath::Clamp(ShapeK * FMath::Lerp(1.f, 0.4f, Along) * H * H, 0.f, 0.9f);
			Pred[k] = FMath::Lerp(Pred[k], Pred[k - 1] + RestDir[k] * SegWorld, static_cast<double>(Alpha));
		}
		// Largo fijo (cada punto sigue al de delante) y sin meterse en la cara: fuera de la boca, lo que entra se saca por
		// la normal de la superficie (en las mejillas, hacia el lado), así que al viento resbala por la mejilla.
		for (int32 k = 1; k < ChainNum; ++k)
		{
			Pred[k] = Pred[k - 1] + (Pred[k] - Pred[k - 1]).GetSafeNormal(UE_SMALL_NUMBER, RestDir[k]) * SegWorld;
			if (k >= 2)
			{
				FVector Local = FaceToWorld.InverseTransformPosition(Pred[k] + Lag);
				if (PushOutOfFace(Local, 0.55f))
				{
					Pred[k] = FaceToWorld.TransformPosition(Local) - Lag;
				}
			}
		}
		for (int32 k = 1; k < ChainNum; ++k)
		{
			ChainVel[k] = (Pred[k] - ChainPos[k]) / H;
			ChainPos[k] = Pred[k];
		}
		ChainPos[0] = Pred[0];
	}
	LastRootWorld = RootWorld;
}

void UTN_TurtleFaceComponent::RebuildTongueMesh(float LengthUnits, bool bCreate)
{
	using namespace TNTurtleFaceDetail;
	if (!TongueMesh || !FaceRoot) { return; }

	// Puntos de la cadena en el espacio de la cara (al crear, una lengua recta cualquiera: solo cuenta la topología).
	FVector Pts[ChainNum];
	if (bCreate)
	{
		for (int32 k = 0; k < ChainNum; ++k) { Pts[k] = TongueRootCenter + FVector(0.0, 0.6 * k, 0.0); }
	}
	else
	{
		const FTransform FaceToWorld = FaceRoot->GetComponentTransform();
		for (int32 k = 0; k < ChainNum; ++k) { Pts[k] = FaceToWorld.InverseTransformPosition(ChainPos[k]); }
	}

	// Curva suave por los puntos (Catmull-Rom) y un marco que viaja por ella sin retorcerse (transporte paralelo): la
	// cara ancha de la lengua empieza mirando arriba.
	FVector Centers[TongueRings];
	for (int32 r = 0; r < TongueRings; ++r)
	{
		const float U = static_cast<float>(r) / (TongueRings - 1) * (ChainNum - 1);
		const int32 I1 = FMath::Min(static_cast<int32>(U), ChainNum - 2);
		Centers[r] = CatmullRom(Pts[FMath::Max(I1 - 1, 0)], Pts[I1], Pts[I1 + 1], Pts[FMath::Min(I1 + 2, ChainNum - 1)], U - I1);
	}
	FVector Along[TongueRings];
	FVector Up[TongueRings];
	FVector Across[TongueRings];
	for (int32 r = 0; r < TongueRings; ++r)
	{
		Along[r] = (Centers[FMath::Min(r + 1, TongueRings - 1)] - Centers[FMath::Max(r - 1, 0)]).GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0, 1.0, 0.0));
	}
	Up[0] = (FVector::UpVector - Along[0] * Along[0].Z).GetSafeNormal(UE_SMALL_NUMBER, FVector(1.0, 0.0, 0.0));
	for (int32 r = 0; r < TongueRings; ++r)
	{
		if (r > 0)
		{
			const FVector Carried = FQuat::FindBetweenNormals(Along[r - 1], Along[r]).RotateVector(Up[r - 1]);
			Up[r] = (Carried - Along[r] * FVector::DotProduct(Carried, Along[r])).GetSafeNormal(UE_SMALL_NUMBER, Up[r - 1]);
		}
		Across[r] = FVector::CrossProduct(Up[r], Along[r]);
	}

	// Perfil: la raíz algo estrecha, se ensancha y acaba en una punta redonda; plana (grosor de un tercio del ancho).
	const float TipLength = FMath::Min(1.2f, 0.45f * FMath::Max(LengthUnits, 0.1f));
	const float TipStart = 1.f - TipLength / FMath::Max(LengthUnits, 0.1f);
	const int32 NumVerts = TongueRings * TongueRingVerts + 1;
	TongueVerts.SetNum(NumVerts);
	TongueNormals.SetNum(NumVerts);
	for (int32 r = 0; r < TongueRings; ++r)
	{
		const float S = static_cast<float>(r) / (TongueRings - 1);
		const float TipT = S <= TipStart ? 0.f : (S - TipStart) / FMath::Max(1.f - TipStart, 0.01f);
		const float Round = FMath::Max(0.06f, FMath::Sqrt(FMath::Max(0.f, 1.f - TipT * TipT)));
		const float HalfW = TongueHalfWidth * (0.78f + 0.22f * Smooth01(0.f, 0.35f, S)) * Round;
		const float HalfT = FMath::Max(0.08f, 0.33f * HalfW);
		for (int32 j = 0; j < TongueRingVerts; ++j)
		{
			const float Ang = FMath::DegreesToRadians(TongueRingAngles[j]);
			const float Ca = FMath::Cos(Ang);
			const float Sa = FMath::Sin(Ang);
			const int32 V = r * TongueRingVerts + j;
			TongueVerts[V] = Centers[r] + Across[r] * (Ca * HalfW) + Up[r] * (Sa * HalfT);
			TongueNormals[V] = (Across[r] * (Ca / HalfW) + Up[r] * (Sa / HalfT)).GetSafeNormal(UE_SMALL_NUMBER, Up[r]);
		}
	}
	TongueVerts[NumVerts - 1] = Centers[TongueRings - 1] + Along[TongueRings - 1] * 0.1;
	TongueNormals[NumVerts - 1] = Along[TongueRings - 1];

	if (!bCreate)
	{
		// Misma topología: solo posiciones y normales (colores y triángulos, los de la creación).
		const TArray<FVector2D> KeepUVs;
		const TArray<FColor> KeepColors;
		const TArray<FProcMeshTangent> KeepTangents;
		TongueMesh->UpdateMeshSection(0, TongueVerts, TongueNormals, KeepUVs, KeepColors, KeepTangents);
		return;
	}

	// Colores (fijos): rosa del HUD, el surco de arriba más oscuro hasta cerca de la punta, la cara de abajo algo más
	// oscura, la punta más clara y la raíz en sombra (está dentro de la boca).
	const float WetA = bWetAlpha ? 0.8f : 0.f;
	const FLinearColor Pink = Lin(0xFF6F8E, WetA);
	const FLinearColor Groove = Lin(0xD94A6A, WetA);
	const FLinearColor Under = Lin(0xE85A7C, WetA);
	const FLinearColor TipPink = Lin(0xFF8CA6, WetA);
	TArray<FLinearColor> Colors;
	Colors.SetNum(NumVerts);
	for (int32 r = 0; r < TongueRings; ++r)
	{
		const float S = static_cast<float>(r) / (TongueRings - 1);
		for (int32 j = 0; j < TongueRingVerts; ++j)
		{
			const float Sa = FMath::Sin(FMath::DegreesToRadians(TongueRingAngles[j]));
			FLinearColor Col = Pink;
			if (Sa < -0.35f) { Col = FMath::Lerp(Col, Under, 0.6f * -Sa); }
			if (j == 0) { Col = FMath::Lerp(Col, Groove, 0.9f * Smooth01(0.9f, 0.65f, S)); }
			Col = FMath::Lerp(Col, TipPink, 0.4f * Smooth01(0.8f, 1.f, S));
			const float Shade = FMath::Lerp(0.55f, 1.f, Smooth01(0.f, 0.2f, S));
			Colors[r * TongueRingVerts + j] = FLinearColor(Col.R * Shade, Col.G * Shade, Col.B * Shade, WetA);
		}
	}
	Colors[NumVerts - 1] = TipPink;

	TArray<int32> Tris;
	Tris.Reserve((TongueRings - 1) * TongueRingVerts * 6 + TongueRingVerts * 3);
	for (int32 r = 0; r + 1 < TongueRings; ++r)
	{
		for (int32 j = 0; j < TongueRingVerts; ++j)
		{
			const int32 A = r * TongueRingVerts + j;
			const int32 B = r * TongueRingVerts + (j + 1) % TongueRingVerts;
			const int32 C = (r + 1) * TongueRingVerts + (j + 1) % TongueRingVerts;
			const int32 D = (r + 1) * TongueRingVerts + j;
			AddOrientedTri(Tris, TongueVerts, TongueNormals, A, B, C);
			AddOrientedTri(Tris, TongueVerts, TongueNormals, A, C, D);
		}
	}
	const int32 Last = (TongueRings - 1) * TongueRingVerts;
	for (int32 j = 0; j < TongueRingVerts; ++j)
	{
		AddOrientedTri(Tris, TongueVerts, TongueNormals, NumVerts - 1, Last + j, Last + (j + 1) % TongueRingVerts);
	}
	const TArray<FVector2D> NoUVs;
	const TArray<FProcMeshTangent> NoTangents;
	TongueMesh->CreateMeshSection_LinearColor(0, TongueVerts, Tris, TongueNormals, NoUVs, Colors, NoTangents, false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Sudor y visibilidad
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TurtleFaceComponent::UpdateSweat(float Dt, const FTNTurtleFaceGoal& Goal, bool bRender)
{
	using namespace TNTurtleFaceDetail;
	SweatShown = FMath::FInterpConstantTo(SweatShown, Goal.Sweat > 0 ? 1.f : 0.f, Dt, 2.5f);
	SweatClock += Dt;
	const float Period = FMath::Max(0.5f, Goal.SweatPeriod);
	for (int32 d = 0; d < SweatDrops.Num(); ++d)
	{
		UProceduralMeshComponent* Drop = SweatDrops[d];
		if (!Drop) { continue; }
		// Gota junto al casco, a su izquierda (+X) y, jadeando, otra a la derecha a contratiempo: aparece con un
		// saltito, resbala cada vez más deprisa y se encoge al final.
		const float Phase = FMath::Frac(SweatClock / Period + 0.5f * d);
		const float Pop = Smooth01(0.f, 0.14f, Phase) * (1.f + 0.25f * FMath::Sin(FMath::Clamp(Phase / 0.14f, 0.f, 1.f) * PI));
		const float Fade = 1.f - Smooth01(0.78f, 1.f, Phase);
		const float DropScale = Pop * Fade * SweatShown;
		const bool bShow = bRender && (d == 0 || Goal.Sweat > 1) && DropScale > 0.02f;
		if (Drop->IsVisible() != bShow) { Drop->SetVisibility(bShow); }
		if (!bShow) { continue; }
		const float SideSign = d == 0 ? 1.f : -1.f;
		const FVector Start(11.9f * SideSign, 7.2f, 50.6f);
		const FVector End(12.3f * SideSign, 7.8f, 46.6f);
		Drop->SetRelativeLocationAndRotation(FMath::Lerp(Start, End, static_cast<double>(Phase * Phase)), FRotator(14.f * SideSign, 0.f, 0.f));
		Drop->SetRelativeScale3D(FVector(DropScale));
	}
}

void UTN_TurtleFaceComponent::SyncPartVisibility(USkeletalMeshComponent* Body)
{
	// Como la malla: si su dueño no la ve, tampoco la lengua ni el sudor.
	const bool bNoSee = Body->bOwnerNoSee;
	if (TongueMesh && TongueMesh->bOwnerNoSee != bNoSee) { TongueMesh->SetOwnerNoSee(bNoSee); }
	for (UProceduralMeshComponent* Drop : SweatDrops)
	{
		if (Drop && Drop->bOwnerNoSee != bNoSee) { Drop->SetOwnerNoSee(bNoSee); }
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Fotograma
// ─────────────────────────────────────────────────────────────────────────────

void UTN_TurtleFaceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(GetOwner());
	USkeletalMeshComponent* Body = Turtle ? Turtle->GetMesh() : nullptr;
	if (!Body) { return; }
	if (!EnsureParts(Body))
	{
		// Otra malla (sin el hueco de la boca de la de demo): nada que hacer.
		if (bPartsTried) { SetComponentTickEnabled(false); }
		return;
	}
	const float Dt = FMath::Clamp(DeltaTime, 1e-4f, 0.1f);
	FaceClock += Dt;
	BindChat();
	UpdateMood(*Turtle);
	UpdateTalk(Dt);
	FTNTurtleFaceGoal Goal;
	BuildGoal(*Turtle, Dt, Goal);
	ApplyFaceMaterial(Body, Dt, Goal);
	SyncPartVisibility(Body);
	// Oculta o lejos de toda cámara: sin física ni mallas (la cadena se recoloca al volver a verse).
	const bool bRender = Body->ShouldRender() && Body->WasRecentlyRendered(0.3f);
	UpdateTongue(Dt, Goal, bRender);
	UpdateSweat(Dt, Goal, bRender);
}
