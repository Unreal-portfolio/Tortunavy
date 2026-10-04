#pragma once
#include "Multiplayer/TN_LocalViews.h"

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "TN_ProcMapMeshKit.h"
#include "TN_ProcMapRuntimeMesh.h"

/**
 * Efectos ambientales ligeros, solo visuales y locales en cada máquina: partículas que son instancias
 * de una malla low-poly (gotas, bocanadas de vapor, brasas, copos) movidas en el Tick de su dueño
 * (géiser, tobogán, generador), y bandadas de pájaros en círculo. Sin Niagara: baratos, con el estilo
 * de caras planas del mapa y dormidos lejos de la cámara local. Nada de esto existe en un servidor
 * dedicado.
 */
namespace TNAmbientFX
{
	using namespace TNProcMesh;

	enum class EShape : uint8 { Drop, Puff, Ember, Flake, Leaf, Streak, Ring };

	/** Cómo nace y se mueve cada partícula de un emisor. */
	struct FEmitterDesc
	{
		EShape Shape = EShape::Drop;
		/** Translúcido y sin iluminación (vapor, bruma, espuma, brasas que brillan). */
		bool bSoft = false;
		/** Con bSoft: borde difuminado (nube redonda y blanda en vez de un polígono). */
		bool bCloud = false;
		FLinearColor Color = FLinearColor::White;
		/** Opacidad del material suave (alfa del vértice). */
		float Alpha = 0.5f;
		int32 MaxParticles = 40;
		/** Partículas por segundo (multiplicado por FEmitter::RateScale). */
		float Rate = 20.f;
		/** Nacen en un disco horizontal de este radio alrededor del origen... */
		float SpawnRadius = 50.f;
		/** ...y hasta esta altura sobre él (0 = en el disco). */
		float SpawnHeight = 0.f;
		FVector Direction = FVector::UpVector;
		float Speed = 600.f;
		float SpeedJitter = 0.3f;
		/** Apertura del cono de salida (0 = recto, 1 = ~45°). */
		float Spread = 0.25f;
		float Gravity = -980.f;
		/** Empuje hacia arriba (vapor, brasas). */
		float Buoyancy = 0.f;
		float Drag = 0.2f;
		float LifeMin = 1.f;
		float LifeMax = 2.f;
		/** Tamaño al nacer y al morir (cm). */
		float SizeStart = 10.f;
		float SizeEnd = 10.f;
		/** Más lejos de la cámara (cm), el emisor se duerme y esconde sus partículas. */
		float WakeDistance = 16000.f;
	};

	struct FParticle
	{
		FVector P = FVector::ZeroVector;
		FVector V = FVector::ZeroVector;
		float Age = 0.f;
		float Life = 1.f;
		float Spin = 0.f;
		bool bAlive = false;
	};

	struct FEmitter
	{
		FEmitterDesc Desc;
		TWeakObjectPtr<UInstancedStaticMeshComponent> ISM;
		FVector Origin = FVector::ZeroVector;
		/** Lo modula el dueño (pulsos del géiser); 0 = no nace nada. */
		float RateScale = 1.f;
		TArray<FParticle> Particles;
		TArray<FTransform> Xf;
		float Accum = 0.f;
		bool bAwake = false;
		uint32 Rng = 0x9E3779B9u;
	};

	/** Bandada que da vueltas: gaviotas sobre la costa, pájaros sobre los bosques, buitres en el desierto. */
	struct FFlock
	{
		TWeakObjectPtr<UInstancedStaticMeshComponent> ISM;
		FVector Center = FVector::ZeroVector;
		float Radius = 3000.f;
		float Height = 3000.f;
		/** rad/s; el signo decide el sentido. */
		float AngularSpeed = 0.2f;
		float Size = 1.f;
		TArray<float> Phase;
		TArray<float> Offset;
		TArray<float> Lift;
		TArray<FTransform> Xf;
		float Time = 0.f;
	};

	struct FOwnerFX
	{
		TArray<FEmitter> Emitters;
		TArray<FFlock> Flocks;
	};

	/** Registro por dueño (un solo mapa para todo el módulo). */
	inline TMap<TWeakObjectPtr<AActor>, FOwnerFX>& Registry()
	{
		static TMap<TWeakObjectPtr<AActor>, FOwnerFX> Map;
		return Map;
	}

	inline uint32 NextRand(uint32& S)
	{
		S ^= S << 13; S ^= S >> 17; S ^= S << 5;
		return S;
	}

