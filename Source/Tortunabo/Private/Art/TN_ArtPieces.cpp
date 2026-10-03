#include "TN_ArtPieces.h"

#include "Art/TN_ArtMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

// TN_Art.cpp
namespace TNArtDetail
{
	bool CanModify(const UActorComponent* Comp);
	void NoteEditorOwner(const UActorComponent* Comp);
	void ApplyMaterials(UMeshComponent* Comp, const TNArt::FResolved& R);
	void CopyCollision(const UPrimitiveComponent* From, UPrimitiveComponent* To);
}

namespace TNArtPiecesDetail
{
	/** La copia Inner va dentro de Outer: cada tramo suyo cae dentro de uno de Outer en el mismo buffer. */
	bool IsInside(const TNArt::FPiece& Inner, const TNArt::FPiece& Outer)
	{
		if (Inner.Ranges.Num() == 0) { return false; }
		for (const TNArt::FPieceRange& In : Inner.Ranges)
		{
			bool bFound = false;
			for (const TNArt::FPieceRange& Out : Outer.Ranges)
			{
				if (Out.Buffer == In.Buffer && Out.T0 <= In.T0 && In.T1 <= Out.T1)
				{
					bFound = true;
					break;
				}
			}
			if (!bFound) { return false; }
		}
		return true;
	}
}

int32 TNArt::FPieceLog::Begin(FName Slot, const FTransform& Pivot, std::initializer_list<const FPieceBuffers*> Buffers)
{
	FPiece& Piece = Pieces.AddDefaulted_GetRef();
	Piece.Slot = Slot;
	Piece.Pivot = Pivot;
	FStart Start;
	Start.Piece = Pieces.Num() - 1;
	for (const FPieceBuffers* B : Buffers)
	{
		if (!B) { continue; }
		FPieceRange& R = Start.At.AddDefaulted_GetRef();
		R.Buffer = B;
		R.V0 = B->Verts.Num();
		R.T0 = B->Tris.Num();
	}
	return Starts.Add(MoveTemp(Start));
}

void TNArt::FPieceLog::End(int32 Handle)
{
	if (!Starts.IsValidIndex(Handle) || !Pieces.IsValidIndex(Starts[Handle].Piece)) { return; }
	FStart& Start = Starts[Handle];
	FPiece& Piece = Pieces[Start.Piece];
	for (FPieceRange R : Start.At)
	{
		R.V1 = R.Buffer->Verts.Num();
		R.T1 = R.Buffer->Tris.Num();
		if (R.T1 > R.T0) { Piece.Ranges.Add(R); }
	}
	Start.Piece = INDEX_NONE;
}

void TNArt::FPieceLog::CollectRemoved(const FPieceBuffers& B, bool bForCollision, TArray<FPieceRange>& OutRanges) const
{
	for (const FPiece& Piece : Pieces)
	{
		const FResolved* Art = nullptr;
		bool bLooked = false;
		for (const FPieceRange& R : Piece.Ranges)
		{
			if (R.Buffer != &B) { continue; }
			if (!bLooked)
			{
				bLooked = true;
				Art = Find(Piece.Slot);
			}
			if (Art && (!bForCollision || Art->bUseArtCollision))
			{
				OutRanges.Add(R);
			}
		}
	}
}

void TNArt::FilterBuffers(const FPieceBuffers& In, TArrayView<const FPieceRange> Removed, FPieceBuffers& Out)
{
	const int32 NumTris = In.Tris.Num() / 3;
	TBitArray<> DropTri(false, NumTris);
	bool bAny = false;
	for (const FPieceRange& R : Removed)
	{
		if (R.Buffer != &In) { continue; }
		for (int32 t = FMath::Max(0, R.T0 / 3); t < FMath::Min(NumTris, (R.T1 + 2) / 3); ++t)
		{
			DropTri[t] = true;
			bAny = true;
		}
	}
	if (!bAny)
	{
		Out = In;
		return;
	}
	const int32 NumVerts = In.Verts.Num();
	TArray<int32> Remap;
	Remap.Init(INDEX_NONE, NumVerts);
	for (int32 t = 0; t < NumTris; ++t)
	{
		if (DropTri[t]) { continue; }
		for (int32 k = 0; k < 3; ++k)
		{
			const int32 V = In.Tris[t * 3 + k];
			if (Remap.IsValidIndex(V)) { Remap[V] = 0; }
		}
	}
	Out.Verts.Reset();
	Out.Normals.Reset();
	Out.UVs.Reset();
	Out.Colors.Reset();
	Out.Tris.Reset();
	for (int32 v = 0; v < NumVerts; ++v)
	{
		if (Remap[v] == INDEX_NONE) { continue; }
		Remap[v] = Out.Verts.Num();
		Out.Verts.Add(In.Verts[v]);
		Out.Normals.Add(In.Normals.IsValidIndex(v) ? In.Normals[v] : FVector::UpVector);
		Out.UVs.Add(In.UVs.IsValidIndex(v) ? In.UVs[v] : FVector2D::ZeroVector);
		Out.Colors.Add(In.Colors.IsValidIndex(v) ? In.Colors[v] : FLinearColor::White);
	}
	Out.Tris.Reserve(In.Tris.Num());
	for (int32 t = 0; t < NumTris; ++t)
	{
		if (DropTri[t]) { continue; }
		const int32 A = In.Tris[t * 3], B = In.Tris[t * 3 + 1], C = In.Tris[t * 3 + 2];
		if (!Remap.IsValidIndex(A) || !Remap.IsValidIndex(B) || !Remap.IsValidIndex(C)) { continue; }
		Out.Tris.Add(Remap[A]);
		Out.Tris.Add(Remap[B]);
		Out.Tris.Add(Remap[C]);
	}
}

