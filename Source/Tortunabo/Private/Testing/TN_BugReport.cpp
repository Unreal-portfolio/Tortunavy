#include "Testing/TN_BugReport.h"

#include "Internationalization/Regex.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace TNBugReportDetail
{
	bool IsHexSha(const FString& Text)
	{
		if (Text.Len() < 7 || Text.Len() > 64)
		{
			return false;
		}
		for (const TCHAR Char : Text)
		{
			if (!FChar::IsHexDigit(Char))
			{
				return false;
			}
		}
		return true;
	}

	FString ReadTrimmed(const FString& Path)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			return FString();
		}
		return Text.TrimStartAndEnd();
	}

	/** SHA de Ref buscando el fichero suelto y luego packed-refs en cada directorio de git candidato. */
	FString ResolveRef(const TArray<FString>& GitDirs, const FString& Ref)
	{
		for (const FString& Dir : GitDirs)
		{
			const FString Loose = ReadTrimmed(FPaths::Combine(Dir, Ref));
			if (IsHexSha(Loose))
			{
				return Loose;
			}
		}
		for (const FString& Dir : GitDirs)
		{
			FString Packed;
			if (FFileHelper::LoadFileToString(Packed, *FPaths::Combine(Dir, TEXT("packed-refs"))))
			{
				const FString Sha = TNBugReport::FindPackedRef(Packed, Ref);
				if (!Sha.IsEmpty())
				{
					return Sha;
				}
			}
		}
		return FString();
	}

	/** Text con cada coincidencia de Pattern sustituida por Replacement (precedido del grupo 1 si bKeepGroup1). */
	FString ReplaceMatches(const FString& Text, const FRegexPattern& Pattern, const TCHAR* Replacement, bool bKeepGroup1 = false)
	{
		FRegexMatcher Matcher(Pattern, Text);
		FString Out;
		int32 Copied = 0;
		while (Matcher.FindNext())
		{
			const int32 Begin = Matcher.GetMatchBeginning();
			const int32 End = Matcher.GetMatchEnding();
			Out += Text.Mid(Copied, Begin - Copied);
			if (bKeepGroup1)
			{
				Out += Matcher.GetCaptureGroup(1);
			}
			Out += Replacement;
			Copied = End;
		}
		Out += Text.Mid(Copied);
		return Out;
	}

	void AddRow(FString& Out, const TCHAR* Field, const FString& Value)
	{
		// Una barra vertical en el valor rompería la tabla de Markdown.
		Out += FString::Printf(TEXT("| %s | %s |\n"), Field, Value.IsEmpty() ? TEXT("—") : *Value.Replace(TEXT("|"), TEXT("/")));
	}
}

FString TNBugReport::FolderName(const FDateTime& When)
{
	return When.ToString(TEXT("%Y-%m-%d_%H-%M-%S"));
}

TArray<FString> TNBugReport::LastLines(const FString& Text, int32 Max)
{
	TArray<FString> All;
	Text.ParseIntoArrayLines(All, true);
	if (Max <= 0)
	{
		return {};
	}
	const int32 First = FMath::Max(0, All.Num() - Max);
	TArray<FString> Out;
	Out.Reserve(All.Num() - First);
	for (int32 Index = First; Index < All.Num(); ++Index)
	{
		Out.Add(All[Index]);
	}
	return Out;
}

TArray<FString> TNBugReport::NotableLogLines(const TArray<FString>& Lines, int32 Max)
{
	TArray<FString> Out;
	for (int32 Index = Lines.Num() - 1; Index >= 0 && Out.Num() < Max; --Index)
	{
		const FString& Line = Lines[Index];
		if (Line.Contains(TEXT(": Error: ")) || Line.Contains(TEXT(": Warning: ")) || Line.Contains(TEXT("Ensure condition failed")))
		{
			Out.Insert(Line, 0);
		}
	}
	return Out;
}

FString TNBugReport::ParseHead(const FString& HeadText, FString& OutRef)
{
	OutRef.Reset();
	const FString Head = HeadText.TrimStartAndEnd();
	if (Head.StartsWith(TEXT("ref:")))
	{
		OutRef = Head.Mid(4).TrimStartAndEnd();
		return FString();
	}
	return TNBugReportDetail::IsHexSha(Head) ? Head : FString();
}

FString TNBugReport::FindPackedRef(const FString& PackedRefsText, const FString& Ref)
{
	TArray<FString> Lines;
	PackedRefsText.ParseIntoArrayLines(Lines, true);
	for (const FString& Line : Lines)
	{
		// «# pack-refs with: ...» y «^<sha>» (etiquetas anotadas) no son referencias.
		if (Line.StartsWith(TEXT("#")) || Line.StartsWith(TEXT("^")))
		{
			continue;
		}
		FString Sha;
		FString Name;
		if (Line.Split(TEXT(" "), &Sha, &Name) && Name.TrimStartAndEnd() == Ref && TNBugReportDetail::IsHexSha(Sha))
		{
			return Sha;
		}
	}
	return FString();
}

FString TNBugReport::ParseGitDirFile(const FString& DotGitText)
{
	const FString Text = DotGitText.TrimStartAndEnd();
	return Text.StartsWith(TEXT("gitdir:")) ? Text.Mid(7).TrimStartAndEnd() : FString();
}

