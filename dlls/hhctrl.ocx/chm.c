/*
 * CHM Utility API
 *
 * Copyright 2005 James Hawkins
 * Copyright 2007 Jacek Caban
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "hhctrl.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(htmlhelp);

#define BLOCK_BITS 12
#define BLOCK_SIZE (1 << BLOCK_BITS)
#define BLOCK_MASK (BLOCK_SIZE-1)

/* Reads a string from the #STRINGS section in the CHM file */
static LPCSTR GetChmString(CHMInfo *chm, DWORD offset)
{
    if(!chm->strings_stream)
        return NULL;

    if(chm->strings_size <= (offset >> BLOCK_BITS)) {
        if(chm->strings)
            chm->strings = hhctrl_realloc_zero(chm->strings,
                    chm->strings_size = ((offset >> BLOCK_BITS)+1)*sizeof(char*));
        else
            chm->strings = hhctrl_alloc_zero(
                    chm->strings_size = ((offset >> BLOCK_BITS)+1)*sizeof(char*));

    }

    if(!chm->strings[offset >> BLOCK_BITS]) {
        LARGE_INTEGER pos;
        DWORD read;
        HRESULT hres;

        pos.QuadPart = offset & ~BLOCK_MASK;
        hres = IStream_Seek(chm->strings_stream, pos, STREAM_SEEK_SET, NULL);
        if(FAILED(hres)) {
            WARN("Seek failed: %08x\n", hres);
            return NULL;
        }

        chm->strings[offset >> BLOCK_BITS] = hhctrl_alloc(BLOCK_SIZE);

        hres = IStream_Read(chm->strings_stream, chm->strings[offset >> BLOCK_BITS],
                            BLOCK_SIZE, &read);
        if(FAILED(hres)) {
            WARN("Read failed: %08x\n", hres);
            hhctrl_free(chm->strings[offset >> BLOCK_BITS]);
            chm->strings[offset >> BLOCK_BITS] = NULL;
            return NULL;
        }
    }

    return chm->strings[offset >> BLOCK_BITS] + (offset & BLOCK_MASK);
}

static BOOL ReadChmSystem(CHMInfo *chm)
{
    IStream *stream;
    DWORD ver=0xdeadbeef, read, buf_size;
    char *buf;
    HRESULT hres;

    struct {
        WORD code;
        WORD len;
    } entry;

    static const WCHAR wszSYSTEM[] = {'#','S','Y','S','T','E','M',0};

    hres = IStorage_OpenStream(chm->pStorage, wszSYSTEM, NULL, STGM_READ, 0, &stream);
    if(FAILED(hres)) {
        WARN("Could not open #SYSTEM stream: %08x\n", hres);
        return FALSE;
    }

    IStream_Read(stream, &ver, sizeof(ver), &read);
    TRACE("version is %x\n", ver);

    buf = hhctrl_alloc(8*sizeof(DWORD));
    buf_size = 8*sizeof(DWORD);

    while(1) {
        hres = IStream_Read(stream, &entry, sizeof(entry), &read);
        if(hres != S_OK)
            break;

        if(entry.len > buf_size)
            buf = hhctrl_realloc(buf, buf_size=entry.len);

        hres = IStream_Read(stream, buf, entry.len, &read);
        if(hres != S_OK)
            break;

        switch(entry.code) {
        case 0x2:
            TRACE("Default topic is %s\n", debugstr_an(buf, entry.len));
            break;
        case 0x3:
            TRACE("Title is %s\n", debugstr_an(buf, entry.len));
            break;
        case 0x5:
            TRACE("Default window is %s\n", debugstr_an(buf, entry.len));
            break;
        case 0x6:
            TRACE("Compiled file is %s\n", debugstr_an(buf, entry.len));
            break;
        case 0x9:
            TRACE("Version is %s\n", debugstr_an(buf, entry.len));
            break;
        case 0xa:
            TRACE("Time is %08x\n", *(DWORD*)buf);
            break;
        case 0xc:
            TRACE("Number of info types: %d\n", *(DWORD*)buf);
            break;
        case 0xf:
            TRACE("Check sum: %x\n", *(DWORD*)buf);
            break;
        default:
            TRACE("unhandled code %x, size %x\n", entry.code, entry.len);
        }
    }

    hhctrl_free(buf);
    IStream_Release(stream);

    return SUCCEEDED(hres);
}

/* Loads the HH_WINTYPE data from the CHM file
 *
 * FIXME: There may be more than one window type in the file, so
 *        add the ability to choose a certain window type
 */
