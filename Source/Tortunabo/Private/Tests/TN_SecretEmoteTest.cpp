// Emotes ocultos (#839, Player/TN_SecretEmote.h), sin mundo ni teclado: el código «tortunabo» letra a letra (aciertos, fallos y
// reinicio al equivocarse), qué teclas son letras y cuáles son los emotes ocultos y su turno.
// Correr desde Session Frontend (categoría "Tortunabo.SecretEmote") o sin ventana:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.SecretEmote; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Player/TN_SecretEmote.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNSecretEmoteTestHelpers
{
	/** Escribe las letras seguidas y dice cuántas veces se completó el código y en qué posiciones (0 = la primera letra). */
	struct FTyped
	{
		int32 Completions = 0;
		TArray<int32> At;
	};

	FTyped Type(TNSecretEmote::FCodeMatcher& Matcher, const TCHAR* Letters)
	{
		FTyped Result;
		for (int32 i = 0; Letters[i] != 0; ++i)
		{
			if (Matcher.Press(Letters[i]))
			{
				++Result.Completions;
				Result.At.Add(i);
			}
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSecretEmoteCodeHitTest,
	"Tortunabo.SecretEmote.Code.Hit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSecretEmoteCodeHitTest::RunTest(const FString& Parameters)
{
	using namespace TNSecretEmote;
	using namespace TNSecretEmoteTestHelpers;

	TestEqual(TEXT("El código es «tortunabo»"), FString(Code), FString(TEXT("tortunabo")));

	{
		FCodeMatcher Matcher;
		const FTyped Typed = Type(Matcher, TEXT("tortunabo"));
		TestEqual(TEXT("Escrito entero, se completa una vez"), Typed.Completions, 1);
		TestEqual(TEXT("Y justo con la última letra"), Typed.At.Num() == 1 ? Typed.At[0] : -1, 8);
		TestEqual(TEXT("Después vuelve a empezar"), Matcher.GetMatched(), 0);
	}
	{
		FCodeMatcher Matcher;
		for (int32 i = 0; i < 8; ++i)
		{
			TestFalse(TEXT("Antes de la última letra no se completa"), Matcher.Press(Code[i]));
			TestEqual(TEXT("Lleva una letra más cada vez"), Matcher.GetMatched(), i + 1);
		}
		TestTrue(TEXT("La última letra lo completa"), Matcher.Press(Code[8]));
	}
	{
		FCodeMatcher Matcher;
		TestEqual(TEXT("Las mayúsculas valen (Mayús o Bloq Mayús)"), Type(Matcher, TEXT("TORTUNABO")).Completions, 1);
	}
	{
		FCodeMatcher Matcher;
		const FTyped Typed = Type(Matcher, TEXT("tortunabotortunabo"));
		TestEqual(TEXT("Escrito dos veces seguidas, se completa dos veces"), Typed.Completions, 2);
		TestTrue(TEXT("En la última letra de cada una"), Typed.At.Num() == 2 && Typed.At[0] == 8 && Typed.At[1] == 17);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSecretEmoteCodeMissTest,
	"Tortunabo.SecretEmote.Code.Miss",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSecretEmoteCodeMissTest::RunTest(const FString& Parameters)
{
	using namespace TNSecretEmote;
	using namespace TNSecretEmoteTestHelpers;

	{
		FCodeMatcher Matcher;
		TestEqual(TEXT("Incompleto no se completa"), Type(Matcher, TEXT("tortunab")).Completions, 0);
		TestEqual(TEXT("Y se queda a una letra"), Matcher.GetMatched(), 8);
	}
	{
		FCodeMatcher Matcher;
		TestEqual(TEXT("Otra palabra no lo activa"), Type(Matcher, TEXT("tortuga")).Completions, 0);
		TestEqual(TEXT("Ni el código con otro final"), Type(Matcher, TEXT("tortunabe")).Completions, 0);
		TestEqual(TEXT("Ni las letras en otro orden"), Type(Matcher, TEXT("obanutrot")).Completions, 0);
	}
	{
		// Equivocarse en mitad del código lo reinicia: lo que falta ya no lo completa.
		FCodeMatcher Matcher;
		TestEqual(TEXT("Un fallo a la mitad y seguir no lo completa"), Type(Matcher, TEXT("tortxunabo")).Completions, 0);
		TestEqual(TEXT("Con el fallo, vuelve a empezar de cero"), Matcher.GetMatched(), 0);
	}
	{
		// ... pero se puede volver a intentar de seguido.
		FCodeMatcher Matcher;
		const FTyped Typed = Type(Matcher, TEXT("tortxtortunabo"));
		TestEqual(TEXT("Tras el fallo, el código entero sí vale"), Typed.Completions, 1);
		TestEqual(TEXT("En la última letra"), Typed.At.Num() == 1 ? Typed.At[0] : -1, 13);
	}
	{
		// La letra que falla puede ser el principio de otro intento, o seguir lo que ya valía.
		FCodeMatcher Matcher;
		TestEqual(TEXT("Una «t» de más al principio no estorba"), Type(Matcher, TEXT("ttortunabo")).Completions, 1);
		TestEqual(TEXT("Una «t» de más a mitad empieza de nuevo"), Type(Matcher, TEXT("torttortunabo")).Completions, 1);
		TestEqual(TEXT("Repetir el principio no estorba («tor» + «tortunabo»)"), Type(Matcher, TEXT("tortortunabo")).Completions, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSecretEmoteCodeResetTest,
	"Tortunabo.SecretEmote.Code.Reset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSecretEmoteCodeResetTest::RunTest(const FString& Parameters)
{
	using namespace TNSecretEmote;
	using namespace TNSecretEmoteTestHelpers;

	// Cualquier otra tecla (o un menú abierto, o un campo de texto con el foco) llama a Reset.
	{
		FCodeMatcher Matcher;
		Type(Matcher, TEXT("tort"));
		TestEqual(TEXT("Lleva cuatro letras"), Matcher.GetMatched(), 4);
		Matcher.Reset();
		TestEqual(TEXT("Reset lo deja en cero"), Matcher.GetMatched(), 0);
		TestEqual(TEXT("Lo que faltaba ya no lo completa"), Type(Matcher, TEXT("unabo")).Completions, 0);
	}
	{
		FCodeMatcher Matcher;
		Type(Matcher, TEXT("tor"));
		Matcher.Reset();
		TestEqual(TEXT("Tras Reset, el código entero sí vale"), Type(Matcher, TEXT("tortunabo")).Completions, 1);
	}
	{
		FCodeMatcher Matcher(TEXT("ab"));
		TestEqual(TEXT("Otro código: un fallo y se completa al escribirlo de nuevo"), Type(Matcher, TEXT("axab")).Completions, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSecretEmoteKeysTest,
	"Tortunabo.SecretEmote.Keys",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSecretEmoteKeysTest::RunTest(const FString& Parameters)
{
	using namespace TNSecretEmote;

	TCHAR Letter = 0;
	TestTrue(TEXT("«T» es una letra"), LetterFromKeyName(TEXT("T"), Letter));
	TestTrue(TEXT("En minúscula"), Letter == TEXT('t'));
	TestTrue(TEXT("«B» es una letra"), LetterFromKeyName(TEXT("B"), Letter));
	TestTrue(TEXT("También en minúscula"), Letter == TEXT('b'));
	TestFalse(TEXT("Espacio no"), LetterFromKeyName(TEXT("SpaceBar"), Letter));
	TestFalse(TEXT("Un número no"), LetterFromKeyName(TEXT("Zero"), Letter));
	TestFalse(TEXT("Mayús izquierdo no"), LetterFromKeyName(TEXT("LeftShift"), Letter));
	TestFalse(TEXT("Un botón del mando no"), LetterFromKeyName(TEXT("Gamepad_FaceButton_Bottom"), Letter));
	TestFalse(TEXT("Un nombre vacío no"), LetterFromKeyName(FString(), Letter));

	// Cada letra del código sale de una tecla del teclado.
	FCodeMatcher Matcher;
	bool bDone = false;
	for (const TCHAR* KeyName : { TEXT("T"), TEXT("O"), TEXT("R"), TEXT("T"), TEXT("U"), TEXT("N"), TEXT("A"), TEXT("B"), TEXT("O") })
	{
		TCHAR Key = 0;
		if (LetterFromKeyName(KeyName, Key)) { bDone = Matcher.Press(Key); }
	}
	TestTrue(TEXT("Las teclas T, O, R, T, U, N, A, B y O completan el código"), bDone);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNSecretEmoteHiddenTest,
	"Tortunabo.SecretEmote.Hidden",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNSecretEmoteHiddenTest::RunTest(const FString& Parameters)
{
	using namespace TNSecretEmote;

	TestTrue(TEXT("El 4 (Aplaudir) es oculto"), IsHiddenEmote(4));
	TestTrue(TEXT("El 7 (Señalar) es oculto"), IsHiddenEmote(7));
	for (const int32 InWheel : { 0, 1, 2, 3, 5, 6, 8, 9 })
	{
		TestFalse(*FString::Printf(TEXT("El %d es de la rueda"), InWheel), IsHiddenEmote(InWheel));
	}
	TestFalse(TEXT("El del derribo (100) no es oculto"), IsHiddenEmote(100));
	TestFalse(TEXT("Sin emote (-1) no es oculto"), IsHiddenEmote(-1));

	TestEqual(TEXT("La primera vez sale el 4"), HiddenEmoteForTurn(0), 4);
	TestEqual(TEXT("La segunda, el 7"), HiddenEmoteForTurn(1), 7);
	TestEqual(TEXT("La tercera vuelve al 4"), HiddenEmoteForTurn(2), 4);
	TestEqual(TEXT("Y así sin parar"), HiddenEmoteForTurn(101), 7);
	for (int32 Turn = 0; Turn < 6; ++Turn)
	{
		TestTrue(TEXT("Siempre sale un emote oculto"), IsHiddenEmote(HiddenEmoteForTurn(Turn)));
	}
	TestTrue(TEXT("Tienen enfriamiento en el servidor"), CooldownSeconds > 0.f);
	return true;
}

#endif
