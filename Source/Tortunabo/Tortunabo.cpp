// Fill out your copyright notice in the Description page of Project Settings.

#include "Tortunabo.h"
#include "Modules/ModuleManager.h"
#include "Settings/TN_GamepadDevice.h"

/** Módulo del juego: además de lo de serie, da de alta el lector de mandos que no son de Xbox (#743, Docs/Mandos.md). */
class FTortunaboModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		TNGamepadDevice::Register();
	}

	virtual void ShutdownModule() override
	{
		TNGamepadDevice::Unregister();
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE( FTortunaboModule, Tortunabo, "Tortunabo" );