	inline float Rand01(uint32& S) { return static_cast<float>(NextRand(S) & 0xFFFFFF) / 16777215.f; }

	/** Malla de una forma de partícula (100 cm de diámetro), con su color en el vértice. */
	inline void BuildShape(FTNProcMeshBuffers& M, EShape Shape, const FLinearColor& Color)
	{
		switch (Shape)
		{
			case EShape::Puff:
			{
				// Bocanada: esfera abollada de caras planas.
				TArray<double> Z, R;
				for (int32 k = 0; k <= 4; ++k)
				{
					const double A = -HALF_PI + PI * k / 4.0;
					Z.Add(50.0 * (1.0 + FMath::Sin(A)));
					R.Add(FMath::Max(0.5, 50.0 * FMath::Cos(A)));
				}
				TNProcAddLathe(M, FVector(0.0, 0.0, -50.0), Z, R, 0.18, 77u, Color, 7);
				break;
			}
			case EShape::Ember:
			{
				const FVector P[6] = { FVector(50, 0, 0), FVector(-50, 0, 0), FVector(0, 50, 0), FVector(0, -50, 0), FVector(0, 0, 50), FVector(0, 0, -50) };
				const int32 F[8][3] = { { 0, 2, 4 }, { 2, 1, 4 }, { 1, 3, 4 }, { 3, 0, 4 }, { 2, 0, 5 }, { 1, 2, 5 }, { 3, 1, 5 }, { 0, 3, 5 } };
				for (const auto& T : F) { M.AddTri(P[T[0]], P[T[1]], P[T[2]], (P[T[0]] + P[T[1]] + P[T[2]]) / 3.0, Color); }
				break;
			}
			case EShape::Ring:
			{
				// Onda en el agua: anillo plano fino (diámetro 100) con las dos caras; se abre sin encoger al final.
				constexpr int32 Seg = 28;
				for (int32 k = 0; k < Seg; ++k)
				{
					const double A0 = 2.0 * PI * k / Seg, A1 = 2.0 * PI * (k + 1) / Seg;
					const FVector I0(FMath::Cos(A0) * 45.0, FMath::Sin(A0) * 45.0, 0.0), I1(FMath::Cos(A1) * 45.0, FMath::Sin(A1) * 45.0, 0.0);
					const FVector O0(FMath::Cos(A0) * 50.0, FMath::Sin(A0) * 50.0, 0.0), O1(FMath::Cos(A1) * 50.0, FMath::Sin(A1) * 50.0, 0.0);
					M.AddQuad(I0, O0, O1, I1, FVector::UpVector, Color);
					M.AddQuad(I0, O0, O1, I1, -FVector::UpVector, Color);
				}
				break;
			}
			case EShape::Flake:
			{
				M.AddQuad(FVector(-50, -30, 0), FVector(50, -30, 0), FVector(50, 30, 0), FVector(-50, 30, 0), FVector::UpVector, Color);
				M.AddQuad(FVector(-50, -30, 0), FVector(50, -30, 0), FVector(50, 30, 0), FVector(-50, 30, 0), -FVector::UpVector, Color * 0.8f);
				break;
			}
			case EShape::Leaf:
			{
				// Hoja: rombo doblado por el nervio, con las dos caras.
				const FVector Tip(50, 0, 0), Stem(-50, 0, 0), L(-5, 26, 6), R(-5, -26, 6);
				for (const double S : { 1.0, -1.0 })
				{
					M.AddTri(Stem, L, Tip, FVector(0.0, 0.0, S), S > 0.0 ? Color : Color * 0.75f);
					M.AddTri(Stem, Tip, R, FVector(0.0, 0.0, S), S > 0.0 ? Color * 0.9f : Color * 0.7f);
				}
				break;
			}
			case EShape::Streak:
			{
				// Estela fina (arena que vuela): cinta a lo largo de X, en cruz para verse de cualquier lado.
				M.AddQuad(FVector(-50, -4, 0), FVector(50, -4, 0), FVector(50, 4, 0), FVector(-50, 4, 0), FVector::UpVector, Color);
				M.AddQuad(FVector(-50, 0, -4), FVector(50, 0, -4), FVector(50, 0, 4), FVector(-50, 0, 4), FVector::RightVector, Color);
				break;
			}
			case EShape::Drop:
			default:
			{
				// Gota: rombo alargado de seis caras arriba y abajo.
				TArray<double> Z = { 0.0, 38.0, 100.0 };
				TArray<double> R = { 1.0, 42.0, 1.0 };
				TNProcAddLathe(M, FVector(0.0, 0.0, -45.0), Z, R, 0.0, 0u, Color, 6);
				break;
			}
		}
	}

