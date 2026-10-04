#include "Core/TN_CosmeticLook.h"
#include "Art/TN_ArtSettings.h"
#include "Art/TN_TurtleArt.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Multiplayer/MP_GameInstance.h"
#include "ReferenceSkeleton.h"
#include "Vehicles/TN_BuggyCosmetics.h"

namespace TNCosmeticLookDetail
{
	const TCHAR* const BodyMaterialPath = TEXT("/Game/Cosmetics/Materials/M_TurtleBody.M_TurtleBody");
	const TCHAR* const HelmetSlotMaterialPath = TEXT("/Game/Cosmetics/Materials/M_TurtleHelmetSlot.M_TurtleHelmetSlot");

	/** Coronilla de TotugaDemo_Rig en su postura de referencia (espacio de la malla, antes del escalado del actor). */
	const FVector HeadTop(0.0, 5.5, 51.0);

	/** Verde del cuerpo de serie (difuso de M_TortugaDemo2) y crema de la barriga, en lineal. */
	const FLinearColor SerieGreen(0.017144f, 0.090625f, 0.000829f, 1.f);
	const FLinearColor SerieBelly(0.896f, 0.768f, 0.381f, 1.f);

	struct FTurtleSlots
	{
		bool bUnified = false;
		int32 HelmetSlot = INDEX_NONE;
		int32 BodySlot = INDEX_NONE;
	};

	/**
	 * Ranuras que pintan los cosméticos, por nombre (UTN_ArtSettings: «lambert2» y «lambert4» en la malla de demo). Una
	 * malla que no las tenga (la de Arte) se queda con sus materiales: nunca se pinta una ranura por su número.
	 */
	FTurtleSlots SlotsOf(const USkeletalMeshComponent* Body)
	{
		FTurtleSlots Slots;
		if (Body->GetNumMaterials() >= 5)
		{
			Slots.bUnified = true;
			return Slots;
		}
		const UTN_ArtSettings* Settings = GetDefault<UTN_ArtSettings>();
		auto Find = [Body](FName Name) { return Name.IsNone() ? INDEX_NONE : Body->GetMaterialIndex(Name); };
		Slots.HelmetSlot = Find(Settings->HelmetMaterialSlot);
		Slots.BodySlot = Find(Settings->BodyMaterialSlot);
		return Slots;
	}

	UMaterialInterface* LoadMaterial(const TCHAR* Path)
	{
		return LoadObject<UMaterialInterface>(nullptr, Path);
	}

	const UMP_GameInstance* GameInstanceOf(const UObject* Context)
	{
		return Context ? Cast<UMP_GameInstance>(UGameplayStatics::GetGameInstance(Context)) : nullptr;
	}
}

void UTN_CosmeticLook::AttachHelmet(USkeletalMeshComponent* Body, UStaticMeshComponent* Helmet, const FTN_HelmetData* Row)
{
	using namespace TNCosmeticLookDetail;
	if (!Body || !Helmet) { return; }
	static const FName SombreroSocket(TEXT("Sombrero"));
	static const FName HeadBone(TEXT("Head"));
	const FVector Offset = Row ? Row->MeshOffset : FVector::ZeroVector;
	const FRotator Turn = Row ? Row->MeshRotation : FRotator::ZeroRotator;
	const FVector Scale = (Row && !Row->MeshScale.IsNearlyZero()) ? Row->MeshScale : FVector::OneVector;

	if (Body->DoesSocketExist(SombreroSocket))
	{
		Helmet->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform, SombreroSocket);
		Helmet->SetRelativeTransform(FTransform(Turn, Offset, Scale));
		return;
	}

	const FTransform HelmetCS(Turn, HeadTop + Offset, Scale);
	const USkinnedAsset* Asset = Body->GetSkinnedAsset();
	const int32 BoneIndex = Asset ? Asset->GetRefSkeleton().FindBoneIndex(HeadBone) : INDEX_NONE;
	if (BoneIndex == INDEX_NONE)
	{
		Helmet->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform);
		Helmet->SetRelativeTransform(HelmetCS);
		return;
	}

	// Hueso de la cabeza en espacio de componente (postura de referencia): el casco queda fijo respecto a él.
	const FReferenceSkeleton& RefSkeleton = Asset->GetRefSkeleton();
	const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();
	FTransform BoneCS = FTransform::Identity;
	for (int32 Index = BoneIndex; Index != INDEX_NONE; Index = RefSkeleton.GetParentIndex(Index))
	{
		BoneCS = BoneCS * RefPose[Index];
	}
	Helmet->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform, HeadBone);
	Helmet->SetRelativeTransform(HelmetCS.GetRelativeTransform(BoneCS));
}

