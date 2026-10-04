// Torreta del buggy construida en ejecución (#435): boca en su sitio y, al girar 360° con todo el cabeceo, sin meterse en
// el arco trasero, las barandillas, el arco de la conductora ni el respaldo de la artillera (cotas de build_buggy.py). La
// holgura con la artillera sentada (malla deformada) la mide TN.Rally.DebugTurretFit en el juego. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Turret; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Vehicles/TN_BuggyRiderAnimComponent.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "Vehicles/TN_RallyTurretLogic.h"
#include "../Vehicles/TN_BuggyTurretMesh.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNTurretMeshTest
{
	using TNProcMesh::FTNProcMeshBuffers;

	/** Delante del pivote, el arco de la conductora (DRIVER_HOOP_X de build_buggy.py respecto a la artillera). */
	constexpr double FrontBarX = 66.0;
	/** Base de los montantes del arco trasero (tapa de la carrocería trasera, GUNNER_FLOOR_Z). */
	constexpr double RearFloorZ = TNBuggyTurretMesh::SeatZ + 90.0 - 127.38;

	FTNProcMeshBuffers Build(void (*Builder)(FTNProcMeshBuffers&))
	{
		FTNProcMeshBuffers Mesh;
		Builder(Mesh);
		return Mesh;
	}

	/** Puntos de la superficie de la malla, cada 2 cm como mucho (las caras grandes también cuentan, no solo sus esquinas). */
	TArray<FVector> Samples(const FTNProcMeshBuffers& Mesh)
	{
		TArray<FVector> Out;
		for (int32 Tri = 0; Tri + 2 < Mesh.Tris.Num(); Tri += 3)
		{
			const FVector& A = Mesh.Verts[Mesh.Tris[Tri]];
			const FVector& B = Mesh.Verts[Mesh.Tris[Tri + 1]];
			const FVector& C = Mesh.Verts[Mesh.Tris[Tri + 2]];
			const int32 Steps = FMath::Clamp(FMath::CeilToInt(FMath::Max3(FVector::Dist(A, B), FVector::Dist(B, C), FVector::Dist(C, A)) / 2.0), 1, 64);
			for (int32 I = 0; I <= Steps; ++I)
			{
				for (int32 J = 0; I + J <= Steps; ++J)
				{
					Out.Add(A + (B - A) * (static_cast<double>(I) / Steps) + (C - A) * (static_cast<double>(J) / Steps));
				}
			}
		}
		return Out;
	}

	/** Distancia mínima (cm) de los puntos a los tubos de la carrocería alrededor de la artillera, menos su radio. */
	double CageClearance(const TArray<FVector>& Points)
	{
		using namespace TNBuggyTurretMesh;
		struct FBar { FVector A; FVector B; };
		const FBar Bars[] = {
			{ FVector(RearBarX, -RailY, RearBarZ), FVector(RearBarX, RailY, RearBarZ) },
			{ FVector(RearBarX, -RailY, RearFloorZ), FVector(RearBarX, -RailY, RearBarZ) },
			{ FVector(RearBarX, RailY, RearFloorZ), FVector(RearBarX, RailY, RearBarZ) },
			{ FVector(RearBarX, -RailY, RailZ), FVector(FrontBarX, -RailY, RailZ) },
			{ FVector(RearBarX, RailY, RailZ), FVector(FrontBarX, RailY, RailZ) },
			{ FVector(FrontBarX, -RailY, RailZ), FVector(FrontBarX, RailY, RailZ) } };
		double Closest = 1.0e9;
		for (const FVector& P : Points)
		{
			for (const FBar& Bar : Bars)
			{
				Closest = FMath::Min(Closest, FMath::PointDistToSegment(P, Bar.A, Bar.B) - BarRadius);
			}
		}
		return Closest;
	}

	/** Puntos dentro del respaldo de la artillera (detrás de ella, hasta BackrestRadius del eje y BackrestTopZ). */
	int32 CountInBackrest(const TArray<FVector>& Points)
	{
		using namespace TNBuggyTurretMesh;
		int32 Inside = 0;
		for (const FVector& P : Points)
		{
			Inside += P.X < -15.0 && FMath::Abs(P.Y) < 25.0 && FVector2D(P.X, P.Y).Size() < BackrestRadius && P.Z < BackrestTopZ ? 1 : 0;
		}
		return Inside;
	}

	TArray<FVector> Rotated(const TArray<FVector>& Points, const FRotator& Rotation)
	{
		TArray<FVector> Out;
		Out.Reserve(Points.Num());
		for (const FVector& P : Points)
		{
			Out.Add(Rotation.RotateVector(P));
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretMuzzleTest,
	"Tortunabo.Rally.Turret.Muzzle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretMuzzleTest::RunTest(const FString& Parameters)
{
	using namespace TNTurretMeshTest;
	constexpr float Forward = UTN_BuggyTurretComponent::MuzzleDistanceCm;
	constexpr float Side = UTN_BuggyTurretComponent::MuzzleSideCm;

	// La boca de la caña: lo más adelantado está a MuzzleDistanceCm, centrado en (MuzzleSideCm, 0).
	const FTNProcMeshBuffers Barrel = Build(&TNBuggyTurretMesh::BuildBarrel);
	TestFalse(TEXT("la caña tiene malla"), Barrel.IsEmpty());
	double MaxX = -1.0e9;
	for (const FVector& V : Barrel.Verts)
	{
		MaxX = FMath::Max(MaxX, V.X);
	}
	FVector MouthCenter = FVector::ZeroVector;
	int32 MouthCount = 0;
	for (const FVector& V : Barrel.Verts)
	{
		if (V.X >= MaxX - 0.01)
		{
			MouthCenter += V;
			++MouthCount;
		}
	}
	MouthCenter /= static_cast<double>(FMath::Max(1, MouthCount));
	TestTrue(FString::Printf(TEXT("la boca está a MuzzleDistanceCm (%.2f)"), MaxX), FMath::IsNearlyEqual(MaxX, static_cast<double>(Forward), 0.01));
	TestTrue(TEXT("la boca está centrada a MuzzleSideCm a la derecha y a la altura del pivote"),
		FMath::IsNearlyEqual(MouthCenter.Y, static_cast<double>(Side), 0.1) && FMath::Abs(MouthCenter.Z) < 0.1);

	// El proyectil sale de la boca visible: la caña girada con el apuntado acaba donde dice MuzzleWorldLocation.
	const FVector Pivot(100.0, -50.0, 30.0);
	const FRotator BuggyRotation(0.f, 90.f, 0.f);
	TestTrue(TEXT("buggy girado 90° y apuntado al frente: boca delante y a la derecha del buggy"),
		TNRallyTurret::MuzzleWorldLocation(Pivot, BuggyRotation, FRotator::ZeroRotator, Forward, Side)
			.Equals(Pivot + FVector(-Side, Forward, 0.0), 0.01));
	for (const FRotator& Aim : { FRotator(30.f, 0.f, 0.f), FRotator(-10.f, 135.f, 0.f), FRotator(45.f, -170.f, 0.f) })
	{
		const FVector Expected = Pivot + (BuggyRotation.Quaternion() * Aim.Quaternion()).RotateVector(MouthCenter);
		const FVector Muzzle = TNRallyTurret::MuzzleWorldLocation(Pivot, BuggyRotation, Aim, Forward, Side);
		TestTrue(FString::Printf(TEXT("apuntado %s: la boca visible y la salida del proyectil coinciden"), *Aim.ToString()),
			Muzzle.Equals(Expected, 0.1));
		const FVector Dir = TNRallyTurret::AimWorldDirection(BuggyRotation, Aim);
		TestTrue(TEXT("la boca está a MuzzleDistanceCm por delante del pivote en la dirección del disparo"),
			FMath::IsNearlyEqual((Muzzle - Pivot) | Dir, static_cast<double>(Forward), 0.01));
	}
	// Caso negativo: un cabeceo fuera de rango se limita también para la boca (no sale de un cañón imposible).
	TestTrue(TEXT("cabeceo de 80° se limita a 45° también para la boca"),
		TNRallyTurret::MuzzleWorldLocation(FVector::ZeroVector, FRotator::ZeroRotator, FRotator(80.f, 0.f, 0.f), Forward, Side)
			.Equals(TNRallyTurret::MuzzleWorldLocation(FVector::ZeroVector, FRotator::ZeroRotator, FRotator(45.f, 0.f, 0.f), Forward, Side), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretMeshClearanceTest,
	"Tortunabo.Rally.Turret.MeshClearance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretMeshClearanceTest::RunTest(const FString& Parameters)
{
	using namespace TNTurretMeshTest;
	using namespace TNBuggyTurretMesh;
	const FTNProcMeshBuffers Ring = Build(&BuildRing);
	const FTNProcMeshBuffers Mount = Build(&BuildMount);
	const FTNProcMeshBuffers Gun = Build(&BuildGun);
	const FTNProcMeshBuffers Barrel = Build(&BuildBarrel);
	for (const FTNProcMeshBuffers* Mesh : { &Ring, &Mount, &Gun, &Barrel })
	{
		TestFalse(TEXT("cada pieza tiene malla"), Mesh->IsEmpty());
		TestEqual(TEXT("buffers coherentes"), Mesh->Normals.Num(), Mesh->Verts.Num());
	}

	// El aro va por fuera del respaldo y por dentro de las barandillas, sin tocarlas; sus patas llegan al suelo.
	TestTrue(TEXT("el aro rodea el respaldo"), RingRadius - RingTube > BackrestRadius);
	TestTrue(TEXT("el aro cabe dentro de las barandillas"), RingRadius + RingTube < RailY - BarRadius);
	double RingLowest = 0.0;
	for (const FVector& V : Ring.Verts)
	{
		RingLowest = FMath::Min(RingLowest, V.Z);
	}
	TestTrue(TEXT("las patas del aro llegan al suelo de la carrocería trasera"), RingLowest <= FloorZ + 0.01);
	TestTrue(TEXT("el aro no toca las barandillas"), CageClearance(Samples(Ring)) > 0.5);

	// Al girar 360° con todo el cabeceo, ni el cañón ni la caña tocan un tubo de la carrocería (el travesaño del arco
	// trasero queda 42 cm detrás del pivote: con el pivote a la altura de Muzzle_Gunner lo atravesaba apuntando atrás).
	const TArray<FVector> GunPoints = Samples(Gun);
	const TArray<FVector> BarrelPoints = Samples(Barrel);
	const TArray<FVector> MountPoints = Samples(Mount);
	double GunClearance = 1.0e9;
	FRotator WorstAim = FRotator::ZeroRotator;
	int32 MountInBackrest = 0;
	double MountClearance = 1.0e9;
	for (int32 Yaw = -180; Yaw < 180; Yaw += 5)
	{
		for (float Pitch = TNRallyTurret::MinPitchDeg; Pitch <= TNRallyTurret::MaxPitchDeg + 0.1f; Pitch += 5.f)
		{
			const FRotator Aim(Pitch, static_cast<float>(Yaw), 0.f);
			const double Clearance = FMath::Min(CageClearance(Rotated(GunPoints, Aim)), CageClearance(Rotated(BarrelPoints, Aim)));
			if (Clearance < GunClearance)
			{
				GunClearance = Clearance;
				WorstAim = Aim;
			}
		}
		const TArray<FVector> MountAtYaw = Rotated(MountPoints, FRotator(0.f, static_cast<float>(Yaw), 0.f));
		MountClearance = FMath::Min(MountClearance, CageClearance(MountAtYaw));
		MountInBackrest += CountInBackrest(MountAtYaw);
	}
	TestTrue(FString::Printf(TEXT("el cañón no toca la carrocería en ningún apuntado (holgura %.1f cm con %s)"), GunClearance,
		*WorstAim.ToString()), GunClearance > 0.5);
	TestTrue(FString::Printf(TEXT("el carro y su poste no tocan la carrocería al girar (holgura %.1f cm)"), MountClearance),
		MountClearance > 0.5);
	TestEqual(TEXT("el carro no se mete en el respaldo al girar"), MountInBackrest, 0);
	TestEqual(TEXT("el aro no se mete en el respaldo"), CountInBackrest(Samples(Ring)), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyTurretPostBehindHeadTest,
	"Tortunabo.Rally.Turret.PostBehindHead",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyTurretPostBehindHeadTest::RunTest(const FString& Parameters)
{
	// El poste del carro gira con el apuntado (MountAngleDeg detrás de él) y la cabeza de la artillera lo sigue hasta
	// MaxYawDeg. Con el tope en 110° el poste le quedaba delante de la cara apuntando atrás (96° de separación a 180°, roce
	// a -150°/45° medido con TN.Rally.DebugTurretFit, #435): en toda la vuelta el poste queda a 130° o más de su cara.
	const FTNGunnerAimTuning Tuning;
	float Closest = 360.f;
	int32 WorstYaw = 0;
	for (int32 Yaw = -180; Yaw <= 180; Yaw += 5)
	{
		const float HeadYaw = TNRiderAnim::GunnerAimTargets(static_cast<float>(Yaw), 45.f, false, Tuning).YawDeg;
		const float PostYaw = static_cast<float>(Yaw + TNBuggyTurretMesh::MountAngleDeg);
		const float Separation = FMath::Abs(FMath::FindDeltaAngleDegrees(HeadYaw, PostYaw));
		if (Separation < Closest)
		{
			Closest = Separation;
			WorstYaw = Yaw;
		}
	}
	TestTrue(FString::Printf(TEXT("el poste queda detrás de la cabeza en toda la vuelta (%.0f° con el apuntado a %d°)"), Closest, WorstYaw),
		Closest >= 130.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
