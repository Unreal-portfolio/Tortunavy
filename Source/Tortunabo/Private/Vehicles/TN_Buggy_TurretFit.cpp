// Medida de los solapes de la torreta con la artillera y con la carrocería (#435), fuera de Shipping:
//   TN.Rally.DebugTurretFit [espera = 2] [carpeta de fotos o -] [cerrar al acabar 0|1] [carpeta de volcado]
// Sienta a la artillera visual, recorre varias punterías (guiñada y cabeceo) y, con cada una, deja en LogTNBuggy una
// línea "[Torreta] ..." por pieza: puntos de su superficie dentro de la artillera (malla deformada en CPU más el casco) o
// dentro de la carrocería (número de giro de la malla cerrada) y holgura mínima de los que quedan fuera. Con carpeta,
// hace una foto cenital y otra lateral de cada puntería. Uso (sin editor, el mapa del Rally con un jugador):
//   LVL_Rally?Seats=1 -game -windowed -ExecCmds="TN.Rally.SpawnBuggy, TN.Rally.DebugTurretFit 3 <carpeta>"

#include "Vehicles/TN_Buggy.h"
#include "TN_BuggyTurretMesh.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Vehicles/TN_RallyTurretLogic.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "StaticMeshResources.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace TNTurretFit
{
	/** Triángulos en espacio de mundo. */
	struct FSoup
	{
		TArray<FVector> Verts;
		TArray<int32> Tris;
		/** Hueso con más peso de cada vértice (solo en la malla deformada; para el volcado). */
		TArray<FName> Bones;
		FBox Bounds = FBox(ForceInit);

		void Append(const FSoup& Other)
		{
			const int32 Base = Verts.Num();
			Verts.Append(Other.Verts);
			for (const int32 Index : Other.Tris)
			{
				Tris.Add(Base + Index);
			}
			Bounds += Other.Bounds;
		}
	};

	/** Distancia entre muestras de la superficie de la torreta (cm). */
	constexpr double SampleSpacingCm = 3.0;
	/** Holgura que se informa como "lejos": más allá no interesa. */
	constexpr double FarCm = 99.0;

	FSoup FromSkinned(USkeletalMeshComponent* Mesh)
	{
		FSoup Out;
		FSkeletalMeshRenderData* RenderData = Mesh ? Mesh->GetSkeletalMeshRenderData() : nullptr;
		if (!RenderData || RenderData->LODRenderData.IsEmpty())
		{
			return Out;
		}
		const FSkeletalMeshLODRenderData& Lod = RenderData->LODRenderData[0];
		const FRawStaticIndexBuffer16or32Interface* Indices = Lod.MultiSizeIndexContainer.GetIndexBuffer();
		if (!Indices || Lod.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices() == 0)
		{
			return Out;
		}
		TArray<FMatrix44f> RefToLocals;
		Mesh->CacheRefToLocalMatrices(RefToLocals);
		TArray<FVector3f> Positions;
		USkinnedMeshComponent::ComputeSkinnedPositions(Mesh, Positions, RefToLocals, Lod, Lod.SkinWeightVertexBuffer);
		const FTransform& ToWorld = Mesh->GetComponentTransform();
		Out.Verts.Reserve(Positions.Num());
		for (const FVector3f& P : Positions)
		{
			Out.Verts.Add(ToWorld.TransformPosition(FVector(P)));
			Out.Bounds += Out.Verts.Last();
		}
		const FSkinWeightVertexBuffer& Weights = Lod.SkinWeightVertexBuffer;
		const FReferenceSkeleton& Ref = Mesh->GetSkinnedAsset()->GetRefSkeleton();
		Out.Bones.Init(NAME_None, Positions.Num());
		for (const FSkelMeshRenderSection& Section : Lod.RenderSections)
		{
			for (uint32 Local = 0; Local < Section.NumVertices; ++Local)
			{
				const int32 Vertex = static_cast<int32>(Section.BaseVertexIndex + Local);
				uint32 BestWeight = 0;
				int32 Best = INDEX_NONE;
				for (uint32 Influence = 0; Influence < Weights.GetMaxBoneInfluences(); ++Influence)
				{
					const uint32 Weight = Weights.GetBoneWeight(Vertex, Influence);
					if (Weight > BestWeight)
					{
						BestWeight = Weight;
						Best = static_cast<int32>(Weights.GetBoneIndex(Vertex, Influence));
					}
				}
				if (Section.BoneMap.IsValidIndex(Best) && Out.Bones.IsValidIndex(Vertex))
				{
					Out.Bones[Vertex] = Ref.GetBoneName(Section.BoneMap[Best]);
				}
			}
		}
		const int32 Count = Indices->Num();
		Out.Tris.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Out.Tris.Add(static_cast<int32>(Indices->Get(Index)));
		}
		return Out;
	}

	FSoup FromStatic(const UStaticMeshComponent* Mesh)
	{
		FSoup Out;
		const UStaticMesh* Asset = Mesh ? Mesh->GetStaticMesh() : nullptr;
		const FStaticMeshRenderData* RenderData = Asset ? Asset->GetRenderData() : nullptr;
		if (!RenderData || RenderData->LODResources.IsEmpty())
		{
			return Out;
		}
		const FStaticMeshLODResources& Lod = RenderData->LODResources[0];
		const FPositionVertexBuffer& Positions = Lod.VertexBuffers.PositionVertexBuffer;
		if (!Positions.GetVertexData() || Positions.GetNumVertices() == 0)
		{
			return Out;
		}
		const FTransform& ToWorld = Mesh->GetComponentTransform();
		for (uint32 Index = 0; Index < Positions.GetNumVertices(); ++Index)
		{
			Out.Verts.Add(ToWorld.TransformPosition(FVector(Positions.VertexPosition(Index))));
			Out.Bounds += Out.Verts.Last();
		}
		TArray<uint32> Indices;
		Lod.IndexBuffer.GetCopy(Indices);
		for (const uint32 Index : Indices)
		{
			Out.Tris.Add(static_cast<int32>(Index));
		}
		return Out;
	}

	/** Pieza de la torreta: la malla que construye Build, colocada como Part. */
	FSoup FromBuilder(const USceneComponent* Part, void (*Build)(TNProcMesh::FTNProcMeshBuffers&))
	{
		FSoup Out;
		if (!Part)
		{
			return Out;
		}
		TNProcMesh::FTNProcMeshBuffers Buffers;
		Build(Buffers);
		const FTransform& ToWorld = Part->GetComponentTransform();
		for (const FVector& V : Buffers.Verts)
		{
			Out.Verts.Add(ToWorld.TransformPosition(V));
			Out.Bounds += Out.Verts.Last();
		}
		Out.Tris = Buffers.Tris;
		return Out;
	}

	/** Puntos repartidos por la superficie (rejilla baricéntrica de SampleSpacingCm). */
	TArray<FVector> Samples(const FSoup& Soup)
	{
		TArray<FVector> Out;
		for (int32 Tri = 0; Tri + 2 < Soup.Tris.Num(); Tri += 3)
		{
			const FVector& A = Soup.Verts[Soup.Tris[Tri]];
			const FVector& B = Soup.Verts[Soup.Tris[Tri + 1]];
			const FVector& C = Soup.Verts[Soup.Tris[Tri + 2]];
			const double Longest = FMath::Max3(FVector::Dist(A, B), FVector::Dist(B, C), FVector::Dist(C, A));
			const int32 Steps = FMath::Clamp(FMath::CeilToInt(Longest / SampleSpacingCm), 1, 64);
			for (int32 I = 0; I <= Steps; ++I)
			{
				for (int32 J = 0; I + J <= Steps; ++J)
				{
					const double U = static_cast<double>(I) / Steps;
					const double V = static_cast<double>(J) / Steps;
					Out.Add(A + (B - A) * U + (C - A) * V);
				}
			}
		}
		return Out;
	}

	/** Número de giro generalizado de la malla alrededor de P: ±1 dentro de una malla cerrada, 0 fuera. */
	double Winding(const FSoup& Soup, const FVector& P)
	{
		double Sum = 0.0;
		for (int32 Tri = 0; Tri + 2 < Soup.Tris.Num(); Tri += 3)
		{
			const FVector A = Soup.Verts[Soup.Tris[Tri]] - P;
			const FVector B = Soup.Verts[Soup.Tris[Tri + 1]] - P;
			const FVector C = Soup.Verts[Soup.Tris[Tri + 2]] - P;
			const double La = A.Size();
			const double Lb = B.Size();
			const double Lc = C.Size();
			const double Num = FVector::DotProduct(A, FVector::CrossProduct(B, C));
			const double Den = La * Lb * Lc + FVector::DotProduct(A, B) * Lc + FVector::DotProduct(B, C) * La + FVector::DotProduct(C, A) * Lb;
			Sum += 2.0 * FMath::Atan2(Num, Den);
		}
		return Sum / (4.0 * UE_DOUBLE_PI);
	}

	struct FResult
	{
		int32 Points = 0;
		int32 Inside = 0;
		double ClearanceCm = FarCm;
	};

	/** Puntos de Points dentro de Obstacle y distancia mínima a su superficie de los que quedan fuera. */
	FResult Measure(const TArray<FVector>& Points, const FSoup& Obstacle)
	{
		FResult Out;
		Out.Points = Points.Num();
		if (Obstacle.Tris.IsEmpty())
		{
			return Out;
		}
		const FBox Near = Obstacle.Bounds.ExpandBy(FarCm);
		for (const FVector& P : Points)
		{
			if (!Near.IsInsideOrOn(P))
			{
				continue;
			}
			if (Obstacle.Bounds.IsInsideOrOn(P) && FMath::Abs(Winding(Obstacle, P)) > 0.5)
			{
				++Out.Inside;
				continue;
			}
			for (int32 Tri = 0; Tri + 2 < Obstacle.Tris.Num(); Tri += 3)
			{
				const FVector& A = Obstacle.Verts[Obstacle.Tris[Tri]];
				// Descarte rápido: el triángulo entero está más lejos que la mejor holgura (lado mayor como cota).
				if (FVector::Dist(P, A) - FVector::Dist(A, Obstacle.Verts[Obstacle.Tris[Tri + 1]])
					- FVector::Dist(A, Obstacle.Verts[Obstacle.Tris[Tri + 2]]) > Out.ClearanceCm)
				{
					continue;
				}
				const FVector Closest = FMath::ClosestPointOnTriangleToPoint(P, A, Obstacle.Verts[Obstacle.Tris[Tri + 1]],
					Obstacle.Verts[Obstacle.Tris[Tri + 2]]);
				Out.ClearanceCm = FMath::Min(Out.ClearanceCm, FVector::Dist(P, Closest));
			}
		}
		return Out;
	}

	FString Describe(const TCHAR* Name, const FResult& Gunner, const FResult* Body)
	{
		FString Line = FString::Printf(TEXT(" %s: artillera %d/%d dentro, holgura %.1f"), Name, Gunner.Inside, Gunner.Points, Gunner.ClearanceCm);
		if (Body)
		{
			Line += FString::Printf(TEXT("; carrocería %d dentro, holgura %.1f"), Body->Inside, Body->ClearanceCm);
		}
		return Line + TEXT(";");
	}

	/** Carpeta donde volcar las mallas medidas (vacía = no se vuelca): TN.Rally.DebugTurretFit, cuarto argumento. */
	FString DumpFolder;

	/** Vuelca Soup en los ejes de Frame a File: líneas "v x y z" y "f a b c" (índices desde 0). */
	void Dump(const FSoup& Soup, const FTransform& Frame, const FString& File)
	{
		FString Text;
		Text.Reserve(Soup.Verts.Num() * 32 + Soup.Tris.Num() * 8);
		for (int32 Index = 0; Index < Soup.Verts.Num(); ++Index)
		{
			const FVector L = Frame.InverseTransformPosition(Soup.Verts[Index]);
			const FName Bone = Soup.Bones.IsValidIndex(Index) ? Soup.Bones[Index] : NAME_None;
			Text += FString::Printf(TEXT("v %.2f %.2f %.2f %s\n"), L.X, L.Y, L.Z, *Bone.ToString());
		}
		for (int32 Tri = 0; Tri + 2 < Soup.Tris.Num(); Tri += 3)
		{
			Text += FString::Printf(TEXT("f %d %d %d\n"), Soup.Tris[Tri], Soup.Tris[Tri + 1], Soup.Tris[Tri + 2]);
		}
		FFileHelper::SaveStringToFile(Text, *File);
	}

	template <typename T>
	T* FindNamed(const AActor* Owner, const TCHAR* Name)
	{
		TArray<T*> Components;
		Owner->GetComponents<T>(Components);
		for (T* Component : Components)
		{
			if (Component->GetFName() == Name)
			{
				return Component;
			}
		}
		return nullptr;
	}
}

