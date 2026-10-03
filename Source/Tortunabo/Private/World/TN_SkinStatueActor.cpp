#include "World/TN_SkinStatueActor.h"
#include "Core/TN_Log.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Core/TN_CoopPlayerState.h"
#include "Core/TN_CosmeticsTypes.h"
#include "Player/TortugaCharacter.h"
#include "Player/MP_GamePlayerController.h"
#include "Multiplayer/MP_GameInstance.h"
#include "Engine/DataTable.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/BoxComponent.h"
#include "Core/TN_CosmeticLook.h"
#include "Engine/SkeletalMesh.h"
#include "Lobby/TN_CastleKit.h"
#include "World/TN_PlaceholderArt.h"
#include "World/TN_PlaceholderArtMeshes.h"
#include "Art/TN_TurtleArt.h"

namespace TNStatueArt
{
	/** Alto de TotugaDemo_Rig a escala 1 (el personaje la lleva a 2,5: unos 133 cm), para la caja que bloquea. */
	constexpr float TurtleHeight = 53.f;
	/** La malla mira a su +Y: girada -90 mira al +X del actor (hacia donde miraba la estatua de cubos). */
	constexpr float TurtleYaw = -90.f;
	constexpr int32 Sides = 8;

	/** Peana octogonal: zócalo, fuste algo más estrecho, cornisa y una losa del color del tipo de estatua. */
	void BuildPedestal(TNProcMesh::FTNProcMeshBuffers& B, double Radius, double Height, uint32 AccentHex)
	{
		const FLinearColor Stone = TNCastleKit::Pal(0xD8C8A4);
		const FLinearColor Trim = TNCastleKit::Pal(0xB89D6C);
		const double Plinth = Height * 0.2;
		const double Cornice = Height * 0.82;
		const double Slab = Height * 0.92;
		TNProcMesh::TNProcAddCylinder(B, FVector::ZeroVector, FVector(0.0, 0.0, Plinth), Radius, Radius, Sides, Trim);
		TNProcMesh::TNProcAddCylinder(B, FVector(0.0, 0.0, Plinth), FVector(0.0, 0.0, Cornice), Radius * 0.86, Radius * 0.82, Sides, Stone);
		TNProcMesh::TNProcAddCylinder(B, FVector(0.0, 0.0, Cornice), FVector(0.0, 0.0, Slab), Radius * 0.96, Radius * 0.96, Sides, Trim);
		TNProcMesh::TNProcAddCylinder(B, FVector(0.0, 0.0, Slab), FVector(0.0, 0.0, Height), Radius * 0.9, Radius * 0.9, Sides, TNCastleKit::Pal(AccentHex));
	}
}

ATN_SkinStatueActor::ATN_SkinStatueActor()
{
	// Sin malla ni pose propias, la estatua es la tortuga del personaje y su saludo (TNTurtleArt, #581).

	PreviewMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PreviewMesh"));
	PreviewMesh->SetupAttachment(SceneRoot);
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMesh->SetIsReplicated(false);

	// SombreroSocket: punto de anclaje del casco — posiciónalo sobre la cabeza en el BP.
	SombreroSocket = CreateDefaultSubobject<USceneComponent>(TEXT("Sombrero"));
	SombreroSocket->SetupAttachment(PreviewMesh);

	HelmetPreviewComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HelmetPreviewMesh"));
	HelmetPreviewComp->SetupAttachment(SombreroSocket);
	HelmetPreviewComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HelmetPreviewComp->SetIsReplicated(false);
	HelmetPreviewComp->SetHiddenInGame(true);

	PromptText = NSLOCTEXT("Tortunabo", "SkinStatuePrompt", "Equipar cosmético");
	CooldownSeconds = 0.5f;
}

void ATN_SkinStatueActor::BeginPlay()
{
	Super::BeginPlay();

	bCodeStatue = BuildCodeStatue();
	if (bCodeStatue)
	{
		return;
	}

	// Statues tipo Helmet no asignan SkeletalMesh al PreviewMesh → el componente
	// spamea "GetSocketByName(None): No SkeletalMesh". Si está vacío, deshabilitamos
	// tick y lo ocultamos — nadie lo usa, pero sin esto el warning llena el log.
	if (PreviewMesh && !PreviewMesh->GetSkeletalMeshAsset())
	{
		PreviewMesh->SetComponentTickEnabled(false);
		PreviewMesh->SetVisibility(false);
	}

	ApplyPreviewCosmetic();
}

