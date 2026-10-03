#include "Multiplayer/TN_LocalPlayRules.h"

TArray<TNLocalPlay::FViewRect> TNLocalPlay::SplitLayout(int32 NumPlayers)
{
	const int32 Count = FMath::Clamp(NumPlayers, 1, MaxPlayers);
	TArray<FViewRect> Out;
	Out.Reserve(Count);
	if (Count == 1)
	{
		Out.Add(FViewRect{ 0.f, 0.f, 1.f, 1.f });
		return Out;
	}
	if (Count == 2)
	{
		// En horizontal: cada uno a lo ancho, uno encima del otro.
		Out.Add(FViewRect{ 0.f, 0.f, 1.f, 0.5f });
		Out.Add(FViewRect{ 0.f, 0.5f, 1.f, 0.5f });
		return Out;
	}
	// Cuadrantes, en orden de lectura; con tres, el de abajo a la derecha queda libre.
	const FViewRect Quadrants[] = { { 0.f, 0.f, 0.5f, 0.5f }, { 0.5f, 0.f, 0.5f, 0.5f }, { 0.f, 0.5f, 0.5f, 0.5f }, { 0.5f, 0.5f, 0.5f, 0.5f } };
	for (int32 i = 0; i < Count; ++i)
	{
		Out.Add(Quadrants[i]);
	}
	return Out;
}

bool TNLocalPlay::EmptyQuadrant(int32 NumPlayers, FViewRect& OutRect)
{
	if (NumPlayers != 3)
	{
		return false;
	}
	OutRect = FViewRect{ 0.5f, 0.5f, 0.5f, 0.5f };
	return true;
}

float TNLocalPlay::UIScaleForViews(int32 NumViews)
{
	if (NumViews <= 1)
	{
		return 1.f;
	}
	return NumViews == 2 ? 0.75f : 0.6f;
}

bool TNLocalPlay::ShouldReduceQuality(int32 NumViews)
{
	return NumViews >= 3;
}

int32 TNLocalPlay::ReducedQualityLevel(int32 Level)
{
	return Level < 0 ? Level : FMath::Max(0, Level - 1);
}

TNLocalPlay::EJoin TNLocalPlay::DecideJoin(const FJoinQuery& Query)
{
	if (!Query.bLocalMode)
	{
		return EJoin::NotLocal;
	}
	if (!Query.bGamepad)
	{
		return EJoin::Keyboard;
	}
	if (Query.bDeviceHasPlayer)
	{
		return EJoin::AlreadyPlaying;
	}
	if (!Query.bInLobby)
	{
		return EJoin::NotLobby;
	}
	if (Query.Players >= MaxPlayers)
	{
		return EJoin::Full;
	}
	return EJoin::Accept;
}

int32 TNLocalPlay::NextGuestNumber(const TArray<int32>& TakenNumbers)
{
	for (int32 Number = 2; Number <= MaxPlayers; ++Number)
	{
		if (!TakenNumbers.Contains(Number))
		{
			return Number;
		}
	}
	return INDEX_NONE;
}

bool TNLocalPlay::CanLeave(bool bLocalMode, bool bInLobby, bool bPrimary)
{
	return bLocalMode && bInLobby && !bPrimary;
}

bool TNLocalPlay::ShouldSave(bool bLocalMode, bool bPrimary)
{
	return !bLocalMode || bPrimary;
}

bool TNLocalPlay::AllowsVR(bool bLocalMode, int32 NumLocalPlayers)
{
	return !bLocalMode || NumLocalPlayers <= 1;
}

TArray<int32> TNLocalPlay::PadsToRelease(const TArray<int32>& PrimaryUserPads, int32 ChosenPad)
{
	TArray<int32> Out;
	for (const int32 Pad : PrimaryUserPads)
	{
		if (Pad != ChosenPad)
		{
			Out.AddUnique(Pad);
		}
	}
	return Out;
}

void TNLocalPlay::CopyPerPlayerSettings(const FTNGameSettings& From, FTNGameSettings& To)
{
	To.MouseSensitivity = From.MouseSensitivity;
	To.GamepadSensitivity = From.GamepadSensitivity;
	To.bInvertMouseY = From.bInvertMouseY;
	To.bInvertGamepadY = From.bInvertGamepadY;
	To.KeyOverrides = From.KeyOverrides;
	To.PauseKey = From.PauseKey;
	To.PausePadKey = From.PausePadKey;
	To.bCameraShake = From.bCameraShake;
	To.FieldOfViewOffset = From.FieldOfViewOffset;
}

FTNGameSettings TNLocalPlay::EffectiveSettings(const FTNGameSettings& Shared, const FTNGameSettings& Own)
{
	FTNGameSettings Out = Shared;
	CopyPerPlayerSettings(Own, Out);
	return Out;
}
