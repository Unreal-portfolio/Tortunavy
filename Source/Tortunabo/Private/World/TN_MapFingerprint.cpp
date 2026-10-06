// Huella del mapa (#828): ver TN_MapFingerprint.h.

#include "World/TN_MapFingerprint.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/TN_Log.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Info.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProceduralMeshComponent.h"
#include "StaticMeshResources.h"

namespace TNMapFingerprintDetail
{
	uint64 Mix(uint64 X)
	{
		// Finalizador de splitmix64: cada bit de entrada cambia la mitad de los de salida.
		X += 0x9E3779B97F4A7C15ull;
		X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ull;
		X = (X ^ (X >> 27)) * 0x94D049BB133111EBull;
		return X ^ (X >> 31);
	}

	uint64 Combine(uint64 Seed, uint64 Value)
	{
		return Mix(Seed ^ (Value + 0x632BE59BD9B4E019ull + (Seed << 6) + (Seed >> 2)));
	}

	int64 Q(double Value, double Quantum)
	{
		return FMath::RoundToInt64(Value / Quantum);
	}

	uint64 HashVector(const FVector& V, double Quantum)
	{
		uint64 H = Mix(static_cast<uint64>(Q(V.X, Quantum)));
		H = Combine(H, static_cast<uint64>(Q(V.Y, Quantum)));
		return Combine(H, static_cast<uint64>(Q(V.Z, Quantum)));
	}

	uint64 HashTransform(const FTransform& T, double Quantum)
	{
		FQuat R = T.GetRotation().GetNormalized();
		// q y -q son el mismo giro.
		if (R.W < 0.0) { R = FQuat(-R.X, -R.Y, -R.Z, -R.W); }
		uint64 H = HashVector(T.GetLocation(), Quantum);
		H = Combine(H, HashVector(FVector(R.X, R.Y, R.Z), 1e-4));
		H = Combine(H, static_cast<uint64>(Q(R.W, 1e-4)));
		return Combine(H, HashVector(T.GetScale3D(), 1e-3));
	}

	uint64 HashString(const FString& S)
	{
		return Mix(static_cast<uint64>(FCrc::StrCrc32(*S)));
	}

	/** Convexos, cajas, esferas y cápsulas de una colisión simple (en su espacio local). */
	uint64 HashAggGeom(const FKAggregateGeom& Agg, double Quantum)
	{
		uint64 Sum = 0;
		for (const FKConvexElem& C : Agg.ConvexElems)
		{
			uint64 Verts = 0;
			for (const FVector& V : C.VertexData) { Verts += Mix(HashVector(V, Quantum)); }
			Sum += Combine(Mix(1), Combine(Verts, static_cast<uint64>(C.VertexData.Num())));
		}
		for (const FKBoxElem& B : Agg.BoxElems)
		{
			Sum += Combine(Mix(2), Combine(HashTransform(B.GetTransform(), Quantum), HashVector(FVector(B.X, B.Y, B.Z), Quantum)));
		}
		for (const FKSphereElem& S : Agg.SphereElems)
		{
			Sum += Combine(Mix(3), Combine(HashVector(S.Center, Quantum), static_cast<uint64>(Q(S.Radius, Quantum))));
		}
		for (const FKSphylElem& S : Agg.SphylElems)
		{
			Sum += Combine(Mix(4), Combine(HashTransform(S.GetTransform(), Quantum),
				Combine(static_cast<uint64>(Q(S.Radius, Quantum)), static_cast<uint64>(Q(S.Length, Quantum)))));
		}
		return Sum;
	}

	/**
	 * Forma de una malla estática. Un asset, por su ruta; una generada en ejecución (sin ruta estable: se llama distinto en
	 * cada máquina), por sus vértices, su caja y su colisión simple.
	 */
	struct FMeshSigCache
	{
		TMap<const UStaticMesh*, uint64> Known;