bool ATN_SkinStatueActor::BuildCodeStatue()
{
	if (GetNetMode() == NM_DedicatedServer || !PreviewMesh || PreviewMesh->GetSkeletalMeshAsset())
	{
		return false;
	}
	const FBox Old = TNPlaceholderArt::VisiblePlaceholderBounds(this);
	USkeletalMesh* Override = StatueTurtleMesh.LoadSynchronous();
	if (!Old.IsValid || (!Override && !TNTurtleArt::GetMesh()))
	{
		return false;
	}
	// Donde estaba la estatua de cubos: centro de su planta, a ras de su base.
	const FVector Ground(Old.GetCenter().X, Old.GetCenter().Y, Old.Min.Z);
	TNPlaceholderArt::HidePlaceholders(this, true);
	BuildPedestal(Ground);
	// La colocación es la de la malla de demo; con otra malla en el personaje, ApplyBody le suma la misma diferencia que a
	// él (pivote, giro y escala) y le pone sus materiales: cambiar la tortuga del Blueprint cambia también la estatua.
	const FTransform DemoWorld(GetActorRotation() + FRotator(0.f, TNStatueArt::TurtleYaw, 0.f),
		Ground + FVector(0.0, 0.0, PedestalHeight), FVector(StatueTurtleScale));
	const USceneComponent* Parent = PreviewMesh->GetAttachParent();
	const FTransform DemoRelative = Parent ? DemoWorld.GetRelativeTransform(Parent->GetComponentTransform()) : DemoWorld;
	if (Override)
	{
		PreviewMesh->SetSkeletalMeshAsset(Override);
		PreviewMesh->SetRelativeTransform(DemoRelative);
	}
	else
	{
		TNTurtleArt::ApplyBody(PreviewMesh, DemoRelative);
	}
	PreviewMesh->SetVisibility(true);
	PreviewMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	FreezePose();
	DressCodeStatue();
	return true;
}

void ATN_SkinStatueActor::BuildPedestal(const FVector& GroundCenter)
{
	// Los colores de los huevos de la salida: coral para los cascos, turquesa para los colores.
	const uint32 AccentHex = TNCastleKit::EggAccent(CosmeticType == ETNCosmeticType::Helmet ? 1 : 0);
	const FString Key = FString::Printf(TEXT("Statue.Pedestal.%.0f.%.0f.%06X"), PedestalRadius, PedestalHeight, AccentHex);
	const double Radius = PedestalRadius;
	const double Height = PedestalHeight;
	UStaticMesh* PedestalMesh = TNPlaceholderArt::CachedArtMesh(Key, [Radius, Height, AccentHex](TNProcMesh::FTNProcMeshBuffers& B)
	{
		TNStatueArt::BuildPedestal(B, Radius, Height, AccentHex);
	}, TNPlaceholderArt::MatteAlpha);
	const FTransform AtGround(GetActorRotation(), GroundCenter);
	PedestalComp = TNPlaceholderArt::AddArtPart(this, GetRootComponent(), PedestalMesh, AtGround.GetRelativeTransform(GetActorTransform()));
	if (PedestalComp)
	{
		PedestalComp->SetAbsolute(false, false, true);
		PedestalComp->SetWorldScale3D(FVector::OneVector);
	}
	// Una caja para la peana y la tortuga: bloquea como la estatua de cubos y la encuentra el escaneo (WorldDynamic).
	const float HalfHeight = (PedestalHeight + TNStatueArt::TurtleHeight * StatueTurtleScale) * 0.5f;
	StatueBlocker = NewObject<UBoxComponent>(this, NAME_None, RF_Transient);
	StatueBlocker->SetupAttachment(GetRootComponent());
	StatueBlocker->SetAbsolute(false, false, true);
	StatueBlocker->SetBoxExtent(FVector(PedestalRadius, PedestalRadius, HalfHeight));
	StatueBlocker->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	StatueBlocker->RegisterComponent();
	StatueBlocker->SetWorldLocationAndRotation(GroundCenter + FVector(0.0, 0.0, HalfHeight), GetActorRotation());
}

void ATN_SkinStatueActor::FreezePose()
{
	UAnimationAsset* Pose = StatuePose.IsNull() ? TNTurtleArt::GetClip(ETNTurtleClip::Salute) : StatuePose.LoadSynchronous();
	if (!Pose)
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[CosmeticStatue] %s: sin animación de pose; la tortuga queda en la postura de referencia."), *GetName());
		return;
	}
	PreviewMesh->SetComponentTickEnabled(true);
	PreviewMesh->PlayAnimation(Pose, false);
	if (UAnimSingleNodeInstance* Single = PreviewMesh->GetSingleNodeInstance())
	{
		Single->SetPosition(FMath::Min(StatuePoseSeconds, Pose->GetPlayLength()), false);
		Single->SetPlaying(false);
	}
}

void ATN_SkinStatueActor::DressCodeStatue()
{
	FTN_TurtleLook Look;
	if (CosmeticType == ETNCosmeticType::Helmet)
	{
		Look.HelmetId = CosmeticId;
	}
	else
	{
		Look.SkinId = CosmeticId;
	}
	UTN_CosmeticLook::ApplyLook(this, PreviewMesh, HelmetPreviewComp, Look, StatueDefaultMaterials);
}