void UTN_CosmeticLook::ApplyLook(const UObject* WorldContext, USkeletalMeshComponent* Body, UStaticMeshComponent* Helmet, const FTN_TurtleLook& Look,
	TArray<TObjectPtr<UMaterialInterface>>& Defaults)
{
	using namespace TNCosmeticLookDetail;
	if (!Body) { return; }
	const UMP_GameInstance* GI = GameInstanceOf(WorldContext ? WorldContext : Body);

	if (Defaults.Num() == 0)
	{
		for (int32 i = 0; i < Body->GetNumMaterials(); ++i) { Defaults.Add(Body->GetMaterial(i)); }
	}
	for (int32 i = 0; i < Defaults.Num(); ++i) { Body->SetMaterial(i, Defaults[i]); }

	const FTN_HelmetData* HelmRow = (GI && Look.HelmetId != NAME_None) ? GI->FindHelmetRow(Look.HelmetId, TEXT("CosmeticLook")) : nullptr;
	const FTN_SkinData* SkinRow = (GI && Look.SkinId != NAME_None) ? GI->FindSkinRow(Look.SkinId, TEXT("CosmeticLook")) : nullptr;
	const FTN_SkinData* ShellRow = (GI && Look.ShellId != NAME_None) ? GI->FindSkinRow(Look.ShellId, TEXT("CosmeticLook")) : nullptr;
	const FTN_SkinData* EyesRow = (GI && Look.EyesId != NAME_None) ? GI->FindSkinRow(Look.EyesId, TEXT("CosmeticLook")) : nullptr;

	UStaticMesh* HelmMesh = HelmRow ? HelmRow->DisplayMesh.Get() : nullptr;
	if (Helmet)
	{
		Helmet->SetStaticMesh(HelmMesh);
		Helmet->SetHiddenInGame(HelmMesh == nullptr);
		if (HelmMesh) { AttachHelmet(Body, Helmet, HelmRow); }
	}

	// Piezas de Arte pegadas a los huesos (caparazón, casco de serie, ojos, lengua; TNTurtleArt): manda el casco de la tienda.
	const bool bArtHelmet = TNTurtleArt::ApplyPieces(Body, HelmMesh != nullptr);

	const FTurtleSlots Slots = SlotsOf(Body);
	if (Slots.bUnified)
	{
		auto Assign = [Body](int32 Slot, UMaterialInterface* Mat) { if (Mat) { Body->SetMaterial(Slot, Mat); } };
		if (SkinRow)
		{
			Assign(0, SkinRow->BellyMaterial);
			Assign(1, SkinRow->EyeShineMaterial);
			Assign(2, SkinRow->EyesMouthMaterial);
			Assign(3, SkinRow->SkinMaterial);
			Assign(4, SkinRow->ShellMaterial);
		}
		if (ShellRow) { Assign(4, ShellRow->ShellMaterial); }
		Body->MarkRenderStateDirty();
		return;
	}

	// Malla de demo: la ranura del casco pinta el casco de serie (o lo recorta si hay otro) y esconde siempre la lengua
	// rígida de la malla; el cuerpo se pinta con M_TurtleBody.
	if (Slots.HelmetSlot != INDEX_NONE)
	{
		UMaterialInterface* SlotMat = LoadMaterial(HelmetSlotMaterialPath);
		if (UMaterialInstanceDynamic* SlotMID = SlotMat ? Body->CreateDynamicMaterialInstance(Slots.HelmetSlot, SlotMat) : nullptr)
		{
			// El casco de serie de Arte (Turtle.Helmet) sustituye al pintado en la malla.
			SlotMID->SetScalarParameterValue(TEXT("HideHelmet"), (HelmMesh || bArtHelmet) ? 1.f : 0.f);
			SlotMID->SetScalarParameterValue(TEXT("HideTongue"), 1.f);
		}
	}
	// Siempre M_TurtleBody en el cuerpo (si existe): con el material original de la malla los ojos salen del color de
	// la piel.
	if (Slots.BodySlot != INDEX_NONE)
	{
		UMaterialInterface* BodyMat = LoadMaterial(BodyMaterialPath);
		if (UMaterialInstanceDynamic* MID = BodyMat ? Body->CreateDynamicMaterialInstance(Slots.BodySlot, BodyMat) : nullptr)
		{
			MID->SetVectorParameterValue(TEXT("BodyColor"), SkinRow ? SkinRow->Color : SerieGreen);
			MID->SetVectorParameterValue(TEXT("BellyColor"), SkinRow ? SkinRow->Color2 : SerieBelly);
			MID->SetScalarParameterValue(TEXT("BellyAmount"), SkinRow ? SkinRow->BellyAmount : 0.f);
			MID->SetScalarParameterValue(TEXT("ShellMatchBody"), ShellRow ? 0.f : 1.f);
			if (ShellRow)
			{
				MID->SetVectorParameterValue(TEXT("ShellColor"), ShellRow->Color);
				MID->SetVectorParameterValue(TEXT("ShellColor2"), ShellRow->Color2);
				MID->SetScalarParameterValue(TEXT("ShellPattern"), static_cast<float>(ShellRow->Pattern));
				MID->SetScalarParameterValue(TEXT("PatternScale"), ShellRow->PatternScale);
				MID->SetScalarParameterValue(TEXT("ShellShine"), ShellRow->Shine);
				MID->SetScalarParameterValue(TEXT("ShellGlow"), ShellRow->Glow);
			}
			else
			{
				MID->SetScalarParameterValue(TEXT("ShellPattern"), 0.f);
				MID->SetScalarParameterValue(TEXT("ShellShine"), 0.f);
				MID->SetScalarParameterValue(TEXT("ShellGlow"), 0.f);
			}
			// Ojos: los de serie son clásicos (blanco con pupila negra y brillo).
			MID->SetScalarParameterValue(TEXT("EyeStyle"), EyesRow ? static_cast<float>(EyesRow->EyeStyle) : 0.f);
			MID->SetVectorParameterValue(TEXT("EyeColor"), EyesRow ? EyesRow->Color : FLinearColor(0.004f, 0.006f, 0.015f, 1.f));
			MID->SetVectorParameterValue(TEXT("EyeColor2"), EyesRow ? EyesRow->Color2 : FLinearColor::White);
			MID->SetScalarParameterValue(TEXT("EyeGlow"), EyesRow ? EyesRow->Glow : 0.f);
			MID->SetScalarParameterValue(TEXT("EyeBlink"), 0.f);
			MID->SetScalarParameterValue(TEXT("EyeDizzy"), 0.f);
			// Cara de reposo (la de las vistas previas y el tendero): sonrisa abierta pequeña, sin cansancio.
			MID->SetScalarParameterValue(TEXT("EyeTired"), 0.f);
			MID->SetScalarParameterValue(TEXT("EyeSqueeze"), 0.f);
			MID->SetScalarParameterValue(TEXT("MouthOpen"), 0.3f);
			MID->SetScalarParameterValue(TEXT("MouthSmile"), 1.f);
			MID->SetScalarParameterValue(TEXT("FaceBlush"), 0.f);
		}
	}
	Body->MarkRenderStateDirty();
}

