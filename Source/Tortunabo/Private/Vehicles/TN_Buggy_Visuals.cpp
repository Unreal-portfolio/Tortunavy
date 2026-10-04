// ATN_Buggy: modelo de Art/Source/Vehicles/Buggy (#290). Mallas de Rally|Assets, skin del equipo y neumáticos movidos
// con el estado de cada rueda Chaos (lo que hacía el AnimBP de SKM_Offroad). Todo cosmético salvo la malla de física.

#include "Vehicles/TN_Buggy.h"
#include "TN_BuggyTurretMesh.h"
#include "Vehicles/TN_BuggyLookComponent.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "World/ProcMap/TN_ProcMapRuntimeMesh.h"
#include "UObject/Package.h"
#include "ChaosVehicleWheel.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace TNBuggyVisuals
{
	/** Giro de los neumáticos derechos: la cara exterior mira afuera. */
	const FQuat RightTireFlip(FRotator(0.f, 180.f, 0.f));

	/** Material de color de vértice de las mallas en ejecución y el de las formas básicas (con el parámetro Color del tinte). */
	const TCHAR* const VertexColorMaterialPath = TEXT("/Game/Cosmetics/Materials/M_CosmeticVertexColor.M_CosmeticVertexColor");
	const TCHAR* const VertexColorFallbackPath = TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial");
	const TCHAR* const TintableMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

	using FTurretBuilder = void (*)(TNProcMesh::FTNProcMeshBuffers&);

	/** Pone en Part la malla que construye Build (sin colisión). */
	void SetTurretMesh(UStaticMeshComponent* Part, FTurretBuilder Build, UMaterialInterface* Material)
	{
		if (!Part || !Material)
		{
			return;
		}
		TNProcMesh::FTNProcMeshBuffers Buffers;
		Build(Buffers);
		Part->SetStaticMesh(TNProcRuntimeMesh::MakeStaticMesh(GetTransientPackage(), Buffers, Material));
	}

	/** Neumático en el eje de su rueda: dirección (guiñada), rodadura (cabeceo) y, en los derechos, media vuelta. */
	FTransform TireTransform(const FVector& RestLocal, const FVector& SuspensionOffset, float SteerDeg, float SpinDeg, bool bRight)
	{
		const FQuat Steer(FVector::UpVector, FMath::DegreesToRadians(SteerDeg));
		const FQuat Spin(FVector::RightVector, FMath::DegreesToRadians(-SpinDeg));
		const FQuat Rotation = Steer * Spin * (bRight ? RightTireFlip : FQuat::Identity);
		return FTransform(Rotation.GetNormalized(), RestLocal + SuspensionOffset);
	}
}

FVector ATN_Buggy::GetBodySocketLocal(FName Socket, const FVector& Fallback) const
{
	const UStaticMesh* BodyMesh = Body ? Body->GetStaticMesh() : nullptr;
	const UStaticMeshSocket* Found = BodyMesh ? BodyMesh->FindSocket(Socket) : nullptr;
	if (!Found)
	{
		return Fallback;
	}
	// La carrocería va en el origen del chasis: su espacio es el del chasis (con su escala, que es 1).
	return Body->GetRelativeTransform().TransformPosition(Found->RelativeLocation);
}

void ATN_Buggy::ApplyModelAssets()
{
	// Un hijo en Blueprint puede cambiar las rutas: se cargan si no coinciden con las del constructor.
	USkeletalMeshComponent* Chassis = GetMesh();
	if (USkeletalMesh* Wanted = ChassisMeshAsset.LoadSynchronous(); Wanted && Chassis->GetSkeletalMeshAsset() != Wanted)
	{
		Chassis->SetSkeletalMesh(Wanted);
	}
	if (UStaticMesh* WantedBody = BodyMeshAsset.LoadSynchronous(); WantedBody && Body->GetStaticMesh() != WantedBody)
	{
		Body->SetStaticMesh(WantedBody);
	}
	UStaticMesh* WantedTire = TireMeshAsset.LoadSynchronous();
	TireRestLocal.SetNum(Tires.Num());
	for (int32 Index = 0; Index < Tires.Num(); ++Index)
	{
		UStaticMeshComponent* Tire = Tires[Index];
		if (!Tire)
		{
			continue;
		}
		if (WantedTire && Tire->GetStaticMesh() != WantedTire)
		{
			Tire->SetStaticMesh(WantedTire);
		}
		// Eje de la rueda: el hueso de SK_TN_BuggyChassis donde Chaos la coloca (sin hueso, donde la puso el constructor).
		const FName Bone = Index < UE_ARRAY_COUNT(WheelBoneNames) ? WheelBoneNames[Index] : NAME_None;
		const bool bHasBone = !Bone.IsNone() && Chassis->GetBoneIndex(Bone) != INDEX_NONE;
		TireRestLocal[Index] = bHasBone ? Chassis->GetSocketTransform(Bone, RTS_Component).GetLocation() : Tire->GetRelativeLocation();
	}
	AppliedSkinIndex = INDEX_NONE;
	TintMaterial = nullptr;
}

