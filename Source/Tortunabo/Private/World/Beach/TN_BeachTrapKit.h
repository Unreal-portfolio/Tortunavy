#pragma once

#include "CoreMinimal.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Player/TortugaCharacter.h"
#include "World/Beach/TN_BeachEnemy.h"
#include "World/Beach/TN_BeachStun.h"
#include "World/Beach/TN_BeachTypes.h"
#include "../../Art/TN_ArtPieces.h"
#include "../../Lobby/Playground/TN_PlaygroundMeshKit.h"

/**
 * Kit de las trampas de la playa (Private/World/Beach): reglas comunes (quién simula a quién, quién puede caer en una
 * trampa), medidas de la huella y piezas de malla de arena sobre el kit del parque de pruebas del lobby
 * (TNPlaygroundKit: colores con brillo en el alfa para M_CosmeticVertexColor, cajas, tubos, conchas, estrellas...).
 */
namespace TNBeachTrapKit
{
	using FBuffers = TNPlaygroundKit::FBuffers;
	using FHulls = TArray<TArray<FVector>>;

	/** Solo simulan el movimiento de un personaje el servidor y el cliente que lo controla. */
	inline bool SimulatesMovement(const APawn* Pawn)
	{
		return Pawn && (Pawn->IsLocallyControlled() || Pawn->HasAuthority());
	}

	/**
	 * Tortuga (no otros personajes, como los enemigos) que puede caer en una trampa: viva, fuera del caparazón y sin aturdir,
	 * que no sujete un enemigo (lo que la mueve manda: una trampa no la relanza por encima). Todo eso se
	 * ve igual en todas las máquinas (las trampas que el dueño predice lo miran también); las reservas de la tormenta y de la
	 * red de seguridad son solo del servidor y ya las respetan StunTurtle y TNBeachRideKit::LaunchAsBall.
	 */
	inline bool IsFreeTurtle(const ACharacter* Character)
	{
		const ATortugaCharacter* Turtle = Cast<ATortugaCharacter>(Character);
		if (!IsValid(Turtle) || Turtle->IsDead() || Turtle->IsInShell())
		{
			return false;
		}
		return !TNBeach::IsTurtleStunned(Turtle) && !ATN_BeachEnemy::IsTurtleHeld(Turtle);
	}

