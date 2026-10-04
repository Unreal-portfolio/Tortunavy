#include "TN_HUDFonts.h"
#include "Core/TN_Log.h"
#include "Settings/TN_LanguageSettings.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/UnicodeBlockRange.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"

// Con nombre (no anónimo): en la compilación por bloques (unity) los nombres de un espacio anónimo se ven en el resto
// del bloque.
namespace TNHUDFontsDetail
{
	/** Carpeta, dentro de Content, donde se dejan las fuentes de reserva de los idiomas (se copia tal cual al empaquetar). */
	const TCHAR* const FontFolder = TEXT("Slate/Fonts");

	void AddBlocks(FCompositeSubFont& SubFont, std::initializer_list<EUnicodeBlockRange> Blocks)
	{
		for (const EUnicodeBlockRange Block : Blocks)
		{
			SubFont.CharacterRanges.Add(FUnicodeBlockRange::GetUnicodeBlockRange(Block).Range);
		}
	}

	/** Los caracteres que cubre la fuente de un idioma según su «FontScript». */
	void AddScriptRanges(FCompositeSubFont& SubFont, const FString& Script)
	{
		if (Script.Equals(TEXT("Cyrillic"), ESearchCase::IgnoreCase))
		{
			AddBlocks(SubFont, { EUnicodeBlockRange::Cyrillic, EUnicodeBlockRange::CyrillicSupplementary, EUnicodeBlockRange::CyrillicExtendedA,
				EUnicodeBlockRange::CyrillicExtendedB, EUnicodeBlockRange::CyrillicExtendedC });
		}
		else if (Script.Equals(TEXT("Latin"), ESearchCase::IgnoreCase))
		{
			AddBlocks(SubFont, { EUnicodeBlockRange::Latin1Supplement, EUnicodeBlockRange::LatinExtendedA, EUnicodeBlockRange::LatinExtendedB,
				EUnicodeBlockRange::LatinExtendedAdditional, EUnicodeBlockRange::LatinExtendedC, EUnicodeBlockRange::LatinExtendedD,
				EUnicodeBlockRange::LatinExtendedE });
		}
		else
		{
			// «CJK» (y lo que no se reconozca): kanji, hanzi, kana, hangul y signos, como las del propio motor para ja, ko y zh.
			AddBlocks(SubFont, { EUnicodeBlockRange::CJKCompatibility, EUnicodeBlockRange::CJKCompatibilityForms, EUnicodeBlockRange::CJKCompatibilityIdeographs,
				EUnicodeBlockRange::CJKCompatibilityIdeographsSupplement, EUnicodeBlockRange::CJKRadicalsSupplement, EUnicodeBlockRange::CJKStrokes,
				EUnicodeBlockRange::CJKSymbolsAndPunctuation, EUnicodeBlockRange::CJKUnifiedIdeographs, EUnicodeBlockRange::CJKUnifiedIdeographsExtensionA,
				EUnicodeBlockRange::CJKUnifiedIdeographsExtensionB, EUnicodeBlockRange::CJKUnifiedIdeographsExtensionC,
				EUnicodeBlockRange::CJKUnifiedIdeographsExtensionD, EUnicodeBlockRange::CJKUnifiedIdeographsExtensionE,
				EUnicodeBlockRange::EnclosedCJKLettersAndMonths, EUnicodeBlockRange::Hiragana, EUnicodeBlockRange::Katakana,
				EUnicodeBlockRange::KatakanaPhoneticExtensions, EUnicodeBlockRange::Kanbun, EUnicodeBlockRange::HalfwidthAndFullwidthForms,
				EUnicodeBlockRange::HangulJamo, EUnicodeBlockRange::HangulJamoExtendedA, EUnicodeBlockRange::HangulJamoExtendedB,
				EUnicodeBlockRange::HangulCompatibilityJamo, EUnicodeBlockRange::HangulSyllables });
		}
	}

	/**
	 * Todos los nombres de peso que pide la interfaz (y los de la fuente del motor), para que la reserva responda a cualquiera:
	 * los gruesos con la negrita y el resto con la normal.
	 */
	void AddWeights(FTypeface& Typeface, const FString& Regular, const FString& Bold)
	{
		const EFontHinting Hinting = EFontHinting::Default;
		const EFontLoadingPolicy Policy = EFontLoadingPolicy::LazyLoad;
		for (const TCHAR* Name : { TEXT("Regular"), TEXT("Italic"), TEXT("Medium"), TEXT("Light"), TEXT("VeryLight") })
		{
			Typeface.AppendFont(FName(Name), Regular, Hinting, Policy);
		}
		for (const TCHAR* Name : { TEXT("Bold"), TEXT("BoldItalic"), TEXT("BoldCondensed"), TEXT("BoldCondensedItalic"), TEXT("Black"), TEXT("BlackItalic") })
		{
			Typeface.AppendFont(FName(Name), Bold, Hinting, Policy);
		}
	}

