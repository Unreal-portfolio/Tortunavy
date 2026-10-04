#include "UI/Credits/TN_CreditsData.h"
#include "Core/TN_Log.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace TNCreditsDataDetail
{
	FString ReadString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Value;
		if (Object.IsValid())
		{
			Object->TryGetStringField(Field, Value);
		}
		return Value.TrimStartAndEnd();
	}

	TArray<FString> ReadStringArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		TArray<FString> Values;
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Array) && Array)
		{
			for (const TSharedPtr<FJsonValue>& Item : *Array)
			{
				FString Value;
				if (Item.IsValid() && Item->TryGetString(Value) && !Value.TrimStartAndEnd().IsEmpty())
				{
					Values.Add(Value.TrimStartAndEnd());
				}
			}
		}
		return Values;
	}

	bool ParseSection(const TSharedPtr<FJsonObject>& Object, int32 Index, FTNCreditsSection& OutSection, FString& InOutWarnings)
	{
		OutSection.Id = ReadString(Object, TEXT("id"));
		if (OutSection.Id.IsEmpty())
		{
			InOutWarnings += FString::Printf(TEXT("Sección %d sin «id»: se salta. "), Index);
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (Object->TryGetArrayField(TEXT("entries"), Entries) && Entries)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Entries)
			{
				const TSharedPtr<FJsonObject>* EntryObject = nullptr;
				if (!Value.IsValid() || !Value->TryGetObject(EntryObject) || !EntryObject)
				{
					InOutWarnings += FString::Printf(TEXT("Línea no válida en «%s»: se salta. "), *OutSection.Id);
					continue;
				}
				FTNCreditsEntry Entry;
				Entry.Name = ReadString(*EntryObject, TEXT("name"));
				if (Entry.Name.IsEmpty())
				{
					InOutWarnings += FString::Printf(TEXT("Línea sin «name» en «%s»: se salta. "), *OutSection.Id);
					continue;
				}
				Entry.Roles = ReadStringArray(*EntryObject, TEXT("roles"));
				Entry.Source = ReadString(*EntryObject, TEXT("source"));
				Entry.License = ReadString(*EntryObject, TEXT("license"));
				OutSection.Entries.Add(MoveTemp(Entry));
			}
		}
		OutSection.Notes = ReadStringArray(Object, TEXT("notes"));
		OutSection.LicenseFile = ReadString(Object, TEXT("licenseFile"));
		return true;
	}

	/** Solo un nombre de fichero junto al JSON: nada de rutas absolutas ni de salir de la carpeta. */
	bool IsSafeSiblingFile(const FString& File)
	{
		return !File.IsEmpty() && !File.Contains(TEXT("..")) && !File.Contains(TEXT("/")) && !File.Contains(TEXT("\\"))
			&& !File.Contains(TEXT(":"));
	}
}

const FTNCreditsSection* FTNCreditsData::FindSection(const FString& Id) const
{
	return Sections.FindByPredicate([&Id](const FTNCreditsSection& Section) { return Section.Id == Id; });
}

FString TNCredits::DefaultPath()
{
	return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Credits"), TEXT("Credits.json"));
}

bool TNCredits::ParseJson(const FString& Json, FTNCreditsData& OutData, FString& OutError)
{
	using namespace TNCreditsDataDetail;
	OutData = FTNCreditsData();
	OutError.Reset();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("JSON no válido: %s"), *Reader->GetErrorMessage());
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Sections = nullptr;
	if (!Root->TryGetArrayField(TEXT("sections"), Sections) || !Sections)
	{
		OutError = TEXT("Falta el campo «sections».");
		return false;
	}
	for (int32 Index = 0; Index < Sections->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* SectionObject = nullptr;
		if (!(*Sections)[Index].IsValid() || !(*Sections)[Index]->TryGetObject(SectionObject) || !SectionObject)
		{
			OutError += FString::Printf(TEXT("Sección %d no es un objeto: se salta. "), Index);
			continue;
		}
		FTNCreditsSection Section;
		if (ParseSection(*SectionObject, Index, Section, OutError))
		{
			OutData.Sections.Add(MoveTemp(Section));
		}
	}
	if (OutData.Sections.Num() == 0)
	{
		OutError += TEXT("Ninguna sección válida.");
		return false;
	}
	return true;
}

