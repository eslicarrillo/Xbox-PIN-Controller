//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//

#ifndef WIN32_NO_STATUS
#include <ntstatus.h>
#define WIN32_NO_STATUS
#endif
#include <unknwn.h>
#include "CCredential.h"
#include "guid.h"
#include "Logger.h"

CCredential::CCredential():
    _cRef(1),
    _pCredProvCredentialEvents(nullptr),
    _pszUserSid(nullptr),
    _pszQualifiedUserName(nullptr),
    _hwndPopupWindow(nullptr),
    _isDragging(false),
    _gdiplusToken(0),
    _fIsLocalUser(false),
    _fChecked(false),
    _fShowControls(false),
    _dwComboIndex(0)
{
    LOG_INFO(L"CCredential::CCredential() creado");
    DllAddRef();
    ZeroMemory(_rgCredProvFieldDescriptors, sizeof(_rgCredProvFieldDescriptors));
    ZeroMemory(_rgFieldStatePairs, sizeof(_rgFieldStatePairs));
    ZeroMemory(_rgFieldStrings, sizeof(_rgFieldStrings));
    ZeroMemory(&_lastMousePos, sizeof(_lastMousePos));
}

CCredential::~CCredential()
{
    LOG_INFO(L"CCredential::~CCredential() destruyendo...");

    if (_hwndPopupWindow)
    {
        LOG_DEBUG(L"CCredential::~CCredential: Destruyendo ventana overlay popup (HWND: %p)", _hwndPopupWindow);
        DestroyWindow(_hwndPopupWindow);
        _hwndPopupWindow = NULL;
    }
    UnregisterClass(L"SampleCredentialPopup", g_hinst);

    for (int i = 0; i < ARRAYSIZE(_rgFieldStrings); i++)
    {
        CoTaskMemFree(_rgFieldStrings[i]);
        CoTaskMemFree(_rgCredProvFieldDescriptors[i].pszLabel);
    }
    xinputController.StopCapturing();
    CoTaskMemFree(_pszUserSid);
    CoTaskMemFree(_pszQualifiedUserName);

    if (_gdiplusToken != 0)
    {
        LOG_DEBUG(L"CCredential::~CCredential: Apagando GDI+");
        Gdiplus::GdiplusShutdown(_gdiplusToken);
        _gdiplusToken = 0;
    }

    DllRelease();
    LOG_INFO(L"CCredential::~CCredential() destruido.");
}

// Initializes one credential with the field information passed in.
// Set the value of the SFI_LARGE_TEXT field to pwzUsername.
HRESULT CCredential::Initialize(CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
                                      _In_ CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR const *rgcpfd,
                                      _In_ FIELD_STATE_PAIR const *rgfsp,
                                      _In_ ICredentialProviderUser *pcpUser)
{
    HRESULT hr = S_OK;
    _cpus = cpus;

    LOG_INFO(L"CCredential::Initialize: Iniciando credencial (cpus=%d)", cpus);

    GUID guidProvider;
    pcpUser->GetProviderID(&guidProvider);
    _fIsLocalUser = (guidProvider == Identity_LocalUserProvider);

    xinputController.StartCapturing(0);

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&_gdiplusToken, &gdiplusStartupInput, NULL);

    // Create the popup window
    hr = CreatePopupWindow();
    if (FAILED(hr))
    {
        LOG_WARN(L"CCredential::Initialize: CreatePopupWindow retorno 0x%08X", hr);
    }

    // Copy the field descriptors for each field.
    for (DWORD i = 0; SUCCEEDED(hr) && i < ARRAYSIZE(_rgCredProvFieldDescriptors); i++)
    {
        _rgFieldStatePairs[i] = rgfsp[i];
        hr = FieldDescriptorCopy(rgcpfd[i], &_rgCredProvFieldDescriptors[i]);
    }

    if (SUCCEEDED(hr))
    {
        hr = SHStrDupW(L"Use Windows Hello PIN option to login", &_rgFieldStrings[SFI_LARGE_TEXT]);
    }

    if (SUCCEEDED(hr))
    {
        hr = pcpUser->GetStringValue(PKEY_Identity_QualifiedUserName, &_pszQualifiedUserName);
        if (SUCCEEDED(hr) && _pszQualifiedUserName)
        {
            LOG_INFO(L"CCredential::Initialize: Usuario asignado: %s", _pszQualifiedUserName);
        }
    }

    if (SUCCEEDED(hr))
    {
        hr = pcpUser->GetSid(&_pszUserSid);
        if (SUCCEEDED(hr) && _pszUserSid)
        {
            LOG_DEBUG(L"CCredential::Initialize: User SID: %s", _pszUserSid);
        }
    }

    LOG_INFO(L"CCredential::Initialize completado con resultado 0x%08X", hr);
    return hr;
}

