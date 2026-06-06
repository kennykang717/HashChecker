#pragma once
#include <windows.h>

BOOL CalculateMD5(const WCHAR* filePath, WCHAR* output, DWORD outputSize);
BOOL CalculateSHA256(const WCHAR* filePath, WCHAR* output, DWORD outputSize);
BOOL CalculateHashes(const WCHAR* filePath, WCHAR* md5Out, WCHAR* sha256Out, DWORD outputSize);
