/////////////////////////////////////////////
//                                         //
//    Copyright (C) 2023-2023 Julian Uy    //
//  https://sites.google.com/site/awertyb  //
//                                         //
//   See details of license at "LICENSE"   //
//                                         //
/////////////////////////////////////////////

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define MIDL_user_allocate MIDL_user_allocate_workaround
#include <objidl.h>
#undef MIDL_user_allocate
#include "tp_stub.h"
#include "plthook.h"
#include <vector>
#include <algorithm>

#define EXPORT(hr) extern "C" __declspec(dllexport) hr __stdcall

#ifndef tjs_string
#define tjs_string std::wstring
#endif

static HMODULE this_hmodule = NULL;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD Reason, LPVOID lpReserved)
{
	if (Reason == DLL_PROCESS_ATTACH)
	{
		this_hmodule = hModule;
		if (hModule != NULL)
		{
			DisableThreadLibraryCalls(hModule);
		}
	}
	return TRUE;
}

struct tTVPFoundPlugin
{
	tjs_string Path;
	tjs_string Name;
	bool operator < (const tTVPFoundPlugin &rhs) const { return Name < rhs.Name; }
};
static void TVPSearchPluginsAt(std::vector<tTVPFoundPlugin> &list, tjs_string folder)
{
	WIN32_FIND_DATA ffd;
	HANDLE handle = ::FindFirstFile((folder + TJS_W("*.tpm")).c_str(), &ffd);
	if(handle != INVALID_HANDLE_VALUE)
	{
		BOOL cont;
		do
		{
			if(!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				tTVPFoundPlugin fp;
				fp.Path = folder;
				fp.Name = ffd.cFileName;
				list.push_back(fp);
			}
			cont = FindNextFile(handle, &ffd);
		} while(cont);
		FindClose(handle);
	}
}

static tjs_int PluginLoadedCur = 0;
static tjs_int PluginLoadedMax = 0;

static void TVPCountPlugins(void)
{
	// This function searches plugins which have an extension of ".tpm"
	// in the default path: 
	//    1. a folder which holds kirikiri executable
	//    2. "plugin" folder of it
	// Plugin load order is to be decided using its name;
	// aaa.tpm is to be loaded before aab.tpm (sorted by ASCII order)

	// search plugins from path: (exepath), (exepath)\system, (exepath)\plugin
	std::vector<tTVPFoundPlugin> list;

	ttstr exepath_ttstr = TVPGetAppPath();
	TVPGetLocalName(exepath_ttstr);
	tjs_string exepath = exepath_ttstr.c_str();

	TVPSearchPluginsAt(list, exepath);
	TVPSearchPluginsAt(list, exepath + TJS_W("system\\"));
#ifdef _M_X64
	TVPSearchPluginsAt(list, exepath + TJS_W("plugin64\\"));
#else
	TVPSearchPluginsAt(list, exepath + TJS_W("plugin\\"));
#endif

	bool increment_plugin_loaded_count = true;


	tjs_string plugin_path = TJS_W("");
	{
		WCHAR* modnamebuf = new WCHAR[32768];
		if (modnamebuf)
		{
			if (this_hmodule)
			{
				DWORD ret_len = GetModuleFileNameW(this_hmodule, modnamebuf, 32768);
				if (ret_len)
				{
					plugin_path = modnamebuf;
				}
			}
			delete[] modnamebuf;
		}
	}

	// sort by filename
	std::sort(list.begin(), list.end());

	// load each plugin
	for(std::vector<tTVPFoundPlugin>::iterator i = list.begin();
		i != list.end();
		i++)
	{
		tjs_string toload_plugin_fullpath = i->Path + i->Name;

		if (increment_plugin_loaded_count)
		{
			PluginLoadedCur += 1;
			if (toload_plugin_fullpath == plugin_path)
			{
				increment_plugin_loaded_count = false;
			}
		}
		PluginLoadedMax += 1;
	}
}

static void DisablePluginBlocker(void);

static HMODULE this_ntdll_stub = NULL;

typedef HMODULE (WINAPI *LOADLIBRARYA)(LPCSTR lpLibFileName);
static LOADLIBRARYA fpLoadLibraryA = NULL;

static HMODULE WINAPI DetourLoadLibraryA(LPCSTR lpLibFileName)
{
	if (lpLibFileName != NULL)
	{
		size_t path_len = strlen(lpLibFileName);
		if (((path_len >= 4) && (!strcmp(lpLibFileName + path_len - 4, ".tpm"))) || (path_len == 0))
		{
			PluginLoadedCur += 1;
			if (PluginLoadedCur >= PluginLoadedMax)
			{
				DisablePluginBlocker();
			}
			// It might be good to return a loadlibrary to ntdll.dll so that the incremented/deincremented will match.
			this_ntdll_stub = fpLoadLibraryA("ntdll.dll");
			return this_ntdll_stub;
		}
	}
	return fpLoadLibraryA(lpLibFileName);
}

typedef HMODULE (WINAPI *LOADLIBRARYW)(LPCWSTR lpLibFileName);
static LOADLIBRARYW fpLoadLibraryW = NULL;

static HMODULE WINAPI DetourLoadLibraryW(LPCWSTR lpLibFileName)
{
	if (lpLibFileName != NULL)
	{
		size_t path_len = wcslen(lpLibFileName);
		if (path_len >= 4)
		{
			if (!wcscmp(lpLibFileName + path_len - 4, L".tpm"))
			{
				PluginLoadedCur += 1;
				if (PluginLoadedCur >= PluginLoadedMax)
				{
					DisablePluginBlocker();
				}
				// It might be good to return a loadlibrary to ntdll.dll so that the incremented/deincremented will match.
				this_ntdll_stub = fpLoadLibraryW(L"ntdll.dll");
				return this_ntdll_stub;
			}
		}
	}
	return fpLoadLibraryW(lpLibFileName);
}

static plthook_t *plthook_blocker = NULL;

static void EnablePluginBlocker(void)
{
	if (plthook_blocker != NULL)
	{
		return;
	}

	if (plthook_open(&plthook_blocker, NULL) != 0)
	{
		return;
	}
	plthook_replace(plthook_blocker, "LoadLibraryA", reinterpret_cast<LPVOID>(DetourLoadLibraryA), reinterpret_cast<LPVOID*>(&fpLoadLibraryA));
	plthook_replace(plthook_blocker, "LoadLibraryW", reinterpret_cast<LPVOID>(DetourLoadLibraryW), reinterpret_cast<LPVOID*>(&fpLoadLibraryW));
}

static void DisablePluginBlocker(void)
{
	if (plthook_blocker == NULL)
	{
		return;
	}
	plthook_replace(plthook_blocker, "LoadLibraryA", reinterpret_cast<LPVOID>(fpLoadLibraryA), NULL);
	plthook_replace(plthook_blocker, "LoadLibraryW", reinterpret_cast<LPVOID>(fpLoadLibraryW), NULL);
	plthook_close(plthook_blocker);
	plthook_blocker = NULL;
	if (this_ntdll_stub)
	{
		this_ntdll_stub = NULL;
	}
}

EXPORT(HRESULT)
V2Link(iTVPFunctionExporter *exporter)
{
	TVPInitImportStub(exporter);
	TVPCountPlugins();
	EnablePluginBlocker();
	return S_OK;
}

EXPORT(HRESULT)
V2Unlink(void)
{
	DisablePluginBlocker();
	TVPUninitImportStub();
	return S_OK;
}