	/** Pájaro de caras planas (envergadura 100 cm): cuerpo, cola y dos alas con peso de aleteo en el alfa. */
	inline void BuildBird(FTNProcMeshBuffers& M, const FLinearColor& Body, const FLinearColor& Wing)
	{
		M.AddBox(FVector(0.0, 0.0, 0.0), FVector(1.0, 0.0, 0.0), FVector(16.0, 5.0, 5.0), Body);
		M.AddTri(FVector(-14.0, 0.0, 1.0), FVector(-30.0, -8.0, 1.0), FVector(-30.0, 8.0, 1.0), FVector::UpVector, Body * 0.9f);
		M.AddTri(FVector(-14.0, 0.0, 1.0), FVector(-30.0, -8.0, 1.0), FVector(-30.0, 8.0, 1.0), -FVector::UpVector, Body * 0.8f);
		M.AddBox(FVector(18.0, 0.0, 2.0), FVector(1.0, 0.0, 0.0), FVector(4.0, 3.0, 3.0), FLinearColor(0.95f, 0.6f, 0.1f));
		for (const double S : { -1.0, 1.0 })
		{
			const FVector Root0(6.0, S * 4.0, 2.0), Root1(-8.0, S * 4.0, 2.0);
			const FVector Mid(0.0, S * 28.0, 5.0), Tip(-10.0, S * 50.0, 3.0);
			M.AddTri(Root0, Mid, Root1, FVector::UpVector, Wing);
			M.AddTri(Root0, Mid, Root1, -FVector::UpVector, Wing * 0.8f);
			M.AddTri(Mid, Tip, Root1, FVector::UpVector, Wing * 1.05f);
			M.AddTri(Mid, Tip, Root1, -FVector::UpVector, Wing * 0.85f);
		}
	}