		uint64 Get(const UStaticMesh* Mesh, double Quantum)
		{
			if (!Mesh) { return 0; }
			if (const uint64* Found = Known.Find(Mesh)) { return *Found; }
			uint64 H = 0;
			if (Mesh->IsAsset())
			{
				H = HashString(Mesh->GetPathName());
			}
			else
			{
				const FBoxSphereBounds B = Mesh->GetBounds();
				H = Combine(HashVector(B.Origin, Quantum), HashVector(B.BoxExtent, Quantum));
				if (const FStaticMeshRenderData* RD = Mesh->GetRenderData(); RD && RD->LODResources.Num() > 0)
				{
					const FStaticMeshLODResources& LOD = RD->LODResources[0];
					const FPositionVertexBuffer& Pos = LOD.VertexBuffers.PositionVertexBuffer;
					H = Combine(H, static_cast<uint64>(Pos.GetNumVertices()));
					H = Combine(H, static_cast<uint64>(LOD.GetNumTriangles()));
					if (Pos.GetVertexData())
					{
						uint64 Verts = 0;
						for (uint32 i = 0; i < Pos.GetNumVertices(); ++i) { Verts += Mix(HashVector(FVector(Pos.VertexPosition(i)), Quantum)); }
						H = Combine(H, Verts);
					}
				}
				if (const UBodySetup* Body = Mesh->GetBodySetup())
				{
					H = Combine(H, HashAggGeom(Body->AggGeom, Quantum));
					H = Combine(H, static_cast<uint64>(Body->CollisionTraceFlag));
				}
			}
			Known.Add(Mesh, H);
			return H;
		}
	};

	/** ¿Bloquea a la tortuga o a un vehículo? (lo que hace muros y suelos; un disparador solo solapa). */
	bool Blocks(const UPrimitiveComponent* Prim)
	{
		if (!Prim->IsCollisionEnabled()) { return false; }
		return Prim->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block
			|| Prim->GetCollisionResponseToChannel(ECC_PhysicsBody) == ECR_Block
			|| Prim->GetCollisionResponseToChannel(ECC_Vehicle) == ECR_Block;
	}