void TNArt::UploadSection(UProceduralMeshComponent* Comp, int32 Section, const FPieceBuffers& B, bool bCollision, UMaterialInterface* Mat,
	const FPieceLog* Log, bool bSRGBConversion)
{
	if (!Comp) { return; }
	TNArtDetail::NoteEditorOwner(Comp);
	const TArray<FProcMeshTangent> NoTangents;
	TArray<FPieceRange> Removed;
	if (Log && TNArtDetail::CanModify(Comp))
	{
		Log->CollectRemoved(B, false, Removed);
	}
	if (Removed.Num() == 0)
	{
		// Sin sustitutos en esta sección: exactamente lo de siempre.
		Comp->CreateMeshSection_LinearColor(Section, B.Verts, B.Tris, B.Normals, B.UVs, B.Colors, NoTangents, bCollision, bSRGBConversion);
		if (Mat) { Comp->SetMaterial(Section, Mat); }
		return;
	}
	FPieceBuffers Visible;
	FilterBuffers(B, Removed, Visible);
	Comp->CreateMeshSection_LinearColor(Section, Visible.Verts, Visible.Tris, Visible.Normals, Visible.UVs, Visible.Colors, NoTangents, false,
		bSRGBConversion);
	if (Mat) { Comp->SetMaterial(Section, Mat); }
	if (bCollision)
	{
		// La colisión, de los buffers enteros (menos las piezas que chocan con su arte), en una sección que no se dibuja.
		TArray<FPieceRange> RemovedCollision;
		Log->CollectRemoved(B, true, RemovedCollision);
		FPieceBuffers Collision;
		FilterBuffers(B, RemovedCollision, Collision);
		const int32 CollisionSection = Section + CollisionSectionOffset;
		Comp->CreateMeshSection_LinearColor(CollisionSection, Collision.Verts, Collision.Tris, Collision.Normals, Collision.UVs, Collision.Colors,
			NoTangents, true, bSRGBConversion);
		Comp->SetMeshSectionVisible(CollisionSection, false);
	}
}

void TNArt::ClearPieceArt(AActor* Owner, FName Group)
{
	if (!Owner) { return; }
	TInlineComponentArray<UTN_ArtMeshComponent*> Arts;
	Owner->GetComponents(Arts);
	for (UTN_ArtMeshComponent* Art : Arts)
	{
		if (Art && !Art->Group.IsNone() && Art->Group == Group)
		{
			Art->DestroyComponent();
		}
	}
}

void TNArt::SpawnPieceArt(USceneComponent* AttachTo, const FPieceLog& Log)
{
	if (!AttachTo) { return; }
	TNArtDetail::NoteEditorOwner(AttachTo);
	AActor* Owner = AttachTo->GetOwner();
	ClearPieceArt(Owner, Log.GetGroup());
	if (!TNArtDetail::CanModify(AttachTo)) { return; }

	// Copias con sustituto (las anidadas dentro de otra con sustituto van con ella). Todas se anotan (TN.Art.Slots).
	TArray<int32> WithArt;
	const TArray<FPiece>& Pieces = Log.GetPieces();
	for (int32 i = 0; i < Pieces.Num(); ++i)
	{
		if (Pieces[i].Ranges.Num() == 0) { continue; }
		const FResolved* Art = Find(Pieces[i].Slot);
		NoteSlot(Pieces[i].Slot, Art ? static_cast<const UObject*>(Art->Mesh.Get()) : nullptr);
		if (Art) { WithArt.Add(i); }
	}
	TMap<FName, TArray<FTransform>> Instances;
	for (const int32 i : WithArt)
	{
		bool bNested = false;
		for (const int32 j : WithArt)
		{
			if (j != i && TNArtPiecesDetail::IsInside(Pieces[i], Pieces[j]) && !(TNArtPiecesDetail::IsInside(Pieces[j], Pieces[i]) && j > i))
			{
				bNested = true;
				break;
			}
		}
		if (!bNested)
		{
			Instances.FindOrAdd(Pieces[i].Slot).Add(Pieces[i].Pivot);
		}
	}
	const UPrimitiveComponent* CollisionFrom = Cast<UPrimitiveComponent>(AttachTo);
	for (TPair<FName, TArray<FTransform>>& Pair : Instances)
	{
		const FResolved* R = Find(Pair.Key);
		if (!R) { continue; }
		UTN_ArtMeshComponent* Art = NewObject<UTN_ArtMeshComponent>(Owner ? static_cast<UObject*>(Owner) : static_cast<UObject*>(AttachTo), NAME_None,
			RF_Transient | RF_DuplicateTransient);
		Art->Slot = Pair.Key;
		Art->Group = Log.GetGroup();
		Art->SetStaticMesh(R->Mesh);
		TNArtDetail::ApplyMaterials(Art, *R);
		if (R->bUseArtCollision && CollisionFrom)
		{
			TNArtDetail::CopyCollision(CollisionFrom, Art);
		}
		else
		{
			Art->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		Art->SetupAttachment(AttachTo);
		Art->RegisterComponent();
		for (FTransform& Xf : Pair.Value) { Xf = R->Adjust * Xf; }
		Art->AddInstances(Pair.Value, false, false);
	}
}
