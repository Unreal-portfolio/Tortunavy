// Muñecos tortuga (#797): regla de recogida (una por muñeco y jugadora) y figurita de código (caja y triángulos). Sin mundo
// ni actores: el mismo código que usa ATN_TurtleDoll. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.ProcMap.Dolls; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "World/ProcMap/TN_TurtleDoll.h"
#include "World/ProcMap/TN_TurtleDollMesh.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTurtleDollCollectTest,
	"Tortunabo.ProcMap.Dolls.Collect",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTurtleDollCollectTest::RunTest(const FString& Parameters)
{
	using TNTurtleDollRules::CanCollect;
	const TArray<int32> Nobody;
	const TArray<int32> Ana = { 256 };
	TestTrue(TEXT("una jugadora en juego coge el muñeco"), CanCollect(Nobody, 256, true));
	TestFalse(TEXT("la misma no lo coge dos veces"), CanCollect(Ana, 256, true));
	TestTrue(TEXT("otra jugadora coge el suyo aunque ya lo tenga la primera"), CanCollect(Ana, 257, true));
	TestFalse(TEXT("muerta o fuera de juego no lo coge"), CanCollect(Nobody, 256, false));
	TestFalse(TEXT("sin PlayerId no lo coge"), CanCollect(Nobody, INDEX_NONE, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTurtleDollMeshTest,
	"Tortunabo.ProcMap.Dolls.Mesh",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTurtleDollMeshTest::RunTest(const FString& Parameters)
{
	TNProcMesh::FTNProcMeshBuffers B;
	TNTurtleDollMesh::Build(B);
	const int32 Tris = B.Tris.Num() / 3;
	TestTrue(FString::Printf(TEXT("la figurita tiene forma (%d triángulos)"), Tris), Tris >= 200 && Tris <= 3000);
	FBox Box(ForceInit);
	for (const FVector& V : B.Verts) { Box += V; }
	const FVector Size = Box.GetSize();
	const FVector Limit = TNTurtleDollMesh::Size();
	TestTrue(FString::Printf(TEXT("cabe en su caja (%s)"), *Size.ToString()), Size.X <= Limit.X && Size.Y <= Limit.Y && Size.Z <= Limit.Z);
	TestTrue(TEXT("nada por debajo de la base de la peana"), Box.Min.Z >= -0.01);
	TestTrue(TEXT("se ve: más de 30 cm de largo y de alto"), Size.X >= 30.0 && Size.Z >= 30.0);
	TestEqual(TEXT("un color por vértice"), B.Colors.Num(), B.Verts.Num());
	return true;
}

#endif
