/**
* Copyright (C) 2017 Elisha Riedlinger
*
* This software is  provided 'as-is', without any express  or implied  warranty. In no event will the
* authors be held liable for any damages arising from the use of this software.
* Permission  is granted  to anyone  to use  this software  for  any  purpose,  including  commercial
* applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*   1. The origin of this software must not be misrepresented; you must not claim that you  wrote the
*      original  software. If you use this  software  in a product, an  acknowledgment in the product
*      documentation would be appreciated but is not required.
*   2. Altered source versions must  be plainly  marked as such, and  must not be  misrepresented  as
*      being the original software.
*   3. This notice may not be removed or altered from any source distribution.
*/

#include <vector>
#include "Dllmain.h"
#include "Logging.h"
#include "..\Hooking\Hook.h"

struct HOOKING
{
	void* apiproc;
	char* apiname;
	void* hookproc;
};

DWORD Counter = 0;
FARPROC wrapper_func[61] = { nullptr };
DLLEXPORTS dllexports[ArraySize];
HMODULE Script_dll[ArraySize] = { nullptr };
HMODULE EXE_dll[ArraySize] = { nullptr };
HMODULE System32_dll[ArraySize] = { nullptr };
std::vector<HOOKING> HookedProcs;

VISIT_PROCS(CREATE_PROC_STUB)

void AttachProcs(HMODULE hModule)
{
	// Init vars
	char path[MAX_PATH];

	// Loop through each WineD3D file
	for (int x = 0; x < ArraySize; x++)
	{
		// Load dll from script folder
		if (!Script_dll[x])
		{
			GetModuleFileName(hModule, path, sizeof(path));
			strcpy_s(strrchr(path, '\\'), MAX_PATH - strlen(path), "\\");
			strcat_s(path, MAX_PATH, dllname[x]);
			Script_dll[x] = LoadLibrary(path);
			Logging::LOG << "Checking " << path << "\n";
		}

		// If WineD3D dll found
		if (Script_dll[x])
		{
			Logging::LOG << "Found " << dllname[x] << "!\n";

			// Load dll from exe folder
			if (!EXE_dll[x])
			{
				GetModuleFileName(nullptr, path, sizeof(path));
				strcpy_s(strrchr(path, '\\'), MAX_PATH - strlen(path), "\\");
				strcat_s(path, MAX_PATH, dllname[x]);
				EXE_dll[x] = LoadLibrary(path);
				Logging::LOG << path << "\n";
			}

			// Load dll from System32 folder
			if (!System32_dll[x])
			{
				GetSystemDirectory(path, MAX_PATH);
				strcat_s(path, MAX_PATH, "\\");
				strcat_s(path, MAX_PATH, dllname[x]);
				System32_dll[x] = LoadLibrary(path);
				Logging::LOG << path << "\n";
			}

			// Check dlls
			if (System32_dll[x] && EXE_dll[x] != Script_dll[x])
			{
				// Loop through each export item
				for (int y = 0; y < dllexports[x].ArraySize; y++)
				{
					Logging::Log() << "Checking " << dllexports[x].Export[y] << " ...\n";

					// Get export address
					FARPROC Script_proc = Hook::GetFunctionAddress(Script_dll[x], dllexports[x].Export[y]);
					FARPROC System32_proc = Hook::GetFunctionAddress(System32_dll[x], dllexports[x].Export[y]);
					FARPROC EXE_proc = (FARPROC)1;
					if (EXE_dll[x])
					{
						EXE_proc = Hook::GetFunctionAddress(EXE_dll[x], dllexports[x].Export[y]);
					}

					// Check export addresses
					if (Script_proc && System32_proc && EXE_proc)
					{
						// Wrapper -> Script
						switch (Counter)
						{
							VISIT_PROCS(SET_PROC_STUB);
						default:
							Logging::Log() << "Too many APIs too hook...\n";
							return;
						}

						// System -> Wrapper
						Logging::Log() << "Hooking " << dllname[x] << " API " << dllexports[x].Export[y] << " ...\n";
						HOOKING NewHook;
						NewHook.apiname = dllexports[x].Export[y];
						NewHook.hookproc = wrapper_func[Counter];
						NewHook.apiproc = System32_proc;
						if ((DWORD)Hook::HotPatch(System32_proc, dllexports[x].Export[y], wrapper_func[Counter]) > 1)
						{
							// Update counter
							Counter++;

							// Logging
							Logging::Log() << "Success! Hooked " << dllexports[x].Export[y] << " count " << Counter << "\n";

							// Record hooked procs
							HookedProcs.push_back(NewHook);
						}
					}
				}
			}
		}
	}
}

// Dll main function
bool APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpReserved)
{
	UNREFERENCED_PARAMETER(lpReserved);

	static bool RunOnThreadFlag = false;

	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
	{
		// Setup Proc arrays
		VISIT_PROCS(SET_FUNC_STUB);
		VISIT_EXPORTS(SET_DLL_EXPORTS);

		// Enable logging
		char path[MAX_PATH];
		GetModuleFileName(hModule, path, sizeof(path));
		strcpy_s(strrchr(path, '.'), MAX_PATH - strlen(path), ".log");
		Logging::LOG.open(path, std::ios::trunc);

		// Attach WineD3D procs
		AttachProcs(hModule);

		// Set flag to run on next thread
		RunOnThreadFlag = true;
	}
	break;
	case DLL_THREAD_ATTACH:
		if (RunOnThreadFlag)
		{
			RunOnThreadFlag = false;

			// Logging
			Logging::Log() << "Starting thread...\n";

			// Try once more to attach WineD3D procs in case some are missed
			AttachProcs(hModule);
		}
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
	{
		// Unhook apis
		Logging::Log() << "Unhooking procs...\n";
		while (HookedProcs.size() != 0)
		{
			// Unhook proc
			Logging::Log() << "Unhooking " << HookedProcs.back().apiname << "\n";
			Hook::UnhookHotPatch(HookedProcs.back().apiproc, HookedProcs.back().apiname, HookedProcs.back().hookproc);
			HookedProcs.pop_back();
		}
		HookedProcs.clear();

		// Unload dlls
		Logging::Log() << "Unloading dlls...\n";
		for (int x = 0; x < ArraySize; x++)
		{
			if (Script_dll[x]) FreeLibrary(Script_dll[x]);
			if (EXE_dll[x]) FreeLibrary(EXE_dll[x]);
			if (System32_dll[x]) FreeLibrary(System32_dll[x]);
		}

		// Quiting
		Logging::Log() << "Quiting LoadWineD3D\n";
	}
	break;
	}
	return true;
}
