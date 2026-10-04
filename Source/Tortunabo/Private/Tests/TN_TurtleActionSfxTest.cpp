// Sonidos de acción de la tortuga (#348): cada evento (derribo, muerte, recoger, lanzar, consumir, latido, reanimar y
// tótem) tiene un sonido en el CDO de BP_TortugaCharacter o un sustituto sintetizado. Si alguien vacía un UPROPERTY o
// mueve un recurso de Content/Audio, este test falla en vez de jugarse la partida en silencio.
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Audio; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Player/TN_CarryComponent.h"
#include "Player/TN_TurtleActionSfx.h"
#include "Player/TortugaCharacter.h"
#include "Sound/SoundBase.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNTurtleActionSfxTest
{
	const TCHAR* const TurtleBlueprintClass = TEXT("/Game/Blueprints/Characters/BP_TortugaCharacter.BP_TortugaCharacter_C");

	const UObject* SoundOn(const UObject& Owner, FName PropertyName)
	{
		const FObjectPropertyBase* Prop = FindFProperty<FObjectPropertyBase>(Owner.GetClass(), PropertyName);
		return Prop ? Prop->GetObjectPropertyValue_InContainer(&Owner) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTurtleActionSoundsTest,
	"Tortunabo.Audio.TurtleActionSounds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTurtleActionSoundsTest::RunTest(const FString& Parameters)
{
	using namespace TNTurtleActionSfxTest;
	const UClass* TurtleClass = LoadClass<ATortugaCharacter>(nullptr, TurtleBlueprintClass);
	if (!TestNotNull(TEXT("Se carga BP_TortugaCharacter"), TurtleClass))
	{
		return false;
	}
	const ATortugaCharacter* Cdo = TurtleClass->GetDefaultObject<ATortugaCharacter>();

	for (uint8 Index = 0; Index < static_cast<uint8>(ETNTurtleActionSfx::Count); ++Index)
	{
		const ETNTurtleActionSfx Sfx = static_cast<ETNTurtleActionSfx>(Index);
		const FName Name = TNTurtleActionSfx::PropertyName(Sfx);
		TestNotNull(*FString::Printf(TEXT("Existe el UPROPERTY %s"),
			*Name.ToString()), FindFProperty<FObjectPropertyBase>(TurtleClass, Name));
		const bool bHasAsset = SoundOn(*Cdo, Name) != nullptr;
		TestTrue(*FString::Printf(TEXT("%s suena (recurso en el CDO o sustituto sintetizado)"), *Name.ToString()),
			bHasAsset || TNTurtleActionSfx::HasSynthFallback(Sfx));
	}

	const UTN_CarryComponent* Carry = Cdo->GetCarryComponent();
	if (TestNotNull(TEXT("El CDO tiene CarryComponent"), Carry))
	{
		TestNotNull(TEXT("Coger a otra tortuga suena (GrabSound)"), SoundOn(*Carry, TEXT("GrabSound")));
		TestNotNull(TEXT("Lanzar a otra tortuga suena (ThrowSound)"), SoundOn(*Carry, TEXT("ThrowSound")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTurtleActionAssetsTest,
	"Tortunabo.Audio.TurtleActionAssets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTurtleActionAssetsTest::RunTest(const FString& Parameters)
{
	for (uint8 Index = 0; Index < static_cast<uint8>(ETNTurtleActionSfx::Count); ++Index)
	{
		const ETNTurtleActionSfx Sfx = static_cast<ETNTurtleActionSfx>(Index);
		const FString Path = TNTurtleActionSfx::DefaultAssetPath(Sfx);
		const FString Name = TNTurtleActionSfx::PropertyName(Sfx).ToString();
		if (Path.IsEmpty())
		{
			TestTrue(*FString::Printf(TEXT("%s sin recurso tiene sustituto"), *Name), TNTurtleActionSfx::HasSynthFallback(Sfx));
			continue;
		}
		TestNotNull(*FString::Printf(TEXT("%s: existe %s"), *Name, *Path), LoadObject<USoundBase>(nullptr, *Path));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNTurtleHeartbeatStopsTest,
	"Tortunabo.Audio.HeartbeatStopsWithoutKnockdown",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNTurtleHeartbeatStopsTest::RunTest(const FString& Parameters)
{
	// El latido sintetizado se para solo cuando la tortuga deja de estar derribada (revisión de #348).
	TestFalse(TEXT("Sin dueño no late"), TNTurtleActionSfx::ShouldKeepHeartbeat(nullptr));
	const ATortugaCharacter* Turtle = GetDefault<ATortugaCharacter>();
	TestFalse(TEXT("La tortuga de pie no late"), Turtle->IsKnockedDown());
	TestFalse(TEXT("Sin derribo el latido se para"), TNTurtleActionSfx::ShouldKeepHeartbeat(Turtle));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