	/** Huella de un componente; 0 si no tiene nada (un ISM sin instancias o una malla procedural vacía no hacen colisión). */
	uint64 HashComponent(const UPrimitiveComponent* Prim, const TNMapFingerprint::FOptions& Options, FMeshSigCache& Meshes, int32& InOutPieces)
	{
		const double Quantum = Options.Quantum;
		const bool bBlocks = Blocks(Prim);
		uint64 H = HashString(Prim->GetClass()->GetName());
		H = Combine(H, bBlocks ? 1u : 0u);
		const FTransform& ToWorld = Prim->GetComponentTransform();

		if (const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Prim))
		{
			const int32 Num = ISM->GetInstanceCount();
			if (Num == 0) { return 0; }
			const uint64 Mesh = Meshes.Get(ISM->GetStaticMesh(), Quantum);
			uint64 Sum = 0;
			for (int32 i = 0; i < Num; ++i)
			{
				FTransform T;
				if (ISM->GetInstanceTransform(i, T, true)) { Sum += Mix(HashTransform(T, Quantum)); }
			}
			InOutPieces += Num;
			return Combine(Combine(H, Mesh), Combine(Sum, static_cast<uint64>(Num)));
		}
		if (const UProceduralMeshComponent* Proc = Cast<UProceduralMeshComponent>(Prim))
		{
			uint64 Sum = 0;
			int32 Verts = 0;
			for (int32 s = 0; s < Proc->GetNumSections(); ++s)
			{
				const FProcMeshSection* Section = const_cast<UProceduralMeshComponent*>(Proc)->GetProcMeshSection(s);
				if (!Section || (!Options.bIncludeVisual && !Section->bEnableCollision)) { continue; }
				uint64 SectionSum = 0;
				for (const FProcMeshVertex& V : Section->ProcVertexBuffer)
				{
					SectionSum += Mix(HashVector(ToWorld.TransformPosition(V.Position), Quantum));
				}
				Sum += Combine(SectionSum, static_cast<uint64>(Section->ProcIndexBuffer.Num()));
				Verts += Section->ProcVertexBuffer.Num();
			}
			const UBodySetup* Body = Proc->BodyInstance.GetBodySetup();
			const int32 Convex = Body ? Body->AggGeom.GetElementCount() : 0;
			if (Verts == 0 && Convex == 0) { return 0; }
			if (Convex > 0)
			{
				Sum += Combine(HashTransform(ToWorld, Quantum), HashAggGeom(Body->AggGeom, Quantum));
			}
			InOutPieces += FMath::Max(1, Verts);
			return Combine(H, Sum);
		}
		if (const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(Prim))
		{
			++InOutPieces;
			return Combine(Combine(H, Meshes.Get(SM->GetStaticMesh(), Quantum)), HashTransform(ToWorld, Quantum));
		}
		if (const UBoxComponent* Box = Cast<UBoxComponent>(Prim))
		{
			++InOutPieces;
			return Combine(Combine(H, HashVector(Box->GetUnscaledBoxExtent(), Quantum)), HashTransform(ToWorld, Quantum));
		}
		if (const USphereComponent* Sphere = Cast<USphereComponent>(Prim))
		{
			++InOutPieces;
			return Combine(Combine(H, static_cast<uint64>(Q(Sphere->GetUnscaledSphereRadius(), Quantum))), HashTransform(ToWorld, Quantum));
		}
		if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Prim))
		{
			++InOutPieces;
			H = Combine(H, static_cast<uint64>(Q(Capsule->GetUnscaledCapsuleRadius(), Quantum)));
			H = Combine(H, static_cast<uint64>(Q(Capsule->GetUnscaledCapsuleHalfHeight(), Quantum)));
			return Combine(H, HashTransform(ToWorld, Quantum));
		}
		// Cualquier otra forma (volúmenes, mallas deformadas): su caja en el mundo, a 10 cm.
		++InOutPieces;
		return Combine(Combine(H, HashVector(Prim->Bounds.Origin, Quantum * 10.0)), HashVector(Prim->Bounds.BoxExtent, Quantum * 10.0));
	}

	uint64 HashActorWith(const AActor* Actor, const TNMapFingerprint::FOptions& Options, FMeshSigCache& Meshes, int32& OutPieces)
	{
		TInlineComponentArray<UPrimitiveComponent*> Prims(Actor);
		uint64 Sum = 0;
		int32 Counted = 0;
		for (const UPrimitiveComponent* Prim : Prims)
		{
			if (!IsValid(Prim) || !Prim->IsRegistered()) { continue; }
			// Lo animado (esqueletos) se mueve: no es mapa.
			if (Prim->IsA<USkinnedMeshComponent>()) { continue; }
			if (!Options.bIncludeVisual && !Blocks(Prim)) { continue; }
			// Suma: el orden en que se crearon los componentes no cambia la huella, solo lo que hay.
			const uint64 H = HashComponent(Prim, Options, Meshes, OutPieces);
			if (H == 0) { continue; }
			Sum += Mix(H);
			++Counted;
		}
		return Counted > 0 ? Combine(Sum, static_cast<uint64>(Counted)) : 0;
	}

	/** Mundos de juego de este proceso (en el PIE, uno por ventana). */
	TArray<UWorld*> GameWorlds()
	{
		TArray<UWorld*> Out;
		if (!GEngine) { return Out; }
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld()) { Out.Add(World); }
		}
		return Out;
	}

	const TCHAR* NetModeName(const UWorld* World)
	{
		switch (World ? World->GetNetMode() : NM_Standalone)
		{
			case NM_Client: return TEXT("cliente");
			case NM_ListenServer: return TEXT("anfitrión");
			case NM_DedicatedServer: return TEXT("servidor dedicado");
			default: return TEXT("solo");
		}
	}

	void LogWorlds(bool bVisual)
	{
		for (UWorld* World : GameWorlds())
		{
			TNMapFingerprint::FOptions Options;
			Options.bIncludeVisual = bVisual;
			const TNMapFingerprint::FResult Result = TNMapFingerprint::Compute(World, Options);
			TNMapFingerprint::LogResult(Result, FString::Printf(TEXT("%s · %s%s"), *World->GetMapName(), NetModeName(World),
				bVisual ? TEXT(" · con lo visual") : TEXT("")));
		}
	}

	void HandleCommand(const TArray<FString>& Args)
	{
		float Delay = 0.f;
		bool bVisual = false;
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("all"), ESearchCase::IgnoreCase)) { bVisual = true; }
			else if (Arg.IsNumeric()) { Delay = FMath::Max(0.f, FCString::Atof(*Arg)); }
		}
		if (Delay <= 0.f)
		{
			LogWorlds(bVisual);
			return;
		}
		// Con retraso, por el ticker del motor: sigue valiendo si entretanto se viaja de mapa (-ExecCmds al arrancar).
		UE_LOG(LogTortunabo, Log, TEXT("[Huella] En %.0f s."), Delay);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bVisual](float)
		{
			LogWorlds(bVisual);
			return false;
		}), Delay);
	}

	FAutoConsoleCommand FingerprintCommand(
		TEXT("TN.Map.Fingerprint"),
		TEXT("Huella del mapa (#828): hash de todo lo que bloquea (terreno, estructuras, rocas, decorado con colisión) con las posiciones ")
		TEXT("redondeadas a 1 cm, total y por clase de actor. Anfitrión y clientes deben sacar la misma. ")
		TEXT("TN.Map.Fingerprint [retraso_s] [all] (all = también lo visual sin colisión, que puede cambiar con la calidad)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&HandleCommand));
}

bool TNMapFingerprint::IsMapActor(const AActor* Actor)
{
	if (!IsValid(Actor) || Actor->IsActorBeingDestroyed()) { return false; }
	if (Actor->IsA<APawn>() || Actor->IsA<AController>() || Actor->IsA<AInfo>() || Actor->IsA<AHUD>() || Actor->IsA<APlayerCameraManager>())
	{
		return false;
	}
	// La brocha de construcción del editor (la del nivel por defecto) no es mapa.
	if (const UWorld* World = Actor->GetWorld(); World && Actor == World->GetDefaultBrush())
	{
		return false;
	}
	// Se mueve por la red o con física: su sitio depende del momento, no del mapa.
	if (Actor->IsReplicatingMovement()) { return false; }
	if (const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()); Root && Root->IsSimulatingPhysics())
	{
		return false;
	}
	// Lo que lleva o tiene un jugador.
	for (const AActor* Up = Actor->GetAttachParentActor(); Up; Up = Up->GetAttachParentActor())
	{
		if (Up->IsA<APawn>()) { return false; }
	}
	for (const AActor* Owner = Actor->GetOwner(); Owner; Owner = Owner->GetOwner())
	{
		if (Owner->IsA<APawn>() || Owner->IsA<AController>()) { return false; }
	}
	return true;
}