HRESULT CCredential::CreatePopupWindow()
{
    LOG_INFO(L"CCredential::CreatePopupWindow: Registrando clase y creando ventana overlay...");

    // Register window class
    WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
    wc.lpfnWndProc = PopupWindowProc;
    wc.hInstance = g_hinst;
    wc.lpszClassName = L"SampleCredentialPopup";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassEx(&wc);

    // Get the size of the working area (excluding taskbar)
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

    // Calculate window position (top-right corner)
    int windowWidth = 500;
    int windowHeight = 400;
    int xPos = workArea.right - windowWidth - 20;
    int yPos = 20;

    LOG_DEBUG(L"CCredential::CreatePopupWindow: Posicion (%d, %d), Tamano (%dx%d)", xPos, yPos, windowWidth, windowHeight);

    // Create the popup window
    _hwndPopupWindow = CreateWindowEx(
        WS_EX_TOPMOST |              // Always on top
        WS_EX_LAYERED |             // For transparency
        WS_EX_TOOLWINDOW |          // Hide from taskbar
        WS_EX_NOACTIVATE |          // Prevent activation
        WS_EX_TRANSPARENT,          // Click-through
        L"SampleCredentialPopup",
        L"Authentication",
        WS_POPUP |                  // Popup window
        WS_VISIBLE,                 // Make it visible
        xPos, yPos,
        windowWidth, windowHeight,
        NULL, NULL,
        g_hinst,
        this
    );

    if (_hwndPopupWindow)
    {
        LOG_INFO(L"CCredential::CreatePopupWindow: Ventana creada exitosamente (HWND: %p)", _hwndPopupWindow);

        SetWindowLongPtr(_hwndPopupWindow, GWLP_USERDATA, (LONG_PTR)this);
        SetLayeredWindowAttributes(_hwndPopupWindow, 0, 200, LWA_ALPHA);

        LoadAndDisplayImage();

        ShowWindow(_hwndPopupWindow, SW_SHOW);
        UpdateWindow(_hwndPopupWindow);

        return S_OK;
    }

    LOG_ERROR(L"CCredential::CreatePopupWindow: Fallo al crear ventana (GetLastError: %lu)", GetLastError());
    return E_FAIL;
}

