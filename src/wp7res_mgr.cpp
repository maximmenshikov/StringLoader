#include "stdafx.h"

/**
 * Read an entire file into a freshly allocated buffer.
 *
 * @param FileName    Path of the file to read.
 * @param Result      Receives a malloc'd buffer with the file contents, or
 *                    NULL for an empty file; the caller must free it.
 *
 * @return S_OK on success (including an empty file), or an HRESULT carrying the
 *         Win32 error code on failure.
 */
HRESULT
ReadFileComplete(BSTR FileName, void **Result)
{
    *Result = NULL;

    WIN32_FILE_ATTRIBUTE_DATA fAttributes;

    if (!GetFileAttributesEx(FileName, GetFileExInfoStandard, &fAttributes))
    {
        return GetLastError();
    }
    if (fAttributes.nFileSizeLow == 0)
    {
        return S_OK;
    }

    HANDLE hFile = CreateFile(FileName,     // file name
                              GENERIC_READ, // open for reading
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE, // share
                              NULL,                  // default security
                              OPEN_EXISTING,         // existing file only
                              FILE_ATTRIBUTE_NORMAL, // normal file
                              NULL);                 // no template

    if (hFile == INVALID_HANDLE_VALUE)
        return GetLastError();

    DWORD dwFileSize = GetFileSize(hFile, NULL);

    if (dwFileSize == INVALID_FILE_SIZE)
    {
        HRESULT error = GetLastError();
        CloseHandle(hFile);
        return error;
    }
    if (dwFileSize == 0)
    {
        CloseHandle(hFile);
        return S_OK;
    }

    void *buffer = malloc(dwFileSize);
    if (buffer == NULL)
    {
        HRESULT error = GetLastError();
        CloseHandle(hFile);
        return error;
    }

    DWORD dwRead = 0;
    int readResult = ReadFile(hFile, buffer, dwFileSize, &dwRead, 0);
    if (readResult == 0)
    {
        HRESULT error = GetLastError();
        CloseHandle(hFile);
        free(buffer);
        return error;
    }

    *Result = buffer;

    CloseHandle(hFile);

    return S_OK;
}

/**
 * Load a localized string resource from a module.
 *
 * The module is first parsed as a raw PE image: its resource directory is
 * walked to the string table for the given id, picking the best language match
 * (system locale, then neutral, then English). If that fails, the function
 * falls back to LoadLibraryEx/LoadString.
 *
 * @param Dll         Module name; a bare name is resolved under \Windows.
 * @param ResourceID  String resource identifier.
 * @param Result      Receives the string in a newly allocated buffer, or NULL
 *                    if not found; the caller owns the allocation.
 *
 * @return S_OK when the lookup completed, or an HRESULT carrying the Win32
 *         error code on failure.
 */
