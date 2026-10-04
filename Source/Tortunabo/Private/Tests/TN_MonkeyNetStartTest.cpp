// Arranque del monkey en un cliente remoto (Testing/TN_MonkeyNetStart.h, issue #80): qué mundo elige -TNMonkeyNet y cuándo
// un proceso se reconoce como cliente por su primer argumento. Docs/Pruebas_Red_Local.md.

#include "Misc/AutomationTest.h"
#include "Testing/TN_MonkeyNetStart.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMonkeyNetStartAddressTest, "Tortunabo.Monkey.NetStart.Address",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMonkeyNetStartAddressTest::RunTest(const FString& Parameters)
{
	using TNMonkey::LooksLikeServerAddress;
	TestTrue(TEXT("127.0.0.1 es una dirección"), LooksLikeServerAddress(TEXT("127.0.0.1")));
	TestTrue(TEXT("Con puerto también"), LooksLikeServerAddress(TEXT("192.168.1.20:7777")));
	TestTrue(TEXT("localhost"), LooksLikeServerAddress(TEXT("localhost:7777")));
	TestTrue(TEXT("Steam"), LooksLikeServerAddress(TEXT("steam.76561198000000000")));
	TestFalse(TEXT("Un mapa corto no"), LooksLikeServerAddress(TEXT("LVL_BeachRace")));
	TestFalse(TEXT("Un mapa con ruta no"), LooksLikeServerAddress(TEXT("/Game/Maps/Run/LVL_BeachRace")));
	TestFalse(TEXT("Un mapa con ?listen no"), LooksLikeServerAddress(TEXT("LVL_BeachRace?listen")));
	TestFalse(TEXT("Octeto fuera de rango"), LooksLikeServerAddress(TEXT("300.0.0.1")));
	TestFalse(TEXT("Tres octetos no"), LooksLikeServerAddress(TEXT("10.0.1")));
	TestFalse(TEXT("Puerto no numérico"), LooksLikeServerAddress(TEXT("127.0.0.1:abc")));
	TestFalse(TEXT("Vacío"), LooksLikeServerAddress(TEXT("")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNMonkeyNetStartFilterTest, "Tortunabo.Monkey.NetStart.Filter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNMonkeyNetStartFilterTest::RunTest(const FString& Parameters)
{
	using namespace TNMonkey;
	TestEqual(TEXT("Primer argumento: salta el .uproject y las opciones"),
		FirstUrlToken(TEXT("\"C:/x/Tortunabo.uproject\" -game 127.0.0.1 -nullrhi")), FString(TEXT("127.0.0.1")));
	TestEqual(TEXT("Sin mapa ni dirección: vacío"), FirstUrlToken(TEXT("-game -nullrhi")), FString());

	TestTrue(TEXT("Cliente por dirección: espera al mundo de cliente"), ResolveNetFilter(TEXT(""), TEXT("127.0.0.1")) == ENetFilter::Client);
	TestTrue(TEXT("Servidor con mapa: cualquiera"), ResolveNetFilter(TEXT(""), TEXT("LVL_BeachRace?listen")) == ENetFilter::Any);
	TestTrue(TEXT("-TNMonkeyNet manda sobre la dirección"), ResolveNetFilter(TEXT("any"), TEXT("127.0.0.1")) == ENetFilter::Any);
	TestTrue(TEXT("-TNMonkeyNet=server"), ResolveNetFilter(TEXT("Server"), TEXT("LVL_BeachRace")) == ENetFilter::Server);
	TestTrue(TEXT("Valor no válido: se deduce"), ResolveNetFilter(TEXT("cliente"), TEXT("127.0.0.1")) == ENetFilter::Client);

	TestFalse(TEXT("Cliente: no en el menú (Standalone)"), ShouldStartIn(ENetFilter::Client, NM_Standalone));
	TestTrue(TEXT("Cliente: sí en el mundo conectado"), ShouldStartIn(ENetFilter::Client, NM_Client));
	TestTrue(TEXT("Servidor: listen"), ShouldStartIn(ENetFilter::Server, NM_ListenServer));
	TestFalse(TEXT("Servidor: no en un cliente"), ShouldStartIn(ENetFilter::Server, NM_Client));
	TestFalse(TEXT("Servidor: no en Standalone"), ShouldStartIn(ENetFilter::Server, NM_Standalone));
	TestTrue(TEXT("Cualquiera: Standalone"), ShouldStartIn(ENetFilter::Any, NM_Standalone));
	return true;
}

#endif
