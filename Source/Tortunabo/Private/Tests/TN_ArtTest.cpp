// Sustitución de las mallas generadas por las de Arte (Docs/Arte_Assets.md): tabla de piezas, nombres del código,
// resolución con y sin sustituto y lo que pasa en un mundo con componentes sueltos, instancias y piezas combinadas.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Art; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Art/TN_Art.h"
#include "Art/TN_ArtCatalog.h"
#include "Art/TN_ArtMeshComponent.h"
#include "Art/TN_ArtSettings.h"
#include "../Art/TN_ArtPieces.h"
#include "../Art/TN_ArtSlots.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ProceduralMeshComponent.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNArtTestDetail
{
	UStaticMesh* EngineMesh(const TCHAR* Name)
	{
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
	}

	/** Catálogo transitorio con una pieza. */
	UTN_ArtCatalog* MakeCatalog(FName Slot, UStaticMesh* Mesh, const FTransform& Adjust = FTransform::Identity, bool bArtCollision = false)
	{
		UTN_ArtCatalog* Cat = NewObject<UTN_ArtCatalog>(GetTransientPackage());
		FTNArtOverride& Entry = Cat->Pieces.Add(Slot);
		Entry.Mesh = Mesh;
		Entry.Adjust = Adjust;
		Entry.bUseArtCollision = bArtCollision;
		return Cat;
	}

	/** Mundo de juego vacío para los componentes de la prueba (crearlo es como empezar una partida o el PIE). */
	struct FTestWorld
	{
		UWorld* World = nullptr;
		/** Al cerrarlo vuelve a los catálogos de los ajustes (SetCatalogsForTest({}), que también vacía la caché). */
		bool bResetCatalogsOnExit = true;

		explicit FTestWorld(bool bInResetCatalogsOnExit = true)
			: bResetCatalogsOnExit(bInResetCatalogsOnExit)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNArtTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
		}

		~FTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			if (bResetCatalogsOnExit)
			{
				TNArt::SetCatalogsForTest({});
			}
		}
	};

	UTN_ArtMeshComponent* ArtChildOf(const USceneComponent* Parent)
	{
		for (USceneComponent* Child : Parent->GetAttachChildren())
		{
			if (UTN_ArtMeshComponent* Art = Cast<UTN_ArtMeshComponent>(Child))
			{
				if (IsValid(Art)) { return Art; }
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNArtSlotTableTest,
	"Tortunabo.Art.SlotTable",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNArtSlotTableTest::RunTest(const FString& Parameters)
{
	const TArrayView<const TNArt::FSlotInfo> Table = TNArt::GetSlotTable();
	TestTrue(TEXT("La tabla de piezas no está vacía"), Table.Num() > 0);
	TSet<FString> Seen;
	const FString PrivateDir = FPaths::Combine(FPaths::GameSourceDir(), TEXT("Tortunabo"), TEXT("Private"));
	for (const TNArt::FSlotInfo& Info : Table)
	{
		const FString Name(Info.Name);
		TestTrue(FString::Printf(TEXT("%s: nombre válido (Zona.Parte...)"), *Name), TNArt::IsValidSlotName(Name));
		TestFalse(FString::Printf(TEXT("%s: sin duplicados en la tabla"), *Name), Seen.Contains(Name));
		Seen.Add(Name);
		const FString Kind(Info.Kind);
		TestTrue(FString::Printf(TEXT("%s: tipo conocido"), *Name),
			Kind == TNArt::SlotKind::Piece || Kind == TNArt::SlotKind::Component || Kind == TNArt::SlotKind::Instances || Kind == TNArt::SlotKind::Bone);
		TestEqual(FString::Printf(TEXT("%s: las piezas de la tortuga (y solo ellas) van pegadas a un hueso"), *Name),
			Kind == TNArt::SlotKind::Bone, TNArt::ZoneOf(Name) == TEXT("Turtle"));
		TestTrue(FString::Printf(TEXT("%s: existe su fichero %s"), *Name, Info.Source),
			FPaths::FileExists(FPaths::Combine(PrivateDir, Info.Source)));
		TestTrue(FString::Printf(TEXT("%s: dice qué es, su tamaño y su pivote"), *Name),
			FCString::Strlen(Info.What) > 0 && FCString::Strlen(Info.Size) > 0 && FCString::Strlen(Info.Pivot) > 0);
		TestTrue(FString::Printf(TEXT("%s: FindSlotInfo la encuentra"), *Name), TNArt::FindSlotInfo(FName(Info.Name)) == &Info);
	}

	TestTrue(TEXT("Nombre válido"), TNArt::IsValidSlotName(TEXT("ProcMap.Rock.Boulder2")));
	TestTrue(TEXT("Pieza de la tortuga"), TNArt::IsValidSlotName(TEXT("Turtle.Shell")));
	TestFalse(TEXT("Sin zona conocida"), TNArt::IsValidSlotName(TEXT("Menu.Logo")));
	TestFalse(TEXT("Solo la zona"), TNArt::IsValidSlotName(TEXT("Lobby")));
	TestFalse(TEXT("Parte en minúscula"), TNArt::IsValidSlotName(TEXT("Lobby.castle")));
	TestFalse(TEXT("Parte vacía"), TNArt::IsValidSlotName(TEXT("Lobby..Castle")));
	TestFalse(TEXT("Con guion bajo"), TNArt::IsValidSlotName(TEXT("Lobby.Castle_Tower")));
	TestEqual(TEXT("Zona"), TNArt::ZoneOf(TEXT("Beach.Gull.Body")), FString(TEXT("Beach")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNArtSlotsInCodeTest,
	"Tortunabo.Art.SlotsInCode",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNArtSlotsInCodeTest::RunTest(const FString& Parameters)
{
	// Todo TN_ART("...") del código está en la tabla y toda pieza de la tabla sale en el código.
	const FString ModuleDir = FPaths::Combine(FPaths::GameSourceDir(), TEXT("Tortunabo"));
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *ModuleDir, TEXT("*.cpp"), true, false, false);
	IFileManager::Get().FindFilesRecursive(Files, *ModuleDir, TEXT("*.h"), true, false, false);
	const FRegexPattern Pattern(TEXT("TN_ART\\(\\s*\"([^\"]+)\"\\s*\\)"));
	TMap<FString, FString> Used;
	for (const FString& File : Files)
	{
		if (File.Contains(TEXT("/Private/Tests/")) || File.EndsWith(TEXT("Art/TN_Art.h"))) { continue; }
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File)) { continue; }
		FRegexMatcher Matcher(Pattern, Text);
		while (Matcher.FindNext())
		{
			Used.FindOrAdd(Matcher.GetCaptureGroup(1), FPaths::GetCleanFilename(File));
		}
	}
	TestTrue(TEXT("El código nombra piezas con TN_ART"), Used.Num() > 0);
	for (const TPair<FString, FString>& Pair : Used)
	{
		TestTrue(FString::Printf(TEXT("%s (%s) es un nombre válido"), *Pair.Key, *Pair.Value), TNArt::IsValidSlotName(Pair.Key));
		if (!TNArt::FindSlotInfo(FName(*Pair.Key)))
		{
			AddError(FString::Printf(TEXT("%s (%s) no está en la tabla de piezas (Private/Art/TN_ArtSlots_*.inl)."), *Pair.Key, *Pair.Value));
		}
	}
	for (const TNArt::FSlotInfo& Info : TNArt::GetSlotTable())
	{
		if (!Used.Contains(Info.Name))
		{
			AddError(FString::Printf(TEXT("%s está en la tabla pero ningún TN_ART del código la usa."), Info.Name));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNArtResolveTest,
	"Tortunabo.Art.Resolve",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNArtResolveTest::RunTest(const FString& Parameters)
{
	using namespace TNArtTestDetail;
	UStaticMesh* Cube = EngineMesh(TEXT("Cube"));
	UStaticMesh* Sphere = EngineMesh(TEXT("Sphere"));
	if (!TestNotNull(TEXT("Malla del motor Cube"), Cube) || !TestNotNull(TEXT("Malla del motor Sphere"), Sphere)) { return false; }
	const FName Slot(TEXT("Lobby.Test.Piece"));
	const FName Other(TEXT("Lobby.Test.Other"));

	// FindIn (pura): el primero con malla gana; sin malla no cuenta; los nulos se saltan.
	UTN_ArtCatalog* Empty = MakeCatalog(Slot, nullptr);
	UTN_ArtCatalog* First = MakeCatalog(Slot, Sphere);
	UTN_ArtCatalog* Second = MakeCatalog(Slot, Cube);
	{
		const UTN_ArtCatalog* Cats[] = { nullptr, Empty, First, Second };
		const FTNArtOverride* Found = TNArt::FindIn(Cats, Slot);
		TestTrue(TEXT("Gana el primer catálogo con malla"), Found && Found->Mesh.Get() == Sphere);
		TestNull(TEXT("Pieza que no está"), TNArt::FindIn(Cats, Other));
		const UTN_ArtCatalog* OnlyEmpty[] = { Empty };
		TestNull(TEXT("Entrada sin malla = sin sustituto"), TNArt::FindIn(OnlyEmpty, Slot));
	}

	// Find/Resolve con los catálogos de la prueba.
	TNArt::SetCatalogsForTest({ First });
	const TNArt::FResolved* R = TNArt::Find(Slot);
	TestTrue(TEXT("Con sustituto: su malla"), R && R->Mesh == Sphere);
	TestNull(TEXT("Sin sustituto: nada"), TNArt::Find(Other));
	TestTrue(TEXT("Resolve con sustituto da la de arte"), TNArt::Resolve(Slot, Cube) == Sphere);
	TestTrue(TEXT("Resolve sin sustituto da la generada"), TNArt::Resolve(Other, Cube) == Cube);
	TestNull(TEXT("Sin pieza generada no hay arte"), TNArt::Resolve(Slot, nullptr));
	TestTrue(TEXT("Resolve anota la pieza"), TNArt::GetNotedSlots().Contains(Slot));

	// TN.Art.Enabled 0: todo lo generado.
	if (IConsoleVariable* Enabled = IConsoleManager::Get().FindConsoleVariable(TEXT("TN.Art.Enabled")))
	{
		Enabled->Set(0, ECVF_SetByCode);
		TestNull(TEXT("TN.Art.Enabled 0: sin sustitutos"), TNArt::Find(Slot));
		Enabled->Set(1, ECVF_SetByCode);
		TestNotNull(TEXT("TN.Art.Enabled 1: vuelve el sustituto"), TNArt::Find(Slot));
	}
	TNArt::SetCatalogsForTest({});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNArtFilterBuffersTest,
	"Tortunabo.Art.FilterBuffers",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNArtFilterBuffersTest::RunTest(const FString& Parameters)
{
	using namespace TNArt;
	FPieceBuffers B;
	FPieceBuffers Decor;
	FPieceLog Log(TEXT("Test"));
	B.AddBox(FVector(0.0), FVector(1.0, 0.0, 0.0), FVector(50.0), FLinearColor::Red);
	{
		FPieceScope Outer(Log, TEXT("Lobby.Test.Outer"), PiecePivot(FVector(100.0, 0.0, 0.0), 90.0), { &B, &Decor });
		B.AddBox(FVector(100.0, 0.0, 0.0), FVector(1.0, 0.0, 0.0), FVector(20.0), FLinearColor::Green);
		{
			FPieceScope Inner(Log, TEXT("Lobby.Test.Inner"), PiecePivot(FVector(100.0, 0.0, 50.0)), { &Decor });
			Decor.AddQuad(FVector(0.0), FVector(10.0, 0.0, 0.0), FVector(10.0, 10.0, 0.0), FVector(0.0, 10.0, 0.0), FVector::UpVector, FLinearColor::Blue);
		}
	}
	B.AddBox(FVector(-100.0, 0.0, 0.0), FVector(1.0, 0.0, 0.0), FVector(30.0), FLinearColor::White);

	const TArray<FPiece>& Pieces = Log.GetPieces();
	if (!TestEqual(TEXT("Dos piezas"), Pieces.Num(), 2)) { return false; }
	TestEqual(TEXT("La de fuera tiene tramos en los dos buffers"), Pieces[0].Ranges.Num(), 2);
	TestEqual(TEXT("La de dentro, solo en el de adornos"), Pieces[1].Ranges.Num(), 1);
	TestEqual(TEXT("Tramo de la caja del medio: 36 vértices"), Pieces[0].Ranges[0].V1 - Pieces[0].Ranges[0].V0, 36);
	TestEqual(TEXT("Empieza tras la primera caja"), Pieces[0].Ranges[0].T0, 36);

	// Sin tramos: igual.
	FPieceBuffers Same;
	FilterBuffers(B, TArrayView<const FPieceRange>(), Same);
	TestEqual(TEXT("Sin tramos: los mismos vértices"), Same.Verts.Num(), B.Verts.Num());
	TestTrue(TEXT("Sin tramos: los mismos índices"), Same.Tris == B.Tris);

	// Quitando la caja del medio quedan las otras dos, enteras y bien indexadas.
	FPieceBuffers Out;
	const FPieceRange Ranges[] = { Pieces[0].Ranges[0], Pieces[0].Ranges[1] };
	FilterBuffers(B, MakeArrayView(Ranges), Out);
	TestEqual(TEXT("Quedan dos cajas de vértices"), Out.Verts.Num(), 72);
	TestEqual(TEXT("Quedan dos cajas de índices"), Out.Tris.Num(), 72);
	TestEqual(TEXT("Normales a la par"), Out.Normals.Num(), Out.Verts.Num());
	TestEqual(TEXT("Colores a la par"), Out.Colors.Num(), Out.Verts.Num());
	bool bIndexOk = true;
	for (const int32 Index : Out.Tris) { bIndexOk &= Out.Verts.IsValidIndex(Index); }
	TestTrue(TEXT("Índices dentro de los vértices"), bIndexOk);
	bool bNoGreen = true;
	for (const FLinearColor& C : Out.Colors) { bNoGreen &= !C.Equals(FLinearColor::Green); }
	TestTrue(TEXT("No queda nada de la pieza quitada"), bNoGreen);
	TestTrue(TEXT("La última caja sigue igual"), Out.Verts.Last() == B.Verts.Last());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNArtApplyInWorldTest,
	"Tortunabo.Art.ApplyInWorld",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNArtApplyInWorldTest::RunTest(const FString& Parameters)
{
	using namespace TNArtTestDetail;
	UStaticMesh* Cube = EngineMesh(TEXT("Cube"));
	UStaticMesh* Sphere = EngineMesh(TEXT("Sphere"));
	if (!TestNotNull(TEXT("Mallas del motor"), Cube && Sphere ? Cube : nullptr)) { return false; }
	FTestWorld TestWorld;
	UWorld* World = TestWorld.World;
	AActor* Actor = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Actor de prueba"), Actor)) { return false; }
	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	const FName Slot(TEXT("Lobby.Test.Piece"));
	const FTransform Adjust(FRotator(0.0, 90.0, 0.0), FVector(0.0, 0.0, 50.0), FVector(2.0));

	// ── Componente suelto ──
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Actor, TEXT("Generated"));
	Comp->SetupAttachment(Root);
	Comp->RegisterComponent();
	Comp->SetCollisionProfileName(TEXT("BlockAll"));

	TNArt::SetCatalogsForTest({ MakeCatalog(TEXT("Lobby.Test.Other"), Sphere) });
	TNArt::SetMesh(Comp, Cube, Slot);
	TestTrue(TEXT("Sin sustituto: la malla generada"), Comp->GetStaticMesh() == Cube);
	TestTrue(TEXT("Sin sustituto: se dibuja"), Comp->bRenderInMainPass && Comp->CastShadow);
	TestNull(TEXT("Sin sustituto: sin malla de arte"), ArtChildOf(Comp));

	TNArt::SetCatalogsForTest({ MakeCatalog(Slot, Sphere, Adjust) });
	TNArt::SetMesh(Comp, Cube, Slot);
	UTN_ArtMeshComponent* Art = ArtChildOf(Comp);
	if (TestNotNull(TEXT("Con sustituto: malla de arte hija"), Art))
	{
		TestTrue(TEXT("La de arte"), Art->GetStaticMesh() == Sphere);
		FTransform Instance;
		TestTrue(TEXT("Una instancia"), Art->GetInstanceCount() == 1 && Art->GetInstanceTransform(0, Instance, false));
		TestTrue(TEXT("Con el ajuste"), Instance.Equals(Adjust));
		TestEqual(TEXT("La de arte no choca"), Art->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	}
	TestTrue(TEXT("El componente generado conserva su malla"), Comp->GetStaticMesh() == Cube);
	TestEqual(TEXT("y su colisión"), Comp->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
	TestTrue(TEXT("y su visibilidad lógica"), Comp->IsVisible());
	TestFalse(TEXT("pero no se dibuja"), Comp->bRenderInMainPass || Comp->bRenderInDepthPass || Comp->CastShadow);

	// bUseArtCollision ya no cambia la colisión (#828): sigue la generada, igual en todas las máquinas con o sin arte.
	TNArt::SetCatalogsForTest({ MakeCatalog(Slot, Sphere, Adjust, true) });
	TNArt::ApplyToComponent(Comp, Slot);
	Art = ArtChildOf(Comp);
	TestTrue(TEXT("bUseArtCollision: la de arte no choca"), Art && Art->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	TestEqual(TEXT("bUseArtCollision: el generado sigue chocando"), Comp->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);

	// Se quita el sustituto: todo como antes.
	TNArt::SetCatalogsForTest({ MakeCatalog(Slot, nullptr) });
	TNArt::ApplyToComponent(Comp, Slot);
	TestNull(TEXT("Sin sustituto otra vez: sin malla de arte"), ArtChildOf(Comp));
	TestTrue(TEXT("Se vuelve a dibujar"), Comp->bRenderInMainPass && Comp->bRenderInDepthPass && Comp->CastShadow);
	TestEqual(TEXT("Recupera su colisión"), Comp->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);

	// ── Instancias ──
	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(Actor, TEXT("Instances"));
	ISM->SetupAttachment(Root);
	ISM->RegisterComponent();
	ISM->SetStaticMesh(Cube);
	ISM->SetCollisionProfileName(TEXT("BlockAll"));
	const FTransform A(FVector(100.0, 0.0, 0.0)), B(FRotator(0.0, 45.0, 0.0), FVector(0.0, 200.0, 0.0));
	ISM->AddInstance(A);
	ISM->AddInstance(B);
	TNArt::SetCatalogsForTest({});
	TNArt::ApplyToInstances(ISM, Slot);
	TestTrue(TEXT("Instancias sin sustituto: igual"), ISM->GetStaticMesh() == Cube && ISM->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);

	TNArt::SetCatalogsForTest({ MakeCatalog(Slot, Sphere, Adjust) });
	TNArt::ApplyToInstances(ISM, Slot);
	TestTrue(TEXT("Instancias con sustituto: la de arte"), ISM->GetStaticMesh() == Sphere);
	TestEqual(TEXT("Mismas instancias"), ISM->GetInstanceCount(), 2);
	FTransform Got;
	ISM->GetInstanceTransform(1, Got, false);
	TestTrue(TEXT("Con el ajuste aplicado a cada una"), Got.Equals(Adjust * B, 0.01));
	TestEqual(TEXT("La de arte no choca"), ISM->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	UInstancedStaticMeshComponent* Twin = nullptr;
	TInlineComponentArray<UInstancedStaticMeshComponent*> All;
	Actor->GetComponents(All);
	for (UInstancedStaticMeshComponent* C : All)
	{
		if (C != ISM && !C->IsA<UTN_ArtMeshComponent>() && C->GetStaticMesh() == Cube) { Twin = C; }
	}
	if (TestNotNull(TEXT("Gemelo con la colisión generada"), Twin))
	{
		TestEqual(TEXT("El gemelo tiene las instancias"), Twin->GetInstanceCount(), 2);
		TestEqual(TEXT("El gemelo choca"), Twin->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
		TestFalse(TEXT("El gemelo no se ve"), Twin->IsVisible());
	}
	// Las animadas: UpdateInstances aplica el ajuste.
	TNArt::UpdateInstances(ISM, 0, { A }, false, true, true);
	ISM->GetInstanceTransform(0, Got, false);
	TestTrue(TEXT("UpdateInstances con el ajuste"), Got.Equals(Adjust * A, 0.01));

	// ── Piezas de una malla combinada ──
	UProceduralMeshComponent* Pmc = NewObject<UProceduralMeshComponent>(Actor, TEXT("Combined"));
	Pmc->SetupAttachment(Root);
	Pmc->RegisterComponent();
	TNArt::FPieceBuffers Buffers;
	TNArt::FPieceLog Log(TEXT("TestGroup"));
	Buffers.AddBox(FVector(0.0), FVector(1.0, 0.0, 0.0), FVector(50.0), FLinearColor::Red);
	const FTransform Pivot(FRotator(0.0, 30.0, 0.0), FVector(300.0, 0.0, 0.0));
	{
		TNArt::FPieceScope Scope(Log, Slot, Pivot, { &Buffers });
		Buffers.AddBox(FVector(300.0, 0.0, 0.0), FVector(1.0, 0.0, 0.0), FVector(20.0), FLinearColor::Green);
	}
	TNArt::SetCatalogsForTest({});
	TNArt::UploadSection(Pmc, 0, Buffers, true, nullptr, &Log);
	TNArt::SpawnPieceArt(Pmc, Log);
	TestEqual(TEXT("Piezas sin sustituto: la sección entera"), Pmc->GetProcMeshSection(0)->ProcVertexBuffer.Num(), Buffers.Verts.Num());
	TestTrue(TEXT("y con su colisión"), Pmc->GetProcMeshSection(0)->bEnableCollision);
	TestEqual(TEXT("y ninguna sección más"), Pmc->GetNumSections(), 1);
	TestNull(TEXT("y sin arte"), ArtChildOf(Pmc));

	TNArt::SetCatalogsForTest({ MakeCatalog(Slot, Sphere, Adjust) });
	Pmc->ClearAllMeshSections();
	TNArt::UploadSection(Pmc, 0, Buffers, true, nullptr, &Log);
	TNArt::SpawnPieceArt(Pmc, Log);
	TestEqual(TEXT("Con sustituto: se dibuja sin la pieza"), Pmc->GetProcMeshSection(0)->ProcVertexBuffer.Num(), 36);
	TestFalse(TEXT("y esa sección no choca"), Pmc->GetProcMeshSection(0)->bEnableCollision);
	const FProcMeshSection* Collision = Pmc->GetProcMeshSection(TNArt::CollisionSectionOffset);
	TestTrue(TEXT("La colisión, entera y sin dibujar"), Collision && Collision->ProcVertexBuffer.Num() == Buffers.Verts.Num()
		&& Collision->bEnableCollision && !Collision->bSectionVisible);
	UTN_ArtMeshComponent* PieceArt = ArtChildOf(Pmc);
	if (TestNotNull(TEXT("Malla de arte de la pieza"), PieceArt))
	{
		FTransform Instance;
		PieceArt->GetInstanceTransform(0, Instance, false);
		TestTrue(TEXT("Una copia, en Adjust * Pivot"), PieceArt->GetInstanceCount() == 1 && Instance.Equals(Adjust * Pivot, 0.01));
		TestTrue(TEXT("Del grupo de la construcción"), PieceArt->Group == FName(TEXT("TestGroup")));
	}
	// Reconstruir quita la anterior.
	TNArt::SpawnPieceArt(Pmc, Log);
	int32 ArtCount = 0;
	for (USceneComponent* Child : Pmc->GetAttachChildren()) { ArtCount += (Cast<UTN_ArtMeshComponent>(Child) && IsValid(Child)) ? 1 : 0; }
	TestEqual(TEXT("Al reconstruir no se duplica"), ArtCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNArtCatalogSeenByNextWorldTest,
	"Tortunabo.Art.CatalogSeenByNextWorld",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNArtCatalogSeenByNextWorldTest::RunTest(const FString& Parameters)
{
	// #319: Arte pone la malla de una pieza en el catálogo y le da al Play, y el PIE dibujaba la generada: la caché de TNArt
	// dura todo el proceso y seguía con lo que leyó al abrir el editor, porque el cambio no pasó por PostEditChangeProperty
	// (Python escribe directamente en el asset; un paquete recargado de git es otro objeto). Cada mundo que empieza tiene que
	// leer el catálogo tal como está en ese momento.
	using namespace TNArtTestDetail;
	UStaticMesh* Cube = EngineMesh(TEXT("Cube"));
	UStaticMesh* Sphere = EngineMesh(TEXT("Sphere"));
	if (!TestNotNull(TEXT("Mallas del motor"), Cube && Sphere ? Cube : nullptr)) { return false; }
	const FName Slot(TEXT("Lobby.Castle.Tower"));

	// Por los catálogos de los ajustes (el camino del juego), no por los de los tests.
	UTN_ArtSettings* Settings = GetMutableDefault<UTN_ArtSettings>();
	const TArray<TSoftObjectPtr<UTN_ArtCatalog>> SavedCatalogs = Settings->Catalogs;
	TStrongObjectPtr<UTN_ArtCatalog> Catalog(MakeCatalog(Slot, nullptr));
	TStrongObjectPtr<UTN_ArtCatalog> Reloaded(MakeCatalog(Slot, Sphere));
	TNArt::SetCatalogsForTest({});
	Settings->Catalogs = { TSoftObjectPtr<UTN_ArtCatalog>(Catalog.Get()) };
	TNArt::InvalidateCache();
	ON_SCOPE_EXIT
	{
		Settings->Catalogs = SavedCatalogs;
		TNArt::InvalidateCache();
	};

	{
		FTestWorld Opened(false);
		TestNull(TEXT("Al abrir, sin malla en el catálogo: la generada"), TNArt::Find(Slot));
	}

	// Arte pone la malla sin pasar por el panel de detalles (como un script de Python) y le da al Play.
	Catalog->Pieces.FindChecked(Slot).Mesh = Cube;
	{
		FTestWorld Play(false);
		const TNArt::FResolved* R = TNArt::Find(Slot);
		TestTrue(TEXT("La partida siguiente ve la malla nueva del catálogo"), R && R->Mesh == Cube);
	}

	// El catálogo de los ajustes pasa a ser otro objeto (asset recargado) y se juega otra vez.
	Settings->Catalogs = { TSoftObjectPtr<UTN_ArtCatalog>(Reloaded.Get()) };
	{
		FTestWorld Play(false);
		const TNArt::FResolved* R = TNArt::Find(Slot);
		TestTrue(TEXT("La partida siguiente lee el catálogo recargado"), R && R->Mesh == Sphere);
	}

	// Empezar un mundo no cambia la versión de los catálogos (el valle no se rehace de más); «Aplicar cambios», sí.
	const uint32 Version = TNArt::GetCatalogVersion();
	{
		FTestWorld Play(false);
	}
	TestEqual(TEXT("Empezar un mundo no cambia la versión de los catálogos"), TNArt::GetCatalogVersion(), Version);
	Reloaded->ApplyChanges();
	TestNotEqual(TEXT("«Aplicar cambios» sube la versión de los catálogos"), TNArt::GetCatalogVersion(), Version);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNArtUnknownPiecesTest,
	"Tortunabo.Art.UnknownPieces",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNArtUnknownPiecesTest::RunTest(const FString& Parameters)
{
	// Una entrada con malla cuyo nombre no es de ninguna pieza no cambia nada: se avisa (FindUnknownPieces).
	using namespace TNArtTestDetail;
	UStaticMesh* Cube = EngineMesh(TEXT("Cube"));
	if (!TestNotNull(TEXT("Malla del motor Cube"), Cube)) { return false; }
	UTN_ArtCatalog* Cat = MakeCatalog(TEXT("Lobby.Castle.Tower"), Cube);
	Cat->Pieces.Add(TEXT("Lobby.Castle.Towers")).Mesh = Cube;
	Cat->Pieces.Add(TEXT("Lobby.Castle.Gatehouse"));
	Cat->Pieces.Add(TEXT("Lobby.Castle.Old"));
	const TArray<FName> Unknown = TNArt::FindUnknownPieces(Cat);
	TestEqual(TEXT("Solo la que tiene malla y no existe"), Unknown.Num(), 1);
	TestTrue(TEXT("La mal escrita"), Unknown.Contains(FName(TEXT("Lobby.Castle.Towers"))));
	TestEqual(TEXT("Sin catálogo, nada"), TNArt::FindUnknownPieces(nullptr).Num(), 0);

	// Los catálogos del proyecto (los que ya existen) no tienen ninguna.
	for (const TSoftObjectPtr<UTN_ArtCatalog>& Soft : GetDefault<UTN_ArtSettings>()->Catalogs)
	{
		if (!FPackageName::DoesPackageExist(Soft.ToSoftObjectPath().GetLongPackageName())) { continue; }
		if (const UTN_ArtCatalog* Project = Soft.LoadSynchronous())
		{
			for (const FName& Name : TNArt::FindUnknownPieces(Project))
			{
				AddError(FString::Printf(TEXT("%s: la pieza %s tiene malla pero no existe en la tabla de piezas."), *Project->GetName(), *Name.ToString()));
			}
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