UMaterialInstanceDynamic* UTN_CosmeticLook::GetBodyMaterial(USkeletalMeshComponent* Body)
{
	using namespace TNCosmeticLookDetail;
	if (!Body) { return nullptr; }
	const FTurtleSlots Slots = SlotsOf(Body);
	if (Slots.bUnified || Slots.BodySlot == INDEX_NONE) { return nullptr; }
	return Cast<UMaterialInstanceDynamic>(Body->GetMaterial(Slots.BodySlot));
}

bool UTN_CosmeticLook::IsDemoTurtle(const USkeletalMeshComponent* Body)
{
	using namespace TNCosmeticLookDetail;
	if (!Body) { return false; }
	const FTurtleSlots Slots = SlotsOf(Body);
	return !Slots.bUnified && Slots.BodySlot != INDEX_NONE && Slots.HelmetSlot != INDEX_NONE;
}

void UTN_CosmeticLook::SetEyeState(USkeletalMeshComponent* Body, float Blink, float Dizzy)
{
	using namespace TNCosmeticLookDetail;
	if (!Body) { return; }
	const FTurtleSlots Slots = SlotsOf(Body);
	if (Slots.bUnified || Slots.BodySlot == INDEX_NONE) { return; }
	if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Body->GetMaterial(Slots.BodySlot)))
	{
		MID->SetScalarParameterValue(TEXT("EyeBlink"), FMath::Clamp(Blink, 0.f, 1.f));
		MID->SetScalarParameterValue(TEXT("EyeDizzy"), FMath::Clamp(Dizzy, 0.f, 1.f));
	}
}

