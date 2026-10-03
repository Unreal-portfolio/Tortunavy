#include "Testing/TN_CpuCoreProbe.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace TNCpuCoreDetail
{
#if PLATFORM_WINDOWS
	// Funciones de Windows 10 que los cabeceros del motor no declaran (fija _WIN32_WINNT más bajo): se buscan en kernel32.
	using FGetSystemCpuSetInformation = BOOL(WINAPI*)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);
	using FSetProcessInformation = BOOL(WINAPI*)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
	using FSetThreadInformation = BOOL(WINAPI*)(HANDLE, THREAD_INFORMATION_CLASS, LPVOID, DWORD);
	using FSetProcessDefaultCpuSets = BOOL(WINAPI*)(HANDLE, const ULONG*, ULONG);

	template <typename T>
	T Kernel32(const char* Name)
	{
		const HMODULE Module = GetModuleHandleW(L"kernel32.dll");
		// Paso por void* para evitar C4191 (FARPROC a otra firma); la firma se comprueba a mano arriba.
		return Module ? reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(Module, Name))) : nullptr;
	}

	/** Clase de eficiencia por (grupo, índice lógico), leída una vez de GetSystemCpuSetInformation. */
	struct FCoreTable
	{
		TMap<uint32, uint8> ClassByCore;
		/** Id de conjunto de CPU de cada núcleo lógico de la clase más alta (rendimiento). */
		TArray<ULONG> PerformanceSetIds;
		uint8 MinClass = MAX_uint8;
		uint8 MaxClass = 0;

		static uint32 Key(uint16 Group, uint8 Index) { return (static_cast<uint32>(Group) << 8) | Index; }

		FCoreTable()
		{
			const FGetSystemCpuSetInformation GetCpuSets = Kernel32<FGetSystemCpuSetInformation>("GetSystemCpuSetInformation");
			if (!GetCpuSets)
			{
				return;
			}
			ULONG Length = 0;
			GetCpuSets(nullptr, 0, &Length, GetCurrentProcess(), 0);
			if (Length == 0)
			{
				return;
			}
			TArray<uint8> Buffer;
			Buffer.SetNumZeroed(static_cast<int32>(Length));
			auto* First = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(Buffer.GetData());
			if (!GetCpuSets(First, Length, &Length, GetCurrentProcess(), 0))
			{
				return;
			}
			for (ULONG Offset = 0; Offset < Length;)
			{
				const auto* Info = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(Buffer.GetData() + Offset);
				if (Info->Size == 0)
				{
					break;
				}
				if (Info->Type == CpuSetInformation)
				{
					const uint8 Class = Info->CpuSet.EfficiencyClass;
					ClassByCore.Add(Key(Info->CpuSet.Group, Info->CpuSet.LogicalProcessorIndex), Class);
					MinClass = FMath::Min(MinClass, Class);
					MaxClass = FMath::Max(MaxClass, Class);
				}
				Offset += Info->Size;
			}
			for (ULONG Offset = 0; Offset < Length;)
			{
				const auto* Info = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(Buffer.GetData() + Offset);
				if (Info->Size == 0)
				{
					break;
				}
				if (Info->Type == CpuSetInformation && Info->CpuSet.EfficiencyClass == MaxClass)
				{
					PerformanceSetIds.Add(Info->CpuSet.Id);
				}
				Offset += Info->Size;
			}
		}
	};

	const FCoreTable& Table()
	{
		static const FCoreTable Instance;
		return Instance;
	}
#endif
}

int32 TNCpuCore::CurrentEfficiencyClass()
{
#if PLATFORM_WINDOWS
	PROCESSOR_NUMBER Number;
	GetCurrentProcessorNumberEx(&Number);
	const uint8* Class = TNCpuCoreDetail::Table().ClassByCore.Find(TNCpuCoreDetail::FCoreTable::Key(Number.Group, Number.Number));
	return Class ? static_cast<int32>(*Class) : INDEX_NONE;
#else
	return INDEX_NONE;
#endif
}

bool TNCpuCore::IsHybrid()
{
#if PLATFORM_WINDOWS
	const TNCpuCoreDetail::FCoreTable& Cores = TNCpuCoreDetail::Table();
	return Cores.ClassByCore.Num() > 0 && Cores.MaxClass > Cores.MinClass;
#else
	return false;
#endif
}

bool TNCpuCore::IsOnEfficiencyCore()
{
#if PLATFORM_WINDOWS
	if (!IsHybrid())
	{
		return false;
	}
	const int32 Class = CurrentEfficiencyClass();
	return Class != INDEX_NONE && Class == TNCpuCoreDetail::Table().MinClass;
#else
	return false;
#endif
}

bool TNCpuCore::RequestHighQoS()
{
#if PLATFORM_WINDOWS
	const auto SetProcessInfo = TNCpuCoreDetail::Kernel32<TNCpuCoreDetail::FSetProcessInformation>("SetProcessInformation");
	const auto SetThreadInfo = TNCpuCoreDetail::Kernel32<TNCpuCoreDetail::FSetThreadInformation>("SetThreadInformation");
	if (!SetProcessInfo || !SetThreadInfo)
	{
		return false;
	}
	// ControlMask con el bit y StateMask a 0: «EcoQoS apagado explícitamente», que Windows trata como QoS alta.
	PROCESS_POWER_THROTTLING_STATE Process = {};
	Process.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	Process.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
	Process.StateMask = 0;
	const bool bProcess = SetProcessInfo(GetCurrentProcess(), ProcessPowerThrottling, &Process, sizeof(Process)) != 0;

	THREAD_POWER_THROTTLING_STATE Thread = {};
	Thread.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
	Thread.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
	Thread.StateMask = 0;
	const bool bThread = SetThreadInfo(GetCurrentThread(), ThreadPowerThrottling, &Thread, sizeof(Thread)) != 0;
	return bProcess && bThread;
#else
	return false;
#endif
}

bool TNCpuCore::PreferPerformanceCores()
{
#if PLATFORM_WINDOWS
	const TNCpuCoreDetail::FCoreTable& Cores = TNCpuCoreDetail::Table();
	const auto SetDefaultCpuSets = TNCpuCoreDetail::Kernel32<TNCpuCoreDetail::FSetProcessDefaultCpuSets>("SetProcessDefaultCpuSets");
	if (!IsHybrid() || Cores.PerformanceSetIds.Num() == 0 || !SetDefaultCpuSets)
	{
		return false;
	}
	// Conjunto por defecto del proceso: vale para los hilos que no fijan el suyo (juego, tareas); no es afinidad dura.
	return SetDefaultCpuSets(GetCurrentProcess(), Cores.PerformanceSetIds.GetData(), static_cast<ULONG>(Cores.PerformanceSetIds.Num())) != 0;
#else
	return false;
#endif
}
