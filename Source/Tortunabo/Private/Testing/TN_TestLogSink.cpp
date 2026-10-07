#include "Testing/TN_TestLogSink.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace TNTestLogSinkDetail
{
	/** Firma de una línea para agrupar repeticiones: sin números ni direcciones, recortada. */
	FString Signature(const FString& Message)
	{
		FString Out;
		Out.Reserve(160);
		for (int32 Index = 0; Index < Message.Len() && Out.Len() < 160; ++Index)
		{
			const TCHAR Char = Message[Index];
			if (FChar::IsDigit(Char))
			{
				if (Out.IsEmpty() || Out[Out.Len() - 1] != TEXT('#'))
				{
					Out.AppendChar(TEXT('#'));
				}
			}
			else
			{
				Out.AppendChar(Char);
			}
		}
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> EntriesToJson(const TArray<FTNTestLogSink::FEntry>& Entries)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FTNTestLogSink::FEntry& Entry : Entries)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("category"), Entry.Category);
			Item->SetStringField(TEXT("message"), Entry.Message.Left(400));
			Item->SetNumberField(TEXT("count"), Entry.Count);
			Out.Add(MakeShared<FJsonValueObject>(Item));
		}
		return Out;
	}

	template <typename KeyType>
	TSharedRef<FJsonObject> CountsToJson(const TMap<KeyType, int32>& Counts)
	{
		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		for (const TPair<KeyType, int32>& Pair : Counts)
		{
			if constexpr (std::is_same_v<KeyType, FName>)
			{
				Out->SetNumberField(Pair.Key.ToString(), Pair.Value);
			}
			else
			{
				Out->SetNumberField(Pair.Key, Pair.Value);
			}
		}
		return Out;
	}
}

FTNTestLogSink::EKind FTNTestLogSink::Classify(const FString& Message, const FName& Category, ELogVerbosity::Type Verbosity)
{
	if (Verbosity <= ELogVerbosity::Error
		&& (Message.Contains(TEXT("Ensure condition failed")) || Message.Contains(TEXT("Assertion failed")) || Message.Contains(TEXT("Fatal error"))))
	{
		return EKind::Ensure;
	}
	if (Message.Contains(TEXT("TN.Shell.Debug")))
	{
		if (Message.Contains(TEXT("caja bajo el terreno")))
		{
			return EKind::ShellSunk;
		}
		if (Message.Contains(TEXT("torbellino")))
		{
			return EKind::ShellSpin;
		}
		if (Message.Contains(TEXT("salto de velocidad")))
		{
			return EKind::ShellVelocityJump;
		}
		return EKind::Other;
	}
	if (Category == FName(TEXT("LogNetPlayerMovement")) && (Message.Contains(TEXT("Correction")) || Message.Contains(TEXT("Error for"))))
	{
		return EKind::NetCorrection;
	}
	return EKind::Other;
}

void FTNTestLogSink::AddEntry(TArray<FEntry>& Into, const FName& Category, const FString& Message) const
{
	const FString Signature = TNTestLogSinkDetail::Signature(Message);
	for (FEntry& Entry : Into)
	{
		if (Entry.Category == Category.ToString() && TNTestLogSinkDetail::Signature(Entry.Message) == Signature)
		{
			++Entry.Count;
			return;
		}
	}
	if (Into.Num() < MaxEntries)
	{
		FEntry& Added = Into.AddDefaulted_GetRef();
		Added.Category = Category.ToString();
		Added.Message = Message;
		Added.Count = 1;
	}
}

void FTNTestLogSink::Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category)
{
	const ELogVerbosity::Type Level = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
	const FString Text(Message);
	const EKind Kind = Classify(Text, Category, Level);

	FScopeLock ScopeLock(&Lock);
	switch (Kind)
	{
		case EKind::Ensure:
			++Ensures;
			AddEntry(EnsureEntries, Category, Text);
			return;
		case EKind::ShellSpin:         ++ShellSpin; break;
		case EKind::ShellSunk:         ++ShellSunk; break;
		case EKind::ShellVelocityJump: ++ShellVelocityJump; break;
		case EKind::NetCorrection:     ++NetCorrections; break;
		default: break;
	}
	if (Level <= ELogVerbosity::Error)
	{
		++Errors;
		++ErrorsByCategory.FindOrAdd(Category);
		AddEntry(ErrorEntries, Category, Text);
	}
	else if (Level == ELogVerbosity::Warning)
	{
		++Warnings;
		++WarningsByCategory.FindOrAdd(Category);
		AddEntry(WarningEntries, Category, Text);
	}
}

int32 FTNTestLogSink::GetErrorCount() const { FScopeLock ScopeLock(&Lock); return Errors; }
int32 FTNTestLogSink::GetEnsureCount() const { FScopeLock ScopeLock(&Lock); return Ensures; }
int32 FTNTestLogSink::GetShellSunkCount() const { FScopeLock ScopeLock(&Lock); return ShellSunk; }
int32 FTNTestLogSink::GetNetCorrectionCount() const { FScopeLock ScopeLock(&Lock); return NetCorrections; }

void FTNTestLogSink::WriteJson(FJsonObject& Out) const
{
	FScopeLock ScopeLock(&Lock);
	Out.SetNumberField(TEXT("log_errors"), Errors);
	Out.SetNumberField(TEXT("log_warnings"), Warnings);
	Out.SetNumberField(TEXT("asserts_ensures"), Ensures);
	Out.SetNumberField(TEXT("net_corrections"), NetCorrections);

	TSharedRef<FJsonObject> Shell = MakeShared<FJsonObject>();
	Shell->SetNumberField(TEXT("spin"), ShellSpin);
	Shell->SetNumberField(TEXT("sunk_under_terrain"), ShellSunk);
	Shell->SetNumberField(TEXT("velocity_jump"), ShellVelocityJump);
	Out.SetObjectField(TEXT("shell_debug"), Shell);

	Out.SetObjectField(TEXT("errors_by_category"), TNTestLogSinkDetail::CountsToJson(ErrorsByCategory));
	Out.SetObjectField(TEXT("warnings_by_category"), TNTestLogSinkDetail::CountsToJson(WarningsByCategory));
	Out.SetArrayField(TEXT("ensure_entries"), TNTestLogSinkDetail::EntriesToJson(EnsureEntries));
	Out.SetArrayField(TEXT("error_entries"), TNTestLogSinkDetail::EntriesToJson(ErrorEntries));
	Out.SetArrayField(TEXT("warning_entries"), TNTestLogSinkDetail::EntriesToJson(WarningEntries));
}
