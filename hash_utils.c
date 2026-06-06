#include "hash_utils.h"
#include <wincrypt.h>
#include <stdlib.h>

static BOOL CalcHash(const WCHAR* filePath, ALG_ID algId, WCHAR* output, DWORD outputSize)
{
    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    BOOL success = FALSE;
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;

    if (CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
    {
        if (CryptCreateHash(hProv, algId, 0, 0, &hHash))
        {
            BYTE buf[65536];
            DWORD read;
            while (ReadFile(hFile, buf, sizeof(buf), &read, NULL) && read > 0)
                CryptHashData(hHash, buf, read, 0);

            DWORD hashSize = 0, sizeSize = sizeof(DWORD);
            if (CryptGetHashParam(hHash, HP_HASHSIZE, (BYTE*)&hashSize, &sizeSize, 0))
            {
                BYTE* hashVal = (BYTE*)malloc(hashSize);
                if (hashVal && CryptGetHashParam(hHash, HP_HASHVAL, hashVal, &hashSize, 0))
                {
                    WCHAR hex[8];
                    output[0] = 0;
                    DWORD pos = 0;
                    for (DWORD i = 0; i < hashSize && pos + 2 < outputSize; i++)
                    {
                        wsprintfW(hex, L"%02x", hashVal[i]);
                        wcscpy(output + pos, hex);
                        pos += 2;
                    }
                    success = TRUE;
                }
                free(hashVal);
            }
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
    CloseHandle(hFile);
    return success;
}

BOOL CalculateHashes(const WCHAR* filePath, WCHAR* md5Out, WCHAR* sha256Out, DWORD outputSize)
{
    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    BOOL success = FALSE;
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hMd5 = 0, hSha256 = 0;
    BYTE* md5Val = NULL, *sha256Val = NULL;

    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        goto cleanup;
    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hMd5))
        goto cleanup;
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hSha256))
        goto cleanup;

    BYTE buf[65536];
    DWORD read;
    while (ReadFile(hFile, buf, sizeof(buf), &read, NULL) && read > 0)
    {
        CryptHashData(hMd5, buf, read, 0);
        CryptHashData(hSha256, buf, read, 0);
    }

    DWORD md5Size = 0, shaSize = 0, sizeSize = sizeof(DWORD);
    if (!CryptGetHashParam(hMd5, HP_HASHSIZE, (BYTE*)&md5Size, &sizeSize, 0))
        goto cleanup;
    if (!CryptGetHashParam(hSha256, HP_HASHSIZE, (BYTE*)&shaSize, &sizeSize, 0))
        goto cleanup;

    md5Val = (BYTE*)malloc(md5Size);
    sha256Val = (BYTE*)malloc(shaSize);
    if (!md5Val || !sha256Val)
        goto cleanup;

    if (!CryptGetHashParam(hMd5, HP_HASHVAL, md5Val, &md5Size, 0))
        goto cleanup;
    if (!CryptGetHashParam(hSha256, HP_HASHVAL, sha256Val, &shaSize, 0))
        goto cleanup;

    DWORD pos = 0;
    for (DWORD i = 0; i < md5Size && pos + 2 < outputSize; i++)
    {
        wsprintfW(md5Out + pos, L"%02x", md5Val[i]);
        pos += 2;
    }

    pos = 0;
    for (DWORD i = 0; i < shaSize && pos + 2 < outputSize; i++)
    {
        wsprintfW(sha256Out + pos, L"%02x", sha256Val[i]);
        pos += 2;
    }

    success = TRUE;

cleanup:
    free(md5Val);
    free(sha256Val);
    if (hMd5) CryptDestroyHash(hMd5);
    if (hSha256) CryptDestroyHash(hSha256);
    if (hProv) CryptReleaseContext(hProv, 0);
    CloseHandle(hFile);
    return success;
}

BOOL CalculateMD5(const WCHAR* filePath, WCHAR* output, DWORD outputSize)
{
    return CalcHash(filePath, CALG_MD5, output, outputSize);
}

BOOL CalculateSHA256(const WCHAR* filePath, WCHAR* output, DWORD outputSize)
{
    return CalcHash(filePath, CALG_SHA_256, output, outputSize);
}
