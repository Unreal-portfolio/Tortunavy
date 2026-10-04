#include "Art/TN_TurtleArt.h"

#include "Animation/AnimSequence.h"
#include "Art/TN_Art.h"
#include "Art/TN_ArtSettings.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/TN_Log.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"
#include "ReferenceSkeleton.h"

namespace TNTurtleArtDetail
{
	/** SetTemplateMeshForTest. */
	TWeakObjectPtr<const USkeletalMeshComponent> TestTemplate;
	bool bHasTestTemplate = false;

	/** Avisos que ya se han dado (para no repetirlos cada vez que se viste una tortuga). */
	TSet<FString> Warned;

	void WarnOnce(const FString& Key, ELogVerbosity::Type Verbosity, const FString& Message)
	{
		if (Warned.Contains(Key)) { return; }
		Warned.Add(Key);
		if (Verbosity == ELogVerbosity::Error) { UE_LOG(LogTortunabo, Error, TEXT("%s"), *Message); }
		else { UE_LOG(LogTortunabo, Warning, TEXT("%s"), *Message); }
	}

	/** Hueso de Asset que sigue Name (un hueso, o el hueso de un socket de la malla o del esqueleto) o NAME_None. */
	FName ResolveBone(const USkinnedAsset* Asset, FName Name)
	{
		if (!Asset || Name.IsNone()) { return NAME_None; }
		if (const USkeletalMeshSocket* Socket = Asset->FindSocket(Name)) { Name = Socket->BoneName; }
		return Asset->GetRefSkeleton().FindBoneIndex(Name) != INDEX_NONE ? Name : NAME_None;
	}

	UTN_TurtlePieceComponent* FindPiece(const USkeletalMeshComponent* Body, FName Slot)
	{
		for (USceneComponent* Child : Body->GetAttachChildren())
		{
			UTN_TurtlePieceComponent* Piece = Cast<UTN_TurtlePieceComponent>(Child);
			if (Piece && Piece->Slot == Slot && IsValid(Piece)) { return Piece; }
		}
		return nullptr;
	}