FString TNBugReport::CommitFromGit(const FString& ProjectDir)
{
	const FString ProjectRoot = FPaths::ConvertRelativePathToFull(ProjectDir);
	const FString DotGit = FPaths::Combine(ProjectRoot, TEXT(".git"));
	FString GitDir = DotGit;
	if (!FPaths::DirectoryExists(DotGit))
	{
		// Worktree: .git es un fichero que apunta a .git/worktrees/<nombre> del repositorio principal.
		GitDir = ParseGitDirFile(TNBugReportDetail::ReadTrimmed(DotGit));
		if (GitDir.IsEmpty())
		{
			return TEXT("desconocido");
		}
		if (FPaths::IsRelative(GitDir))
		{
			GitDir = FPaths::Combine(ProjectRoot, GitDir);
		}
	}
	TArray<FString> GitDirs = { GitDir };
	// Las ramas de un worktree están en el directorio común (fichero commondir, normalmente «../..»).
	const FString Common = TNBugReportDetail::ReadTrimmed(FPaths::Combine(GitDir, TEXT("commondir")));
	if (!Common.IsEmpty())
	{
		GitDirs.Add(FPaths::IsRelative(Common) ? FPaths::Combine(GitDir, Common) : Common);
	}

	FString Ref;
	FString Sha = ParseHead(TNBugReportDetail::ReadTrimmed(FPaths::Combine(GitDir, TEXT("HEAD"))), Ref);
	if (Sha.IsEmpty() && !Ref.IsEmpty())
	{
		Sha = TNBugReportDetail::ResolveRef(GitDirs, Ref);
	}
	if (Sha.IsEmpty())
	{
		return TEXT("desconocido");
	}
	const FString Short = Sha.Left(9);
	Ref.RemoveFromStart(TEXT("refs/heads/"));
	return Ref.IsEmpty() ? Short : FString::Printf(TEXT("%s (%s)"), *Short, *Ref);
}

FString TNBugReport::RedactLine(const FString& Line)
{
	using TNBugReportDetail::ReplaceMatches;
	FString Out = ReplaceMatches(Line, FRegexPattern(TEXT(R"((?i)([A-Z]:[\\/]+Users[\\/]+)[^\\/\r\n"'<>|]+)")), TEXT("<usuario>"), true);
	Out = ReplaceMatches(Out, FRegexPattern(TEXT(R"(\[U:1:\d+\])")), TEXT("<steamid>"));
	Out = ReplaceMatches(Out, FRegexPattern(TEXT(R"((?<!\d)7656119\d{10}(?!\d))")), TEXT("<steamid>"));
	// Sin dígitos pegados delante ni detrás (un punto final de frase sí puede ir detrás).
	return ReplaceMatches(Out, FRegexPattern(TEXT(R"((?<!\d)(?<!\d\.)(?:\d{1,3}\.){3}\d{1,3}(?::\d{1,5})?(?!\.?\d))")), TEXT("<ip>"));
}

FString TNBugReport::FormatMarkdown(const FContext& Context)
{
	using TNBugReportDetail::AddRow;
	FString Out;
	Out += TEXT("<!-- Informe creado con F8 (Docs/Pruebas_Red_Local.md). Pega este texto en una issue nueva con la plantilla «Fallo» y rellena lo marcado. -->\n\n");
	Out += TEXT("### Pasos para reproducirlo\n\n");
	Out += FString::Printf(TEXT("1. Mapa `%s`, %s, %s.\n2. <!-- qué hiciste -->\n\n"), *Context.Map, *Context.NetMode, *Context.Network);
	Out += TEXT("### Qué esperabas y qué pasó\n\n<!-- esperado / obtenido -->\n\n");
	Out += TEXT("### Criterios de aceptación\n\n- [ ] El fallo ya no ocurre con los mismos pasos.\n\n");
	Out += FString::Printf(TEXT("### Rama o commit\n\n%s\n\n"), *Context.Commit);

	Out += TEXT("### Contexto (F8)\n\n| Campo | Valor |\n|---|---|\n");
	AddRow(Out, TEXT("Fecha"), Context.Date);
	AddRow(Out, TEXT("Commit"), Context.Commit);
	AddRow(Out, TEXT("Compilación"), Context.Build);
	AddRow(Out, TEXT("Mapa"), Context.Map);
	AddRow(Out, TEXT("Modo"), Context.Mode);
	AddRow(Out, TEXT("Red"), FString::Printf(TEXT("%s · %s"), *Context.NetMode, *Context.Network));
	AddRow(Out, TEXT("Posición (m)"), Context.Position);
	AddRow(Out, TEXT("Semilla"), Context.Seeds.Num() > 0 ? FString::Join(Context.Seeds, TEXT(", ")) : FString(TEXT("ninguna")));
	AddRow(Out, TEXT("Creado con"), Context.Trigger);
	Out += TEXT("\n");

	if (Context.Notable.Num() > 0)
	{
		Out += FString::Printf(TEXT("Últimos errores y avisos del registro (%d):\n\n```text\n"), Context.Notable.Num());
		for (const FString& Line : Context.Notable)
		{
			Out += RedactLine(Line).Left(300) + TEXT("\n");
		}
		Out += TEXT("```\n\n");
	}
	else
	{
		Out += TEXT("Sin errores ni avisos en las últimas líneas del registro.\n\n");
	}

	// La carpeta sale absoluta (con el usuario) si no se puede hacer relativa al proyecto.
	Out += FString::Printf(TEXT("Ficheros en `%s`: "), *RedactLine(Context.Folder));
	if (Context.bHasScreenshot)
	{
		Out += FString::Printf(TEXT("`%s` (arrástrala a la issue), "), ScreenshotFile());
	}
	Out += FString::Printf(TEXT("`%s` (últimas %d líneas), `%s` (GameState, semillas, red), `%s` (jugadores locales).\n"), LogFile(), LogLines, MatchFile(),
		PlayerFile());
	if (!Context.bHasScreenshot)
	{
		Out += TEXT("Sin captura: este proceso no dibuja (-nullrhi o servidor dedicado).\n");
	}
	return Out;
}
