/////////////////////////////////////////////
//                                         //
//    Copyright (C) 2023-2023 Julian Uy    //
//  https://sites.google.com/site/awertyb  //
//                                         //
//   See details of license at "LICENSE"   //
//                                         //
/////////////////////////////////////////////

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
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

// KRSELFLOAD CODE BEGIN
// Modified TVPGetXP3ArchiveOffset from XP3Archive.cpp
static bool IsXP3File(IStream *st)
{
	st->Seek({0}, STREAM_SEEK_SET, NULL);
	tjs_uint8 mark[11+1];
	static tjs_uint8 XP3Mark1[] =
		{ 0x58/*'X'*/, 0x50/*'P'*/, 0x33/*'3'*/, 0x0d/*'\r'*/,
		  0x0a/*'\n'*/, 0x20/*' '*/, 0x0a/*'\n'*/, 0x1a/*EOF*/,
		  0xff /* sentinel */,
		// Extra junk data to break it up a bit (in case of compiler optimization)
		0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
		};
	static tjs_uint8 XP3Mark2[] =
		{ 0x8b, 0x67, 0x01, 0xff/* sentinel */ };

	// XP3 header mark contains:
	// 1. line feed and carriage return to detect corruption by unnecessary
	//    line-feeds convertion
	// 2. 1A EOF mark which indicates file's text readable header ending.
	// 3. 8B67 KANJI-CODE to detect curruption by unnecessary code convertion
	// 4. 01 file structure version and character coding
	//    higher 4 bits are file structure version, currently 0.
	//    lower 4 bits are character coding, currently 1, is BMP 16bit Unicode.

	static tjs_uint8 XP3Mark[11+1];
		// +1: I was warned by CodeGuard that the code will do
		// access overrun... because a number of 11 is not aligned by DWORD,
		// and the processor may read the value of DWORD at last of this array
		// from offset 8. Then the last 1 byte would cause a fail.
#if 0
	static bool DoInit = true;
	if(DoInit)
	{
		// the XP3 header above is splitted into two part; to avoid
		// mis-finding of the header in the program's initialized data area.
		DoInit = false;
		memcpy(XP3Mark, XP3Mark1, 8);
		memcpy(XP3Mark + 8, XP3Mark2, 3);
		// here joins it.
	}
#else
	if (memcmp(XP3Mark, XP3Mark1, 8))
	{
		memcpy(XP3Mark, XP3Mark1, 8);
		memcpy(XP3Mark + 8, XP3Mark2, 3);
	}
#endif

	mark[0] = 0; // sentinel
	st->Read(mark, 11, NULL);
	if(mark[0] == 0x4d/*'M'*/ && mark[1] == 0x5a/*'Z'*/)
	{
		// "MZ" is a mark of Win32/DOS executables,
		// TVP searches the first mark of XP3 archive
		// in the executeble file.
		bool found = false;

		st->Seek({16}, STREAM_SEEK_SET, NULL);

		// XP3 mark must be aligned by a paragraph ( 16 bytes )
		const tjs_uint one_read_size = 256*1024;
		ULONG read;
		tjs_uint8 buffer[one_read_size]; // read 256kbytes at once

		while(st->Read(buffer, one_read_size, &read) == S_OK && read != 0)
		{
			tjs_uint p = 0;
			while(p<read)
			{
				if(!memcmp(XP3Mark, buffer + p, 11))
				{
					// found the mark
					found = true;
					break;
				}
				p+=16;
			}
			if(found) break;
		}

		if(!found)
		{
			return false;
		}
	}
	else if(!memcmp(XP3Mark, mark, 11))
	{
	}
	else
	{
		return false;
	}

	return true;
}

// Overridable function, in case you want to set up your own storage (e.g. minizip or libsquashfs)
// Return false to skip the XP3 file locator and current directory setting.
extern "C" bool __attribute__((weak)) prepare_storage(void)
{
	return true;
}
// KRSELFLOAD CODE END

EXPORT(HRESULT)
V2Link(iTVPFunctionExporter *exporter)
{
	TVPInitImportStub(exporter);
	TVPCountPlugins();
	EnablePluginBlocker();

	// KRSELFLOAD CODE BEGIN
	if (prepare_storage())
	{
		WCHAR* modnamebuf = new WCHAR[32768];
		if (modnamebuf)
		{
			if (this_hmodule)
			{
				DWORD ret_len = GetModuleFileNameW(this_hmodule, modnamebuf, 32768);
				if (ret_len)
				{
					ttstr arcname = modnamebuf;
					ttstr normmodname = TVPNormalizeStorageName(modnamebuf);
					arcname += TJS_W(">");
					ttstr normarcname = TVPNormalizeStorageName(arcname);
					IStream *in = TVPCreateIStream(normmodname, TJS_BS_READ);
					if (in)
					{
						if (IsXP3File(in))
						{
							TVPSetCurrentDirectory(normarcname);
						}
						in->Release();
					}
				}
			}
			delete[] modnamebuf;
		}
	}

	{
		const tjs_char name_krselfload[] = TJS_W("krselfload.tjs");
		if (TVPIsExistentStorage(name_krselfload))
		{
			TVPExecuteStorage(name_krselfload, NULL, false, TJS_W(""));
		}
	}
	// KRSELFLOAD CODE END

	return S_OK;
}

EXPORT(HRESULT)
V2Unlink(void)
{
	DisablePluginBlocker();
	TVPUninitImportStub();
	return S_OK;
}