	/** Hora del servidor sin suavizar (en el servidor, la del mundo). */
	inline double ServerNow(const UWorld* World)
	{
		if (!World)
		{
			return 0.0;
		}
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			return GameState->GetServerWorldTimeSeconds();
		}
		return World->GetTimeSeconds();
	}

	/** Radio de la huella (cm) del elemento con su SizeScale: todo el elemento cabe dentro. */
	inline double FitRadius(ETNBeachElement Element, float SizeScale)
	{
		return TNBeach::FootprintRadius(Element) * FMath::Clamp(static_cast<double>(SizeScale), 0.3, 2.0);
	}

	/** Mezcla de bits entera (decisiones iguales en todas las máquinas a partir de la semilla). */
	inline uint32 MixBits(uint32 X)
	{
		X ^= X >> 16;
		X *= 0x7feb352dU;
		X ^= X >> 15;
		X *= 0x846ca68bU;
		X ^= X >> 16;
		return X;
	}

	/** Semilla derivada de la de la variante y una sal por uso. */
	inline uint32 SeedOf(int32 Seed, uint32 Salt)
	{
		return MixBits(static_cast<uint32>(Seed) * 0x9E3779B9u + Salt * 0x85EBCA6Bu + 0x2545F491u);
	}

	/** Número estable en [0, 1) por índices. */
	inline double Hash01(int32 A, int32 B, uint32 Seed)
	{
		return TNPlaygroundKit::Hash01(A, B, Seed);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Colores
	// ─────────────────────────────────────────────────────────────────────────

	inline FLinearColor SandSide() { return TNPlaygroundKit::Rgb(0xE8C889); }
	inline FLinearColor SandTop() { return TNPlaygroundKit::Rgb(0xF6E0AE); }
	inline FLinearColor SandMark() { return TNPlaygroundKit::Rgb(0xD2AC6A); }
	inline FLinearColor SandWet() { return TNPlaygroundKit::Rgb(0xB8925A); }
	inline FLinearColor SandDeep() { return TNPlaygroundKit::Rgb(0x9C7A48); }
	inline FLinearColor WoodTone(int32 Index) { static const uint32 Hex[3] = { 0xC8925A, 0xB98049, 0xA8764A }; return TNPlaygroundKit::Rgb(Hex[((Index % 3) + 3) % 3]); }
	inline FLinearColor RockTone(int32 Index) { static const uint32 Hex[3] = { 0x9A9690, 0x8A857E, 0xA8A39A }; return TNPlaygroundKit::Rgb(Hex[((Index % 3) + 3) % 3], 0.05f); }
	inline FLinearColor ShellTone(int32 Index) { static const uint32 Hex[5] = { 0xFFD6C9, 0xFFE9D6, 0xF7C6B8, 0xE9D5F2, 0xFFF3E4 }; return TNPlaygroundKit::Rgb(Hex[((Index % 5) + 5) % 5], 0.3f); }

	// ─────────────────────────────────────────────────────────────────────────
	// Componentes
	// ─────────────────────────────────────────────────────────────────────────

	/** Malla visual sin colisión. */
	inline void ConfigureVisual(UStaticMeshComponent* Comp)
	{
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCanEverAffectNavigation(false);
	}

	/** Colisión convexa sólida (solo cascos, sin secciones); con bBlockCamera también para la cámara. */
	inline void ConfigureSolid(UProceduralMeshComponent* Comp, bool bBlockCamera)
	{
		Comp->bUseComplexAsSimpleCollision = false;
		Comp->bUseAsyncCooking = false;
		Comp->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
		if (!bBlockCamera)
		{
			Comp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		}
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCanEverAffectNavigation(false);
	}

	/** Pone en Comp la malla estática de los buffers (o ninguna si están vacíos). */
	inline void SetMesh(UStaticMeshComponent* Comp, UObject* Outer, const FBuffers& B)
	{
		if (!Comp)
		{
			return;
		}
		Comp->SetStaticMesh(B.IsEmpty() ? nullptr : TNPlaygroundKit::BuildMesh(Outer, B, TNPlaygroundKit::VertexColorMaterial()));
	}

	/**
	 * Lo mismo, como pieza de arte Slot (Docs/Arte_Assets.md, TNArt::SetMesh): con sustituto, la malla de arte va de hija de
	 * Comp y se mueve, se esconde y se enseña con él. Llamar con las sombras y la colisión de Comp ya puestas.
	 */
	inline void SetMesh(UStaticMeshComponent* Comp, UObject* Outer, const FBuffers& B, FName Slot)
	{
		if (!Comp)
		{
			return;
		}
		TNArt::SetMesh(Comp, B.IsEmpty() ? nullptr : TNPlaygroundKit::BuildMesh(Outer, B, TNPlaygroundKit::VertexColorMaterial()), Slot);
	}

	/**
	 * Como SetMesh, con piezas de arte dentro de la malla (las de Log, TN_ArtPieces.h): sin las que tienen sustituto. Su malla
	 * de arte la pone luego TNArt::SpawnPieceArt(Comp, Log), una vez para todas las mallas del registro (los ejes de Comp han
	 * de ser los de los buffers). Sin sustitutos, exactamente SetMesh. Solo para mallas sin colisión (ConfigureVisual).
	 */
	inline void SetMeshWithPieces(UStaticMeshComponent* Comp, UObject* Outer, const FBuffers& B, const TNArt::FPieceLog& Log)
	{
		if (!Comp)
		{
			return;
		}
		TArray<TNArt::FPieceRange> Removed;
		// Como TNArt: solo donde se pueden cambiar las mallas (si no, SpawnPieceArt no pone nada).
		if (TNArt::CanModify(Comp))
		{
			Log.CollectRemoved(B, false, Removed);
		}
		if (Removed.Num() == 0)
		{
			SetMesh(Comp, Outer, B);
			return;
		}
		FBuffers Visible;
		TNArt::FilterBuffers(B, Removed, Visible);
		SetMesh(Comp, Outer, Visible);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Arena
	// ─────────────────────────────────────────────────────────────────────────

	/**
	 * Bloque de arena de molde entre Min y Max (ejes del actor): lados de arena, tapa más clara, franja húmeda abajo y
	 * marcas de cubo cada 70 cm; con Hulls, su casco de colisión.
	 */
	inline void AddSandBox(FBuffers& B, FHulls* Hulls, const FVector& Min, const FVector& Max, bool bMarks = true)
	{
		const FVector Center = (Min + Max) * 0.5;
		const FVector Half = (Max - Min) * 0.5;
		if (Half.X < 0.5 || Half.Y < 0.5 || Half.Z < 0.5)
		{
			return;
		}
		TNPlaygroundKit::AddAxisBox(B, Center, Half, SandSide());
		TNPlaygroundKit::AddAxisBox(B, FVector(Center.X, Center.Y, Max.Z + 1.0), FVector(FMath::Max(1.0, Half.X - 5.0), FMath::Max(1.0, Half.Y - 5.0), 1.0), SandTop());
		if (bMarks && Half.Z > 45.0)
		{
			for (double Z = Min.Z + 60.0; Z < Max.Z - 30.0; Z += 70.0)
			{
				TNPlaygroundKit::AddAxisBox(B, FVector(Center.X, Center.Y, Z), FVector(Half.X + 2.5, Half.Y + 2.5, 4.0), SandMark());
			}
		}
		if (Hulls)
		{
			Hulls->Add(TNPlaygroundKit::HullAxisBox(Center, Half));
		}
	}

	/**
	 * Rampa de arena (cuña) en el espacio de Xf: sube a lo largo de X local de ZLow (en X = 0) a ZHigh (en X = Length), con
	 * HalfWidth de semiancho y la base en ZBottom. Con Hulls, su casco.
	 */
	inline void AddSandRamp(FBuffers& B, FHulls* Hulls, const FTransform& Xf, double Length, double HalfWidth, double ZLow, double ZHigh, double ZBottom)
	{
		const FVector P[8] = {
			Xf.TransformPosition(FVector(0.0, -HalfWidth, ZBottom)), Xf.TransformPosition(FVector(0.0, HalfWidth, ZBottom)),
			Xf.TransformPosition(FVector(Length, HalfWidth, ZBottom)), Xf.TransformPosition(FVector(Length, -HalfWidth, ZBottom)),
			Xf.TransformPosition(FVector(0.0, -HalfWidth, ZLow)), Xf.TransformPosition(FVector(0.0, HalfWidth, ZLow)),
			Xf.TransformPosition(FVector(Length, HalfWidth, ZHigh)), Xf.TransformPosition(FVector(Length, -HalfWidth, ZHigh)) };
		const FVector Up = Xf.TransformVectorNoScale(FVector::UpVector);
		const FVector Fwd = Xf.TransformVectorNoScale(FVector::ForwardVector);
		const FVector Side = Xf.TransformVectorNoScale(FVector::RightVector);
		const FVector SlopeN = (Up * Length - Fwd * (ZHigh - ZLow)).GetSafeNormal();
		B.AddQuad(P[4], P[5], P[6], P[7], SlopeN, SandTop());
		B.AddQuad(P[0], P[3], P[7], P[4], -Side, SandSide());
		B.AddQuad(P[1], P[5], P[6], P[2], Side, SandSide());
		B.AddQuad(P[2], P[6], P[7], P[3], Fwd, TNPlaygroundKit::Shade(SandSide(), 0.92));
		if (ZLow > ZBottom + 1.0)
		{
			B.AddQuad(P[0], P[4], P[5], P[1], -Fwd, TNPlaygroundKit::Shade(SandSide(), 0.92));
		}
		if (Hulls)
		{
			TArray<FVector> Hull;
			for (const FVector& Pt : P)
			{
				Hull.Add(Pt);
			}
			Hulls->Add(Hull);
		}
	}

	/**
	 * Superficie de revolución de caras planas (arena apilada, cráteres, rocas redondas): el perfil va en (radio, altura)
	 * con el aire a su izquierda (al recorrerlo, la cara mira a la izquierda), Seg lados y un temblor radial de hecho a mano.
	 * Colors, uno por tramo del perfil.
	 */
	inline void AddLatheProfile(FBuffers& B, const FVector& Center, const TArray<FVector2D>& Profile, const TArray<FLinearColor>& Colors, int32 Seg, double Jitter,
		uint32 Seed)
	{
		const int32 NumP = Profile.Num();
		if (NumP < 2 || Seg < 3)
		{
			return;
		}
		TArray<FVector> Grid;
		Grid.SetNum(NumP * Seg);
		for (int32 p = 0; p < NumP; ++p)
		{
			for (int32 k = 0; k < Seg; ++k)
			{
				const double A = TNPlaygroundKit::KitTwoPi * k / Seg;
				const double R = FMath::Max(0.0, Profile[p].X * (1.0 + Jitter * TNProcMesh::TNProcHashNoise(p, k, Seed)));
				Grid[p * Seg + k] = Center + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, Profile[p].Y);
			}
		}
		for (int32 p = 0; p + 1 < NumP; ++p)
		{
			const FVector2D Dir = Profile[p + 1] - Profile[p];
			const FVector2D Left(-Dir.Y, Dir.X);
			const FLinearColor& Col = Colors.IsValidIndex(p) ? Colors[p] : (Colors.Num() > 0 ? Colors.Last() : SandSide());
			for (int32 k = 0; k < Seg; ++k)
			{
				const int32 K1 = (k + 1) % Seg;
				const double Am = TNPlaygroundKit::KitTwoPi * (k + 0.5) / Seg;
				const FVector Hint(Left.X * FMath::Cos(Am), Left.X * FMath::Sin(Am), Left.Y);
				B.AddQuad(Grid[p * Seg + k], Grid[p * Seg + K1], Grid[(p + 1) * Seg + K1], Grid[(p + 1) * Seg + k], Hint, Col);
			}
		}
	}

	/** Casco de un tramo de corona (anillo) entre los ángulos A0 y A1 con la sección convexa Section (radio, altura). */
	inline TArray<FVector> HullRingSector(const FVector& Center, double A0, double A1, const TArray<FVector2D>& Section)
	{
		TArray<FVector> Pts;
		Pts.Reserve(Section.Num() * 2);
		for (const double A : { A0, A1 })
		{
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.0);
			for (const FVector2D& S : Section)
			{
				Pts.Add(Center + Dir * S.X + FVector(0.0, 0.0, S.Y));
			}
		}
		return Pts;
	}

	/** Guijarro redondeado (adorno del suelo). */
	inline void AddPebble(FBuffers& B, const FVector& At, double Radius, uint32 Seed, const FLinearColor& Color)
	{
		TNPlaygroundKit::AddEllipsoid(B, At, FVector::ForwardVector, FVector::RightVector, FVector::UpVector,
			FVector(Radius * (0.9 + 0.3 * Hash01(1, 2, Seed)), Radius * (0.8 + 0.3 * Hash01(3, 4, Seed)), Radius * 0.55), 8, 4, Color);
	}
}
