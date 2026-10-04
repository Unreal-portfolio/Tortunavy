// Dorado del reparto de la carrera (TNBeachLayout::GenerateRound): 24 semillas × 3 dificultades con su huella (hash) y el
// hash del relieve fijo. Cualquier cambio del reparto, aunque sea de un milímetro, cambia la huella de su semilla: así un
// arreglo dice exactamente qué rondas toca y el relieve (que no depende de la ronda) se queda como está.
//
// Si un cambio del reparto es intencionado, se regenera el dorado en un commit aparte que explique por qué: el test, al
// fallar, escribe en el registro la tabla nueva lista para pegar (LogAutomationTest, «Dorado nuevo»).
// Correr: Automation RunTests Tortunabo.Beach.Golden

#include "Misc/AutomationTest.h"
#include "World/Beach/TN_BeachLayout.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNBeachGoldenTest
{
	constexpr int32 NumSeeds = 24;
	constexpr int32 NumDifficulties = 3;
	const ETNProcDifficulty Difficulties[NumDifficulties] = { ETNProcDifficulty::Easy, ETNProcDifficulty::Normal, ETNProcDifficulty::Hard };

	/** Las semillas del dorado: las mismas que la prueba de reglas (Tortunabo.Beach.Layout.Rules). */
	int32 SeedAt(int32 Index)
	{
		return 1000 + Index * 7919;
	}

	/** FNV-1a de 64 bits sobre enteros (las medidas, redondeadas al milímetro: la huella no depende del último bit). */
	struct FHash
	{
		uint64 Value = 1469598103934665603ull;

		void Add(int64 V)
		{
			for (int32 Byte = 0; Byte < 8; ++Byte)
			{
				Value ^= static_cast<uint64>((V >> (Byte * 8)) & 0xFF);
				Value *= 1099511628211ull;
			}
		}

		/** Una medida en cm, al milímetro. */
		void AddCm(double V) { Add(static_cast<int64>(FMath::RoundToDouble(V * 10.0))); }

		/** Un valor sin unidad (giros, escalas), a la diezmilésima. */
		void AddFine(double V) { Add(static_cast<int64>(FMath::RoundToDouble(V * 10000.0))); }

		void AddVec(const FVector2D& V)
		{
			AddCm(V.X);
			AddCm(V.Y);
		}
	};

	uint64 LayoutHash(const TNBeachLayout::FRoundLayout& L)
	{
		FHash H;
		H.Add(L.Items.Num());
		for (const TNBeachLayout::FItem& It : L.Items)
		{
			H.Add(static_cast<int64>(It.Element));
			H.AddVec(It.Pos);
			H.AddFine(It.Yaw);
			H.AddCm(It.Radius);
			H.AddCm(It.Core);
			H.AddCm(It.HalfLength);
			H.Add(It.Spec.Seed);
			H.AddFine(It.Spec.SizeScale);
			H.AddCm(It.Spec.Extent);
			H.Add(static_cast<int64>(It.Role));
			H.Add((It.bBlocking ? 1 : 0) | (It.bOverlay ? 2 : 0));
		}
		H.Add(L.Stamps.Num());
		H.Add(L.Interest.Num());
		for (const TNBeachLayout::FInterestPoint& Point : L.Interest)
		{
			H.Add(static_cast<int64>(Point.Kind));
			H.AddVec(Point.Pos);
			H.AddVec(Point.To);
			H.AddCm(Point.Height);
			H.Add(static_cast<int64>(Point.Source));
		}
		H.AddVec(L.DungeonPos);
		H.Add(L.DungeonGapSide);
		H.Add(L.bPassageOk ? 1 : 0);
		return H.Value;
	}

	/** El relieve fijo (arena, bancos, crestas, pozas, trincheras, repisa y fondo) cada 5 m, con 30-60 m de margen. */
	uint64 HeightfieldHash()
	{
		FHash H;
		for (double X = -3000.0; X <= TNBeachLayout::Length + 3000.0; X += 500.0)
		{
			for (double Y = -TNBeachLayout::HalfWidth - 6000.0; Y <= TNBeachLayout::HalfWidth + 6000.0; Y += 500.0)
			{
				H.AddCm(TNBeachLayout::SurfaceZ(X, Y));
			}
		}
		return H.Value;
	}

	/**
	 * Huellas del reparto por dificultad (Fácil, Normal, Difícil) y semilla (SeedAt). Regeneradas en un commit aparte cada
	 * vez que el reparto cambia a propósito (ver el historial del fichero).
	 */
	const uint64 GoldenLayout[NumDifficulties][NumSeeds] = {
		{ 0xECABA5FCDBFCA247ull, 0xB473E92B28694917ull, 0x291DC11C7C7A0B2Aull, 0xCECB0BA297B707CCull,
		  0xA8C0F867903C419Eull, 0x17C546F087426895ull, 0xEDFF12CAE2611633ull, 0x6304428C6A9B3603ull,
		  0xF6F706C3F43D8E29ull, 0x33008A9DDA2E450Aull, 0xA91E8DB73B36C11Bull, 0x029E99CABBC555ECull,
		  0x9CC983424C3EC5D8ull, 0x76E79F028D5DEFADull, 0xDD406E5D23316A91ull, 0xE0DA99A76425DFEFull,
		  0x5DCE421ACF33FCAAull, 0x90922168865262F4ull, 0x4ED08E78D2480FE6ull, 0x96CD095A90651F46ull,
		  0xB2B14FFC483062F5ull, 0x1219D66EA656CD10ull, 0x94811A30B2E2A567ull, 0x05DBF499F0FC0071ull },
		{ 0x55B834D8743323BDull, 0x1C1C44F2BBCEF65Bull, 0x2677DC01E04246A2ull, 0xB893556775C4BC2Eull,
		  0xD3A8B296889AE378ull, 0x25AFA38C64C2A483ull, 0xA10FAEAADC3E0C81ull, 0x7733089ED1593842ull,
		  0x4AFF8B6F2EA9D400ull, 0xFFE398A8437CA245ull, 0xB025D8746B9F07A5ull, 0xDADB27C21FB746EFull,
		  0xD44BB40F6B636349ull, 0xD2A2CACE94E16FF8ull, 0x1F2CAB3B5A7E07A7ull, 0x65235A232D01F89Full,
		  0xFF24200A1DABD319ull, 0x76C8C8037F863613ull, 0x82530B491C3474BFull, 0x58BCFECF09EE0023ull,
		  0x6EB53ED1E7C8D5DBull, 0x209B4714574787B0ull, 0x7A7C1859B796BD62ull, 0x4530D597351CDD49ull },
		{ 0x3917E62F40F919EDull, 0x5A05184C1A830573ull, 0x645D6A990E4A5E9Full, 0xA749E4123745BC37ull,
		  0x4B21921C495A9914ull, 0xA2E99695AA048886ull, 0xFC703E2EA5B69BF8ull, 0x80E3091C67C2BEBDull,
		  0x469C658387013654ull, 0xF22D9ECB79BC4CE1ull, 0xD53136348F790730ull, 0xACE7A271A7B9108Dull,
		  0x04D22EC38134F8D4ull, 0xED6851D8BB8824BDull, 0x632C6CFE7DAC55E6ull, 0x2E3BDADE4498EE4Cull,
		  0xBC28C6C2B7583444ull, 0xB2630AB9AB8CE5B8ull, 0x334C247D5497884Aull, 0xB958A2EF1FC10407ull,
		  0x691636C3AB502E58ull, 0x3BF7DC533A371437ull, 0x10A09887BDCBCDE4ull, 0x8E7EB4D576DA65EDull },
	};

	const uint64 GoldenHeightfield = 0x760728A54DE825B7ull;

	FString FormatTable(const uint64 (&Table)[NumDifficulties][NumSeeds])
	{
		FString Out = TEXT("\n\tconst uint64 GoldenLayout[NumDifficulties][NumSeeds] = {\n");
		for (int32 d = 0; d < NumDifficulties; ++d)
		{
			Out += TEXT("\t\t{");
			for (int32 s = 0; s < NumSeeds; ++s)
			{
				Out += FString::Printf(TEXT("%s0x%016llXull"), s == 0 ? TEXT(" ") : (s % 4 == 0 ? TEXT(",\n\t\t  ") : TEXT(", ")), Table[d][s]);
			}
			Out += TEXT(" },\n");
		}
		Out += TEXT("\t};\n");
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachLayoutGoldenTest,
	"Tortunabo.Beach.Golden.Layout",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachLayoutGoldenTest::RunTest(const FString& Parameters)
{
	using namespace TNBeachGoldenTest;
	uint64 Actual[NumDifficulties][NumSeeds] = {};
	int32 Changed = 0;
	FString ChangedList;
	for (int32 d = 0; d < NumDifficulties; ++d)
	{
		for (int32 s = 0; s < NumSeeds; ++s)
		{
			TNBeachLayout::FRoundLayout L;
			TNBeachLayout::GenerateRound(SeedAt(s), Difficulties[d], L);
			Actual[d][s] = LayoutHash(L);
			if (Actual[d][s] != GoldenLayout[d][s])
			{
				++Changed;
				ChangedList += FString::Printf(TEXT(" %d/%d"), d, SeedAt(s));
			}
		}
	}
	if (Changed > 0)
	{
		AddError(FString::Printf(TEXT("El reparto ha cambiado en %d de %d rondas (dificultad/semilla):%s"), Changed, NumDifficulties * NumSeeds, *ChangedList));
		AddInfo(TEXT("Dorado nuevo:") + FormatTable(Actual));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBeachHeightfieldGoldenTest,
	"Tortunabo.Beach.Golden.Heightfield",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBeachHeightfieldGoldenTest::RunTest(const FString& Parameters)
{
	// El relieve no depende de la ronda (TerrainSeed es fija): ningún cambio del reparto lo puede tocar.
	const uint64 Actual = TNBeachGoldenTest::HeightfieldHash();
	TestEqual(FString::Printf(TEXT("hash del relieve fijo (nuevo: 0x%016llXull)"), Actual), Actual, TNBeachGoldenTest::GoldenHeightfield);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
