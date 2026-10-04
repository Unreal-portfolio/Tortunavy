// Carrocerías de tortuga del buggy del Rally (#297), generadas en C++ (TN_BuggyArt): cada pieza de cada modelo tiene
// geometría, nombre estable, códigos de zona válidos para M_BuggyPaint y medidas que casan con el buggy de serie de
// Art/Source: las ruedas de Chaos, las dos tortugas sentadas en sus sockets y la torreta (TNBuggyTurretMesh) con sus
// barandillas. Sin mundo ni assets:
//   UnrealEditor-Win64-DebugGame-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Buggy.Art; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyCosmetics.h"
#include "Vehicles/TN_BuggyTurretComponent.h"
#include "../Vehicles/TN_BuggyArt.h"
#include "../Vehicles/TN_BuggyTurretMesh.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNBuggyArtTest
{
	const ETNBuggyBodyStyle Styles[] = { ETNBuggyBodyStyle::Classic, ETNBuggyBodyStyle::Offroad, ETNBuggyBodyStyle::Racer };

	/** Caja alineada con los ejes por mínimos y máximos (cm, chasis). */
	bool Inside(const FVector& V, const FVector& Min, const FVector& Max)
	{
		return V.X > Min.X && V.X < Max.X && V.Y > Min.Y && V.Y < Max.Y && V.Z > Min.Z && V.Z < Max.Z;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBuggyArtPiecesTest,
	"Tortunabo.Rally.Buggy.Art.Pieces",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBuggyArtPiecesTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggyArt;
	using TNBuggyArtTest::Inside;
	namespace F = TNBuggyArt::Frame;
	TestEqual(TEXT("Cadera de la conductora en el socket de ATN_Buggy"), F::DriverHip, FVector(ATN_Buggy::DriverSeatLocal));
	TestEqual(TEXT("Cadera de la artillera en el socket de ATN_Buggy"), F::GunnerHip, FVector(ATN_Buggy::GunnerSeatLocal));
	const double PivotZ = ATN_Buggy::GunnerSeatLocal.Z + UTN_BuggyTurretComponent::PivotAboveSeatCm;
	TestEqual(TEXT("Pivote de la torreta"), F::TurretPivotZ, PivotZ, 0.01);
	TestEqual(TEXT("Barandillas a la altura del aro de la torreta"), F::RailZ, PivotZ + TNBuggyTurretMesh::RailZ, 0.01);
	TestEqual(TEXT("Barandillas al ancho del aro de la torreta"), F::RailY, TNBuggyTurretMesh::RailY, 0.01);
	TestTrue(TEXT("El aro y el carro caben en lo que se deja libre"),
		TNBuggyTurretMesh::RingRadius + TNBuggyTurretMesh::RingTube + 2.5 < F::TurretSweepRadius
		&& PivotZ + TNBuggyTurretMesh::RingZ - TNBuggyTurretMesh::RingTube - 2.0 > F::TurretRingBottomZ);
	TestTrue(TEXT("El respaldo de la artillera, por dentro del poste del carro"),
		PivotZ + TNBuggyTurretMesh::BackrestTopZ < F::GunnerBackrestTopZ + 0.5);

	const FVector2D Axis(F::GunnerHip.X, F::GunnerHip.Y);
	TSet<FName> Names;
	for (const ETNBuggyBodyStyle Style : TNBuggyArtTest::Styles)
	{
		int32 BodyTris = 0;
		for (int32 p = 0; p < static_cast<int32>(EPiece::Count); ++p)
		{
			const EPiece Piece = static_cast<EPiece>(p);
			const FName Name = PieceName(Style, Piece);
			const FString Who = Name.ToString();
			TestTrue(Who + TEXT(": nombre estable Rally.Buggy.<Pieza>.<Modelo>"), Who.StartsWith(TEXT("Rally.Buggy.")));
			TestFalse(Who + TEXT(": nombre repetido"), Names.Contains(Name));
			Names.Add(Name);
			const TNProcMesh::FTNProcMeshBuffers B = BuildPiece(Style, Piece);
			TestFalse(Who + TEXT(": tiene geometría"), B.IsEmpty());
			const int32 N = B.Verts.Num();
			TestTrue(Who + TEXT(": buffers del mismo tamaño"), B.Normals.Num() == N && B.Colors.Num() == N && B.UVs.Num() == N);
			TestEqual(Who + TEXT(": triángulos completos"), B.Tris.Num() % 3, 0);
			bool bIndices = true;
			for (const int32 T : B.Tris) { bIndices &= T >= 0 && T < N; }
			TestTrue(Who + TEXT(": índices dentro"), bIndices);
			bool bZones = true;
			for (const FLinearColor& C : B.Colors)
			{
				// Octavos: 0 mate, 2 metal, 4 luz, 6 equipo, 7 pintura sin dibujo, 8 pintura.
				const float Code = C.A * 8.f;
				const int32 Rounded = FMath::RoundToInt(Code);
				bZones &= FMath::Abs(Code - Rounded) < 0.02f && (Rounded == 0 || Rounded == 2 || Rounded == 4 || Rounded == 6 || Rounded == 7 || Rounded == 8);
				bZones &= C.R >= 0.f && C.R <= 1.f && C.G >= 0.f && C.G <= 1.f && C.B >= 0.f && C.B <= 1.f;
			}
			TestTrue(Who + TEXT(": códigos de zona de M_BuggyPaint"), bZones);
			const bool bBody = p < NumBodyPieces;
			if (bBody) { BodyTris += B.Tris.Num() / 3; }
			FBox Box(ForceInit);
			int32 InTurret = 0;
			int32 InDriver = 0;
			int32 InGunner = 0;
			for (const FVector& V : B.Verts)
			{
				Box += V;
				if (!bBody) { continue; }
				// Torreta: el aro y el carro barren un anillo alrededor del eje; por encima del respaldo, el cañón todo el círculo.
				const double R = FVector2D::Distance(FVector2D(V.X, V.Y), Axis);
				if (R < F::TurretSweepRadius && (V.Z > F::GunnerBackrestTopZ || (R > 39.0 && V.Z > F::TurretRingBottomZ))) { ++InTurret; }
				// Conductora sentada: tronco y cabeza sobre el cojín, y piernas del borde del cojín a los pies.
				if (Inside(V, FVector(4.0, -16.0, 96.0), FVector(40.0, 16.0, 162.0)) || Inside(V, FVector(46.0, -14.0, 58.0), FVector(76.0, 14.0, 92.0))) { ++InDriver; }
				// Artillera sentada: tronco y cabeza sobre su sillín, y piernas hacia el reposapiés del hueco.
				if (Inside(V, FVector(-98.0, -16.0, 116.0), FVector(-62.0, 16.0, 198.0)) || Inside(V, FVector(-50.0, -14.0, 92.0), FVector(-28.0, 14.0, 126.0)))
				{
					++InGunner;
				}
			}
			if (bBody)
			{
				TestEqual(Who + TEXT(": deja libre la torreta"), InTurret, 0);
				TestEqual(Who + TEXT(": deja sitio a la conductora"), InDriver, 0);
				TestEqual(Who + TEXT(": deja sitio a la artillera"), InGunner, 0);
				TestTrue(Who + TEXT(": dentro del largo del buggy"), Box.Min.X > -265.0 && Box.Max.X < 265.0);
				TestTrue(Who + TEXT(": dentro del ancho"), Box.Min.Y > -178.0 && Box.Max.Y < 178.0);
				TestTrue(Who + TEXT(": por encima del suelo"), Box.Min.Z > 25.0);
				TestTrue(Who + TEXT(": por debajo de la artillera"), Box.Max.Z < 175.0);
			}
			else if (Piece == EPiece::Wheel)
			{
				const double Radius = FMath::Max(FMath::Max(FMath::Abs(Box.Min.X), Box.Max.X), FMath::Max(FMath::Abs(Box.Min.Z), Box.Max.Z));
				TestTrue(Who + TEXT(": radio de la rueda de Chaos"), Radius > F::WheelRadius - 1.0 && Radius < F::WheelRadius + 1.5);
				TestTrue(Who + TEXT(": ancho de la rueda de Chaos"), Box.Max.Y - Box.Min.Y < F::WheelWidth + 1.0);
				// El buje cromado (zona metal) va en la cara de fuera.
				double HubY = 0.0;
				int32 HubVerts = 0;
				for (int32 i = 0; i < N; ++i)
				{
					if (FMath::RoundToInt(B.Colors[i].A * 8.f) == 2) { HubY += B.Verts[i].Y; ++HubVerts; }
				}
				TestTrue(Who + TEXT(": la llanta mira a -Y, como SM_TN_BuggyTire"), HubVerts > 0 && HubY / HubVerts < -5.0);
				TestTrue(Who + TEXT(": presupuesto de la rueda"), B.Tris.Num() / 3 < 2500);
			}
		}
		TestTrue(FString::Printf(TEXT("Carrocería %d: presupuesto de triángulos"), static_cast<int32>(Style)), BodyTris > 1500 && BodyTris < 16000);
		const FVector Mount = AntennaMount(Style);
		TestTrue(FString::Printf(TEXT("Carrocería %d: la antena lejos de la torreta"), static_cast<int32>(Style)),
			FVector2D::Distance(FVector2D(Mount.X, Mount.Y), Axis) > F::TurretSweepRadius + 40.0);
		const FVector Exhaust = ExhaustLocal(Style, FVector::ZeroVector);
		TestTrue(FString::Printf(TEXT("Carrocería %d: el turbo sale por detrás"), static_cast<int32>(Style)), Exhaust.X < -200.0);
	}

	// El de serie es SM_TN_BuggyBody: sin piezas propias salvo la antena, que va lejos de la torreta y de los tirantes.
	TestTrue(TEXT("El de serie no construye carrocería"), BuildPiece(ETNBuggyBodyStyle::Stock, EPiece::Shell).IsEmpty());
	TestFalse(TEXT("El de serie lleva antena"), BuildPiece(ETNBuggyBodyStyle::Stock, EPiece::Antenna).IsEmpty());
	const FVector StockMount = AntennaMount(ETNBuggyBodyStyle::Stock);
	TestTrue(TEXT("Antena del de serie lejos de la torreta"), FVector2D::Distance(FVector2D(StockMount.X, StockMount.Y), Axis) > F::TurretSweepRadius + 40.0);
	TestEqual(TEXT("El turbo del de serie, donde diga ATN_Buggy"), ExhaustLocal(ETNBuggyBodyStyle::Stock, FVector(1.0, 2.0, 3.0)), FVector(1.0, 2.0, 3.0));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