	/** Cultures de una fuente es una lista separada por «;» («zh-Hans;zh-Hant»). */
	bool CulturesInclude(const FString& Cultures, const FString& Culture)
	{
		TArray<FString> Names;
		Cultures.ParseIntoArray(Names, TEXT(";"));
		return Names.ContainsByPredicate([&Culture](const FString& Name) { return Name.TrimStartAndEnd().Equals(Culture, ESearchCase::IgnoreCase); });
	}

	TSharedRef<const FCompositeFont> Build()
	{
		// Una copia de la fuente de serie del motor (Roboto, la reserva CJK y las de rangos sueltos)...
		const TSharedRef<const FCompositeFont> Base = FCoreStyle::GetDefaultFont();
		const TSharedRef<FStandaloneCompositeFont> Composite = MakeShared<FStandaloneCompositeFont>();
		Composite->DefaultTypeface = Base->DefaultTypeface;
		Composite->FallbackTypeface = Base->FallbackTypeface;
		Composite->SubTypefaces = Base->SubTypefaces;
		Composite->bEnableAscentDescentOverride = Base->bEnableAscentDescentOverride;

		// ...más una fuente de reserva por idioma cuyos archivos existan. El motor la usa solo cuando el juego está en ese idioma
		// (Cultures) y solo para los caracteres de su escritura (CharacterRanges); lo demás sigue con Roboto.
		const FString Folder = TNHUDFonts::GetFontFolder();
		for (const FTNLanguageEntry& Entry : TNLanguage::GetLanguages())
		{
			if (Entry.FontRegular.IsEmpty())
			{
				continue;
			}
			const TNHUDFonts::FFallbackFiles Files = TNHUDFonts::ResolveFallback(Entry, Folder);
			if (!Files.bFound)
			{
				UE_LOG(LogTortunabo, Verbose, TEXT("[Fuentes] %s: no está %s; se queda con la de reserva del motor."), *Entry.Culture,
					*(Folder / Entry.FontRegular));
				continue;
			}
			// En el editor (también en PIE) la fuente del motor ya trae una propia para ja, ko y zh-Hans (GenEiGothicPro,
			// NanumGothic, Droid Sans Fallback). Con dos fuentes de la misma cultura para los mismos caracteres, el motor no
			// garantiza cuál usa: se quita la suya para que mande la nuestra.
			Composite->SubTypefaces.RemoveAll([&Entry](const FCompositeSubFont& Existing)
			{
				return CulturesInclude(Existing.Cultures, Entry.Culture);
			});
			FCompositeSubFont SubFont;
			SubFont.Cultures = Entry.Culture;
			AddScriptRanges(SubFont, Entry.FontScript);
			AddWeights(SubFont.Typeface, Files.Regular, Files.Bold);
			Composite->SubTypefaces.Add(MoveTemp(SubFont));
			UE_LOG(LogTortunabo, Log, TEXT("[Fuentes] %s: fuente de reserva %s."), *Entry.Culture, *Entry.FontRegular);
		}

		// Sin destruirla nunca: es un FGCObject y, al cerrar el proceso, ya no habría recolector al que avisar.
		const TSharedRef<const FCompositeFont>* const Leaked = new TSharedRef<const FCompositeFont>(Composite);
		return *Leaked;
	}
}

namespace TNHUDFonts
{
	FString GetFontFolder()
	{
		return FPaths::ProjectContentDir() / TNHUDFontsDetail::FontFolder;
	}

	FFallbackFiles ResolveFallback(const FTNLanguageEntry& Entry, const FString& Folder)
	{
		FFallbackFiles Files;
		if (Entry.FontRegular.IsEmpty())
		{
			return Files;
		}
		const FString Regular = Folder / Entry.FontRegular;
		if (!FPaths::FileExists(Regular))
		{
			return Files;
		}
		const FString Bold = Entry.FontBold.IsEmpty() ? FString() : Folder / Entry.FontBold;
		Files.bFound = true;
		Files.Regular = Regular;
		Files.Bold = !Bold.IsEmpty() && FPaths::FileExists(Bold) ? Bold : Regular;
		return Files;
	}

	TSharedRef<const FCompositeFont> GetComposite()
	{
		static const TSharedRef<const FCompositeFont> Composite = TNHUDFontsDetail::Build();
		return Composite;
	}

	FSlateFontInfo Make(FName Weight, int32 Size)
	{
		return FSlateFontInfo(GetComposite(), static_cast<float>(Size), Weight);
	}
}
