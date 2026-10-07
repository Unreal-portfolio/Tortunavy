// Dorado del reparto de la playa (TNBeachLayout::GenerateRound): 24 semillas × 3 dificultades con su huella (hash) y el
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
		{ 0x71C0B95A6B4761AEull, 0xB9A247E3F65021EBull, 0xF110FFE3B5299017ull, 0x36453C4999B332E8ull,
		  0x1892732460BAF85Aull, 0xECAC01E4213397D5ull, 0xA26CEF39A58581B5ull, 0x727CD96AA224E2B1ull,
		  0x54602A1418162FB4ull, 0x51776B5EDAE5701Bull, 0xA1D469720A31820Dull, 0xC8E09B3D7FCD4765ull,
		  0xA39393549874EBD9ull, 0x9FF49BF974C74E00ull, 0xDB066F4CDFB615F1ull, 0xD22524188CE0A236ull,
		  0x397450820D7AA67Bull, 0x06808CC6F9CA405Dull, 0x97DA65A9A0E19F79ull, 0xF5C43C8D17B52970ull,
		  0x95476C5AD1803C17ull, 0xB7EFBA55BDFC495Dull, 0x09EB6CC6C2485ADCull, 0x9CD29BE396E1FEBFull },
		{ 0x0810D3BC5E905770ull, 0xF043155597A3419Full, 0x88A880CB47DA9976ull, 0x16FE24FD949E0BF0ull,
		  0xFEF97727C8206693ull, 0xE79D49FF57499B9Bull, 0xAD16269E7D030993ull, 0x5877B41E73EA7BB2ull,
		  0xC58F602969AB1EC7ull, 0x6363499741BB0320ull, 0xF9E716FFDCDF5038ull, 0x8B3CA5844E1F9987ull,
		  0x9F2250D5100E0D4Dull, 0xAA4E4899DF19F21Eull, 0xBAC87432EE544C86ull, 0x20DAA48E36507074ull,
		  0x58050971DB09155Bull, 0x803D8F55C7EAB805ull, 0x1DECDD810C91F965ull, 0xD9A6CC4D4AD0D3CFull,
		  0xD8FF6BB5BA394D26ull, 0x0442BABC7C94305Cull, 0x5E8E20B9960D408Bull, 0xB8E3FB50AA79B021ull },
		{ 0x7A65F4FDB59A5B92ull, 0x5AAB111AF77190EAull, 0x19ABBC842EE6A74Cull, 0x669E01DBADC0B06Full,
		  0x657062E0EB2BC7F7ull, 0xAA04522887DE3834ull, 0x9C73C68EF225DFB4ull, 0xB3F912C639062687ull,
		  0xA6ADD3D336365907ull, 0x2CDB9C769FC4AF7Eull, 0xBECDE7FBF7F8958Full, 0xD93B16B4432610F1ull,
		  0x94EE01E98AE2EC9Dull, 0xA6C324178DF5E150ull, 0x4A59FD190F4139BEull, 0x3607DEF0BA411168ull,
		  0xB7E5F2835B2EF764ull, 0x1C3F2EA2061FA7D3ull, 0xA470FE69E612BECDull, 0x3277571EAA02E670ull,
		  0xDD8E35E71AAE3211ull, 0xAE88CA2059E98DBBull, 0xA98661F8F0EAA3CBull, 0xC5194C1AAF7F9FBDull },
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