HRESULT CCredential::LoadAndDisplayImage()
{
    if (!_hwndPopupWindow)
        return E_FAIL;

    HDC hdcWindow = GetDC(_hwndPopupWindow);
    if (!hdcWindow)
        return E_FAIL;

    RECT rcWindow;
    GetClientRect(_hwndPopupWindow, &rcWindow);

    HDC hdcMem = CreateCompatibleDC(hdcWindow);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcWindow, rcWindow.right, rcWindow.bottom);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    HBRUSH hBrush = CreateSolidBrush(RGB(30, 30, 30));
    FillRect(hdcMem, &rcWindow, hBrush);
    DeleteObject(hBrush);

    Gdiplus::Graphics graphics(hdcMem);
    Gdiplus::Image* pImage = nullptr;

    // 1. Intentar cargar recurso embebido IDR_COMBO_PNG
    HRSRC hRsrc = FindResource(HINST_THISDLL, MAKEINTRESOURCE(IDR_COMBO_PNG), RT_RCDATA);
    if (hRsrc)
    {
        DWORD len = SizeofResource(HINST_THISDLL, hRsrc);
        HGLOBAL hGlobal = LoadResource(HINST_THISDLL, hRsrc);
        if (hGlobal && len > 0)
        {
            LPVOID pData = LockResource(hGlobal);
            if (pData)
            {
                HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, len);
                if (hBuffer)
                {
                    LPVOID pBuffer = GlobalLock(hBuffer);
                    if (pBuffer)
                    {
                        CopyMemory(pBuffer, pData, len);
                        GlobalUnlock(hBuffer);
                        IStream* pStream = nullptr;
                        if (SUCCEEDED(CreateStreamOnHGlobal(hBuffer, TRUE, &pStream)))
                        {
                            pImage = new Gdiplus::Image(pStream);
                            pStream->Release();
                            if (pImage && pImage->GetLastStatus() == Gdiplus::Ok)
                            {
                                LOG_DEBUG(L"LoadAndDisplayImage: Recurso embebido IDR_COMBO_PNG cargado (%lu bytes)", len);
                            }
                            else if (pImage)
                            {
                                LOG_WARN(L"LoadAndDisplayImage: Fallo estado de Gdiplus::Image desde stream (%d)", pImage->GetLastStatus());
                                delete pImage;
                                pImage = nullptr;
                            }
                        }
                    }
                    else
                    {
                        GlobalFree(hBuffer);
                    }
                }
            }
        }
    }

    // 2. Fallback a archivo en System32 si no se pudo cargar el recurso embebido
    if (!pImage)
    {
        LOG_INFO(L"LoadAndDisplayImage: Intentando fallback a C:\\Windows\\System32\\combo.png");
        pImage = new Gdiplus::Image(L"C:\\Windows\\System32\\combo.png");
        if (pImage && pImage->GetLastStatus() != Gdiplus::Ok)
        {
            LOG_WARN(L"LoadAndDisplayImage: Fallo al cargar combo.png de disco");
            delete pImage;
            pImage = nullptr;
        }
    }

    if (pImage)
    {
        float imageWidth = (float)pImage->GetWidth();
        float imageHeight = (float)pImage->GetHeight();
        float windowWidth = (float)(rcWindow.right - rcWindow.left);
        float windowHeight = (float)(rcWindow.bottom - rcWindow.top);

        float scale = min(windowWidth / imageWidth, windowHeight / imageHeight);
        float scaledWidth = imageWidth * scale;
        float scaledHeight = imageHeight * scale;

        float x = (windowWidth - scaledWidth) / 2;
        float y = (windowHeight - scaledHeight) / 2;

        graphics.DrawImage(pImage, x, y, scaledWidth, scaledHeight);
        delete pImage;
    }

    BitBlt(hdcWindow, 0, 0, rcWindow.right, rcWindow.bottom, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(_hwndPopupWindow, hdcWindow);

    return S_OK;
}