uint64 TNMapFingerprint::HashActor(const AActor* Actor, const FOptions& Options, int32* OutPieces)
{
	TNMapFingerprintDetail::FMeshSigCache Meshes;
	int32 Pieces = 0;
	const uint64 H = Actor ? TNMapFingerprintDetail::HashActorWith(Actor, Options, Meshes, Pieces) : 0;
	if (OutPieces) { *OutPieces = Pieces; }
	return H;
}

TNMapFingerprint::FResult TNMapFingerprint::Compute(const UWorld* World, const FOptions& Options)
{
	using namespace TNMapFingerprintDetail;
	FResult Result;
	if (!World) { return Result; }
	FMeshSigCache Meshes;
	TMap<FString, FCategory> ByName;
	for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		const AActor* Actor = *It;
		if (!IsMapActor(Actor) || (Options.Filter && !Options.Filter(Actor))) { continue; }
		int32 Pieces = 0;
		const uint64 H = HashActorWith(Actor, Options, Meshes, Pieces);
		if (H == 0) { continue; }
		const bool bReplicated = Actor->GetIsReplicated();
		const FString Name = Actor->GetClass()->GetName() + (bReplicated ? TEXT(" (rep)") : TEXT(""));
		FCategory& Cat = ByName.FindOrAdd(Name);
		Cat.Name = Name;
		Cat.bReplicated = bReplicated;
		Cat.Pieces += Pieces;
		// Suma: el orden de los actores en el mundo (distinto en cada máquina) no cuenta.
		Cat.Hash += Mix(H);
	}
	ByName.KeySort([](const FString& A, const FString& B) { return A < B; });
	uint64 Total = Mix(0x828);
	uint64 Local = Mix(0x828);
	uint64 Replicated = Mix(0x828);
	for (TPair<FString, FCategory>& Pair : ByName)
	{
		FCategory& Cat = Pair.Value;
		Cat.Hash = Combine(Cat.Hash, static_cast<uint64>(Cat.Pieces));
		const uint64 Entry = Combine(HashString(Cat.Name), Cat.Hash);
		Total = Combine(Total, Entry);
		uint64& Group = Cat.bReplicated ? Replicated : Local;
		Group = Combine(Group, Entry);
		Result.Pieces += Cat.Pieces;
		Result.Categories.Add(Cat);
	}
	Result.Total = Total;
	Result.Local = Local;
	Result.Replicated = Replicated;
	return Result;
}

FString TNMapFingerprint::ToHex(uint64 Hash)
{
	return FString::Printf(TEXT("%016llX"), Hash);
}

void TNMapFingerprint::LogResult(const FResult& Result, const FString& Context)
{
	UE_LOG(LogTortunabo, Display, TEXT("[Huella] %s: mapa %s · generado en cada máquina %s · replicado %s · %d piezas en %d clases."),
		*Context, *ToHex(Result.Total), *ToHex(Result.Local), *ToHex(Result.Replicated), Result.Pieces, Result.Categories.Num());
	for (const FCategory& Cat : Result.Categories)
	{
		UE_LOG(LogTortunabo, Display, TEXT("[Huella]   %s %s · %d piezas"), *ToHex(Cat.Hash), *Cat.Name, Cat.Pieces);
	}
}
