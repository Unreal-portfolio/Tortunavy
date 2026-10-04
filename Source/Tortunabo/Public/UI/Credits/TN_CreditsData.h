#pragma once

#include "CoreMinimal.h"

/**
 * Créditos del juego leídos de un fichero de datos versionado (Content/Credits/Credits.json, Docs/Creditos.md), no escritos en
 * C++. Los nombres propios, los orígenes y las licencias se enseñan tal cual en todos los idiomas; los títulos de sección y los
 * papeles («Programación», «Arte»...) son claves que se traducen aquí (NSLOCTEXT «TNCredits»).
 */

/** Una línea de los créditos: quién o qué, su papel (claves traducibles), de dónde sale y con qué licencia. */
struct FTNCreditsEntry
{
	/** Nombre propio (persona, recurso, fuente tipográfica): no se traduce. */
	FString Name;
	/** Claves de papel («director», «code», «art»...): se traducen con TNCredits::RoleName. */
	TArray<FString> Roles;
	/** Origen (empresa, web): no se traduce. */
	FString Source;
	/** Nombre de la licencia: no se traduce. */
	FString License;
};

/** Una sección: su clave (título traducible), sus líneas, notas legales y, si hay, el texto completo de una licencia. */
struct FTNCreditsSection
{
	FString Id;
	TArray<FTNCreditsEntry> Entries;
	/** Avisos legales (marca registrada, licencias): se enseñan tal cual, en el idioma en que los exige su dueño. */
	TArray<FString> Notes;
	/** Fichero de licencia junto al JSON (p. ej. «OFL-1.1.txt») y su texto, ya leído. */
	FString LicenseFile;
	FString LicenseText;
};

struct FTNCreditsData
{
	TArray<FTNCreditsSection> Sections;

	/** La sección con esa clave, o nulo. */
	const FTNCreditsSection* FindSection(const FString& Id) const;
};

namespace TNCredits
{
	/** Ruta del fichero de créditos: Content/Credits/Credits.json (se empaqueta: DirectoriesToAlwaysStageAsUFS en DefaultGame.ini). */
	FString DefaultPath();

	/**
	 * Lee los créditos de un texto JSON. Falla (con el motivo en OutError) si el JSON no es válido o no tiene ninguna sección
	 * válida; las secciones sin «id» y las líneas sin «name» se saltan con un aviso en OutError. No lee ficheros de licencia.
	 */
	bool ParseJson(const FString& Json, FTNCreditsData& OutData, FString& OutError);

	/**
	 * Lee el fichero de créditos y los ficheros de licencia que cite (en su misma carpeta; una ruta absoluta o con «..» se
	 * rechaza). Falla si el fichero no existe o no se puede leer.
	 */
	bool LoadFile(const FString& Path, FTNCreditsData& OutData, FString& OutError);

	/** La sección tiene título traducido (las que no, salen con su clave en mayúsculas). */
	bool IsKnownSection(const FString& Id);

	/** Título de una sección, traducido («EQUIPO», «FUENTES TIPOGRÁFICAS»...). */
	FText SectionTitle(const FString& Id);

	/** El papel tiene nombre traducido (los que no, salen con su clave tal cual). */
	bool IsKnownRole(const FString& Key);

	/** Nombre de un papel, traducido («Dirección», «Programación»...). */
	FText RoleName(const FString& Key);
}
