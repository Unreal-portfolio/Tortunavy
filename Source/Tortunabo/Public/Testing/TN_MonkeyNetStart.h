#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Misc/Parse.h"

/**
 * En qué mundo arranca el monkey pedido por línea de órdenes (-TNMonkey). Un cliente lanzado con «127.0.0.1 -game -TNMonkey=60:9»
 * que no llega a conectar vuelve al mapa por defecto (LVL_Menu) sin jugadores; si el monkey arrancase ahí, el informe saldría
 * vacío (Docs/Estres-Monkey-2026-09-29.md, hallazgo 4). Con -TNMonkeyNet=client|server|any se elige el mundo; sin él, un proceso
 * cuyo primer argumento es la dirección de un servidor espera al mundo de cliente. Lógica pura: Tortunabo.Monkey.NetStart.
 */
namespace TNMonkey
{
	enum class ENetFilter : uint8
	{
		/** Cualquier mundo de juego (el comportamiento de siempre). */
		Any,
		/** Solo el mundo conectado a un servidor (NM_Client). */
		Client,
		/** Solo el mundo que hace de servidor (listen o dedicado). */
		Server
	};

	inline const TCHAR* NetFilterName(ENetFilter Filter)
	{
		switch (Filter)
		{
			case ENetFilter::Client: return TEXT("client");
			case ENetFilter::Server: return TEXT("server");
			default:                 return TEXT("any");
		}
	}

	/** «client», «server» o «any» (sin distinguir mayúsculas). false si el texto no es ninguno. */
	inline bool ParseNetFilter(const FString& Text, ENetFilter& Out)
	{
		const FString Value = Text.TrimStartAndEnd();
		if (Value.Equals(TEXT("client"), ESearchCase::IgnoreCase))
		{
			Out = ENetFilter::Client;
			return true;
		}
		if (Value.Equals(TEXT("server"), ESearchCase::IgnoreCase))
		{
			Out = ENetFilter::Server;
			return true;
		}
		if (Value.Equals(TEXT("any"), ESearchCase::IgnoreCase))
		{
			Out = ENetFilter::Any;
			return true;
		}
		return false;
	}

	/**
	 * ¿Es la dirección de un servidor y no un mapa? IPv4 con o sin puerto, «localhost[:puerto]» o «steam.<id>». Los mapas
	 * («LVL_BeachRace», «/Game/Maps/...», con «?listen») no lo son.
	 */
	inline bool LooksLikeServerAddress(const FString& Token)
	{
		FString Host = Token.TrimStartAndEnd();
		if (Host.IsEmpty() || Host.StartsWith(TEXT("/")) || Host.StartsWith(TEXT("-")) || Host.Contains(TEXT("?")))
		{
			return false;
		}
		if (Host.StartsWith(TEXT("steam."), ESearchCase::IgnoreCase))
		{
			return true;
		}
		FString Port;
		if (Host.Split(TEXT(":"), &Host, &Port) && (Port.IsEmpty() || !Port.IsNumeric()))
		{
			return false;
		}
		if (Host.Equals(TEXT("localhost"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		TArray<FString> Parts;
		Host.ParseIntoArray(Parts, TEXT("."), false);
		if (Parts.Num() != 4)
		{
			return false;
		}
		for (const FString& Part : Parts)
		{
			if (Part.IsEmpty() || Part.Len() > 3 || !Part.IsNumeric() || FCString::Atoi(*Part) > 255)
			{
				return false;
			}
		}
		return true;
	}

	/** Primer argumento de la línea de órdenes que no es una opción («-x») ni el .uproject: el mapa o la dirección. */
	inline FString FirstUrlToken(const TCHAR* CommandLine)
	{
		FString Token;
		const TCHAR* Cursor = CommandLine;
		while (FParse::Token(Cursor, Token, false))
		{
			if (!Token.StartsWith(TEXT("-")) && !Token.EndsWith(TEXT(".uproject"), ESearchCase::IgnoreCase))
			{
				return Token;
			}
		}
		return FString();
	}

	/** Filtro efectivo: el de -TNMonkeyNet si es válido; si no, Client cuando el proceso se conecta a una dirección y Any en el resto. */
	inline ENetFilter ResolveNetFilter(const FString& Requested, const FString& FirstToken)
	{
		ENetFilter Parsed = ENetFilter::Any;
		if (!Requested.IsEmpty() && ParseNetFilter(Requested, Parsed))
		{
			return Parsed;
		}
		return LooksLikeServerAddress(FirstToken) ? ENetFilter::Client : ENetFilter::Any;
	}

	/** ¿Arranca el monkey en un mundo con este modo de red? */
	inline bool ShouldStartIn(ENetFilter Filter, ENetMode Mode)
	{
		switch (Filter)
		{
			case ENetFilter::Client: return Mode == NM_Client;
			case ENetFilter::Server: return Mode == NM_ListenServer || Mode == NM_DedicatedServer;
			default:                 return true;
		}
	}
}