FText UTN_CosmeticLook::GetDisplayName(const UObject* WorldContext, ETNCosmeticCategory Category, FName Id)
{
	using namespace TNCosmeticLookDetail;
	// Buggy: catálogo en C++ (NAME_None = el de serie, que también tiene nombre en el catálogo).
	if (Category == ETNCosmeticCategory::BuggyModel) { return TNBuggyCosmetics::ResolveModel(Id).Name; }
	if (Category == ETNCosmeticCategory::BuggyPaint) { return TNBuggyCosmetics::ResolvePaint(Id).Name; }
	if (Id == NAME_None)
	{
		switch (Category)
		{
		case ETNCosmeticCategory::Helmet: return NSLOCTEXT("Tortunabo", "SerieHelmet", "Casco de serie");
		case ETNCosmeticCategory::Shell:  return NSLOCTEXT("Tortunabo", "SerieShell", "Caparazón de serie");
		case ETNCosmeticCategory::Eyes:   return NSLOCTEXT("Tortunabo", "SerieEyes", "Ojos de serie");
		default:                          return NSLOCTEXT("Tortunabo", "SerieBody", "Verde de serie");
		}
	}
	const UMP_GameInstance* GI = GameInstanceOf(WorldContext);
	if (Category == ETNCosmeticCategory::Helmet)
	{
		const FTN_HelmetData* Row = GI ? GI->FindHelmetRow(Id, TEXT("CosmeticName")) : nullptr;
		// Sin nombre en la tabla: el identificador de la fila, tal cual (no es texto del juego).
		return Row && !Row->DisplayName.IsEmpty() ? Row->DisplayName : FText::AsCultureInvariant(Id.ToString());
	}
	const FTN_SkinData* Row = GI ? GI->FindSkinRow(Id, TEXT("CosmeticName")) : nullptr;
	// Sin nombre en la tabla: el identificador de la fila, tal cual (no es texto del juego).
	return Row && !Row->DisplayName.IsEmpty() ? Row->DisplayName : FText::AsCultureInvariant(Id.ToString());
}

FText UTN_CosmeticLook::GetDescription(const UObject* WorldContext, ETNCosmeticCategory Category, FName Id)
{
	using namespace TNCosmeticLookDetail;
	if (Category == ETNCosmeticCategory::BuggyModel) { return TNBuggyCosmetics::ResolveModel(Id).Description; }
	if (Category == ETNCosmeticCategory::BuggyPaint) { return TNBuggyCosmetics::ResolvePaint(Id).Description; }
	if (Id == NAME_None)
	{
		switch (Category)
		{
		case ETNCosmeticCategory::Helmet: return NSLOCTEXT("Tortunabo", "SerieHelmetDesc", "El casco rojo de siempre. Protege de los cocos.");
		case ETNCosmeticCategory::Shell:  return NSLOCTEXT("Tortunabo", "SerieShellDesc", "Tu caparazón original, a juego con tu color.");
		case ETNCosmeticCategory::Eyes:   return NSLOCTEXT("Tortunabo", "SerieEyesDesc", "Los de toda la vida: blancos, redondos y con su brillito.");
		default:                          return NSLOCTEXT("Tortunabo", "SerieBodyDesc", "Verde tortuga, el clásico que nunca falla.");
		}
	}
	const UMP_GameInstance* GI = GameInstanceOf(WorldContext);
	if (Category == ETNCosmeticCategory::Helmet)
	{
		const FTN_HelmetData* Row = GI ? GI->FindHelmetRow(Id, TEXT("CosmeticDesc")) : nullptr;
		return Row ? Row->Description : FText::GetEmpty();
	}
	const FTN_SkinData* Row = GI ? GI->FindSkinRow(Id, TEXT("CosmeticDesc")) : nullptr;
	return Row ? Row->Description : FText::GetEmpty();
}