	/** Material de los efectos: suave (translúcido) o el de la vegetación (opaco, color de vértice). */
	inline UMaterialInterface* MaterialFor(bool bSoft, bool bCloud = false)
	{
		static TWeakObjectPtr<UMaterialInterface> Soft, Solid, Cloud;
		if (bSoft && bCloud)
		{
			if (!Cloud.IsValid()) { Cloud = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcFXCloud.M_ProcFXCloud")); }
			if (Cloud.IsValid()) { return Cloud.Get(); }
		}
		if (bSoft)
		{
			if (!Soft.IsValid()) { Soft = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcFXSoft.M_ProcFXSoft")); }
			if (Soft.IsValid()) { return Soft.Get(); }
		}
		if (!Solid.IsValid()) { Solid = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcFoliage.M_ProcFoliage")); }
		return Solid.Get();
	}

	/**
	 * Malla de partícula en caché por forma, color y material. Las mallas de la caché quedan en la raíz
	 * del recolector (se reutilizan entre partidas y el motor las libera al cerrarse).
	 */
	inline UStaticMesh* ShapeMesh(EShape Shape, const FLinearColor& Color, bool bSoft, float Alpha, bool bCloud = false)
	{
		static TMap<uint64, UStaticMesh*> Cache;
		const FColor Q = Color.ToFColor(false);
		const uint64 Key = (static_cast<uint64>(Shape) << 40) ^ (bSoft ? (1ull << 36) : 0ull) ^ (bCloud ? (1ull << 37) : 0ull) ^ (static_cast<uint64>(Q.R) << 16)
			^ (static_cast<uint64>(Q.G) << 8) ^ Q.B ^ (static_cast<uint64>(FMath::RoundToInt32(Alpha * 63.f)) << 24);
		if (UStaticMesh** Found = Cache.Find(Key)) { return *Found; }
		FTNProcMeshBuffers B;
		BuildShape(B, Shape, Color);
		UStaticMesh* Mesh = TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), B, MaterialFor(bSoft, bCloud), false, 0.f, 1.f, bSoft ? Alpha : 0.f);
		if (Mesh) { Mesh->AddToRoot(); }
		Cache.Add(Key, Mesh);
		return Mesh;
	}

	inline UInstancedStaticMeshComponent* MakeISM(AActor* Owner, UStaticMesh* Mesh, int32 Count, bool bShadow)
	{
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(Owner, NAME_None, RF_Transient);
		ISM->SetStaticMesh(Mesh);
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->SetCanEverAffectNavigation(false);
		ISM->SetCastShadow(bShadow);
		ISM->bEvaluateWorldPositionOffset = false;
		ISM->SetMobility(EComponentMobility::Movable);
		if (USceneComponent* Root = Owner->GetRootComponent()) { ISM->SetupAttachment(Root); }
		ISM->RegisterComponent();
		ISM->SetAbsolute(true, true, true);
		ISM->SetWorldTransform(FTransform::Identity);
		TArray<FTransform> Hidden;
		Hidden.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), Count);
		ISM->AddInstances(Hidden, false, false);
		return ISM;
	}

	/** Añade un emisor a Owner con origen en mundo. Devuelve su índice (o INDEX_NONE en servidor dedicado). */
	inline int32 AddEmitter(AActor* Owner, const FEmitterDesc& Desc, const FVector& Origin)
	{
		if (!Owner || !Owner->GetWorld() || Owner->GetWorld()->GetNetMode() == NM_DedicatedServer) { return INDEX_NONE; }
		FOwnerFX& FX = Registry().FindOrAdd(Owner);
		FEmitter& E = FX.Emitters.AddDefaulted_GetRef();
		E.Desc = Desc;
		E.Origin = Origin;
		E.Rng ^= static_cast<uint32>(GetTypeHash(Origin)) | 1u;
		E.Particles.SetNum(Desc.MaxParticles);
		E.Xf.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), Desc.MaxParticles);
		E.ISM = MakeISM(Owner, ShapeMesh(Desc.Shape, Desc.Color, Desc.bSoft, Desc.Alpha, Desc.bCloud), Desc.MaxParticles, false);
		return FX.Emitters.Num() - 1;
	}

	inline FEmitter* GetEmitter(AActor* Owner, int32 Index)
	{
		FOwnerFX* FX = Registry().Find(Owner);
		return FX && FX->Emitters.IsValidIndex(Index) ? &FX->Emitters[Index] : nullptr;
	}

	/** Bandada de Count pájaros alrededor de Center (mundo). */
	inline void AddFlock(AActor* Owner, const FVector& Center, int32 Count, float Radius, float AngularSpeed, float Size,
		const FLinearColor& Body, const FLinearColor& Wing, uint32 Seed)
	{
		if (!Owner || !Owner->GetWorld() || Owner->GetWorld()->GetNetMode() == NM_DedicatedServer) { return; }
		static TMap<uint32, UStaticMesh*> Birds;
		const FColor Qb = Body.ToFColor(false), Qw = Wing.ToFColor(false);
		const uint32 Key = (static_cast<uint32>(Qb.R) << 24) ^ (static_cast<uint32>(Qb.G) << 16) ^ (static_cast<uint32>(Qw.R) << 8) ^ Qw.G;
		UStaticMesh* Mesh = nullptr;
		if (UStaticMesh** Found = Birds.Find(Key)) { Mesh = *Found; }
		if (!Mesh)
		{
			FTNProcMeshBuffers B;
			BuildBird(B, Body, Wing);
			UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/ProcMap/Materials/M_ProcBird.M_ProcBird"));
			// Alfa = peso del aleteo (las puntas de las alas); el material M_ProcBird las mueve.
			FTNProcMeshBuffers Weighted = B;
			for (int32 i = 0; i < Weighted.Verts.Num(); ++i) { Weighted.Colors[i].A = static_cast<float>(FMath::Clamp(FMath::Abs(Weighted.Verts[i].Y) / 50.0, 0.0, 1.0)); }
			Mesh = TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), Weighted, Mat ? Mat : MaterialFor(false), false, 0.f, 1.f, -2.f);
			if (Mesh) { Mesh->AddToRoot(); }
			Birds.Add(Key, Mesh);
		}
		FOwnerFX& FX = Registry().FindOrAdd(Owner);
		FFlock& F = FX.Flocks.AddDefaulted_GetRef();
		F.Center = Center;
		F.Radius = Radius;
		F.AngularSpeed = AngularSpeed;
		F.Size = Size;
		uint32 S = Seed | 1u;
		for (int32 b = 0; b < Count; ++b)
		{
			F.Phase.Add(TNProcMap::TwoPi * b / Count + Rand01(S) * 0.6f);
			F.Offset.Add(FMath::Lerp(-0.25f, 0.25f, Rand01(S)));
			F.Lift.Add(FMath::Lerp(-300.f, 300.f, Rand01(S)));
		}
		F.Xf.Init(FTransform::Identity, Count);
		F.ISM = MakeISM(Owner, Mesh, Count, false);
		if (UInstancedStaticMeshComponent* ISM = F.ISM.Get()) { ISM->bEvaluateWorldPositionOffset = true; ISM->WorldPositionOffsetDisableDistance = 20000; }
	}

	/** Nace una partícula en un hueco libre (false si no queda ninguno). */
	inline bool SpawnOne(FEmitter& E)
	{
		const FEmitterDesc& D = E.Desc;
		int32 Free = 0;
		while (Free < E.Particles.Num() && E.Particles[Free].bAlive) { ++Free; }
		if (Free >= E.Particles.Num()) { return false; }
		FParticle& P = E.Particles[Free];
		const float A = Rand01(E.Rng) * TNProcMap::TwoPi;
		const float Rr = D.SpawnRadius * FMath::Sqrt(Rand01(E.Rng));
		P.P = E.Origin + FVector(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, D.SpawnHeight * Rand01(E.Rng));
		const FVector Dir = D.Direction.GetSafeNormal();
		const FVector U = FVector::CrossProduct(Dir, FMath::Abs(Dir.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
		const FVector W = FVector::CrossProduct(Dir, U);
		const float B = Rand01(E.Rng) * TNProcMap::TwoPi;
		const float Open = D.Spread * FMath::Sqrt(Rand01(E.Rng));
		const FVector V = (Dir + (U * FMath::Cos(B) + W * FMath::Sin(B)) * Open).GetSafeNormal();
		P.V = V * D.Speed * (1.f + D.SpeedJitter * (Rand01(E.Rng) * 2.f - 1.f));
		P.Age = 0.f;
		P.Life = FMath::Lerp(D.LifeMin, D.LifeMax, Rand01(E.Rng));
		P.Spin = Rand01(E.Rng) * 360.f;
		P.bAlive = true;
		return true;
	}

	/** Estallido: Count partículas de golpe (lanzamiento del géiser, confeti de la meta). */
	inline void Burst(FEmitter& E, int32 Count)
	{
		E.bAwake = true;
		for (int32 k = 0; k < Count && SpawnOne(E); ++k) {}
	}

	inline void TickEmitter(FEmitter& E, float Dt, const FVector& View)
	{
		UInstancedStaticMeshComponent* ISM = E.ISM.Get();
		if (!ISM) { return; }
		const FEmitterDesc& D = E.Desc;
		const bool bNear = FVector::DistSquared(View, E.Origin) < FMath::Square(D.WakeDistance);
		if (!bNear)
		{
			if (E.bAwake)
			{
				for (FParticle& P : E.Particles) { P.bAlive = false; }
				for (FTransform& T : E.Xf) { T.SetScale3D(FVector::ZeroVector); }
				ISM->BatchUpdateInstancesTransforms(0, E.Xf, true, false, false);
				E.bAwake = false;
			}
			return;
		}
		E.bAwake = true;
		// Nacimientos.
		E.Accum += D.Rate * E.RateScale * Dt;
		while (E.Accum >= 1.f)
		{
			E.Accum -= 1.f;
			if (!SpawnOne(E)) { E.Accum = 0.f; break; }
		}
		// Movimiento y transformadas.
		const float DragK = FMath::Max(0.f, 1.f - D.Drag * Dt);
		for (int32 i = 0; i < E.Particles.Num(); ++i)
		{
			FParticle& P = E.Particles[i];
			if (P.bAlive)
			{
				P.Age += Dt;
				if (P.Age >= P.Life) { P.bAlive = false; }
			}
			if (!P.bAlive)
			{
				E.Xf[i].SetScale3D(FVector::ZeroVector);
				continue;
			}
			P.V.Z += (D.Gravity + D.Buoyancy) * Dt;
			P.V *= DragK;
			P.P += P.V * Dt;
			const float T = P.Age / P.Life;
			const float Fade = FMath::Min(1.f, (P.Life - P.Age) / 0.35f) * FMath::Min(1.f, P.Age / 0.08f + 0.2f);
			// Las ondas se abren sin encoger al final (se hunden bajo el agua, que las apaga).
			const float Size = FMath::Lerp(D.SizeStart, D.SizeEnd, T) * (D.Shape == EShape::Ring ? 1.f : Fade) / 100.f;
			FQuat Rot = FQuat(FVector::UpVector, FMath::DegreesToRadians(P.Spin + P.Age * 90.f));
			if (D.Shape == EShape::Drop && !P.V.IsNearlyZero())
			{
				// Las gotas se estiran en la dirección en que vuelan.
				Rot = FQuat::FindBetweenNormals(FVector::UpVector, P.V.GetSafeNormal());
			}
			else if (D.Shape == EShape::Flake || D.Shape == EShape::Leaf)
			{
				Rot = FQuat(FVector(1.f, 0.3f, 0.2f).GetSafeNormal(), FMath::DegreesToRadians(P.Spin + P.Age * 420.f));
			}
			else if (D.Shape == EShape::Streak && !P.V.IsNearlyZero())
			{
				// La estela, a lo largo de su vuelo.
				Rot = FQuat::FindBetweenNormals(FVector::ForwardVector, P.V.GetSafeNormal());
			}
			E.Xf[i] = FTransform(Rot, P.P, FVector(Size, Size, D.Shape == EShape::Drop ? Size * 1.6f : Size));
		}
		// Cada fotograma, sin MarkRenderStateDirty: TransformChanged ya actualiza instancias y límites al final del fotograma sin rehacer el proxy (#566).
		ISM->BatchUpdateInstancesTransforms(0, E.Xf, true, false, false);
	}

	inline void TickFlock(FFlock& F, float Dt)
	{
		UInstancedStaticMeshComponent* ISM = F.ISM.Get();
		if (!ISM) { return; }
		F.Time += Dt;
		for (int32 b = 0; b < F.Phase.Num(); ++b)
		{
			const float A = F.Phase[b] + F.AngularSpeed * F.Time;
			const float R = F.Radius * (1.f + F.Offset[b]) * (1.f + 0.12f * FMath::Sin(F.Time * 0.21f + b));
			const FVector P = F.Center + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, F.Lift[b] + 180.f * FMath::Sin(F.Time * 0.5f + F.Phase[b] * 3.f));
			// Mira en la tangente del círculo y se inclina hacia dentro al girar.
			const float Sgn = F.AngularSpeed >= 0.f ? 1.f : -1.f;
			const FVector Fwd(-FMath::Sin(A) * Sgn, FMath::Cos(A) * Sgn, 0.f);
			const FRotator Rot(0.f, FMath::RadiansToDegrees(FMath::Atan2(Fwd.Y, Fwd.X)), -18.f * Sgn);
			F.Xf[b] = FTransform(Rot, P, FVector(F.Size));
		}
		// Cada fotograma, sin MarkRenderStateDirty: TransformChanged ya actualiza instancias y límites al final del fotograma sin rehacer el proxy (#566).
		ISM->BatchUpdateInstancesTransforms(0, F.Xf, true, false, false);
	}

	/** Mueve todos los efectos de Owner (llamar desde su Tick). */
	inline void TickOwner(AActor* Owner, float Dt)
	{
		if (!Owner) { return; }
		FOwnerFX* FX = Registry().Find(Owner);
		if (!FX) { return; }
		UWorld* World = Owner->GetWorld();
		// Con la pantalla partida (#311), la cámara local más cercana.
		FVector View = Owner->GetActorLocation();
		TNLocalViews::ClosestCamera(World, Owner->GetActorLocation(), View);
		for (FEmitter& E : FX->Emitters) { TickEmitter(E, Dt, View); }
		for (FFlock& F : FX->Flocks) { TickFlock(F, Dt); }
	}

	/** Borra los efectos de Owner (al regenerar o al destruirse). */
	inline void RemoveOwner(AActor* Owner)
	{
		FOwnerFX* FX = Registry().Find(Owner);
		if (!FX) { return; }
		for (FEmitter& E : FX->Emitters) { if (UInstancedStaticMeshComponent* ISM = E.ISM.Get()) { ISM->DestroyComponent(); } }
		for (FFlock& F : FX->Flocks) { if (UInstancedStaticMeshComponent* ISM = F.ISM.Get()) { ISM->DestroyComponent(); } }
		Registry().Remove(Owner);
		// De paso, fuera los dueños que ya no existen.
		for (auto It = Registry().CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
	}
}