BOOL CHM_LoadWinTypeFromCHM(CHMInfo *pChmInfo, HH_WINTYPEW *pHHWinType)
{
    LARGE_INTEGER liOffset;
    IStorage *pStorage = pChmInfo->pStorage;
    IStream *pStream;
    HRESULT hr;
    DWORD cbRead;

    static const WCHAR windowsW[] = {'#','W','I','N','D','O','W','S',0};

    hr = IStorage_OpenStream(pStorage, windowsW, NULL, STGM_READ, 0, &pStream);
    if (FAILED(hr))
        return FALSE;

    /* jump past the #WINDOWS header */
    liOffset.QuadPart = sizeof(DWORD) * 2;

    hr = IStream_Seek(pStream, liOffset, STREAM_SEEK_SET, NULL);
    if (FAILED(hr)) goto done;

    /* read the HH_WINTYPE struct data */
    hr = IStream_Read(pStream, pHHWinType, sizeof(*pHHWinType), &cbRead);
    if (FAILED(hr)) goto done;

    /* convert the #STRINGS offsets to actual strings */
    pHHWinType->pszType = strdupAtoW(GetChmString(pChmInfo, (DWORD)pHHWinType->pszType));
    pHHWinType->pszCaption = strdupAtoW(GetChmString(pChmInfo, (DWORD)pHHWinType->pszCaption));
    pHHWinType->pszToc = strdupAtoW(GetChmString(pChmInfo, (DWORD)pHHWinType->pszToc));
    pHHWinType->pszIndex = strdupAtoW(GetChmString(pChmInfo, (DWORD)pHHWinType->pszIndex));
    pHHWinType->pszFile = strdupAtoW(GetChmString(pChmInfo, (DWORD)pHHWinType->pszFile));
    pHHWinType->pszHome = strdupAtoW(GetChmString(pChmInfo, (DWORD)pHHWinType->pszHome));
    pHHWinType->pszJump1 = strdupAtoW(GetChmString(pChmInfo, (DWORD)pHHWinType->pszJump1));
    pHHWinType->pszJump2 = strdupAtoW(GetChmString(pChmInfo, (DWORD)pHHWinType->pszJump2));
    pHHWinType->pszUrlJump1 = strdupAtoW(GetChmString(pChmInfo, (DWORD)pHHWinType->pszUrlJump1));
    pHHWinType->pszUrlJump2 = strdupAtoW(GetChmString(pChmInfo, (DWORD)pHHWinType->pszUrlJump2));
    
    /* FIXME: pszCustomTabs is a list of multiple zero-terminated strings so ReadString won't
     * work in this case
     */
#if 0
    pHHWinType->pszCustomTabs = CHM_ReadString(pChmInfo, (DWORD)pHHWinType->pszCustomTabs);
#endif

done:
    IStream_Release(pStream);

    return SUCCEEDED(hr);
}

/* Opens the CHM file for reading */
CHMInfo *OpenCHM(LPCWSTR szFile)
{
    HRESULT hres;

    static const WCHAR wszSTRINGS[] = {'#','S','T','R','I','N','G','S',0};

    CHMInfo *ret = hhctrl_alloc_zero(sizeof(CHMInfo));

    ret->szFile = szFile;

    hres = CoCreateInstance(&CLSID_ITStorage, NULL, CLSCTX_INPROC_SERVER,
            &IID_IITStorage, (void **) &ret->pITStorage) ;
    if(FAILED(hres)) {
        WARN("Could not create ITStorage: %08x\n", hres);
        return CloseCHM(ret);
    }

    hres = IITStorage_StgOpenStorage(ret->pITStorage, szFile, NULL,
            STGM_READ | STGM_SHARE_DENY_WRITE, NULL, 0, &ret->pStorage);
    if(FAILED(hres)) {
        WARN("Could not open storage: %08x\n", hres);
        return CloseCHM(ret);
    }

    hres = IStorage_OpenStream(ret->pStorage, wszSTRINGS, NULL, STGM_READ, 0,
            &ret->strings_stream);
    if(FAILED(hres)) {
        WARN("Could not open #STRINGS stream: %08x\n", hres);
        return CloseCHM(ret);
    }

    if(!ReadChmSystem(ret)) {
        WARN("Could not read #SYSTEM\n");
        return CloseCHM(ret);
    }

    return ret;
}

CHMInfo *CloseCHM(CHMInfo *chm)
{
    if(chm->pITStorage)
        IITStorage_Release(chm->pITStorage);

    if(chm->pStorage)
        IStorage_Release(chm->pStorage);

    if(chm->strings_stream)
        IStream_Release(chm->strings_stream);

    if(chm->strings_size) {
        int i;

        for(i=0; i<chm->strings_size; i++)
            hhctrl_free(chm->strings[i]);
    }

    hhctrl_free(chm->strings);
    hhctrl_free(chm);

    return NULL;
}
