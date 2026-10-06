// Reglas del modo local (#311): TNLocalPlay (Multiplayer/TN_LocalPlayRules.h). Reparto de la pantalla, límite de cuatro,
// quién se une y con qué mando, qué se guarda y qué ajustes son de cada jugador. Correr desde Session Frontend (categoría
// "Tortunabo.LocalPlay") o headless:
//   UnrealEditor-Win64-DebugGame-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.LocalPlay; Quit" -NullRHI -unattended

#include "Misc/AutomationTest.h"
#include "Multiplayer/TN_LocalPlayRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNLocalPlayRulesTest
{
	/** Área que comparten dos trozos de pantalla (0 si no se tocan). */
	float Overlap(const TNLocalPlay::FViewRect& A, const TNLocalPlay::FViewRect& B)
	{
		const float W = FMath::Min(A.X + A.W, B.X + B.W) - FMath::Max(A.X, B.X);
		const float H = FMath::Min(A.Y + A.H, B.Y + B.H) - FMath::Max(A.Y, B.Y);
		return W > 0.f && H > 0.f ? W * H : 0.f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNLocalPlaySplitLayoutTest,
	"Tortunabo.LocalPlay.SplitLayout",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNLocalPlaySplitLayoutTest::RunTest(const FString& Parameters)
{
	using namespace TNLocalPlay;
	using TNLocalPlayRulesTest::Overlap;

	const TArray<FViewRect> One = SplitLayout(1);
	TestEqual(TEXT("Uno: una vista"), One.Num(), 1);
	TestTrue(TEXT("Uno: la pantalla entera"), One[0] == FViewRect{ 0.f, 0.f, 1.f, 1.f });

	const TArray<FViewRect> Two = SplitLayout(2);
	TestEqual(TEXT("Dos: dos vistas"), Two.Num(), 2);
	TestTrue(TEXT("Dos: el 1 arriba, a lo ancho"), Two[0] == FViewRect{ 0.f, 0.f, 1.f, 0.5f });
	TestTrue(TEXT("Dos: el 2 abajo, a lo ancho"), Two[1] == FViewRect{ 0.f, 0.5f, 1.f, 0.5f });

	const TArray<FViewRect> Three = SplitLayout(3);
	TestEqual(TEXT("Tres: tres vistas"), Three.Num(), 3);
	TestTrue(TEXT("Tres: el 1 arriba a la izquierda"), Three[0] == FViewRect{ 0.f, 0.f, 0.5f, 0.5f });
	TestTrue(TEXT("Tres: el 2 arriba a la derecha"), Three[1] == FViewRect{ 0.5f, 0.f, 0.5f, 0.5f });
	TestTrue(TEXT("Tres: el 3 abajo a la izquierda"), Three[2] == FViewRect{ 0.f, 0.5f, 0.5f, 0.5f });
	FViewRect Empty;
	TestTrue(TEXT("Tres: sobra un cuadrante"), EmptyQuadrant(3, Empty));
	TestTrue(TEXT("Tres: el libre es el de abajo a la derecha"), Empty == FViewRect{ 0.5f, 0.5f, 0.5f, 0.5f });

	const TArray<FViewRect> Four = SplitLayout(4);
	TestEqual(TEXT("Cuatro: cuatro vistas"), Four.Num(), 4);
	TestTrue(TEXT("Cuatro: el 4 abajo a la derecha"), Four[3] == FViewRect{ 0.5f, 0.5f, 0.5f, 0.5f });
	TestFalse(TEXT("Cuatro: no sobra ninguno"), EmptyQuadrant(4, Empty));
	TestFalse(TEXT("Dos: no sobra ninguno"), EmptyQuadrant(2, Empty));

	// Nunca se pisan y, con 1, 2 y 4, tapan la pantalla entera (con 3, todo menos el cuadrante libre).
	for (int32 Players = 1; Players <= MaxPlayers; ++Players)
	{
		const TArray<FViewRect> Rects = SplitLayout(Players);
		float Area = 0.f;
		for (int32 i = 0; i < Rects.Num(); ++i)
		{
			Area += Rects[i].W * Rects[i].H;
			TestTrue(FString::Printf(TEXT("%d jugadores: la vista %d está dentro de la pantalla"), Players, i + 1),
				Rects[i].X >= 0.f && Rects[i].Y >= 0.f && Rects[i].X + Rects[i].W <= 1.f + KINDA_SMALL_NUMBER && Rects[i].Y + Rects[i].H <= 1.f + KINDA_SMALL_NUMBER);
			for (int32 j = i + 1; j < Rects.Num(); ++j)
			{
				TestEqual(FString::Printf(TEXT("%d jugadores: las vistas %d y %d no se pisan"), Players, i + 1, j + 1), Overlap(Rects[i], Rects[j]), 0.f);
			}
		}
		TestEqual(FString::Printf(TEXT("%d jugadores: área cubierta"), Players), Area, Players == 3 ? 0.75f : 1.f, KINDA_SMALL_NUMBER);
	}

	// Fuera de rango: se recorta a 1..4.
	TestEqual(TEXT("Cero jugadores: como uno"), SplitLayout(0).Num(), 1);
	TestEqual(TEXT("Seis jugadores: como cuatro"), SplitLayout(6).Num(), MaxPlayers);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNLocalPlayJoinTest,
	"Tortunabo.LocalPlay.Join",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNLocalPlayJoinTest::RunTest(const FString& Parameters)
{
	using namespace TNLocalPlay;
	TestEqual(TEXT("Hasta cuatro jugadores locales"), MaxPlayers, 4);

	FJoinQuery Query;
	Query.bLocalMode = true;
	Query.bInLobby = true;
	Query.Players = 1;
	Query.bGamepad = true;
	TestTrue(TEXT("Un mando libre en el lobby: entra"), DecideJoin(Query) == EJoin::Accept);

	Query.Players = 3;
	TestTrue(TEXT("Con tres: entra el cuarto"), DecideJoin(Query) == EJoin::Accept);
	Query.Players = 4;
	TestTrue(TEXT("Con cuatro: no entra nadie más"), DecideJoin(Query) == EJoin::Full);
	Query.Players = 7;
	TestTrue(TEXT("Más de cuatro (no debería pasar): lleno"), DecideJoin(Query) == EJoin::Full);
	Query.Players = 2;

	Query.bGamepad = false;
	TestTrue(TEXT("El teclado nunca crea otro jugador"), DecideJoin(Query) == EJoin::Keyboard);
	Query.bGamepad = true;

	Query.bDeviceHasPlayer = true;
	TestTrue(TEXT("Un mando que ya juega (también el del jugador 1) no crea otra tortuga"), DecideJoin(Query) == EJoin::AlreadyPlaying);
	Query.bDeviceHasPlayer = false;

	Query.bInLobby = false;
	TestTrue(TEXT("En una partida empezada no se entra"), DecideJoin(Query) == EJoin::NotLobby);
	Query.bInLobby = true;

	// Con las gafas de VR activas no entra nadie (#639): en el lobby se rechaza con su propio motivo, que es el que avisa.
	Query.bVR = true;
	TestTrue(TEXT("Con gafas, un mando libre en el lobby no entra"), DecideJoin(Query) == EJoin::VR);
	Query.Players = 1;
	TestTrue(TEXT("Con gafas, tampoco el segundo jugador"), DecideJoin(Query) == EJoin::VR);
	Query.bInLobby = false;
	TestTrue(TEXT("Con gafas fuera del lobby: el motivo es el lobby y no se avisa de las gafas"), DecideJoin(Query) == EJoin::NotLobby);
	Query.bInLobby = true;
	Query.bDeviceHasPlayer = true;
	TestTrue(TEXT("Con gafas, el mando del jugador 1 sigue siendo suyo"), DecideJoin(Query) == EJoin::AlreadyPlaying);
	Query.bDeviceHasPlayer = false;
	Query.bGamepad = false;
	TestTrue(TEXT("Con gafas, el teclado sigue sin crear jugador"), DecideJoin(Query) == EJoin::Keyboard);
	Query.bGamepad = true;
	Query.bVR = false;
	TestTrue(TEXT("Sin gafas, el modo local no cambia"), DecideJoin(Query) == EJoin::Accept);
	Query.Players = 2;

	Query.bLocalMode = false;
	TestTrue(TEXT("En red no se entra con Start"), DecideJoin(Query) == EJoin::NotLocal);

	// Números de los invitados: el más bajo libre.
	TestEqual(TEXT("Primer invitado: el 2"), NextGuestNumber({}), 2);
	TestEqual(TEXT("Con el 2 y el 4: el 3"), NextGuestNumber({ 2, 4 }), 3);
	TestEqual(TEXT("Se fue el 2: vuelve a ser el 2"), NextGuestNumber({ 3, 4 }), 2);
	TestEqual(TEXT("Con los tres: ninguno"), NextGuestNumber({ 2, 3, 4 }), INDEX_NONE);

	// Salir: los invitados, en el lobby; el jugador 1, nunca.
	TestTrue(TEXT("Un invitado sale en el lobby"), CanLeave(true, true, false));
	TestFalse(TEXT("Un invitado no sale en una partida empezada"), CanLeave(true, false, false));
	TestFalse(TEXT("El jugador 1 no sale con B"), CanLeave(true, true, true));
	TestFalse(TEXT("En red no hay invitados locales"), CanLeave(false, true, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNLocalPlayPadsTest,
	"Tortunabo.LocalPlay.Pads",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNLocalPlayPadsTest::RunTest(const FString& Parameters)
{
	using namespace TNLocalPlay;
	// El sistema da al jugador 1 el teclado y el primer mando (PrimaryUserSharesKeyboardAndFirstGamepad).
	TestTrue(TEXT("Eligió «Local» con el teclado: suelta su mando"), PadsToRelease({ 3 }, INDEX_NONE) == TArray<int32>{ 3 });
	TestEqual(TEXT("Eligió «Local» con ese mando: se lo queda"), PadsToRelease({ 3 }, 3).Num(), 0);
	TestTrue(TEXT("Eligió con uno y tenía dos: suelta el otro"), PadsToRelease({ 3, 5 }, 5) == TArray<int32>{ 3 });
	TestEqual(TEXT("Sin mandos: nada que soltar"), PadsToRelease({}, INDEX_NONE).Num(), 0);
	TestTrue(TEXT("Repetidos: una vez"), PadsToRelease({ 4, 4 }, INDEX_NONE) == TArray<int32>{ 4 });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNLocalPlaySaveTest,
	"Tortunabo.LocalPlay.Save",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNLocalPlaySaveTest::RunTest(const FString& Parameters)
{
	using namespace TNLocalPlay;
	TestTrue(TEXT("En red se guarda siempre"), ShouldSave(false, false));
	TestTrue(TEXT("En red, el anfitrión también"), ShouldSave(false, true));
	TestTrue(TEXT("En local, lo del jugador 1 se guarda"), ShouldSave(true, true));
	TestFalse(TEXT("En local, lo de un invitado dura la partida"), ShouldSave(true, false));

	TestTrue(TEXT("VR en red"), AllowsVR(false, 1));
	TestTrue(TEXT("VR en local con uno"), AllowsVR(true, 1));
	TestFalse(TEXT("VR en local con dos: no"), AllowsVR(true, 2));
	TestFalse(TEXT("VR en local con cuatro: no"), AllowsVR(true, 4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNLocalPlaySettingsTest,
	"Tortunabo.LocalPlay.Settings",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNLocalPlaySettingsTest::RunTest(const FString& Parameters)
{
	using namespace TNLocalPlay;
	// Los del PC (del jugador 1) y los de un invitado que ha tocado su cámara y sus teclas.
	FTNGameSettings Shared;
	Shared.MasterVolume = 0.4f;
	Shared.Language = TEXT("en");
	Shared.UIScale = 1.2f;
	Shared.MouseSensitivity = 2.f;
	Shared.KeyOverrides.Add(TEXT("IA_Jump#1"), TEXT("Gamepad_FaceButton_Left"));
	Shared.FieldOfViewOffset = 10.f;

	FTNGameSettings Own;
	Own.MasterVolume = 1.f;
	Own.GamepadSensitivity = 0.5f;
	Own.bInvertGamepadY = true;
	Own.FieldOfViewOffset = -5.f;
	Own.bCameraShake = false;
	Own.PausePadKey = TEXT("Gamepad_Special_Left");

	const FTNGameSettings Effective = EffectiveSettings(Shared, Own);
	TestEqual(TEXT("El volumen es del PC"), Effective.MasterVolume, 0.4f);
	TestEqual(TEXT("El idioma es del PC"), Effective.Language, FString(TEXT("en")));
	TestEqual(TEXT("El tamaño de la interfaz es del PC"), Effective.UIScale, 1.2f);
	TestEqual(TEXT("La sensibilidad es suya"), Effective.GamepadSensitivity, 0.5f);
	TestEqual(TEXT("La del ratón también (la de serie, no la del jugador 1)"), Effective.MouseSensitivity, 1.f);
	TestTrue(TEXT("Invertir es suyo"), Effective.bInvertGamepadY);
	TestEqual(TEXT("El campo de visión es suyo"), Effective.FieldOfViewOffset, -5.f);
	TestFalse(TEXT("El temblor de cámara es suyo"), Effective.bCameraShake);
	TestEqual(TEXT("Sus teclas: las de serie (las del jugador 1 no le llegan)"), Effective.KeyOverrides.Num(), 0);
	TestEqual(TEXT("Su botón del menú"), Effective.PausePadKey, FName(TEXT("Gamepad_Special_Left")));

	// Copiar lo de cada jugador no toca lo del PC.
	FTNGameSettings Target = Shared;
	CopyPerPlayerSettings(Own, Target);
	TestEqual(TEXT("Copiar lo de cada jugador deja el volumen del PC"), Target.MasterVolume, 0.4f);
	TestEqual(TEXT("Copiar lo de cada jugador lleva su campo de visión"), Target.FieldOfViewOffset, -5.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNLocalPlayViewsTest,
	"Tortunabo.LocalPlay.Views",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNLocalPlayViewsTest::RunTest(const FString& Parameters)
{
	using namespace TNLocalPlay;
	TestEqual(TEXT("Una vista: interfaz de siempre"), UIScaleForViews(1), 1.f);
	TestTrue(TEXT("Dos vistas: interfaz algo menor"), UIScaleForViews(2) < 1.f);
	TestTrue(TEXT("Cuatro vistas: interfaz aún menor"), UIScaleForViews(4) < UIScaleForViews(2));
	TestEqual(TEXT("Tres y cuatro, igual (cuadrantes)"), UIScaleForViews(3), UIScaleForViews(4));

	TestFalse(TEXT("Con una vista no se baja la calidad"), ShouldReduceQuality(1));
	TestFalse(TEXT("Con dos tampoco"), ShouldReduceQuality(2));
	TestTrue(TEXT("Con tres, sí"), ShouldReduceQuality(3));
	TestTrue(TEXT("Con cuatro, sí"), ShouldReduceQuality(4));
	TestEqual(TEXT("Épica → alta"), ReducedQualityLevel(3), 2);
	TestEqual(TEXT("Baja se queda en baja"), ReducedQualityLevel(0), 0);
	TestEqual(TEXT("Cinematográfica → épica"), ReducedQualityLevel(4), 3);
	TestEqual(TEXT("Personalizada (-1) no se toca"), ReducedQualityLevel(-1), -1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
