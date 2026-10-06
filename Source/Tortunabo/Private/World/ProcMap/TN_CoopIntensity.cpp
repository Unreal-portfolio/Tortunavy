#include "World/ProcMap/TN_CoopIntensity.h"

#include "Core/TN_Log.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace TNCoopIntensity
{
	namespace
	{
		/** Minúsculas y sin tildes: «Difícil», «DIFICIL» y «dificil» son lo mismo. */
		FString Normalize(const FString& In)
		{
			FString Out = In.TrimStartAndEnd().ToLower();
			static const TCHAR* const From[] = { TEXT("á"), TEXT("é"), TEXT("í"), TEXT("ó"), TEXT("ú"), TEXT("Á"), TEXT("É"), TEXT("Í"), TEXT("Ó"), TEXT("Ú") };
			static const TCHAR* const To[] = { TEXT("a"), TEXT("e"), TEXT("i"), TEXT("o"), TEXT("u"), TEXT("a"), TEXT("e"), TEXT("i"), TEXT("o"), TEXT("u") };
			for (int32 i = 0; i < UE_ARRAY_COUNT(From); ++i)
			{
				Out.ReplaceInline(From[i], To[i], ESearchCase::CaseSensitive);
			}
			return Out;
		}

		/** Mezcla de 32 bits (splitmix): la misma elección en todas las máquinas con la misma semilla. */
		uint32 Mix(uint32 X)
		{
			X += 0x9E3779B9u;
			X = (X ^ (X >> 16)) * 0x85EBCA6Bu;
			X = (X ^ (X >> 13)) * 0xC2B2AE35u;
			return X ^ (X >> 16);
		}

		bool ParseRounds(const TSharedPtr<FJsonObject>& Root, FTable& Out, FString& OutError)
		{
			const TArray<TSharedPtr<FJsonValue>>* Rounds = nullptr;
			if (!Root->TryGetArrayField(TEXT("rondas"), Rounds) || !Rounds || Rounds->Num() == 0)
			{
				OutError = TEXT("falta «rondas»");
				return false;
			}
			for (int32 r = 0; r < Rounds->Num(); ++r)
			{
				const TSharedPtr<FJsonObject> Round = (*Rounds)[r].IsValid() ? (*Rounds)[r]->AsObject() : nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Tramos = nullptr;
				if (!Round.IsValid() || !Round->TryGetArrayField(TEXT("tramos"), Tramos) || !Tramos || Tramos->Num() != Out.NumTramos)
				{
					OutError = FString::Printf(TEXT("la ronda %d no tiene %d tramos"), r + 1, Out.NumTramos);
					return false;
				}
				TArray<EDifficulty>& Row = Out.Rounds.AddDefaulted_GetRef();
				for (const TSharedPtr<FJsonValue>& Value : *Tramos)
				{
					EDifficulty D;
					const FString Name = Value.IsValid() ? Value->AsString() : FString();
					if (!ParseDifficulty(Name, D))
					{
						OutError = FString::Printf(TEXT("dificultad desconocida «%s» en la ronda %d"), *Name, r + 1);
						return false;
					}
					Row.Add(D);
				}
			}
			return true;
		}

		bool ParseModules(const TSharedPtr<FJsonObject>& Root, FTable& Out, FString& OutError)
		{
			const TArray<TSharedPtr<FJsonValue>>* Modules = nullptr;
			if (!Root->TryGetArrayField(TEXT("modulos"), Modules) || !Modules || Modules->Num() == 0)
			{
				OutError = TEXT("falta «modulos»");
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Value : *Modules)
			{
				const TSharedPtr<FJsonObject> Obj = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Obj.IsValid())
				{
					OutError = TEXT("un módulo no es un objeto");
					return false;
				}
				FModuleRow Row;
				Obj->TryGetNumberField(TEXT("id"), Row.Id);
				Obj->TryGetNumberField(TEXT("intensidad"), Row.Intensity);
				const FString Name = Obj->GetStringField(TEXT("dificultad"));
				if (!ParseDifficulty(Name, Row.Difficulty))
				{
					OutError = FString::Printf(TEXT("dificultad desconocida «%s» en el módulo %d"), *Name, Row.Id);
					return false;
				}
				const TArray<TSharedPtr<FJsonValue>>* Enemies = nullptr;
				if (Obj->TryGetArrayField(TEXT("enemigos"), Enemies) && Enemies)
				{
					for (const TSharedPtr<FJsonValue>& E : *Enemies)
					{
						const TSharedPtr<FJsonObject> EObj = E.IsValid() ? E->AsObject() : nullptr;
						if (!EObj.IsValid()) { continue; }
						FEnemyCount Enemy;
						Enemy.Name = EObj->GetStringField(TEXT("nombre")).TrimStartAndEnd();
						EObj->TryGetNumberField(TEXT("cantidad"), Enemy.Count);
						if (!Enemy.Name.IsEmpty() && Enemy.Count > 0)
						{
							Row.Enemies.Add(MoveTemp(Enemy));
						}
					}
				}
				Out.Modules.Add(MoveTemp(Row));
			}
			return true;
		}
	}

	bool ParseDifficulty(const FString& Name, EDifficulty& Out)
	{
		const FString Key = Normalize(Name);
		if (Key == TEXT("facil")) { Out = EDifficulty::Easy; return true; }
		if (Key == TEXT("medio")) { Out = EDifficulty::Medium; return true; }
		if (Key == TEXT("dificil")) { Out = EDifficulty::Hard; return true; }
		if (Key == TEXT("puzle") || Key == TEXT("puzzle")) { Out = EDifficulty::Puzzle; return true; }
		return false;
	}

	const TCHAR* DifficultyName(EDifficulty Difficulty)
	{
		switch (Difficulty)
		{
			case EDifficulty::Easy: return TEXT("Fácil");
			case EDifficulty::Medium: return TEXT("Medio");
			case EDifficulty::Hard: return TEXT("Difícil");
			case EDifficulty::Puzzle: return TEXT("Puzle");
			default: return TEXT("?");
		}
	}

	bool ParseTable(const FString& JsonText, FTable& Out, FString& OutError)
	{
		Out = FTable();
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) || !Root.IsValid())
		{
			OutError = TEXT("no es JSON");
			return false;
		}
		Root->TryGetNumberField(TEXT("version"), Out.Version);
		int32 NumTramos = DEFAULT_TRAMOS;
		Root->TryGetNumberField(TEXT("tramos_por_ronda"), NumTramos);
		if (NumTramos <= 0)
		{
			OutError = TEXT("«tramos_por_ronda» tiene que ser mayor que 0");
			return false;
		}
		Out.NumTramos = NumTramos;
		if (!ParseRounds(Root, Out, OutError) || !ParseModules(Root, Out, OutError))
		{
			Out = FTable();
			return false;
		}
		return true;
	}

	FString DefaultTablePath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/Coop/IntensityTable.json");
	}

	const FTable* GetDefaultTable()
	{
		static bool bLoaded = false;
		static FTable Cached;
		if (!bLoaded)
		{
			bLoaded = true;
			const FString Path = DefaultTablePath();
			FString Text;
			FString Error;
			if (!FFileHelper::LoadFileToString(Text, *Path))
			{
				UE_LOG(LogTortunabo, Warning, TEXT("[Intensidad] No está la tabla de intensidad (%s): el coop coloca sin ella."), *Path);
			}
			else if (!ParseTable(Text, Cached, Error))
			{
				UE_LOG(LogTortunabo, Warning, TEXT("[Intensidad] La tabla de intensidad (%s) no se entiende: %s. El coop coloca sin ella."), *Path, *Error);
			}
		}
		return Cached.IsValid() ? &Cached : nullptr;
	}

	EDifficulty DifficultyFor(const FTable& Table, int32 Round, int32 Tramo)
	{
		if (Table.Rounds.Num() == 0)
		{
			return EDifficulty::Easy;
		}
		const TArray<EDifficulty>& Row = Table.Rounds[FMath::Clamp(Round - 1, 0, Table.Rounds.Num() - 1)];
		return Row.Num() > 0 ? Row[FMath::Clamp(Tramo, 0, Row.Num() - 1)] : EDifficulty::Easy;
	}

	int32 TramoOfStep(int32 Step, int32 NumSteps, int32 NumTramos)
	{
		if (NumSteps <= 0 || NumTramos <= 0)
		{
			return 0;
		}
		const int32 Clamped = FMath::Clamp(Step, 0, NumSteps - 1);
		return FMath::Clamp(static_cast<int32>((static_cast<int64>(Clamped) * NumTramos) / NumSteps), 0, NumTramos - 1);
	}

	const FModuleRow* PickModule(const FTable& Table, EDifficulty Difficulty, uint32 Seed)
	{
		TArray<const FModuleRow*> Pool;
		for (const FModuleRow& Row : Table.Modules)
		{
			if (Row.Difficulty == Difficulty) { Pool.Add(&Row); }
		}
		return Pool.Num() > 0 ? Pool[Mix(Seed) % static_cast<uint32>(Pool.Num())] : nullptr;
	}

	TArray<FTramoPlan> PlanRound(const FTable& Table, int32 Round, uint32 Seed)
	{
		TArray<FTramoPlan> Plan;
		for (int32 t = 0; t < Table.NumTramos; ++t)
		{
			FTramoPlan& Tramo = Plan.AddDefaulted_GetRef();
			Tramo.Difficulty = DifficultyFor(Table, Round, t);
			if (const FModuleRow* Row = PickModule(Table, Tramo.Difficulty, Mix(Seed + static_cast<uint32>(t) * 7919u)))
			{
				Tramo.ModuleId = Row->Id;
				Tramo.Intensity = Row->Intensity;
				// Los módulos de puzle no llevan enemigos aunque el fichero los nombre.
				if (Tramo.Difficulty != EDifficulty::Puzzle)
				{
					Tramo.Enemies = Row->Enemies;
				}
			}
		}
		return Plan;
	}

	EKnownEnemy ResolveEnemy(const FString& Name)
	{
		const FString Key = Normalize(Name);
		if (Key == TEXT("algas") || Key == TEXT("alga"))
		{
			return EKnownEnemy::Seaweed;
		}
		return EKnownEnemy::None;
	}

	TArray<FString> UnknownEnemies(const TArray<FTramoPlan>& Plan)
	{
		TArray<FString> Out;
		for (const FTramoPlan& Tramo : Plan)
		{
			for (const FEnemyCount& Enemy : Tramo.Enemies)
			{
				if (ResolveEnemy(Enemy.Name) == EKnownEnemy::None)
				{
					Out.AddUnique(Enemy.Name);
				}
			}
		}
		return Out;
	}

	bool AllowsHazard(EDifficulty Tramo, bool bEnemy, int32 MinDifficulty)
	{
		if (!bEnemy)
		{
			return true;
		}
		switch (Tramo)
		{
			case EDifficulty::Puzzle: return false;
			case EDifficulty::Easy: return MinDifficulty <= 0;
			case EDifficulty::Medium: return MinDifficulty <= 1;
			case EDifficulty::Hard:
			default: return true;
		}
	}

	bool AllowsAnnelid(EDifficulty Tramo)
	{
		return Tramo == EDifficulty::Easy || Tramo == EDifficulty::Medium;
	}
}
