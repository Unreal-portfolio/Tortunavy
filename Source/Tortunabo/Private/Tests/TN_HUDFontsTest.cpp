// Fuentes de reserva por idioma (UI/HUD/TN_HUDFonts.h, Docs/Localizacion.md «Fuentes»): que los cuatro idiomas CJK encuentran
// su Noto Sans normal y negrita en Content/Slate/Fonts, que un archivo que falta no rompe nada y que la fuente compuesta de
// la interfaz las lleva. Correr desde Session Frontend (categoría "Tortunabo.UI.Fonts") o headless:
//   UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Tortunabo.UI.Fonts; Quit" -nullrhi -unattended

#include "../UI/HUD/TN_HUDFonts.h"
#include "Fonts/CompositeFont.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Settings/TN_LanguageSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TNHUDFontsTestDetail
{
	/** Los cuatro idiomas que llevan fuente propia de serie. */
	const TCHAR* const CjkCultures[] = { TEXT("ja"), TEXT("ko"), TEXT("zh-Hans"), TEXT("zh-Hant") };

	const FTNLanguageEntry* FindEntry(const TCHAR* Culture)
	{
		const int32 Index = TNLanguage::IndexOf(Culture);
		return Index == INDEX_NONE ? nullptr : &TNLanguage::GetLanguages()[Index];
	}

	/** El archivo de un peso en la fuente de reserva de una cultura dentro de la fuente compuesta (vacío si no está). */
	FString FontFileFor(const FCompositeFont& Composite, const FString& Culture, FName Weight)
	{
		for (const FCompositeSubFont& SubFont : Composite.SubTypefaces)
		{
			if (SubFont.Cultures != Culture)
			{
				continue;
			}
			for (const FTypefaceEntry& Entry : SubFont.Typeface.Fonts)
			{
				if (Entry.Name == Weight)
				{
					return Entry.Font.GetFontFilename();
				}
			}
		}
		return FString();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Resolver los archivos: los ocho Noto están y cada idioma tiene su negrita
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNHUDFontsResolveTest,
	"Tortunabo.UI.Fonts.Resolve",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNHUDFontsResolveTest::RunTest(const FString& Parameters)
{
	using namespace TNHUDFontsTestDetail;
	const FString Folder = TNHUDFonts::GetFontFolder();
	for (const TCHAR* Culture : CjkCultures)
	{
		const FTNLanguageEntry* Entry = FindEntry(Culture);
		if (!TestNotNull(FString::Printf(TEXT("%s está en la lista de idiomas"), Culture), Entry))
		{
			continue;
		}
		const TNHUDFonts::FFallbackFiles Files = TNHUDFonts::ResolveFallback(*Entry, Folder);
		TestTrue(FString::Printf(TEXT("%s: está %s en Content/Slate/Fonts"), Culture, *Entry->FontRegular), Files.bFound);
		TestEqual(FString::Printf(TEXT("%s: normal"), Culture), FPaths::GetCleanFilename(Files.Regular), Entry->FontRegular);
		TestEqual(FString::Printf(TEXT("%s: negrita propia, no la normal"), Culture), FPaths::GetCleanFilename(Files.Bold), Entry->FontBold);
		// Un .ttf de Noto CJK de verdad pesa varios MB (no un puntero de LFS ni un archivo vacío).
		TestTrue(FString::Printf(TEXT("%s: la normal es una fuente completa"), Culture), IFileManager::Get().FileSize(*Files.Regular) > 1024 * 1024);
		TestTrue(FString::Printf(TEXT("%s: la negrita es una fuente completa"), Culture), IFileManager::Get().FileSize(*Files.Bold) > 1024 * 1024);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Archivos que faltan: sin normal no hay reserva; sin negrita, la normal hace de negrita
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNHUDFontsMissingTest,
	"Tortunabo.UI.Fonts.Missing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNHUDFontsMissingTest::RunTest(const FString& Parameters)
{
	const FString Folder = TNHUDFonts::GetFontFolder();

	FTNLanguageEntry NoFont;
	NoFont.Culture = TEXT("xx");
	TestFalse(TEXT("Sin FontRegular no hay reserva"), TNHUDFonts::ResolveFallback(NoFont, Folder).bFound);

	FTNLanguageEntry Missing = NoFont;
	Missing.FontRegular = TEXT("NoExiste-Regular.ttf");
	Missing.FontBold = TEXT("NoExiste-Bold.ttf");
	TestFalse(TEXT("Con el archivo normal ausente no hay reserva"), TNHUDFonts::ResolveFallback(Missing, Folder).bFound);

	FTNLanguageEntry NoBold = NoFont;
	NoBold.FontRegular = TEXT("NotoSansJP-Regular.ttf");
	NoBold.FontBold = TEXT("NoExiste-Bold.ttf");
	const TNHUDFonts::FFallbackFiles Files = TNHUDFonts::ResolveFallback(NoBold, Folder);
	TestTrue(TEXT("Con la normal y sin la negrita sí hay reserva"), Files.bFound);
	TestEqual(TEXT("La negrita que falta la hace la normal"), Files.Bold, Files.Regular);

	TestFalse(TEXT("En otra carpeta no se encuentra"), TNHUDFonts::ResolveFallback(NoBold, FPaths::ProjectContentDir() / TEXT("NoHayFuentes")).bFound);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// La fuente compuesta de la interfaz lleva la reserva de cada idioma CJK con sus dos pesos
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNHUDFontsCompositeTest,
	"Tortunabo.UI.Fonts.Composite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNHUDFontsCompositeTest::RunTest(const FString& Parameters)
{
	using namespace TNHUDFontsTestDetail;
	const TSharedRef<const FCompositeFont> Composite = TNHUDFonts::GetComposite();
	for (const TCHAR* Culture : CjkCultures)
	{
		const FTNLanguageEntry* Entry = FindEntry(Culture);
		if (!Entry)
		{
			continue;
		}
		// Una sola fuente para la cultura: con dos (la del editor y la nuestra) el motor no garantiza cuál usa.
		const int32 Count = Composite->SubTypefaces.FilterByPredicate([Culture](const FCompositeSubFont& SubFont)
		{
			return SubFont.Cultures.Contains(Culture);
		}).Num();
		TestEqual(FString::Printf(TEXT("%s: una sola fuente de reserva para la cultura"), Culture), Count, 1);
		TestEqual(FString::Printf(TEXT("%s: Regular"), Culture), FPaths::GetCleanFilename(FontFileFor(*Composite, Culture, TEXT("Regular"))),
			Entry->FontRegular);
		// La pantalla de carga y los rótulos piden Bold y el «PUM» del huevo Black: los dos con la negrita.
		TestEqual(FString::Printf(TEXT("%s: Bold"), Culture), FPaths::GetCleanFilename(FontFileFor(*Composite, Culture, TEXT("Bold"))),
			Entry->FontBold);
		TestEqual(FString::Printf(TEXT("%s: Black"), Culture), FPaths::GetCleanFilename(FontFileFor(*Composite, Culture, TEXT("Black"))),
			Entry->FontBold);
	}
	const FSlateFontInfo Font = TNHUDFonts::Make(TEXT("Bold"), 36);
	TestTrue(TEXT("TNHUDFonts::Make usa la fuente compuesta del juego"), Font.CompositeFont.Get() == &Composite.Get());
	return true;
}

#endif