LRESULT CALLBACK CCredential::PopupWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CCredential* pProvider = (CCredential*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!pProvider)
        return DefWindowProc(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_LBUTTONDOWN:
    {
        pProvider->_isDragging = true;
        SetCapture(hwnd);
        GetCursorPos(&pProvider->_lastMousePos);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (pProvider->_isDragging)
        {
            POINT currentPos;
            GetCursorPos(&currentPos);

            int dx = currentPos.x - pProvider->_lastMousePos.x;
            int dy = currentPos.y - pProvider->_lastMousePos.y;

            RECT rcWindow;
            GetWindowRect(hwnd, &rcWindow);

            SetWindowPos(hwnd, NULL,
                rcWindow.left + dx,
                rcWindow.top + dy,
                0, 0,
                SWP_NOSIZE | SWP_NOZORDER);

            pProvider->_lastMousePos = currentPos;
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (pProvider->_isDragging)
        {
            pProvider->_isDragging = false;
            ReleaseCapture();
        }
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        pProvider->LoadAndDisplayImage();
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

HRESULT CCredential::Advise(_In_ ICredentialProviderCredentialEvents *pcpce)
{
    LOG_DEBUG(L"CCredential::Advise llamado por LogonUI");
    if (_pCredProvCredentialEvents != nullptr)
    {
        _pCredProvCredentialEvents->Release();
    }
    return pcpce->QueryInterface(IID_PPV_ARGS(&_pCredProvCredentialEvents));
}

HRESULT CCredential::UnAdvise()
{
    LOG_DEBUG(L"CCredential::UnAdvise llamado por LogonUI");
    if (_pCredProvCredentialEvents)
    {
        _pCredProvCredentialEvents->Release();
    }
    _pCredProvCredentialEvents = nullptr;
    return S_OK;
}

HRESULT CCredential::SetSelected(_Out_ BOOL *pbAutoLogon)
{
    LOG_INFO(L"CCredential::SetSelected: Tile de XboxPINController seleccionado");
    *pbAutoLogon = FALSE;
    return S_OK;
}

HRESULT CCredential::SetDeselected()
{
    LOG_INFO(L"CCredential::SetDeselected: Tile de XboxPINController deseleccionado");
    return S_OK;
}

HRESULT CCredential::GetFieldState(DWORD dwFieldID,
                                         _Out_ CREDENTIAL_PROVIDER_FIELD_STATE *pcpfs,
                                         _Out_ CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE *pcpfis)
{
    HRESULT hr;

    if ((dwFieldID < ARRAYSIZE(_rgFieldStatePairs)))
    {
        *pcpfs = _rgFieldStatePairs[dwFieldID].cpfs;
        *pcpfis = _rgFieldStatePairs[dwFieldID].cpfis;
        hr = S_OK;
    }
    else
    {
        hr = E_INVALIDARG;
    }
    return hr;
}

HRESULT CCredential::GetStringValue(DWORD dwFieldID, _Outptr_result_nullonfailure_ PWSTR *ppwsz)
{
    HRESULT hr;
    *ppwsz = nullptr;

    if (dwFieldID < ARRAYSIZE(_rgCredProvFieldDescriptors))
    {
        hr = SHStrDupW(_rgFieldStrings[dwFieldID], ppwsz);
    }
    else
    {
        hr = E_INVALIDARG;
    }

    return hr;
}

HRESULT CCredential::GetBitmapValue(DWORD dwFieldID, _Outptr_result_nullonfailure_ HBITMAP *phbmp)
{
    HRESULT hr;
    *phbmp = nullptr;

    if ((SFI_TILEIMAGE == dwFieldID))
    {
        HBITMAP hbmp = LoadBitmap(HINST_THISDLL, MAKEINTRESOURCE(IDB_TILE_IMAGE));
        if (hbmp != nullptr)
        {
            hr = S_OK;
            *phbmp = hbmp;
        }
        else
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            LOG_WARN(L"CCredential::GetBitmapValue: Fallo LoadBitmap (0x%08X)", hr);
        }
    }
    else
    {
        hr = E_INVALIDARG;
    }

    return hr;
}

HRESULT CCredential::GetSubmitButtonValue(DWORD dwFieldID, _Out_ DWORD *pdwAdjacentTo)
{
    UNREFERENCED_PARAMETER(dwFieldID);
    UNREFERENCED_PARAMETER(pdwAdjacentTo);
    return S_OK;
}

HRESULT CCredential::SetStringValue(DWORD dwFieldID, _In_ PCWSTR pwz)
{
    HRESULT hr;

    if (dwFieldID < ARRAYSIZE(_rgCredProvFieldDescriptors) &&
        (CPFT_EDIT_TEXT == _rgCredProvFieldDescriptors[dwFieldID].cpft ||
        CPFT_PASSWORD_TEXT == _rgCredProvFieldDescriptors[dwFieldID].cpft))
    {
        PWSTR *ppwszStored = &_rgFieldStrings[dwFieldID];
        CoTaskMemFree(*ppwszStored);
        hr = SHStrDupW(pwz, ppwszStored);
    }
    else
    {
        hr = E_INVALIDARG;
    }

    return hr;
}

HRESULT CCredential::GetCheckboxValue(DWORD dwFieldID, _Out_ BOOL *pbChecked, _Outptr_result_nullonfailure_ PWSTR *ppwszLabel)
{
    *ppwszLabel = nullptr;

    if (dwFieldID < ARRAYSIZE(_rgCredProvFieldDescriptors) &&
        (CPFT_CHECKBOX == _rgCredProvFieldDescriptors[dwFieldID].cpft))
    {
        *pbChecked = _fChecked;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT CCredential::SetCheckboxValue(DWORD dwFieldID, BOOL bChecked)
{
    if (dwFieldID < ARRAYSIZE(_rgCredProvFieldDescriptors) &&
        (CPFT_CHECKBOX == _rgCredProvFieldDescriptors[dwFieldID].cpft))
    {
        _fChecked = bChecked;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT CCredential::GetComboBoxValueCount(DWORD dwFieldID, _Out_ DWORD *pcItems, _Deref_out_range_(<, *pcItems) _Out_ DWORD *pdwSelectedItem)
{
    *pcItems = 0;
    *pdwSelectedItem = 0;

    if (dwFieldID < ARRAYSIZE(_rgCredProvFieldDescriptors) &&
        (CPFT_COMBOBOX == _rgCredProvFieldDescriptors[dwFieldID].cpft))
    {
        *pcItems = ARRAYSIZE(s_rgComboBoxStrings);
        *pdwSelectedItem = 0;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT CCredential::GetComboBoxValueAt(DWORD dwFieldID, DWORD dwItem, _Outptr_result_nullonfailure_ PWSTR *ppwszItem)
{
    *ppwszItem = nullptr;

    if (dwFieldID < ARRAYSIZE(_rgCredProvFieldDescriptors) &&
        (CPFT_COMBOBOX == _rgCredProvFieldDescriptors[dwFieldID].cpft))
    {
        return SHStrDupW(s_rgComboBoxStrings[dwItem], ppwszItem);
    }
    return E_INVALIDARG;
}

HRESULT CCredential::SetComboBoxSelectedValue(DWORD dwFieldID, DWORD dwSelectedItem)
{
    if (dwFieldID < ARRAYSIZE(_rgCredProvFieldDescriptors) &&
        (CPFT_COMBOBOX == _rgCredProvFieldDescriptors[dwFieldID].cpft))
    {
        _dwComboIndex = dwSelectedItem;
        return S_OK;
    }
    return E_INVALIDARG;
}

HRESULT CCredential::CommandLinkClicked(DWORD dwFieldID)
{
    UNREFERENCED_PARAMETER(dwFieldID);
    return E_INVALIDARG;
}

HRESULT CCredential::GetSerialization(_Out_ CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE *pcpgsr,
                                            _Out_ CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION *pcpcs,
                                            _Outptr_result_maybenull_ PWSTR *ppwszOptionalStatusText,
                                            _Out_ CREDENTIAL_PROVIDER_STATUS_ICON *pcpsiOptionalStatusIcon)
{
    LOG_DEBUG(L"CCredential::GetSerialization llamado por LogonUI");
    *pcpgsr = CPGSR_NO_CREDENTIAL_NOT_FINISHED;
    *ppwszOptionalStatusText = nullptr;
    *pcpsiOptionalStatusIcon = CPSI_NONE;
    ZeroMemory(pcpcs, sizeof(*pcpcs));
    return E_UNEXPECTED;
}

struct REPORT_RESULT_STATUS_INFO
{
    NTSTATUS ntsStatus;
    NTSTATUS ntsSubstatus;
    PWSTR     pwzMessage;
    CREDENTIAL_PROVIDER_STATUS_ICON cpsi;
};

static const REPORT_RESULT_STATUS_INFO s_rgLogonStatusInfo[] =
{
    { STATUS_LOGON_FAILURE, STATUS_SUCCESS, L"Incorrect password or username.", CPSI_ERROR, },
    { STATUS_ACCOUNT_RESTRICTION, STATUS_ACCOUNT_DISABLED, L"The account is disabled.", CPSI_WARNING },
};

HRESULT CCredential::ReportResult(NTSTATUS ntsStatus,
                                        NTSTATUS ntsSubstatus,
                                        _Outptr_result_maybenull_ PWSTR *ppwszOptionalStatusText,
                                        _Out_ CREDENTIAL_PROVIDER_STATUS_ICON *pcpsiOptionalStatusIcon)
{
    LOG_INFO(L"CCredential::ReportResult: status=0x%08X substatus=0x%08X", ntsStatus, ntsSubstatus);
    *ppwszOptionalStatusText = nullptr;
    *pcpsiOptionalStatusIcon = CPSI_NONE;

    DWORD dwStatusInfo = (DWORD)-1;

    for (DWORD i = 0; i < ARRAYSIZE(s_rgLogonStatusInfo); i++)
    {
        if (s_rgLogonStatusInfo[i].ntsStatus == ntsStatus && s_rgLogonStatusInfo[i].ntsSubstatus == ntsSubstatus)
        {
            dwStatusInfo = i;
            break;
        }
    }

    if ((DWORD)-1 != dwStatusInfo)
    {
        if (SUCCEEDED(SHStrDupW(s_rgLogonStatusInfo[dwStatusInfo].pwzMessage, ppwszOptionalStatusText)))
        {
            *pcpsiOptionalStatusIcon = s_rgLogonStatusInfo[dwStatusInfo].cpsi;
        }
    }

    return S_OK;
}

HRESULT CCredential::GetUserSid(_Outptr_result_nullonfailure_ PWSTR *ppszSid)
{
    *ppszSid = nullptr;
    HRESULT hr = E_UNEXPECTED;
    if (_pszUserSid != nullptr)
    {
        hr = SHStrDupW(_pszUserSid, ppszSid);
    }
    return hr;
}

HRESULT CCredential::GetFieldOptions(DWORD dwFieldID,
                                           _Out_ CREDENTIAL_PROVIDER_CREDENTIAL_FIELD_OPTIONS *pcpcfo)
{
    UNREFERENCED_PARAMETER(dwFieldID);
    *pcpcfo = CPCFO_NONE;
    return S_OK;
}
