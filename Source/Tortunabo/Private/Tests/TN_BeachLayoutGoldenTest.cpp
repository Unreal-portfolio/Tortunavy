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
		{ 0xC96DAB3D413A5FC4ull, 0x4390006E50EC8917ull, 0xCBD6594D1FA0CD23ull, 0x2E7067FC10017364ull,
		  0x92DB2D47AC95A371ull, 0x063A77D08B605B7Cull, 0x6ADB1A965982B6C0ull, 0x130DC012AAF6893Dull,
		  0xF495C61C789382DDull, 0xD20E1D08E5636431ull, 0x143B8906EDDB0839ull, 0xA1884D96FA6AB890ull,
		  0x8A18B4736367AE4Aull, 0xC90FDCF4C5D6C64Cull, 0xA5EBBA8924CAE00Bull, 0x150877CB7ECB1915ull,
		  0x4CBE3F131C2B7D21ull, 0xCD82396136F4A95Dull, 0xEEBA95CFE555F7BCull, 0x04D8D04EA86C06A2ull,
		  0xD6E46C41A9D56C99ull, 0xFB24047057B8F6FDull, 0x307A5AD1437800BBull, 0x66280D74402F0BF2ull },
		{ 0x85553B61EE9C8A30ull, 0xC2336C7E5FE400FEull, 0xF83BCCE1D2A0ACBFull, 0xD60118BB349B8CD3ull,
		  0xBABA9F54063CB577ull, 0xC075401ADB2CAAB7ull, 0xE915B8638D9EEB5Eull, 0xD34E572AB3B9C5DDull,
		  0xC8EEA02A63548E95ull, 0xACCAF29E0F5130ACull, 0x7126CB4603664028ull, 0xBD2098EB4CECF19Bull,
		  0x3AA18CDA6DAE9E80ull, 0xA4C5AA5C943130FFull, 0x3D77BB739B0A5ACCull, 0x141BDE25969F4E01ull,
		  0xC03FD6143B76C0D0ull, 0x36277C22E1539F91ull, 0x1208869793CEAEAFull, 0xC24FA6CD9AAB8971ull,
		  0x4388D86DB6EE26DCull, 0x3890D53C7BEA3368ull, 0x58AECEB699C7B12Cull, 0xFD13C3B9C7DE9721ull },
		{ 0xD67D17BFC512C1AEull, 0x8DAFAFE0564F658Cull, 0xB09CD41F7B26D251ull, 0x5ADE34F320C582EEull,
		  0xD4148C658AED9557ull, 0xF7B927E8C35CD1E2ull, 0x4EC72693FF5940EDull, 0xB47E1661E70311D7ull,
		  0xCFD1B27E17923011ull, 0xFA2DD01BEF6EE69Dull, 0x2581DF493C97D46Bull, 0x1654F7F131F498D2ull,
		  0xAD36FDE03F7E1DB5ull, 0x1C34ED922B181EF7ull, 0x1A04C57BAFF3E3F9ull, 0x175041D12946415Bull,
		  0xC737104E1282CDA7ull, 0x82F41B4F5F6DF712ull, 0x4F94143D6BF1E4B7ull, 0x66C2AC56E79E7AD1ull,
		  0x93ED5159947D0A6Bull, 0xFE5C23E6C7BE3770ull, 0xD5B573B424C21E0Aull, 0xB1BF58BD17DD6911ull },
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
