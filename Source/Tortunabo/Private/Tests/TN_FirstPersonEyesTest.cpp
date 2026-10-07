// Ojos de la primera persona y del modo VR (#740, Player/TN_FirstPersonEyes.h): a la altura de los ojos de la tortuga, no
// en el cuello (el hueso Head de TotugaDemo_Rig está en la base del cuello), y por encima de la boca, de donde sale la lengua.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.VR.FirstPersonEyes; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Art/TN_TurtleArt.h"
#include "Engine/SkeletalMesh.h"
#include "Player/TN_FirstPersonEyes.h"
#include "Player/TortugaCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNFirstPersonEyesTest
{
	const TCHAR* const DemoMeshPath = TEXT("/Game/Meshses/Characters/Player/TotugaDemo_Rig.TotugaDemo_Rig");
	/** Ojos de TotugaDemo_Rig (Scripts/build_cosmetics.py): esferas de radio 3,98 con el centro a z = 46,06. */
	constexpr double EyeCenterZ = 46.06;
	constexpr double EyeRadius = 3.98;
	/** Donde nace la lengua dentro de la boca (TN_TurtleFaceComponent.cpp, TongueRootCenter). */
	const FVector TongueRoot(0.0, 12.8, 42.1);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNFirstPersonEyesTest,
	"Tortunabo.VR.FirstPersonEyes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNFirstPersonEyesTest::RunTest(const FString& Parameters)
{
	using namespace TNFirstPersonEyesTest;
	const USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, DemoMeshPath);
	if (!TestNotNull(TEXT("TotugaDemo_Rig carga"), Mesh)) { return false; }

	FTransform HeadRef;
	FVector Eyes;
	bool bFromSocket = true;
	if (!TestTrue(TEXT("Tiene el hueso Head"), TNFirstPersonEyes::EyesInRefPose(Mesh, HeadRef, Eyes, bFromSocket))) { return false; }
	TestFalse(TEXT("Sin socket de ojos: los de la malla de demo"), bFromSocket);

	// Escala y altura de la malla en el personaje (BP_TortugaCharacter: Z -70, x2,5).
	const FTransform& MeshInActor = TNTurtleArt::GetReferenceMeshTransform();
	const double Scale = MeshInActor.GetScale3D().Z;
	const double AboveHeadCm = (Eyes.Z - HeadRef.GetLocation().Z) * Scale;
	AddInfo(FString::Printf(TEXT("Hueso Head (malla): %s; ojos: %s; ojos sobre el hueso: %.1f cm"),
		*HeadRef.GetLocation().ToString(), *Eyes.ToString(), AboveHeadCm));

	// La altura de los ojos respecto del hueso de la cabeza: unos 20 cm por encima en la postura de referencia (antes, 6 cm:
	// la vista salía del cuello).
	TestTrue(FString::Printf(TEXT("Los ojos, 15-25 cm por encima del hueso Head (%.1f)"), AboveHeadCm), AboveHeadCm > 15.0 && AboveHeadCm < 25.0);
	TestTrue(FString::Printf(TEXT("A la altura de los ojos de la malla (z %.2f)"), Eyes.Z), FMath::Abs(Eyes.Z - EyeCenterZ) < EyeRadius * 0.5);
	TestTrue(TEXT("Entre los dos ojos (x = 0)"), FMath::IsNearlyZero(Eyes.X, 0.5));
	// La lengua nace por debajo y por delante: se ve abajo al mirar abajo, nunca por encima de la vista.
	TestTrue(FString::Printf(TEXT("Por encima de la boca: %.1f cm sobre la raíz de la lengua"), (Eyes.Z - TongueRoot.Z) * Scale),
		(Eyes.Z - TongueRoot.Z) * Scale > 8.0);
	const double TongueDistCm = (TongueRoot - Eyes).Size() * Scale;
	TestTrue(FString::Printf(TEXT("La raíz de la lengua, fuera del plano cercano de la cámara (%.1f cm > 12)"), TongueDistCm), TongueDistCm > 12.0);

	// Con gafas y de pie la vista va a una altura fija sobre la cápsula (VREyeOffset): la de los ojos de la malla.
	const ATortugaCharacter* Defaults = GetDefault<ATortugaCharacter>();
	const FStructProperty* OffsetProp = FindFProperty<FStructProperty>(ATortugaCharacter::StaticClass(), TEXT("VREyeOffset"));
	if (TestNotNull(TEXT("VREyeOffset existe"), OffsetProp))
	{
		const FVector HeadsetEyes = *OffsetProp->ContainerPtrToValuePtr<FVector>(Defaults);
		const double MeshEyesCm = MeshInActor.TransformPosition(Eyes).Z;
		TestTrue(FString::Printf(TEXT("Gafas: VREyeOffset.Z (%.1f) a ±5 cm de los ojos de la malla (%.1f)"), HeadsetEyes.Z, MeshEyesCm),
			FMath::Abs(HeadsetEyes.Z - MeshEyesCm) < 5.0);
		// Y delante (#918): con el cuello echado hacia delante, los ojos de la malla quedan entre 20 y 30 cm delante del centro
		// (a 21 cm en la postura de referencia), y la raíz de la lengua, 8-14 cm delante y 8-14 cm debajo del punto de vista.
		const double NeckRad = FMath::DegreesToRadians(static_cast<double>(TNVRGestures::NeckForwardDeg));
		const double HeadRad = FMath::DegreesToRadians(static_cast<double>(TNVRGestures::HeadLevelDeg));
		const FVector NeckRef = HeadRef.GetLocation() - FVector(0.0, 1.077, 2.718);
		auto Posed = [&](const FVector& Point)
		{
			auto RotX = [](const FVector& V, const FVector& Origin, double Rad)
			{
				const FVector Rel = V - Origin;
				return Origin + FVector(Rel.X, Rel.Y * FMath::Cos(Rad) - Rel.Z * FMath::Sin(Rad), Rel.Y * FMath::Sin(Rad) + Rel.Z * FMath::Cos(Rad));
			};
			const FVector HeadPosed = RotX(HeadRef.GetLocation(), NeckRef, NeckRad);
			return MeshInActor.TransformPosition(RotX(RotX(Point, NeckRef, NeckRad), HeadPosed, HeadRad));
		};
		const FVector PosedEyes = Posed(Eyes);
		const FVector PosedTongue = Posed(TongueRoot);
		TestTrue(FString::Printf(TEXT("Gafas: VREyeOffset.X (%.1f) a ±6 cm de los ojos con el cuello adelantado (%.1f)"), HeadsetEyes.X, PosedEyes.X),
			FMath::Abs(HeadsetEyes.X - PosedEyes.X) < 6.0);
		const double TongueForward = PosedTongue.X - HeadsetEyes.X;
		const double TongueBelow = HeadsetEyes.Z - PosedTongue.Z;
		TestTrue(FString::Printf(TEXT("Lengua %.1f cm delante del punto de vista (8-16)"), TongueForward), TongueForward > 8.0 && TongueForward < 16.0);
		TestTrue(FString::Printf(TEXT("Lengua %.1f cm debajo del punto de vista (6-14)"), TongueBelow), TongueBelow > 6.0 && TongueBelow < 14.0);
	}
	return true;
}

#endif
