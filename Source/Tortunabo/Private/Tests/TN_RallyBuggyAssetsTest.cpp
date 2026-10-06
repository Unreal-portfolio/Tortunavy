// Modelo del buggy del Rally (#290): los assets de Art/Source/Vehicles/Buggy cargan, la carrocería tiene sus sockets y
// coinciden con las constantes de C++, y las ruedas Chaos tienen las medidas del neumático. Necesita el contenido:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Buggy.Assets; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Vehicles/TN_BuggyWheel.h"
#include "ChaosVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/PhysicsAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNBuggyAssetsTest
{
	const TCHAR* const ChassisPath = TEXT("/Game/Art/Source/Vehicles/Buggy/export/SK_TN_BuggyChassis.SK_TN_BuggyChassis");
	const TCHAR* const BodyPath = TEXT("/Game/Art/Source/Vehicles/Buggy/export/SM_TN_BuggyBody.SM_TN_BuggyBody");
	const TCHAR* const TirePath = TEXT("/Game/Art/Source/Vehicles/Buggy/export/SM_TN_BuggyTire.SM_TN_BuggyTire");
	const TCHAR* const SkinPaths[] = {
		TEXT("/Game/Art/Source/Vehicles/Buggy/export/MI_TN_Buggy_Mar.MI_TN_Buggy_Mar"),
		TEXT("/Game/Art/Source/Vehicles/Buggy/export/MI_TN_Buggy_Alga.MI_TN_Buggy_Alga"),
		TEXT("/Game/Art/Source/Vehicles/Buggy/export/MI_TN_Buggy_Medusa.MI_TN_Buggy_Medusa"),
	};
	const TCHAR* const OffroadPath = TEXT("/Game/Vehicles/OffroadCar/SKM_Offroad.SKM_Offroad");
	/** Hueso del cuerpo del chasis en el PhysicsAsset copiado del template. */
	const FName ChassisBodyBone(TEXT("OffroadCar"));
	/** Tolerancia de las cotas (cm): el manifest las redondea a centésimas. */
	constexpr double SocketToleranceCm = 0.5;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyBuggyAppliedInputsTest,
	"Tortunabo.Rally.Buggy.AppliedInputs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyBuggyAppliedInputsTest::RunTest(const FString& Parameters)
{
	// ATN_Buggy::GetApplied* leen por reflexión las entradas que aplica Chaos (protegidas): si el motor las renombra, la balsa de
	// Karts dejaría de remar en el servidor sin avisar (#710).
	const UClass* MoveClass = UChaosVehicleMovementComponent::StaticClass();
	for (const FName Name : { ATN_Buggy::AppliedSteeringProperty, ATN_Buggy::AppliedThrottleProperty, ATN_Buggy::AppliedBrakeProperty })
	{
		TestNotNull(*FString::Printf(TEXT("UChaosVehicleMovementComponent tiene la propiedad float %s"), *Name.ToString()),
			CastField<FFloatProperty>(MoveClass->FindPropertyByName(Name)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyBuggyAssetsTest,
	"Tortunabo.Rally.Buggy.Assets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyBuggyAssetsTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggyAssetsTest;
	const USkeletalMesh* Chassis = LoadObject<USkeletalMesh>(nullptr, ChassisPath);
	const UStaticMesh* Body = LoadObject<UStaticMesh>(nullptr, BodyPath);
	const UStaticMesh* Tire = LoadObject<UStaticMesh>(nullptr, TirePath);
	if (!TestNotNull(TEXT("SK_TN_BuggyChassis carga"), Chassis) || !TestNotNull(TEXT("SM_TN_BuggyBody carga"), Body)
		|| !TestNotNull(TEXT("SM_TN_BuggyTire carga"), Tire))
	{
		return false;
	}

	// Chasis: cuerpo físico en la raíz y un hueso por rueda, con la rueda apoyada en el suelo del origen.
	const UPhysicsAsset* Physics = Chassis->GetPhysicsAsset();
	TestNotNull(TEXT("el chasis tiene PhysicsAsset"), Physics);
	TestTrue(TEXT("el PhysicsAsset tiene el cuerpo del chasis"), Physics && Physics->FindBodyIndex(ChassisBodyBone) != INDEX_NONE);
	const FReferenceSkeleton& Ref = Chassis->GetRefSkeleton();
	const float WheelRadius = GetDefault<UTN_BuggyWheelFront>()->WheelRadius;
	for (const FName& Bone : ATN_Buggy::WheelBoneNames)
	{
		const int32 Index = Ref.FindBoneIndex(Bone);
		if (!TestTrue(FString::Printf(TEXT("el chasis tiene el hueso %s"), *Bone.ToString()), Index != INDEX_NONE))
		{
			continue;
		}
		const FVector Axle = Chassis->GetComposedRefPoseMatrix(Index).GetOrigin();
		TestEqual(FString::Printf(TEXT("%s: la rueda toca el suelo del origen"), *Bone.ToString()), Axle.Z - WheelRadius, 0.0, 1.5);
	}

	// Neumático: el radio y el ancho de las ruedas Chaos salen de su malla.
	const FBox TireBox = Tire->GetBoundingBox();
	TestEqual(TEXT("radio de la rueda = radio de SM_TN_BuggyTire"), static_cast<double>(WheelRadius), TireBox.GetExtent().Z, 0.2);
	TestEqual(TEXT("ancho de la rueda = ancho de SM_TN_BuggyTire"), static_cast<double>(GetDefault<UTN_BuggyWheelRear>()->WheelWidth),
		TireBox.GetSize().Y, 0.5);

	// Carrocería: sockets de asientos y boca, iguales a las constantes de C++ y coherentes con la torreta.
	const UStaticMeshSocket* Driver = Body->FindSocket(ATN_Buggy::DriverSeatSocket);
	const UStaticMeshSocket* Gunner = Body->FindSocket(ATN_Buggy::GunnerSeatSocket);
	const UStaticMeshSocket* Muzzle = Body->FindSocket(ATN_Buggy::MuzzleSocket);
	if (!TestNotNull(TEXT("socket Seat_Driver"), Driver) || !TestNotNull(TEXT("socket Seat_Gunner"), Gunner)
		|| !TestNotNull(TEXT("socket Muzzle_Gunner"), Muzzle))
	{
		return false;
	}
	TestTrue(TEXT("Seat_Driver = DriverSeatLocal"), Driver->RelativeLocation.Equals(ATN_Buggy::DriverSeatLocal, SocketToleranceCm));
	TestTrue(TEXT("Seat_Gunner = GunnerSeatLocal"), Gunner->RelativeLocation.Equals(ATN_Buggy::GunnerSeatLocal, SocketToleranceCm));
	TestTrue(TEXT("Muzzle_Gunner = MuzzleLocal"), Muzzle->RelativeLocation.Equals(ATN_Buggy::MuzzleLocal, SocketToleranceCm));
	const FVector MuzzleFromSeat = Muzzle->RelativeLocation - Gunner->RelativeLocation;
	TestEqual(TEXT("Muzzle_Gunner a la altura de MuzzleSocketAboveSeatCm"), MuzzleFromSeat.Z,
		static_cast<double>(UTN_BuggyTurretComponent::MuzzleSocketAboveSeatCm),
		SocketToleranceCm);
	TestEqual(TEXT("boca de la torreta apuntando al frente en Muzzle_Gunner"), MuzzleFromSeat.X,
		static_cast<double>(UTN_BuggyTurretComponent::MuzzleDistanceCm), SocketToleranceCm);
	TestTrue(TEXT("la conductora va delante de la artillera"), Driver->RelativeLocation.X > Gunner->RelativeLocation.X);

	// Skins: las tres cargan, con la pintura como parámetro y distinta entre sí.
	TArray<FLinearColor> Paints;
	for (const TCHAR* SkinPath : SkinPaths)
	{
		const UMaterialInterface* Skin = LoadObject<UMaterialInterface>(nullptr, SkinPath);
		FLinearColor Paint;
		if (TestNotNull(FString::Printf(TEXT("skin %s carga"), SkinPath), Skin)
			&& TestTrue(TEXT("la skin tiene PaintColor"), Skin->GetVectorParameterValue(FHashedMaterialParameterInfo(ATN_Buggy::TintParameterName), Paint)))
		{
			for (const FLinearColor& Other : Paints)
			{
				TestFalse(TEXT("cada skin tiene su pintura"), Other.Equals(Paint, 0.01f));
			}
			Paints.Add(Paint);
		}
	}

	// El buggy usa el modelo nuevo y ya no carga SKM_Offroad (8,9 MB) ni su AnimBP.
	const ATN_Buggy* Defaults = GetDefault<ATN_Buggy>();
	TestTrue(TEXT("el chasis del buggy es SK_TN_BuggyChassis"), Defaults->GetMesh()->GetSkeletalMeshAsset() == Chassis);
	TestNull(TEXT("el chasis no tiene AnimBP"), Defaults->GetMesh()->GetAnimClass());
	TestTrue(TEXT("la carrocería del buggy es SM_TN_BuggyBody"), Defaults->GetBody() && Defaults->GetBody()->GetStaticMesh() == Body);
	TestNull(TEXT("SKM_Offroad no está cargado"), FindObject<USkeletalMesh>(nullptr, OffroadPath));
	return true;
}

#endif
