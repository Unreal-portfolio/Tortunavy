// Informe de bug con F8 (Testing/TN_BugReport.h, issue #67): carpeta, cola del registro, commit leído de .git y el Markdown
// para pegar en una issue. Docs/Pruebas_Red_Local.md.

#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Testing/TN_BugReport.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBugReportLogTest, "Tortunabo.BugReport.Log",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBugReportLogTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Carpeta con fecha ordenable y sin «:»"), TNBugReport::FolderName(FDateTime(2026, 10, 2, 9, 5, 7)), FString(TEXT("2026-10-02_09-05-07")));

	FString Text;
	for (int32 Index = 1; Index <= 2500; ++Index)
	{
		Text += FString::Printf(TEXT("[línea %d]\r\n"), Index);
	}
	const TArray<FString> Tail = TNBugReport::LastLines(Text, TNBugReport::LogLines);
	TestEqual(TEXT("Se quedan 2000 líneas"), Tail.Num(), 2000);
	TestEqual(TEXT("La primera es la 501"), Tail[0], FString(TEXT("[línea 501]")));
	TestEqual(TEXT("La última es la 2500, sin fin de línea"), Tail.Last(), FString(TEXT("[línea 2500]")));
	TestEqual(TEXT("Menos líneas que el tope: todas"), TNBugReport::LastLines(TEXT("a\nb\n\nc"), 10).Num(), 3);

	const TArray<FString> Lines = {
		TEXT("[2026.10.02-09.00.00:000][  1]LogTortunabo: todo bien"),
		TEXT("[2026.10.02-09.00.01:000][  2]LogNet: Warning: aviso 1"),
		TEXT("[2026.10.02-09.00.02:000][  3]LogTortunabo: Error: error 1"),
		TEXT("[2026.10.02-09.00.03:000][  4]LogOutputDevice: Error: Ensure condition failed: x"),
		TEXT("[2026.10.02-09.00.04:000][  5]LogNet: Warning: aviso 2"),
	};
	const TArray<FString> Notable = TNBugReport::NotableLogLines(Lines, 3);
	TestEqual(TEXT("Solo errores y avisos, los 3 últimos"), Notable.Num(), 3);
	TestTrue(TEXT("En orden: el error primero"), Notable.Num() == 3 && Notable[0].Contains(TEXT("error 1")) && Notable[2].Contains(TEXT("aviso 2")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBugReportGitTest, "Tortunabo.BugReport.Git",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBugReportGitTest::RunTest(const FString& Parameters)
{
	const FString Sha = TEXT("42c4a85dc0123456789abcdef0123456789abcde");
	FString Ref;
	TestEqual(TEXT("HEAD con rama: sin SHA"), TNBugReport::ParseHead(TEXT("ref: refs/heads/dev\n"), Ref), FString());
	TestEqual(TEXT("… y con la referencia"), Ref, FString(TEXT("refs/heads/dev")));
	TestEqual(TEXT("HEAD separado: el SHA"), TNBugReport::ParseHead(Sha + TEXT("\n"), Ref), Sha);
	TestTrue(TEXT("… sin referencia"), Ref.IsEmpty());
	TestEqual(TEXT("HEAD basura: nada"), TNBugReport::ParseHead(TEXT("hola"), Ref), FString());

	const FString Packed = FString(TEXT("# pack-refs with: peeled fully-peeled sorted\n")) + TEXT("1111111111111111111111111111111111111111 refs/heads/main\n") + Sha
		+ TEXT(" refs/heads/dev\n^2222222222222222222222222222222222222222\n");
	TestEqual(TEXT("packed-refs: la rama pedida"), TNBugReport::FindPackedRef(Packed, TEXT("refs/heads/dev")), Sha);
	TestEqual(TEXT("packed-refs: rama que no está"), TNBugReport::FindPackedRef(Packed, TEXT("refs/heads/otra")), FString());
	TestEqual(TEXT("Fichero .git de worktree"), TNBugReport::ParseGitDirFile(TEXT("gitdir: C:/repo/.git/worktrees/loteC\n")),
		FString(TEXT("C:/repo/.git/worktrees/loteC")));
	TestEqual(TEXT("Otro contenido: vacío"), TNBugReport::ParseGitDirFile(TEXT("[core]")), FString());

	// De punta a punta sobre un worktree falso: HEAD en el worktree y la rama en packed-refs del directorio común.
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("TNBugReportTest")));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	const FString Common = FPaths::Combine(Root, TEXT("repo/.git"));
	const FString WorktreeGit = FPaths::Combine(Common, TEXT("worktrees/lote"));
	const FString Project = FPaths::Combine(Root, TEXT("lote"));
	FFileHelper::SaveStringToFile(TEXT("ref: refs/heads/feat/65-x\n"), *FPaths::Combine(WorktreeGit, TEXT("HEAD")));
	FFileHelper::SaveStringToFile(TEXT("../..\n"), *FPaths::Combine(WorktreeGit, TEXT("commondir")));
	FFileHelper::SaveStringToFile(Sha + TEXT(" refs/heads/feat/65-x\n"), *FPaths::Combine(Common, TEXT("packed-refs")));
	FFileHelper::SaveStringToFile(TEXT("gitdir: ") + WorktreeGit + TEXT("\n"), *FPaths::Combine(Project, TEXT(".git")));
	TestEqual(TEXT("Worktree: SHA corto y rama"), TNBugReport::CommitFromGit(Project), FString(TEXT("42c4a85dc (feat/65-x)")));
	TestEqual(TEXT("Sin .git: desconocido"), TNBugReport::CommitFromGit(FPaths::Combine(Root, TEXT("nada"))), FString(TEXT("desconocido")));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBugReportMarkdownTest, "Tortunabo.BugReport.Markdown",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBugReportMarkdownTest::RunTest(const FString& Parameters)
{
	TNBugReport::FContext Context;
	Context.Date = TEXT("2026-10-02 09:05:07");
	Context.Commit = TEXT("42c4a85dc (dev)");
	Context.Build = TEXT("DebugGame");
	Context.Map = TEXT("LVL_BeachRace");
	Context.Mode = TEXT("TN_BeachRaceGameMode");
	Context.NetMode = TEXT("Client");
	Context.Network = TEXT("4 jugadores · ping 150 ms | x");
	Context.Position = TEXT("1.0, 2.0, 3.0");
	Context.Seeds = { TEXT("TN_ProcMapGameState.MapSeed=7"), TEXT("TN.Monkey=9") };
	Context.Folder = TEXT("Saved/BugReports/2026-10-02_09-05-07");
	Context.Trigger = TEXT("F8");
	Context.Notable = { TEXT("LogNet: Warning: aviso") };

	const FString Markdown = TNBugReport::FormatMarkdown(Context);
	for (const TCHAR* Section : { TEXT("### Pasos para reproducirlo"), TEXT("### Qué esperabas y qué pasó"), TEXT("### Criterios de aceptación"),
			 TEXT("### Rama o commit"), TEXT("### Contexto (F8)") })
	{
		TestTrue(FString::Printf(TEXT("Sección de la plantilla «Fallo»: %s"), Section), Markdown.Contains(Section));
	}
	TestTrue(TEXT("Criterio como casilla"), Markdown.Contains(TEXT("- [ ] ")));
	TestTrue(TEXT("Commit en la tabla"), Markdown.Contains(TEXT("| Commit | 42c4a85dc (dev) |")));
	TestTrue(TEXT("Mapa en la tabla"), Markdown.Contains(TEXT("| Mapa | LVL_BeachRace |")));
	TestTrue(TEXT("Semillas juntas"), Markdown.Contains(TEXT("| Semilla | TN_ProcMapGameState.MapSeed=7, TN.Monkey=9 |")));
	TestTrue(TEXT("Posición"), Markdown.Contains(TEXT("| Posición (m) | 1.0, 2.0, 3.0 |")));
	TestTrue(TEXT("Una barra en un valor no rompe la tabla"), Markdown.Contains(TEXT("| Red | Client · 4 jugadores · ping 150 ms / x |")));
	TestTrue(TEXT("Errores del registro en un bloque"), Markdown.Contains(TEXT("```text\nLogNet: Warning: aviso\n```")));
	TestTrue(TEXT("Lista los ficheros"), Markdown.Contains(TNBugReport::LogFile()) && Markdown.Contains(TNBugReport::MatchFile())
		&& Markdown.Contains(TNBugReport::PlayerFile()));
	TestTrue(TEXT("Sin RHI avisa de que no hay captura"), Markdown.Contains(TEXT("Sin captura")) && !Markdown.Contains(TNBugReport::ScreenshotFile()));

	Context.bHasScreenshot = true;
	Context.Seeds.Reset();
	Context.Notable.Reset();
	const FString WithShot = TNBugReport::FormatMarkdown(Context);
	TestTrue(TEXT("Con captura la nombra"), WithShot.Contains(TNBugReport::ScreenshotFile()) && !WithShot.Contains(TEXT("Sin captura")));
	TestTrue(TEXT("Sin semillas lo dice"), WithShot.Contains(TEXT("| Semilla | ninguna |")));
	TestTrue(TEXT("Sin errores lo dice"), WithShot.Contains(TEXT("Sin errores ni avisos")));

	// Lo que se pega en una issue no lleva IP, SteamID ni el usuario de Windows.
	Context.Notable = { TEXT("LogNet: Warning: conexión con 192.168.1.20:7777 de 76561198012345678 en C:\\Users\\Rodrigo\\Tortunabo\\Saved") };
	Context.Folder = TEXT("C:/Users/Rodrigo/Tortunabo/Saved/BugReports/x");
	const FString Redacted = TNBugReport::FormatMarkdown(Context);
	TestTrue(TEXT("Registro sin IP, SteamID ni usuario"), Redacted.Contains(TEXT("conexión con <ip> de <steamid> en C:\\Users\\<usuario>\\Tortunabo\\Saved")));
	TestTrue(TEXT("Carpeta absoluta sin usuario"), Redacted.Contains(TEXT("`C:/Users/<usuario>/Tortunabo/Saved/BugReports/x`")));
	TestFalse(TEXT("Ni rastro de Rodrigo"), Redacted.Contains(TEXT("Rodrigo")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTNBugReportRedactTest, "Tortunabo.BugReport.Redact",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTNBugReportRedactTest::RunTest(const FString& Parameters)
{
	using TNBugReport::RedactLine;
	TestEqual(TEXT("IPv4 con puerto"), RedactLine(TEXT("Conectando a 10.0.0.5:17777...")), FString(TEXT("Conectando a <ip>...")));
	TestEqual(TEXT("IPv4 al final de frase"), RedactLine(TEXT("Servidor 127.0.0.1.")), FString(TEXT("Servidor <ip>.")));
	TestEqual(TEXT("SteamID de 64 bits"), RedactLine(TEXT("Id=76561198012345678 entra")), FString(TEXT("Id=<steamid> entra")));
	TestEqual(TEXT("SteamID3"), RedactLine(TEXT("Steam [U:1:52070950] listo")), FString(TEXT("Steam <steamid> listo")));
	TestEqual(TEXT("Usuario con espacio en una ruta con barras invertidas"), RedactLine(TEXT("Log en c:\\users\\Ana María\\x")),
		FString(TEXT("Log en c:\\users\\<usuario>\\x")));
	TestEqual(TEXT("Usuario en una ruta con barras normales"), RedactLine(TEXT("D:/Users/rodri/Saved")), FString(TEXT("D:/Users/<usuario>/Saved")));

	// Lo que no es personal se queda igual: marcas de tiempo, versiones de tres números, números largos que no son SteamID.
	const FString Stamp = TEXT("[2026.10.02-09.00.00:000][  1]LogTortunabo: v5.6.1, semilla 1234567890123456789, 1.5 m");
	TestEqual(TEXT("Sin datos personales: igual"), RedactLine(Stamp), Stamp);
	return true;
}

#endif