bool TNCredits::LoadFile(const FString& Path, FTNCreditsData& OutData, FString& OutError)
{
	using namespace TNCreditsDataDetail;
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutData = FTNCreditsData();
		OutError = FString::Printf(TEXT("No se puede leer %s."), *Path);
		return false;
	}
	if (!ParseJson(Json, OutData, OutError))
	{
		return false;
	}
	const FString Folder = FPaths::GetPath(Path);
	for (FTNCreditsSection& Section : OutData.Sections)
	{
		if (Section.LicenseFile.IsEmpty())
		{
			continue;
		}
		if (!IsSafeSiblingFile(Section.LicenseFile))
		{
			OutError += FString::Printf(TEXT("Fichero de licencia rechazado en «%s»: %s. "), *Section.Id, *Section.LicenseFile);
			continue;
		}
		if (!FFileHelper::LoadFileToString(Section.LicenseText, *FPaths::Combine(Folder, Section.LicenseFile)))
		{
			OutError += FString::Printf(TEXT("No se puede leer la licencia %s. "), *Section.LicenseFile);
			continue;
		}
		// Saltos de línea de Windows a «\n» (Slate pintaría el «\r»).
		Section.LicenseText.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		Section.LicenseText.TrimStartAndEndInline();
	}
	if (!OutError.IsEmpty())
	{
		UE_LOG(LogTortunabo, Warning, TEXT("[Créditos] %s"), *OutError);
	}
	return true;
}

bool TNCredits::IsKnownSection(const FString& Id)
{
	return Id == TEXT("team") || Id == TEXT("art") || Id == TEXT("audio") || Id == TEXT("fonts") || Id == TEXT("maps")
		|| Id == TEXT("engine");
}

FText TNCredits::SectionTitle(const FString& Id)
{
	if (Id == TEXT("team")) { return NSLOCTEXT("TNCredits", "SectionTeam", "EQUIPO"); }
	if (Id == TEXT("art")) { return NSLOCTEXT("TNCredits", "SectionArt", "ARTE DE TERCEROS"); }
	if (Id == TEXT("audio")) { return NSLOCTEXT("TNCredits", "SectionAudio", "AUDIO DE TERCEROS"); }
	if (Id == TEXT("fonts")) { return NSLOCTEXT("TNCredits", "SectionFonts", "FUENTES TIPOGRÁFICAS"); }
	if (Id == TEXT("maps")) { return NSLOCTEXT("TNCredits", "SectionMaps", "DATOS GEOGRÁFICOS"); }
	if (Id == TEXT("engine")) { return NSLOCTEXT("TNCredits", "SectionEngine", "MOTOR"); }
	return FText::AsCultureInvariant(Id.ToUpper());
}

bool TNCredits::IsKnownRole(const FString& Key)
{
	return Key == TEXT("director") || Key == TEXT("code") || Key == TEXT("art") || Key == TEXT("levels") || Key == TEXT("audio")
		|| Key == TEXT("animations");
}

FText TNCredits::RoleName(const FString& Key)
{
	if (Key == TEXT("director")) { return NSLOCTEXT("TNCredits", "RoleDirector", "Dirección"); }
	if (Key == TEXT("code")) { return NSLOCTEXT("TNCredits", "RoleCode", "Programación"); }
	if (Key == TEXT("art")) { return NSLOCTEXT("TNCredits", "RoleArt", "Arte"); }
	if (Key == TEXT("levels")) { return NSLOCTEXT("TNCredits", "RoleLevels", "Niveles"); }
	if (Key == TEXT("audio")) { return NSLOCTEXT("TNCredits", "RoleAudio", "Música y sonido"); }
	if (Key == TEXT("animations")) { return NSLOCTEXT("TNCredits", "RoleAnimations", "Animaciones"); }
	return FText::AsCultureInvariant(Key);
}
