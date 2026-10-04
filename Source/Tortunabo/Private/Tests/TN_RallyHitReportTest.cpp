// Confirmación de impactos del Rally (#332, Rally/TN_RallyHitReport.h): líneas del registro, sus 3 últimas y la marca de
// la mira. Sin mundo. Headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.Rally.HitReport; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"
#include "Rally/TN_RallyHitReport.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNRallyHitReportTest
{
	FTNRallyHitReport Make(ETNRallyAmmo Ammo, ETNRallyHitZone Zone, bool bOutgoing, bool bBlocked)
	{
		FTNRallyHitReport Report;
		Report.Ammo = Ammo;
		Report.Zone = Zone;
		Report.bOutgoing = bOutgoing;
		Report.bBlocked = bBlocked;
		return Report;
	}

	TNRallyHitLog::FLine Line(const TCHAR* Text)
	{
		TNRallyHitLog::FLine Out;
		Out.Text = FText::AsCultureInvariant(Text);
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyHitReportLinesTest,
	"Tortunabo.Rally.HitReport.Lines",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyHitReportLinesTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyHitLog;
	using TNRallyHitReportTest::Make;
	const FText Rival = FText::AsCultureInvariant(TEXT("Rubi"));

	const FString Out = LineFor(Make(ETNRallyAmmo::Coco, ETNRallyHitZone::Front, true, false), Rival).ToString();
	TestTrue(TEXT("al dar: la munición"), Out.Contains(AmmoName(ETNRallyAmmo::Coco).ToString()));
	TestTrue(TEXT("al dar: a quién"), Out.Contains(TEXT("Rubi")));
	TestTrue(TEXT("al dar: la zona"), Out.Contains(ZoneName(ETNRallyHitZone::Front).ToString()));

	const FString In = LineFor(Make(ETNRallyAmmo::Tinta, ETNRallyHitZone::Rear, false, false), FText::GetEmpty()).ToString();
	TestTrue(TEXT("al recibir: la munición"), In.Contains(AmmoName(ETNRallyAmmo::Tinta).ToString()));
	TestTrue(TEXT("al recibir: la zona"), In.Contains(ZoneName(ETNRallyHitZone::Rear).ToString()));
	const FString InFrom = LineFor(Make(ETNRallyAmmo::Tinta, ETNRallyHitZone::Rear, false, false), Rival).ToString();
	TestTrue(TEXT("al recibir, con quién"), InFrom.Contains(TEXT("Rubi")) && InFrom != In);
	TestNotEqual(TEXT("dar y recibir se distinguen"), LineFor(Make(ETNRallyAmmo::Coco, ETNRallyHitZone::Side, true, false), Rival).ToString(),
		LineFor(Make(ETNRallyAmmo::Coco, ETNRallyHitZone::Side, false, false), Rival).ToString());

	const FString Blocked = LineFor(Make(ETNRallyAmmo::Coco, ETNRallyHitZone::Front, true, true), Rival).ToString();
	TestFalse(TEXT("con escudo no dice la zona"), Blocked.Contains(ZoneName(ETNRallyHitZone::Front).ToString()));
	TestNotEqual(TEXT("con escudo cambia la línea"), Blocked, Out);

	TestNotEqual(TEXT("morro y cola se distinguen"), ZoneName(ETNRallyHitZone::Front).ToString(), ZoneName(ETNRallyHitZone::Rear).ToString());
	TestNotEqual(TEXT("lateral tiene nombre propio"), ZoneName(ETNRallyHitZone::Side).ToString(), ZoneName(ETNRallyHitZone::Front).ToString());
	for (const ETNRallyAmmo Ammo : { ETNRallyAmmo::Coco, ETNRallyAmmo::Alga, ETNRallyAmmo::Mortero, ETNRallyAmmo::Tinta, ETNRallyAmmo::Ancla })
	{
		TestFalse(TEXT("cada munición que da tiene nombre"), AmmoName(Ammo).IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyHitReportLogTest,
	"Tortunabo.Rally.HitReport.Log",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyHitReportLogTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyHitLog;
	using TNRallyHitReportTest::Line;
	TArray<FLine> Log;
	for (const TCHAR* Text : { TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4") })
	{
		Log = Push(Log, Line(Text));
	}
	TestEqual(TEXT("como mucho 3 líneas"), Log.Num(), MaxLines);
	TestEqual(TEXT("la más nueva arriba"), Log[0].Text.ToString(), FString(TEXT("4")));
	TestEqual(TEXT("la más vieja que queda, abajo"), Log[2].Text.ToString(), FString(TEXT("2")));

	TArray<FLine> Older = Age(Log, LineSeconds * 0.5f);
	TestEqual(TEXT("a media vida siguen todas"), Older.Num(), 3);
	TestEqual(TEXT("envejecen"), Older[0].Age, LineSeconds * 0.5f, 1e-4f);
	Older = Push(Older, Line(TEXT("5")));
	Older = Age(Older, LineSeconds * 0.6f);
	TestEqual(TEXT("se apagan las que pasan de su vida"), Older.Num(), 1);
	TestEqual(TEXT("queda la nueva"), Older[0].Text.ToString(), FString(TEXT("5")));
	TestEqual(TEXT("un paso negativo no rejuvenece"), Age(Log, -5.f)[0].Age, 0.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNRallyHitReportMarkerTest,
	"Tortunabo.Rally.HitReport.Marker",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNRallyHitReportMarkerTest::RunTest(const FString& Parameters)
{
	using namespace TNRallyHitLog;
	using TNRallyHitReportTest::Make;
	TestTrue(TEXT("al dar: marca"), ShowsMarker(Make(ETNRallyAmmo::Coco, ETNRallyHitZone::Front, true, false)));
	TestTrue(TEXT("al dar contra un escudo: marca (celeste)"), ShowsMarker(Make(ETNRallyAmmo::Coco, ETNRallyHitZone::Front, true, true)));
	TestFalse(TEXT("al recibir: sin marca"), ShowsMarker(Make(ETNRallyAmmo::Coco, ETNRallyHitZone::Front, false, false)));
	TestEqual(TEXT("la marca dura 0,3 s"), MarkerSeconds, 0.3f, 1e-4f);
	return true;
}

#endif
