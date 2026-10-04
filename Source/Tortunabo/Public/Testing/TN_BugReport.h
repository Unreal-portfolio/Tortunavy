#pragma once

#include "CoreMinimal.h"

/**
 * Lógica pura del informe de bug con F8 (UTN_BugReportSubsystem, Docs/Pruebas_Red_Local.md): nombre de la carpeta, cola del
 * registro, commit leído de .git y el Markdown listo para pegar en una issue con la plantilla «Fallo». Sin mundo ni actores:
 * se testea igual (Tortunabo.BugReport).
 */
namespace TNBugReport
{
	/** Líneas del registro que se guardan en log.txt. */
	constexpr int32 LogLines = 2000;
	/** Líneas de error o aviso que se copian en el Markdown. */
	constexpr int32 NotableLines = 15;

	/** Nombres de los ficheros de la carpeta del informe. */
	inline const TCHAR* ScreenshotFile() { return TEXT("captura.png"); }
	inline const TCHAR* LogFile() { return TEXT("log.txt"); }
	inline const TCHAR* MatchFile() { return TEXT("partida.json"); }
	inline const TCHAR* PlayerFile() { return TEXT("jugador.json"); }
	inline const TCHAR* MarkdownFile() { return TEXT("informe.md"); }

	/** Lo que el Markdown cuenta de la partida en el momento de pulsar F8. */
	struct FContext
	{
		FString Date;
		FString Commit;
		FString Build;
		FString Map;
		FString Mode;
		FString NetMode;
		/** Jugadores, ping y emulación de red, en una línea. */
		FString Network;
		/** Posición del primer jugador local, en metros. */
		FString Position;
		/** «Nombre=valor» de cada semilla encontrada. */
		TArray<FString> Seeds;
		/** Carpeta del informe tal como se enseña (relativa a Saved/ si se puede). */
		FString Folder;
		bool bHasScreenshot = false;
		/** Qué lo ha creado: «F8», «consola» o «línea de órdenes». */
		FString Trigger;
		/** Últimas líneas de error o aviso del registro. */
		TArray<FString> Notable;
	};

	/** aaaa-mm-dd_hh-mm-ss: ordena por fecha y no lleva caracteres prohibidos en Windows. */
	TORTUNABO_API FString FolderName(const FDateTime& When);

	/** Las últimas Max líneas no vacías del texto (sin el fin de línea). */
	TORTUNABO_API TArray<FString> LastLines(const FString& Text, int32 Max);

	/** Las últimas Max líneas de error o aviso («Error:» o «Warning:» tras la categoría, o un ensure). */
	TORTUNABO_API TArray<FString> NotableLogLines(const TArray<FString>& Lines, int32 Max);

	/**
	 * Commit a partir del contenido de HEAD («ref: refs/heads/x» o un SHA suelto). OutRef recibe la referencia si la hay.
	 * Devuelve el SHA si HEAD lo lleva directamente (HEAD separado) y vacío si hay que resolver la referencia.
	 */
	TORTUNABO_API FString ParseHead(const FString& HeadText, FString& OutRef);

	/** SHA de Ref en el contenido de packed-refs (vacío si no está). */
	TORTUNABO_API FString FindPackedRef(const FString& PackedRefsText, const FString& Ref);

	/** Directorio de git a partir del contenido de un fichero .git de worktree («gitdir: ruta»); vacío si no lo es. */
	TORTUNABO_API FString ParseGitDirFile(const FString& DotGitText);

	/** Commit del proyecto leído de .git (worktrees incluidos), con la rama si la hay: «abc1234 (rama)». «desconocido» si no hay .git. */
	TORTUNABO_API FString CommitFromGit(const FString& ProjectDir);

	/**
	 * La línea sin datos personales, para lo que se pega en una issue: direcciones IPv4 (con puerto) por «<ip>», SteamID
	 * (64 bits o [U:1:n]) por «<steamid>» y el usuario de las rutas «X:\Users\<nombre>» por «<usuario>».
	 */
	TORTUNABO_API FString RedactLine(const FString& Line);

	/**
	 * Markdown para pegar en una issue: secciones de la plantilla «Fallo» por rellenar y el contexto automático. Las líneas
	 * del registro pasan por RedactLine.
	 */
	TORTUNABO_API FString FormatMarkdown(const FContext& Context);
}
