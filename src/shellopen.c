#define COBJMACROS
#include "shellopen.h"

#include <exdisp.h>
#include <oleauto.h>
#include <servprov.h>
#include <shldisp.h>
#include <shlguid.h>
#include <shlobj.h>

/* We run elevated; a browser started by ShellExecute here would render the
   web with administrator rights. Explorer runs as the signed-in user, so the
   link is handed to it: desktop view -> its automation object ->
   IShellDispatch2::ShellExecute. The method is Raymond Chen's ("How can I
   launch an unelevated process from my elevated process", Old New Thing,
   2013), the same one Mozilla's installer uses. */
int shell_open_unelevated(const wchar_t *target)
{
    IShellWindows       *wins = NULL;
    IDispatch           *disp = NULL, *bg = NULL, *app = NULL;
    IServiceProvider    *sp   = NULL;
    IShellBrowser       *br   = NULL;
    IShellView          *view = NULL;
    IShellFolderViewDual *fv  = NULL;
    IShellDispatch2     *sd   = NULL;
    PIDLIST_ABSOLUTE     desk = NULL;
    VARIANT              loc, empty;
    long                 hwnd = 0;
    BSTR                 file = NULL;
    HRESULT              hr;

    VariantInit(&empty);
    VariantInit(&loc);

    hr = SHGetKnownFolderIDList(&FOLDERID_Desktop, 0, NULL, &desk);
    if (SUCCEEDED(hr))
        hr = CoCreateInstance(&CLSID_ShellWindows, NULL, CLSCTX_LOCAL_SERVER,
                              &IID_IShellWindows, (void **)&wins);
    if (SUCCEEDED(hr)) {
        loc.vt    = VT_VARIANT | VT_BYREF;
        loc.byref = desk;
        hr = IShellWindows_FindWindowSW(wins, &loc, &empty, SWC_DESKTOP, &hwnd,
                                        SWFO_NEEDDISPATCH, &disp);
        if (hr == S_FALSE || !disp) hr = E_FAIL;
    }
    if (SUCCEEDED(hr)) hr = IDispatch_QueryInterface(disp, &IID_IServiceProvider, (void **)&sp);
    if (SUCCEEDED(hr)) hr = IServiceProvider_QueryService(sp, &SID_STopLevelBrowser,
                                                          &IID_IShellBrowser, (void **)&br);
    if (SUCCEEDED(hr)) hr = IShellBrowser_QueryActiveShellView(br, &view);
    if (SUCCEEDED(hr)) hr = IShellView_GetItemObject(view, SVGIO_BACKGROUND,
                                                     &IID_IDispatch, (void **)&bg);
    if (SUCCEEDED(hr)) hr = IDispatch_QueryInterface(bg, &IID_IShellFolderViewDual, (void **)&fv);
    if (SUCCEEDED(hr)) hr = IShellFolderViewDual_get_Application(fv, &app);
    if (SUCCEEDED(hr)) hr = IDispatch_QueryInterface(app, &IID_IShellDispatch2, (void **)&sd);
    if (SUCCEEDED(hr)) {
        file = SysAllocString(target);
        hr = file ? IShellDispatch2_ShellExecute(sd, file, empty, empty, empty, empty)
                  : E_OUTOFMEMORY;
    }

    if (file) SysFreeString(file);
    if (sd)   IShellDispatch2_Release(sd);
    if (app)  IDispatch_Release(app);
    if (fv)   IShellFolderViewDual_Release(fv);
    if (bg)   IDispatch_Release(bg);
    if (view) IShellView_Release(view);
    if (br)   IShellBrowser_Release(br);
    if (sp)   IServiceProvider_Release(sp);
    if (disp) IDispatch_Release(disp);
    if (wins) IShellWindows_Release(wins);
    if (desk) CoTaskMemFree(desk);
    return SUCCEEDED(hr);
}