void ATN_Buggy::ApplyTint()
{
	if (GetNetMode() == NM_DedicatedServer || SkinMaterials.IsEmpty())
	{
		return;
	}
	// Con un modelo o una pintura de la tienda manda el aspecto de la conductora (TN_Buggy_Look.cpp), que lleva el color
	// del equipo en el banderín de la antena.
	if (!UsesTeamSkin())
	{
		RefreshBuggyLook(false);
		return;
	}
	const int32 SkinIndex = TeamIndex >= 0 ? TeamIndex % SkinMaterials.Num() : 0;
	if (!TintMaterial || SkinIndex != AppliedSkinIndex)
	{
		UMaterialInterface* Skin = SkinMaterials[SkinIndex].LoadSynchronous();
		if (!Skin)
		{
			UE_LOG(LogTNBuggy, Warning, TEXT("%s: falta la skin %s"), *GetName(), *SkinMaterials[SkinIndex].ToString());
			return;
		}
		// Un solo material dinámico para la carrocería y los neumáticos: comparten M_TN_Buggy (la zona de ruedas incluida).
		TintMaterial = UMaterialInstanceDynamic::Create(Skin, this);
		Body->SetMaterial(0, TintMaterial);
		for (UStaticMeshComponent* Tire : Tires)
		{
			if (Tire)
			{
				Tire->SetMaterial(0, TintMaterial);
			}
		}
		AppliedSkinIndex = SkinIndex;
	}
	if (bPaintWithTeamColor && TeamIndex >= 0)
	{
		TintMaterial->SetVectorParameterValue(TintParameterName, TNBuggy::TeamColor(TeamIndex));
	}
}

void ATN_Buggy::UpdateWheelVisuals()
{
	UChaosWheeledVehicleMovementComponent* Move = GetWheeledMovement();
	// Los getters de la rueda leen la salida de la simulación: sin ella, los neumáticos se quedan en reposo. Con una
	// carrocería de tortuga, la de serie va oculta: basta con que se vea algún neumático.
	bool bSeen = Body && Body->IsVisible() && Body->WasRecentlyRendered(0.2f);
	for (int32 Index = 0; Index < Tires.Num() && !bSeen; ++Index)
	{
		bSeen = Tires[Index] && Tires[Index]->WasRecentlyRendered(0.2f);
	}
	if (!Move || !Move->HasValidPhysicsState() || !Move->PhysicsVehicleOutput() || !bSeen)
	{
		return;
	}
	const int32 Count = FMath::Min3(Tires.Num(), TireRestLocal.Num(),
		FMath::Min(Move->Wheels.Num(), Move->PhysicsVehicleOutput()->Wheels.Num()));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const UChaosVehicleWheel* Wheel = Move->Wheels[Index];
		UStaticMeshComponent* Tire = Tires[Index];
		if (!Wheel || !Tire)
		{
			continue;
		}
		// Como FVehicleAnimationInstanceProxy (el nodo WheelController del AnimBP): baja por el eje de la suspensión.
		const FVector Suspension = -Wheel->GetSuspensionAxis() * Wheel->GetSuspensionOffset();
		Tire->SetRelativeTransform(TNBuggyVisuals::TireTransform(TireRestLocal[Index], Suspension, Wheel->GetSteerAngle(),
			Wheel->GetRotationAngle(), Index % 2 == 1));
	}
}

void ATN_Buggy::BuildTurretVisuals()
{
	if (Turret)
	{
		Turret->SetYawFollower(TurretMount);
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	using namespace TNBuggyVisuals;
	UMaterialInterface* VertexColor = LoadObject<UMaterialInterface>(nullptr, VertexColorMaterialPath, nullptr, LOAD_NoWarn);
	VertexColor = VertexColor ? VertexColor : LoadObject<UMaterialInterface>(nullptr, VertexColorFallbackPath);
	UMaterialInterface* Tintable = LoadObject<UMaterialInterface>(nullptr, TintableMaterialPath);
	if (!VertexColor || !Tintable)
	{
		UE_LOG(LogTNBuggy, Warning, TEXT("%s: sin materiales para la torreta"), *GetName());
	}
	SetTurretMesh(TurretRing, &TNBuggyTurretMesh::BuildRing, VertexColor);
	SetTurretMesh(TurretMount, &TNBuggyTurretMesh::BuildMount, VertexColor);
	SetTurretMesh(TurretGun, &TNBuggyTurretMesh::BuildGun, VertexColor);
	SetTurretMesh(TurretBarrel, &TNBuggyTurretMesh::BuildBarrel, Tintable);
	// La caña ya tiene malla y material: toma el color de la munición seleccionada.
	if (Turret)
	{
		Turret->RefreshSelectedLook();
	}
}