	/** La pieza se dibuja como su malla: mismas luces, sombras y capturas, sin choques. */
	void CopyRender(const USkeletalMeshComponent* Body, UTN_TurtlePieceComponent* Piece)
	{
		bool bDirty = false;
		const FLightingChannels& From = Body->LightingChannels;
		const FLightingChannels& To = Piece->LightingChannels;
		if (To.bChannel0 != From.bChannel0 || To.bChannel1 != From.bChannel1 || To.bChannel2 != From.bChannel2)
		{
			Piece->LightingChannels = Body->LightingChannels;
			bDirty = true;
		}
		if (Piece->bVisibleInSceneCaptureOnly != Body->bVisibleInSceneCaptureOnly)
		{
			Piece->bVisibleInSceneCaptureOnly = Body->bVisibleInSceneCaptureOnly;
			bDirty = true;
		}
		if (Piece->bHiddenInSceneCapture != Body->bHiddenInSceneCapture)
		{
			Piece->bHiddenInSceneCapture = Body->bHiddenInSceneCapture;
			bDirty = true;
		}
		if (Piece->bReceivesDecals != Body->bReceivesDecals)
		{
			Piece->bReceivesDecals = Body->bReceivesDecals;
			bDirty = true;
		}
		Piece->SetCastShadow(Body->CastShadow);
		if (bDirty && Piece->IsRegistered()) { Piece->MarkRenderStateDirty(); }
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Cuerpo
// ─────────────────────────────────────────────────────────────────────────────

TArrayView<const TNTurtleArt::FPieceInfo> TNTurtleArt::GetPieces()
{
	// Huesos de TotugaDemo_Rig (Mixamo): Spine1 es la mitad de la espalda (Hips a 25 de alto, Spine2 a 32, el caparazón de
	// 22 a 38); Head, la cabeza.
	static const FPieceInfo Pieces[] = {
		{ TN_ART("Turtle.Shell"), FName(TEXT("Spine1")) },
		{ TN_ART("Turtle.Helmet"), FName(TEXT("Head")) },
		{ TN_ART("Turtle.Eyes"), FName(TEXT("Head")) },
		{ TN_ART("Turtle.Tongue"), FName(TEXT("Head")) },
	};
	return MakeArrayView(Pieces);
}

FName TNTurtleArt::HelmetPiece()
{
	static const FName Helmet = TN_ART("Turtle.Helmet");
	return Helmet;
}

UClass* TNTurtleArt::GetCharacterClass()
{
	using namespace TNTurtleArtDetail;
	const UTN_ArtSettings* Settings = GetDefault<UTN_ArtSettings>();
	if (!Settings || Settings->TurtleCharacter.IsNull())
	{
		WarnOnce(TEXT("NoCharacter"), ELogVerbosity::Error,
			TEXT("[Arte] Falta el personaje de la tortuga (Ajustes del proyecto > Tortunavy > Arte > Tortuga): las copias se quedan sin malla."));
		return nullptr;
	}
	UClass* Class = Settings->TurtleCharacter.Get();
	if (!Class) { Class = Settings->TurtleCharacter.LoadSynchronous(); }
	if (!Class)
	{
		WarnOnce(TEXT("NoClass"), ELogVerbosity::Error, FString::Printf(TEXT("[Arte] No carga el personaje de la tortuga %s: las copias se quedan sin malla."),
			*Settings->TurtleCharacter.ToString()));
	}
	return Class;
}

const USkeletalMeshComponent* TNTurtleArt::GetTemplateMesh()
{
	using namespace TNTurtleArtDetail;
	if (bHasTestTemplate) { return TestTemplate.Get(); }
	const UClass* Class = GetCharacterClass();
	const ACharacter* Defaults = Class ? Cast<ACharacter>(Class->GetDefaultObject()) : nullptr;
	return Defaults ? Defaults->GetMesh() : nullptr;
}

USkeletalMesh* TNTurtleArt::GetMesh()
{
	const USkeletalMeshComponent* Template = GetTemplateMesh();
	return Template ? Template->GetSkeletalMeshAsset() : nullptr;
}

const FTransform& TNTurtleArt::GetReferenceMeshTransform()
{
	static const FTransform Reference(FRotator(0.f, -90.f, 0.f), FVector(0.0, 0.0, -70.0), FVector(2.5));
	return Reference;
}

FTransform TNTurtleArt::ComputeCopyCorrection(const FTransform& CharacterMesh)
{
	const FTransform& Reference = GetReferenceMeshTransform();
	if (CharacterMesh.Equals(Reference, 1.e-3)) { return FTransform::Identity; }
	// Copia = Personaje · Referencia⁻¹ · Demo: en la postura de demo de la copia, la malla queda como queda en el personaje
	// respecto a su malla de demo (mismo pivote, giro y escala relativos).
	return CharacterMesh * Reference.Inverse();
}

FTransform TNTurtleArt::GetCopyCorrection()
{
	const USkeletalMeshComponent* Template = GetTemplateMesh();
	return Template ? ComputeCopyCorrection(Template->GetRelativeTransform()) : FTransform::Identity;
}

bool TNTurtleArt::ApplyBody(USkeletalMeshComponent* Copy, const FTransform& DemoRelative)
{
	if (!Copy) { return false; }
	const USkeletalMeshComponent* Template = GetTemplateMesh();
	Copy->SetRelativeTransform(Template ? ComputeCopyCorrection(Template->GetRelativeTransform()) * DemoRelative : DemoRelative);
	USkeletalMesh* Mesh = Template ? Template->GetSkeletalMeshAsset() : nullptr;
	if (!Mesh || Copy->GetSkeletalMeshAsset() == Mesh) { return false; }
	Copy->SetSkeletalMeshAsset(Mesh);
	Copy->EmptyOverrideMaterials();
	for (int32 i = 0; i < Template->OverrideMaterials.Num(); ++i)
	{
		if (Template->OverrideMaterials[i]) { Copy->SetMaterial(i, Template->OverrideMaterials[i]); }
	}
	return true;
}

UAnimSequence* TNTurtleArt::GetClip(ETNTurtleClip Clip)
{
	const UTN_ArtSettings* Settings = GetDefault<UTN_ArtSettings>();
	if (!Settings) { return nullptr; }
	const TSoftObjectPtr<UAnimSequence>* Soft = nullptr;
	switch (Clip)
	{
	case ETNTurtleClip::Idle:   Soft = &Settings->IdleAnim; break;
	case ETNTurtleClip::Walk:   Soft = &Settings->WalkAnim; break;
	case ETNTurtleClip::Cheer:  Soft = &Settings->CheerAnim; break;
	case ETNTurtleClip::Salute: Soft = &Settings->SaluteAnim; break;
	}
	if (!Soft || Soft->IsNull()) { return nullptr; }
	UAnimSequence* Anim = Soft->Get();
	if (!Anim) { Anim = Soft->LoadSynchronous(); }
	if (!Anim)
	{
		TNTurtleArtDetail::WarnOnce(Soft->ToString(), ELogVerbosity::Warning,
			FString::Printf(TEXT("[Arte] No carga la animación de la tortuga %s (Ajustes del proyecto > Tortunavy > Arte)."), *Soft->ToString()));
	}
	return Anim;
}

bool TNTurtleArt::IsTurtleMesh(const USkinnedAsset* Asset)
{
	if (!Asset) { return false; }
	if (const USkeletalMesh* Turtle = GetMesh())
	{
		if (Asset == Turtle) { return true; }
		const USkeletalMesh* Other = Cast<USkeletalMesh>(Asset);
		if (Other && Turtle->GetSkeleton() && Other->GetSkeleton() == Turtle->GetSkeleton()) { return true; }
	}
	// Las maquetas de LVL_Lobby llevan la de demo aunque el personaje ya tenga otra (y otro esqueleto).
	return Asset->GetName().Contains(TEXT("TotugaDemo"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Piezas
// ─────────────────────────────────────────────────────────────────────────────

FTransform TNTurtleArt::ComputePieceRelative(const USkinnedAsset* Asset, FName Bone, const FTransform& Adjust)
{
	const FName BoneName = TNTurtleArtDetail::ResolveBone(Asset, Bone);
	if (BoneName.IsNone()) { return Adjust; }
	// El hueso en el espacio de la malla, en la postura de referencia: la pieza queda fija respecto a él.
	const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
	const TArray<FTransform>& Pose = Ref.GetRefBonePose();
	FTransform BoneCS = FTransform::Identity;
	for (int32 Index = Ref.FindBoneIndex(BoneName); Index != INDEX_NONE; Index = Ref.GetParentIndex(Index))
	{
		BoneCS = BoneCS * Pose[Index];
	}
	return Adjust.GetRelativeTransform(BoneCS);
}

bool TNTurtleArt::ApplyPieces(USkeletalMeshComponent* Body, bool bCosmeticHelmet)
{
	using namespace TNTurtleArtDetail;
	if (!Body || !TNArt::CanModify(Body)) { return false; }
	// Solo se ven: un servidor dedicado no las necesita.
	if (Body->GetNetMode() == NM_DedicatedServer) { return false; }
	const USkinnedAsset* Asset = Body->GetSkinnedAsset();
	AActor* Owner = Body->GetOwner();
	bool bArtHelmet = false;
	for (const FPieceInfo& Info : GetPieces())
	{
		const TNArt::FResolved* R = Asset ? TNArt::Find(Info.Slot) : nullptr;
		UTN_TurtlePieceComponent* Piece = FindPiece(Body, Info.Slot);
		if (!R)
		{
			// Sin malla en el catálogo: la tortuga es solo su malla (y se quita la pieza de una vez anterior).
			TNArt::NoteSlot(Info.Slot, Asset);
			if (Piece) { Piece->DestroyComponent(); }
			continue;
		}
		const FName Wanted = R->Bone.IsNone() ? Info.DefaultBone : R->Bone;
		const FName BoneName = ResolveBone(Asset, Wanted);
		if (BoneName.IsNone())
		{
			WarnOnce(FString::Printf(TEXT("%s|%s|%s"), *Info.Slot.ToString(), *Wanted.ToString(), *GetNameSafe(Asset)), ELogVerbosity::Warning,
				FString::Printf(TEXT("[Arte] %s: la malla %s no tiene el hueso ni el socket %s; la pieza va pegada a la malla sin seguir la animación."),
					*Info.Slot.ToString(), *GetNameSafe(Asset), *Wanted.ToString()));
		}
		if (!Piece)
		{
			Piece = NewObject<UTN_TurtlePieceComponent>(Owner ? static_cast<UObject*>(Owner) : static_cast<UObject*>(Body), NAME_None,
				RF_Transient | RF_DuplicateTransient);
			Piece->Slot = Info.Slot;
			Piece->SetupAttachment(Body, BoneName);
			CopyRender(Body, Piece);
			Piece->RegisterComponent();
		}
		else
		{
			if (Piece->GetAttachSocketName() != BoneName)
			{
				Piece->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform, BoneName);
			}
			CopyRender(Body, Piece);
		}
		if (Piece->GetStaticMesh() != R->Mesh) { Piece->SetStaticMesh(R->Mesh); }
		Piece->EmptyOverrideMaterials();
		for (int32 i = 0; i < R->Materials.Num(); ++i)
		{
			if (R->Materials[i]) { Piece->SetMaterial(i, R->Materials[i]); }
		}
		Piece->SetRelativeTransform(ComputePieceRelative(Asset, BoneName, R->Adjust));
		const bool bHelmet = Info.Slot == HelmetPiece();
		Piece->bSuppressed = bHelmet && bCosmeticHelmet;
		Piece->SyncWithBody();
		bArtHelmet |= bHelmet;
		TNArt::NoteSlot(Info.Slot, R->Mesh);
	}
	return bArtHelmet;
}

void TNTurtleArt::GetPieceComponents(const USkeletalMeshComponent* Body, TArray<UPrimitiveComponent*>& Out)
{
	if (!Body) { return; }
	for (USceneComponent* Child : Body->GetAttachChildren())
	{
		UTN_TurtlePieceComponent* Piece = Cast<UTN_TurtlePieceComponent>(Child);
		if (Piece && IsValid(Piece)) { Out.Add(Piece); }
	}
}

void TNTurtleArt::SetTemplateMeshForTest(const USkeletalMeshComponent* Template)
{
	TNTurtleArtDetail::TestTemplate = Template;
	TNTurtleArtDetail::bHasTestTemplate = Template != nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Componente de una pieza
// ─────────────────────────────────────────────────────────────────────────────

UTN_TurtlePieceComponent::UTN_TurtlePieceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	bTickInEditor = false;
	SetIsReplicatedByDefault(false);
	SetMobility(EComponentMobility::Movable);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
}

void UTN_TurtlePieceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	SyncWithBody();
}

void UTN_TurtlePieceComponent::SyncWithBody()
{
	// Se esconde y se enseña con la malla (el código la esconde y la enseña como siempre) y su dueño la ve como la malla.
	const USceneComponent* Parent = GetAttachParent();
	const bool bWant = Parent && Parent->IsVisible() && !bSuppressed;
	if (bWant != GetVisibleFlag()) { SetVisibility(bWant); }
	if (const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Parent))
	{
		if (bOwnerNoSee != Body->bOwnerNoSee) { SetOwnerNoSee(Body->bOwnerNoSee); }
		if (bCastHiddenShadow != Body->bCastHiddenShadow)
		{
			bCastHiddenShadow = Body->bCastHiddenShadow;
			MarkRenderStateDirty();
		}
	}
}