void ATN_SkinStatueActor::ApplyPreviewCosmetic()
{
	if (CosmeticId == NAME_None)
	{
		return;
	}

	const UMP_GameInstance* GI = Cast<UMP_GameInstance>(GetGameInstance());
	if (!GI)
	{
		return;
	}

	switch (CosmeticType)
	{
		case ETNCosmeticType::Skin:
		{
			const FTN_SkinData* Row = GI->FindSkinRow(CosmeticId, TEXT("StatuePreview"));
			if (!Row) { break; }

			// Mapeo de la estatua (componentes legacy con nombres) hacia los slots
			// del nuevo sistema de 5 materiales:
			//   "Body"             → ShellMaterial (caparazón)
			//   "Body1"            → BellyMaterial (panza, fallback Shell)
			//   "Mesh1..5","Mesh13"→ SkinMaterial (cabeza/patas/extremidades)
			// Los slots EyeShineMaterial y EyesMouthMaterial son específicos del
			// SkM unificado del personaje y no tienen target en la estatua vieja.
			static const TArray<FName> ShellNames = { TEXT("Body") };
			static const TArray<FName> BellyNames = { TEXT("Body1") };
			static const TArray<FName> SkinNames = {
				TEXT("Mesh1"), TEXT("Mesh2"), TEXT("Mesh3"),
				TEXT("Mesh4"), TEXT("Mesh5"), TEXT("Mesh13")
			};

			// Iterar todos los StaticMeshComponents hijos de la estatua y aplicar
			// el material correcto según el nombre del componente.
			TArray<UActorComponent*> AllComps;
			GetComponents(AllComps);
			for (UActorComponent* ActComp : AllComps)
			{
				UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(ActComp);
				if (!SMC || SMC == HelmetPreviewComp) { continue; }

				const FName CompName = SMC->GetFName();

				UMaterialInterface* MatToApply = nullptr;
				if (ShellNames.Contains(CompName) && Row->ShellMaterial)
				{
					MatToApply = Row->ShellMaterial;
				}
				else if (BellyNames.Contains(CompName))
				{
					MatToApply = Row->BellyMaterial ? Row->BellyMaterial : Row->ShellMaterial;
				}
				else if (SkinNames.Contains(CompName) && Row->SkinMaterial)
				{
					MatToApply = Row->SkinMaterial;
				}

				if (!MatToApply) { continue; }

				const int32 N = SMC->GetNumMaterials();
				for (int32 i = 0; i < N; ++i)
				{
					SMC->SetMaterial(i, MatToApply);
				}
			}
			break;
		}

		case ETNCosmeticType::Helmet:
		{
			const FTN_HelmetData* Row = GI->FindHelmetRow(CosmeticId, TEXT("StatuePreview"));
			if (!Row || !Row->DisplayMesh) { break; }

			HelmetPreviewComp->SetStaticMesh(Row->DisplayMesh);
			HelmetPreviewComp->SetRelativeScale3D(Row->MeshScale.IsNearlyZero() ? FVector::OneVector : Row->MeshScale);
			HelmetPreviewComp->SetRelativeLocation(Row->MeshOffset);
			HelmetPreviewComp->SetRelativeRotation(Row->MeshRotation);
			HelmetPreviewComp->SetHiddenInGame(false);
			break;
		}
	}
}

void ATN_SkinStatueActor::Interact(APawn* Interactor)
{
	if (!HasAuthority() || !CanInteract(Interactor) || !Interactor)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(Interactor->GetController());
	ATN_CoopPlayerState* TNPS = PC ? PC->GetPlayerState<ATN_CoopPlayerState>() : nullptr;
	ATortugaCharacter* TurtleChar = Cast<ATortugaCharacter>(Interactor);
	AMP_GamePlayerController* TNPC = Cast<AMP_GamePlayerController>(PC);

	if (!TNPS)
	{
		return;
	}

	switch (CosmeticType)
	{
		case ETNCosmeticType::Skin:
		{
			const FName NewSkin = (TNPS->EquippedSkinId == CosmeticId) ? NAME_None : CosmeticId;
			TNPS->EquippedSkinId = NewSkin;
			TNPS->ForceNetUpdate();

			if (TurtleChar) { TurtleChar->UpdateSkinVisual(NewSkin); }
			if (TNPC)       { TNPC->NotifySkinEquipped(NewSkin); }

			UE_LOG(LogTortunabo, Log, TEXT("[CosmeticStatue] SKIN '%s' → '%s'"),
				*GetNameSafe(Interactor),
				NewSkin == NAME_None ? TEXT("(ninguno)") : *NewSkin.ToString());
			break;
		}

		case ETNCosmeticType::Helmet:
		{
			const FName NewHelmet = (TNPS->EquippedHelmetId == CosmeticId) ? NAME_None : CosmeticId;
			TNPS->EquippedHelmetId = NewHelmet;
			TNPS->ForceNetUpdate();

			if (TurtleChar) { TurtleChar->UpdateHelmetMesh(NewHelmet); }
			if (TNPC)       { TNPC->NotifyHelmetEquipped(NewHelmet); }

			UE_LOG(LogTortunabo, Log, TEXT("[CosmeticStatue] HELMET '%s' → '%s'"),
				*GetNameSafe(Interactor),
				NewHelmet == NAME_None ? TEXT("(ninguno)") : *NewHelmet.ToString());
			break;
		}
	}

	Super::Interact(Interactor);
}