void ATN_Buggy::DebugShowGunner()
{
#if !UE_BUILD_SHIPPING
	if (!HasAuthority())
	{
		return;
	}
	bGunnerSeated = true;
	RefreshSeatVisuals(true);
	if (USkeletalMeshComponent* Turtle = TNTurretFit::FindNamed<USkeletalMeshComponent>(this, TEXT("GunnerTurtle")))
	{
		// Que la pose se calcule aunque la cámara no la vea: la medida lee los huesos.
		Turtle->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
#endif
}

FString ATN_Buggy::DebugMeasureTurretFit() const
{
#if !UE_BUILD_SHIPPING
	using namespace TNTurretFit;
	USkeletalMeshComponent* Turtle = FindNamed<USkeletalMeshComponent>(this, TEXT("GunnerTurtle"));
	if (!Turtle || Turtle->bHiddenInGame || !Turret)
	{
		return TEXT("sin artillera visible o sin torreta");
	}
	FSoup Gunner = FromSkinned(Turtle);
	if (const UStaticMeshComponent* Helmet = FindNamed<UStaticMeshComponent>(this, TEXT("GunnerTurtleHelmet")); Helmet && Helmet->IsVisible())
	{
		Gunner.Append(FromStatic(Helmet));
	}
	if (Gunner.Tris.IsEmpty())
	{
		return TEXT("la malla de la artillera no tiene datos en CPU");
	}
	const FSoup BodySoup = FromStatic(Body);
	const TArray<FVector> Ring = Samples(FromBuilder(TurretRing, &TNBuggyTurretMesh::BuildRing));
	const TArray<FVector> Mount = Samples(FromBuilder(TurretMount, &TNBuggyTurretMesh::BuildMount));
	const TArray<FVector> Gun = Samples(FromBuilder(TurretGun, &TNBuggyTurretMesh::BuildGun));
	const TArray<FVector> Barrel = Samples(FromBuilder(TurretBarrel, &TNBuggyTurretMesh::BuildBarrel));
	// El aro no se mide contra la carrocería: los pies de sus patas apoyan en el suelo trasero a propósito.
	const FResult MountBody = Measure(Mount, BodySoup);
	const FResult GunBody = Measure(Gun, BodySoup);
	const FResult BarrelBody = Measure(Barrel, BodySoup);
	const FRotator Aim = Turret->GetDisplayAim();
	if (!DumpFolder.IsEmpty())
	{
		const FTransform Frame(GetMesh()->GetComponentQuat(), Turret->GetComponentLocation());
		Dump(Gunner, Frame, FPaths::Combine(DumpFolder, FString::Printf(TEXT("gunner_y%.0f_p%.0f.txt"), Aim.Yaw, Aim.Pitch)));
		Dump(BodySoup, Frame, FPaths::Combine(DumpFolder, TEXT("body.txt")));
		// Aro, carro y cañón tal como están en esta puntería, para ver qué parte toca.
		Dump(FromBuilder(TurretRing, &TNBuggyTurretMesh::BuildRing), Frame, FPaths::Combine(DumpFolder, TEXT("ring.txt")));
		Dump(FromBuilder(TurretMount, &TNBuggyTurretMesh::BuildMount), Frame,
			FPaths::Combine(DumpFolder, FString::Printf(TEXT("mount_y%.0f_p%.0f.txt"), Aim.Yaw, Aim.Pitch)));
		Dump(FromBuilder(TurretGun, &TNBuggyTurretMesh::BuildGun), Frame,
			FPaths::Combine(DumpFolder, FString::Printf(TEXT("gun_y%.0f_p%.0f.txt"), Aim.Yaw, Aim.Pitch)));
	}
	// Silueta de la artillera en los ejes del chasis con origen en el pivote: radio máximo y X mínima y máxima por franja de
	// 5 cm de altura (para dimensionar el aro y el cañón).
	FString Envelope;
	const FTransform PivotFrame(GetMesh()->GetComponentQuat(), Turret->GetComponentLocation());
	for (int32 Band = -8; Band <= 8; ++Band)
	{
		double MaxRadius = 0.0;
		double MinX = 1.0e9;
		double MaxX = -1.0e9;
		int32 Count = 0;
		for (const FVector& V : Gunner.Verts)
		{
			const FVector L = PivotFrame.InverseTransformPosition(V);
			if (L.Z >= Band * 5.0 && L.Z < Band * 5.0 + 5.0)
			{
				MaxRadius = FMath::Max(MaxRadius, FVector2D(L.X, L.Y).Size());
				MinX = FMath::Min(MinX, L.X);
				MaxX = FMath::Max(MaxX, L.X);
				++Count;
			}
		}
		if (Count > 0)
		{
			Envelope += FString::Printf(TEXT(" z%+d:r%.0f x[%.0f,%.0f]"), Band * 5, MaxRadius, MinX, MaxX);
		}
	}
	UE_LOG(LogTNBuggy, Log, TEXT("[Silueta] guiñada %.0f cabeceo %.0f%s"), Aim.Yaw, Aim.Pitch, *Envelope);
	return FString::Printf(TEXT("guiñada %.0f cabeceo %.0f;"), Aim.Yaw, Aim.Pitch)
		+ Describe(TEXT("aro"), Measure(Ring, Gunner), nullptr)
		+ Describe(TEXT("carro"), Measure(Mount, Gunner), &MountBody)
		+ Describe(TEXT("cañón"), Measure(Gun, Gunner), &GunBody)
		+ Describe(TEXT("caña"), Measure(Barrel, Gunner), &BarrelBody);
#else
	return FString();
#endif
}

#if !UE_BUILD_SHIPPING

namespace TNTurretFit
{
	/** Punterías medidas: la vuelta entera cada 30° con el cabeceo mínimo, recto y máximo, y las de la prueba del director. */
	TArray<FRotator> MeasuredAims()
	{
		TArray<FRotator> Out = { FRotator(0.f, 42.f, 0.f), FRotator(0.f, 176.f, 0.f), FRotator(0.f, -176.f, 0.f) };
		for (int32 Yaw = -180; Yaw < 180; Yaw += 30)
		{
			for (const float Pitch : { TNRallyTurret::MinPitchDeg, 0.f, TNRallyTurret::MaxPitchDeg })
			{
				Out.Add(FRotator(Pitch, static_cast<float>(Yaw), 0.f));
			}
		}
		return Out;
	}
	/** Segundos con cada puntería antes de medir (la artillera gira el cuerpo con muelles). */
	constexpr float SettleSeconds = 1.5f;

	void After(UWorld* World, float Seconds, TFunction<void()> Action)
	{
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(MoveTemp(Action)), FMath::Max(Seconds, 0.01f), false);
	}

	ATN_Buggy* FindBuggy(UWorld* World)
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (ATN_Buggy* Mine = Cast<ATN_Buggy>(PC->GetPawn()))
			{
				return Mine;
			}
		}
		TActorIterator<ATN_Buggy> It(World);
		return It ? *It : nullptr;
	}

	/** Cámara de fotos sobre la torreta: cenital o desde la derecha del buggy, a la altura del pivote. */
	void Photo(UWorld* World, ATN_Buggy* Buggy, ACameraActor* Camera, bool bTop, const FString& File)
	{
		const FVector Pivot = Buggy->GetTurret()->GetComponentLocation();
		const FVector Eye = bTop ? Pivot + FVector(0.0, 0.0, 330.0) - Buggy->GetActorForwardVector() * 20.0
			: Pivot + Buggy->GetActorRightVector() * 300.0 + FVector(0.0, 0.0, 20.0);
		Camera->SetActorLocationAndRotation(Eye, (Pivot - Eye).Rotation());
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			PC->SetViewTargetWithBlend(Camera, 0.f);
		}
		After(World, 0.35f, [File]()
		{
			FScreenshotRequest::RequestScreenshot(File, false, false);
			UE_LOG(LogTNBuggy, Log, TEXT("[Torreta] foto %s"), *File);
		});
	}

	FAutoConsoleCommandWithWorldAndArgs CmdDebugTurretFit(TEXT("TN.Rally.DebugTurretFit"),
		TEXT("Rally (servidor o standalone): TN.Rally.DebugTurretFit [espera = 2] [carpeta o -] [cerrar 0|1]: sienta a la artillera visual y mide los solapes de la torreta con ella y con la carrocería en varias punterías (y fotos si hay carpeta)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const float Wait = Args.IsValidIndex(0) ? FCString::Atof(*Args[0]) : 2.f;
			const FString Folder = Args.IsValidIndex(1) && Args[1] != TEXT("-") ? Args[1] : FString();
			const bool bQuit = Args.IsValidIndex(2) && FCString::Atoi(*Args[2]) != 0;
			DumpFolder = Args.IsValidIndex(3) ? Args[3] : FString();
			TWeakObjectPtr<UWorld> WeakWorld(World);
			After(World, Wait, [WeakWorld, Folder, bQuit]()
			{
				UWorld* Alive = WeakWorld.Get();
				ATN_Buggy* Buggy = Alive ? FindBuggy(Alive) : nullptr;
				if (!Buggy || !Buggy->HasAuthority() || !Buggy->GetTurret())
				{
					UE_LOG(LogTNBuggy, Warning, TEXT("[Torreta] sin buggy con torreta en el servidor"));
					return;
				}
				Buggy->SetEngineLocked(true);
				Buggy->SetRaceBrakeHeld(true);
				Buggy->DebugShowGunner();
				ACameraActor* Camera = Folder.IsEmpty() ? nullptr : Alive->SpawnActor<ACameraActor>();
				if (Camera)
				{
					Camera->GetCameraComponent()->SetFieldOfView(40.f);
				}
				TWeakObjectPtr<ATN_Buggy> WeakBuggy(Buggy);
				TWeakObjectPtr<ACameraActor> WeakCamera(Camera);
				const float Step = SettleSeconds + (Camera ? 1.6f : 0.4f);
				const TArray<FRotator> Aims = MeasuredAims();
				for (int32 Index = 0; Index < Aims.Num(); ++Index)
				{
					const FRotator Aim = Aims[Index];
					After(Alive, Step * Index, [WeakBuggy, Aim]()
					{
						if (WeakBuggy.IsValid())
						{
							WeakBuggy->GetTurret()->SetAimRelative(Aim);
						}
					});
					After(Alive, Step * Index + SettleSeconds, [WeakWorld, WeakBuggy, WeakCamera, Aim, Folder]()
					{
						if (!WeakBuggy.IsValid() || !WeakWorld.IsValid())
						{
							return;
						}
						UE_LOG(LogTNBuggy, Log, TEXT("[Torreta] %s"), *WeakBuggy->DebugMeasureTurretFit());
						if (!WeakCamera.IsValid())
						{
							return;
						}
						const FString Base = FPaths::Combine(Folder, FString::Printf(TEXT("torreta_y%.0f_p%.0f"), Aim.Yaw, Aim.Pitch));
						Photo(WeakWorld.Get(), WeakBuggy.Get(), WeakCamera.Get(), true, Base + TEXT("_cenital.png"));
						After(WeakWorld.Get(), 0.8f, [WeakWorld, WeakBuggy, WeakCamera, Base]()
						{
							if (WeakWorld.IsValid() && WeakBuggy.IsValid() && WeakCamera.IsValid())
							{
								Photo(WeakWorld.Get(), WeakBuggy.Get(), WeakCamera.Get(), false, Base + TEXT("_lateral.png"));
							}
						});
					});
				}
				After(Alive, Step * Aims.Num() + 1.f, [bQuit]()
				{
					UE_LOG(LogTNBuggy, Log, TEXT("[Torreta] fin de la medida"));
					if (bQuit)
					{
						FPlatformMisc::RequestExit(false, TEXT("TN.Rally.DebugTurretFit"));
					}
				});
			});
		}));
}

#endif // !UE_BUILD_SHIPPING
