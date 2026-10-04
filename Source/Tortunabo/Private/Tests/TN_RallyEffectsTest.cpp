// Efectos del buggy del Rally sin Niagara: la llama del turbo (#294, cono emisivo construido en ejecución) y el humo a media
// vida (#296, esferas tintadas, como ATN_RallyBurstFX) son mallas propias cuando no hay sistema asignado. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.Effects; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Vehicles/TN_Buggy.h"
#include "Vehicles/TN_BuggyHealthComponent.h"
#include "Vehicles/TN_BuggyMath.h"
#include "Vehicles/TN_RallyCombatLogic.h"
#include "Vehicles/TN_RallyProjectile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#if WITH_EDITORONLY_DATA
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyEffectsTest
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;

		FScopedTestWorld()
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TNRallyEffectsTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyEffectsBoostFlameTest,
	"Tortunabo.Rally.Effects.BoostFlame",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyEffectsBoostFlameTest::RunTest(const FString& Parameters)
{
	using namespace TNBuggy;
	const FVector Exhaust(-200.f, 31.f, 111.f);
	const FVector Dir = FVector(-0.91f, 0.13f, 0.40f).GetSafeNormal();
	const FTransform Flame = BoostFlameTransform(Exhaust, Dir, 80.f, 16.f, 1.f);
	const float HalfCone = 0.5f * BasicConeSizeCm;
	TestTrue(TEXT("la base del cono cae en el escape"), Flame.TransformPosition(FVector(0.f, 0.f, -HalfCone)).Equals(Exhaust, 0.01f));
	TestTrue(TEXT("la punta sale 80 cm en la dirección del tubo"),
		Flame.TransformPosition(FVector(0.f, 0.f, HalfCone)).Equals(Exhaust + Dir * 80.f, 0.01f));
	TestEqual(TEXT("diámetro de la base"), static_cast<float>(Flame.GetScale3D().X) * BasicConeSizeCm, 16.f, 0.01f);
	const FTransform Long = BoostFlameTransform(Exhaust, Dir, 80.f, 16.f, 1.3f);
	TestTrue(TEXT("el parpadeo alarga la llama desde el escape"),
		Long.TransformPosition(FVector(0.f, 0.f, HalfCone)).Equals(Exhaust + Dir * 104.f, 0.01f));
	TestTrue(TEXT("sin dirección, hacia atrás"),
		BoostFlameTransform(Exhaust, FVector::ZeroVector, 50.f, 10.f, 1.f).TransformPosition(FVector(0.f, 0.f, HalfCone)).Equals(Exhaust - FVector(50.f, 0.f, 0.f), 0.01f));

	float MinFlicker = 10.f;
	float MaxFlicker = -10.f;
	for (float Time = 0.f; Time < 5.f; Time += 0.01f)
	{
		const float Flicker = BoostFlameFlicker(Time, 0.3f);
		MinFlicker = FMath::Min(MinFlicker, Flicker);
		MaxFlicker = FMath::Max(MaxFlicker, Flicker);
	}
	TestTrue(TEXT("el parpadeo se queda en [0,7; 1,3]"), MinFlicker >= 0.7f - 1e-3f && MaxFlicker <= 1.3f + 1e-3f);
	TestTrue(TEXT("y de verdad parpadea"), MaxFlicker - MinFlicker > 0.3f);

	// Sin Niagara asignado, el buggy trae su llama de malla propia (antes no se veía nada, #294).
	const ATN_Buggy* Defaults = GetDefault<ATN_Buggy>();
	TestTrue(TEXT("el turbo tiene llama (Niagara o malla propia)"), Defaults->HasBoostVisual());
	const UStaticMesh* FlameMesh = Defaults->GetBoostFlameMesh();
	if (!TestNotNull(TEXT("malla de la llama"), FlameMesh))
	{
		return false;
	}
	// La llama brilla (revisión de #294): su material tiene emisivo; BasicShapeMaterial, el de antes, no.
	const UMaterialInterface* FlameMaterial = Defaults->GetBoostFlameMaterial();
	if (!TestNotNull(TEXT("material de la llama"), FlameMaterial) || !TestNotNull(TEXT("material base"), FlameMaterial->GetMaterial()))
	{
		return false;
	}
#if WITH_EDITORONLY_DATA
	TestTrue(TEXT("el material de la llama es emisivo"), FlameMaterial->GetMaterial()->GetEditorOnlyData()->EmissiveColor.IsConnected());
	const UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (TestNotNull(TEXT("BasicShapeMaterial"), Basic))
	{
		TestFalse(TEXT("caso negativo: BasicShapeMaterial no es emisivo"), Basic->GetMaterial()->GetEditorOnlyData()->EmissiveColor.IsConnected());
	}
#endif
	const FBox Bounds = FlameMesh->GetBoundingBox();
	TestTrue(TEXT("el cono mide 100 cm y está centrado"), Bounds.Min.Equals(FVector(-HalfCone), 0.5f) && Bounds.Max.Equals(FVector(HalfCone), 0.5f));
#if WITH_EDITORONLY_DATA
	// La punta del cono está en +Z: los vértices más altos caen en el eje.
	if (const FMeshDescription* Description = FlameMesh->GetMeshDescription(0))
	{
		const TVertexAttributesConstRef<FVector3f> Positions = FStaticMeshConstAttributes(*Description).GetVertexPositions();
		float TopZ = -1000.f;
		float TopRadius = 0.f;
		for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
		{
			const FVector3f Position = Positions[Vertex];
			if (Position.Z > TopZ + 0.01f)
			{
				TopZ = Position.Z;
				TopRadius = FVector2f(Position.X, Position.Y).Size();
			}
			else if (FMath::IsNearlyEqual(Position.Z, TopZ, 0.01f))
			{
				TopRadius = FMath::Max(TopRadius, FVector2f(Position.X, Position.Y).Size());
			}
		}
		TestEqual(TEXT("la punta está arriba"), TopZ, HalfCone, 0.5f);
		TestTrue(TEXT("y en el eje (la base ancha queda en el escape)"), TopRadius < 1.f);
	}
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyEffectsSmokeTest,
	"Tortunabo.Rally.Effects.Smoke",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyEffectsSmokeTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyCombat;
	TestEqual(TEXT("con vida llena no hay bocanadas"), SmokePuffsPerSecond(100.f, 100.f), 0.f);
	TestEqual(TEXT("justo por encima de media vida, tampoco"), SmokePuffsPerSecond(51.f, 100.f), 0.f);
	TestEqual(TEXT("a media vida, las mínimas"), SmokePuffsPerSecond(50.f, 100.f), SmokeMinPuffsPerSecond, 0.01f);
	TestTrue(TEXT("con menos vida, más seguidas"), SmokePuffsPerSecond(10.f, 100.f) > SmokePuffsPerSecond(40.f, 100.f));
	TestTrue(TEXT("casi a 0, cerca de las máximas"), SmokePuffsPerSecond(0.5f, 100.f) > SmokeMaxPuffsPerSecond - 0.2f);
	TestEqual(TEXT("reventado no echa humo (es la nube)"), SmokePuffsPerSecond(0.f, 100.f), 0.f);

	// Sin Niagara asignado, el humo es de malla propia (antes no se veía nada, #296).
	const UTN_BuggyHealthComponent* Health = GetDefault<UTN_BuggyHealthComponent>();
	TestTrue(TEXT("el humo tiene Niagara o bocanadas de malla propia"), Health->SmokeFX != nullptr || Health->SmokePuffRadiusCm > 0.f);

	TNRallyEffectsTest::FScopedTestWorld Scoped;
	if (!TestNotNull(TEXT("Mundo de prueba"), Scoped.World))
	{
		return false;
	}
	const FVector Start(0.f, 0.f, 100.f);
	ATN_RallyBurstFX* Puff = ATN_RallyBurstFX::Spawn(Scoped.World, ETNRallyBurstKind::Smoke, Start, 45.f);
	ATN_RallyBurstFX* Sparks = ATN_RallyBurstFX::Spawn(Scoped.World, ETNRallyBurstKind::Sparks, Start, 45.f);
	if (!TestNotNull(TEXT("la bocanada se crea"), Puff) || !TestNotNull(TEXT("las chispas se crean"), Sparks))
	{
		return false;
	}
	Puff->Tick(0.5f);
	Sparks->Tick(0.1f);
	TestTrue(TEXT("la bocanada sube"), Puff->GetActorLocation().Z > Start.Z + 50.f);
	TestEqual(TEXT("las demás ráfagas no se mueven"), Sparks->GetActorLocation().Z, Start.Z, 0.01);
	return true;
}

#endif