extern "C" HRESULT
ReadResourceString(BSTR Dll, uint ResourceID, BSTR *Result)
{
    if (Dll == NULL || Result == NULL)
        return E_INVALIDARG;

    wchar_t tempDllName[0x200];
    if (wcschr(Dll, L'\\'))
    {
        wcscpy_s(tempDllName, 0x200, Dll);
    }
    else
    {
        wcscpy_s(tempDllName, 0x200, L"\\Windows\\");
        wcscat_s(tempDllName, 0x200, Dll);
    }

    *Result = NULL;
    bool found = false;
    void *base = NULL;
    WCHAR *stringPtr = NULL;
    WCHAR stringLen = 0;

    DWORD attr = ::GetFileAttributes(tempDllName);
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        return GetLastError();
    }
    if ((attr & 0x00002000) == 0)
    {
        __try
        {
            HRESULT getBaseResult = ReadFileComplete(tempDllName, &base);

            if (getBaseResult >= 0)
            {
                DWORD blockID = (ResourceID >> 4) + 1;
                DWORD itemID = ResourceID % 0x10;

                PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)base;
                if (pDosHeader->e_magic == IMAGE_DOS_SIGNATURE)
                {
                    PIMAGE_NT_HEADERS pNtHeaders =
                        (PIMAGE_NT_HEADERS)((ULONG_PTR)base +
                                            pDosHeader->e_lfanew);
                    if (pNtHeaders->Signature == IMAGE_NT_SIGNATURE)
                    {
                        if (pNtHeaders->OptionalHeader.NumberOfRvaAndSizes >=
                            IMAGE_DIRECTORY_ENTRY_RESOURCE)
                        {
                            DWORD resourceDirRVA =
                                pNtHeaders->OptionalHeader
                                    .DataDirectory
                                        [IMAGE_DIRECTORY_ENTRY_RESOURCE]
                                    .VirtualAddress;
                            ULONG resourceDirSize =
                                pNtHeaders->OptionalHeader
                                    .DataDirectory
                                        [IMAGE_DIRECTORY_ENTRY_RESOURCE]
                                    .Size;

                            PIMAGE_SECTION_HEADER section =
                                IMAGE_FIRST_SECTION(pNtHeaders);
                            bool sectionFound = false;
                            for (int i = 0;
                                 i < pNtHeaders->FileHeader.NumberOfSections;
                                 i++)
                            {
                                if ((resourceDirRVA >=
                                     section->VirtualAddress) &&
                                    (resourceDirRVA <
                                     (section->VirtualAddress +
                                      section->Misc.VirtualSize)))
                                {
                                    sectionFound = true;
                                    break;
                                }
                                else
                                    section++;
                            }
                            if (sectionFound)
                            {
                                void *pRoot =
                                    (void *)((ULONG_PTR)base + resourceDirRVA +
                                             section->PointerToRawData -
                                             (ULONG_PTR)
                                                 section->VirtualAddress);

                                IMAGE_RESOURCE_DIRECTORY *pResourceDir =
                                    (IMAGE_RESOURCE_DIRECTORY *)pRoot;

                                IMAGE_RESOURCE_DIRECTORY_ENTRY *rootEntries =
                                    (IMAGE_RESOURCE_DIRECTORY_ENTRY
                                         *)(pResourceDir + 1);
                                IMAGE_RESOURCE_DIRECTORY *pStringDir = NULL;
                                for (int i = pResourceDir->NumberOfNamedEntries;
                                     i < (pResourceDir->NumberOfNamedEntries +
                                          pResourceDir->NumberOfIdEntries);
                                     i++)
                                {
                                    if ((rootEntries[i].Id == 6) &&
                                        (rootEntries[i]
                                             .DataIsDirectory)) // string_id = 6
                                    {
                                        pStringDir =
                                            (IMAGE_RESOURCE_DIRECTORY
                                                 *)((char *)pRoot +
                                                    rootEntries[i]
                                                        .OffsetToDirectory);
                                        break;
                                    }
                                }

                                if (pStringDir != NULL)
                                {
                                    IMAGE_RESOURCE_DIRECTORY_ENTRY
                                    *stringEntries =
                                        (IMAGE_RESOURCE_DIRECTORY_ENTRY
                                             *)(pStringDir + 1);
                                    IMAGE_RESOURCE_DIRECTORY *pSearchIdDir =
                                        NULL;
                                    for (int i =
                                             pStringDir->NumberOfNamedEntries;
                                         i < (pStringDir->NumberOfNamedEntries +
                                              pStringDir->NumberOfIdEntries);
                                         i++)
                                    {
                                        if ((stringEntries[i].Id == blockID) &&
                                            (stringEntries[i].DataIsDirectory))
                                        {
                                            pSearchIdDir =
                                                (IMAGE_RESOURCE_DIRECTORY
                                                     *)((char *)pRoot +
                                                        stringEntries[i]
                                                            .OffsetToDirectory);
                                            break;
                                        }
                                    }

                                    if (pSearchIdDir != NULL)
                                    {
                                        short foundLanguagePriority = 100;
                                        LCID systemLCID =
                                            ::GetSystemDefaultLCID();
                                        WORD LangID0 =
                                            LANGIDFROMLCID(systemLCID);
                                        WORD LangID1 = MAKELANGID(
                                            PRIMARYLANGID(systemLCID),
                                            SUBLANG_NEUTRAL);
                                        WORD LangID2 = MAKELANGID(
                                            LANG_NEUTRAL, SUBLANG_NEUTRAL);
                                        WORD LangID3 = MAKELANGID(
                                            LANG_ENGLISH, SUBLANG_DEFAULT);

                                        IMAGE_RESOURCE_DIRECTORY_ENTRY
                                        *searchIdEntries =
                                            (IMAGE_RESOURCE_DIRECTORY_ENTRY
                                                 *)(pSearchIdDir + 1);
                                        IMAGE_RESOURCE_DIRECTORY *pResultDir =
                                            NULL;
                                        IMAGE_RESOURCE_DATA_ENTRY
                                        *pResourceDataEntry = NULL;
                                        for (int i = pSearchIdDir
                                                         ->NumberOfNamedEntries;
                                             i <
                                             (pSearchIdDir
                                                  ->NumberOfNamedEntries +
                                              pSearchIdDir->NumberOfIdEntries);
                                             i++)
                                        {
                                            if (!searchIdEntries[i]
                                                     .DataIsDirectory)
                                            {
                                                if (pResourceDataEntry == NULL)
                                                    pResourceDataEntry =
                                                        (IMAGE_RESOURCE_DATA_ENTRY
                                                             *)((char *)pRoot +
                                                                searchIdEntries[i]
                                                                    .OffsetToDirectory);

                                                if (searchIdEntries[i].Id ==
                                                    LangID0)
                                                {
                                                    pResourceDataEntry =
                                                        (IMAGE_RESOURCE_DATA_ENTRY
                                                             *)((char *)pRoot +
                                                                searchIdEntries[i]
                                                                    .OffsetToDirectory);
                                                    break;
                                                }
                                                else if (
                                                    (searchIdEntries[i].Id ==
                                                     LangID1) &&
                                                    (foundLanguagePriority > 1))
                                                {
                                                    foundLanguagePriority = 1;
                                                    pResourceDataEntry =
                                                        (IMAGE_RESOURCE_DATA_ENTRY
                                                             *)((char *)pRoot +
                                                                searchIdEntries[i]
                                                                    .OffsetToDirectory);
                                                }
                                                else if (
                                                    (searchIdEntries[i].Id ==
                                                     LangID2) &&
                                                    (foundLanguagePriority > 2))
                                                {
                                                    foundLanguagePriority = 2;
                                                    pResourceDataEntry =
                                                        (IMAGE_RESOURCE_DATA_ENTRY
                                                             *)((char *)pRoot +
                                                                searchIdEntries[i]
                                                                    .OffsetToDirectory);
                                                }
                                                else if (
                                                    (searchIdEntries[i].Id ==
                                                     LangID3) &&
                                                    (foundLanguagePriority > 3))
                                                {
                                                    foundLanguagePriority = 3;
                                                    pResourceDataEntry =
                                                        (IMAGE_RESOURCE_DATA_ENTRY
                                                             *)((char *)pRoot +
                                                                searchIdEntries[i]
                                                                    .OffsetToDirectory);
                                                }
                                            }
                                        }

                                        if (pResourceDataEntry != NULL)
                                        {
                                            section =
                                                IMAGE_FIRST_SECTION(pNtHeaders);
                                            sectionFound = false;
                                            for (int i = 0;
                                                 i < pNtHeaders->FileHeader
                                                         .NumberOfSections;
                                                 i++)
                                            {
                                                if ((pResourceDataEntry
                                                         ->OffsetToData >=
                                                     section->VirtualAddress) &&
                                                    (pResourceDataEntry
                                                         ->OffsetToData <
                                                     (section->VirtualAddress +
                                                      section->Misc
                                                          .VirtualSize)))
                                                {
                                                    sectionFound = true;
                                                    break;
                                                }
                                                else
                                                    section++;
                                            }
                                            if (sectionFound)
                                            {

                                                void *pResourceData =
                                                    (void
                                                         *)((ULONG_PTR)base +
                                                            pResourceDataEntry
                                                                ->OffsetToData +
                                                            section
                                                                ->PointerToRawData -
                                                            (ULONG_PTR)section
                                                                ->VirtualAddress);

                                                WCHAR *tableDataBlock =
                                                    (WCHAR *)pResourceData;
                                                DWORD tableBlockSize =
                                                    pResourceDataEntry->Size;
                                                DWORD searchOffset = 0;
                                                DWORD stringIndex = 0;

                                                while (searchOffset <
                                                       tableBlockSize)
                                                {
                                                    if (stringIndex == itemID)
                                                    {
                                                        if (tableDataBlock
                                                                [searchOffset] !=
                                                            0x0000)
                                                        {

                                                            stringPtr =
                                                                &tableDataBlock
                                                                    [searchOffset +
                                                                     1];
                                                            stringLen = tableDataBlock
                                                                [searchOffset];

                                                            if (stringLen > 0)
                                                            {
                                                                *Result = new wchar_t
                                                                    [stringLen +
                                                                     1];
                                                                memset(
                                                                    *Result, 0,
                                                                    (stringLen +
                                                                     1) *
                                                                        sizeof(
                                                                            wchar_t));
                                                                wcsncpy(
                                                                    *Result,
                                                                    stringPtr,
                                                                    stringLen);

                                                                found = true;
                                                            }
                                                        }
                                                        else
                                                        {
                                                            stringPtr = NULL;
                                                            stringLen = 0;
                                                        }
                                                        break;
                                                    }

                                                    searchOffset +=
                                                        tableDataBlock
                                                            [searchOffset] +
                                                        1;
                                                    stringIndex++;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            found = false;
        }
    }

    if (base != NULL)
    {
        free(base);
        base = NULL;
    }

    if (found)
        return S_OK;

    __try
    {
        HINSTANCE hInstance =
            ::LoadLibraryEx(Dll, NULL, LOAD_LIBRARY_AS_DATAFILE);
        if (hInstance == 0)
            return GetLastError();
        TCHAR *buffer = new TCHAR[0x200];
        int size = ::LoadString(hInstance, ResourceID, buffer, 0x200);
        if (size == 0)
        {
            HRESULT error = GetLastError();
            delete[] buffer;
            ::FreeLibrary(hInstance);
            return error;
        }
        *Result = new wchar_t[wcslen(buffer) + 1];
        memset(*Result, 0, (wcslen(buffer) + 1) * sizeof(wchar_t));
        wcscpy(*Result, buffer);
        delete[] buffer;
        ::FreeLibrary(hInstance);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return S_OK;
}