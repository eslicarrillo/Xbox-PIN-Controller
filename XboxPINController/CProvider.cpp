//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// CProvider implements ICredentialProvider, which is the main
// interface that logonUI uses to decide which tiles to display.

#include <initguid.h>
#include "CProvider.h"
#include "CCredential.h"
#include "guid.h"
#include "Logger.h"

CProvider::CProvider():
    _cRef(1),
    _pCredential(nullptr),
    _fRecreateEnumeratedCredentials(false),
    _cpus(CPUS_INVALID),
    _pCredProviderUserArray(nullptr)
{
    LOG_INFO(L"CProvider::CProvider() creado.");
    DllAddRef();
}

CProvider::~CProvider()
{
    LOG_INFO(L"CProvider::~CProvider() destruyendo...");
    if (_pCredential != nullptr)
    {
        _pCredential->Release();
        _pCredential = nullptr;
    }
    if (_pCredProviderUserArray != nullptr)
    {
        _pCredProviderUserArray->Release();
        _pCredProviderUserArray = nullptr;
    }

    DllRelease();
    LOG_INFO(L"CProvider::~CProvider() destruido.");
}

// SetUsageScenario is the provider's cue that it's going to be asked for tiles
// in a subsequent call.
HRESULT CProvider::SetUsageScenario(
    CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
    DWORD /*dwFlags*/)
{
    HRESULT hr;

    const wchar_t* scenarioName = L"DESCONOCIDO";
    switch (cpus)
    {
    case CPUS_LOGON:
        scenarioName = L"CPUS_LOGON (Inicio de sesion)";
        break;
    case CPUS_UNLOCK_WORKSTATION:
        scenarioName = L"CPUS_UNLOCK_WORKSTATION (Desbloqueo)";
        break;
    case CPUS_CHANGE_PASSWORD:
        scenarioName = L"CPUS_CHANGE_PASSWORD";
        break;
    case CPUS_CREDUI:
        scenarioName = L"CPUS_CREDUI";
        break;
    }

    LOG_INFO(L"CProvider::SetUsageScenario: %s (cpus=%d)", scenarioName, cpus);

    switch (cpus)
    {
    case CPUS_LOGON:
    case CPUS_UNLOCK_WORKSTATION:
        _cpus = cpus;
        _fRecreateEnumeratedCredentials = true;
        hr = S_OK;
        LOG_INFO(L"CProvider::SetUsageScenario aceptado (S_OK)");
        break;

    case CPUS_CHANGE_PASSWORD:
    case CPUS_CREDUI:
        hr = E_NOTIMPL;
        LOG_INFO(L"CProvider::SetUsageScenario no implementado para este escenario (E_NOTIMPL)");
        break;

    default:
        hr = E_INVALIDARG;
        LOG_WARN(L"CProvider::SetUsageScenario argumento invalido (E_INVALIDARG)");
        break;
    }

    return hr;
}

HRESULT CProvider::SetSerialization(
    _In_ CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION const * /*pcpcs*/)
{
    return E_NOTIMPL;
}

HRESULT CProvider::Advise(
    _In_ ICredentialProviderEvents * /*pcpe*/,
    _In_ UINT_PTR /*upAdviseContext*/)
{
    return E_NOTIMPL;
}

HRESULT CProvider::UnAdvise()
{
    return E_NOTIMPL;
}

HRESULT CProvider::GetFieldDescriptorCount(
    _Out_ DWORD *pdwCount)
{
    *pdwCount = SFI_NUM_FIELDS;
    return S_OK;
}

HRESULT CProvider::GetFieldDescriptorAt(
    DWORD dwIndex,
    _Outptr_result_nullonfailure_ CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR **ppcpfd)
{
    HRESULT hr;
    *ppcpfd = nullptr;

    if ((dwIndex < SFI_NUM_FIELDS) && ppcpfd)
    {
        hr = FieldDescriptorCoAllocCopy(s_rgCredProvFieldDescriptors[dwIndex], ppcpfd);
    }
    else
    {
        hr = E_INVALIDARG;
    }

    return hr;
}

HRESULT CProvider::GetCredentialCount(
    _Out_ DWORD *pdwCount,
    _Out_ DWORD *pdwDefault,
    _Out_ BOOL *pbAutoLogonWithDefault)
{
    *pdwDefault = CREDENTIAL_PROVIDER_NO_DEFAULT;
    *pbAutoLogonWithDefault = FALSE;

    if (_fRecreateEnumeratedCredentials)
    {
        _fRecreateEnumeratedCredentials = false;
        _ReleaseEnumeratedCredentials();
        _CreateEnumeratedCredentials();
    }

    if (_pCredential != nullptr)
    {
        *pdwCount = 1;
        LOG_INFO(L"CProvider::GetCredentialCount: Retornando 1 credencial");
    }
    else
    {
        *pdwCount = 0;
        LOG_WARN(L"CProvider::GetCredentialCount: Retornando 0 credenciales (sin credencial)");
    }

    return S_OK;
}

HRESULT CProvider::GetCredentialAt(
    DWORD dwIndex,
    _Outptr_result_nullonfailure_ ICredentialProviderCredential **ppcpc)
{
    HRESULT hr = E_INVALIDARG;
    *ppcpc = nullptr;

    if ((dwIndex == 0) && ppcpc)
    {
        if (_pCredential != nullptr)
        {
            hr = _pCredential->QueryInterface(IID_PPV_ARGS(ppcpc));
            LOG_INFO(L"CProvider::GetCredentialAt: QueryInterface retorno 0x%08X", hr);
        }
        else
        {
            hr = E_UNEXPECTED;
            LOG_ERROR(L"CProvider::GetCredentialAt: _pCredential es nulo (E_UNEXPECTED)");
        }
    }
    return hr;
}

HRESULT CProvider::SetUserArray(_In_ ICredentialProviderUserArray *users)
{
    if (_pCredProviderUserArray)
    {
        _pCredProviderUserArray->Release();
    }
    _pCredProviderUserArray = users;

    DWORD userCount = 0;
    if (_pCredProviderUserArray)
    {
        _pCredProviderUserArray->AddRef();
        _pCredProviderUserArray->GetCount(&userCount);
    }
    LOG_INFO(L"CProvider::SetUserArray: %lu usuarios proporcionados por LogonUI", userCount);

    return S_OK;
}

void CProvider::_CreateEnumeratedCredentials()
{
    switch (_cpus)
    {
    case CPUS_LOGON:
    case CPUS_UNLOCK_WORKSTATION:
        {
            _EnumerateCredentials();
            break;
        }
    default:
        break;
    }
}

void CProvider::_ReleaseEnumeratedCredentials()
{
    if (_pCredential != nullptr)
    {
        LOG_DEBUG(L"CProvider::_ReleaseEnumeratedCredentials: Liberando credencial");
        _pCredential->Release();
        _pCredential = nullptr;
    }
}

HRESULT CProvider::_EnumerateCredentials()
{
    HRESULT hr = E_UNEXPECTED;
    if (_pCredProviderUserArray != nullptr)
    {
        DWORD dwUserCount;
        _pCredProviderUserArray->GetCount(&dwUserCount);
        LOG_INFO(L"CProvider::_EnumerateCredentials: %lu usuarios encontrados", dwUserCount);

        if (dwUserCount > 0)
        {
            ICredentialProviderUser *pCredUser;
            hr = _pCredProviderUserArray->GetAt(0, &pCredUser);
            if (SUCCEEDED(hr))
            {
                _pCredential = new(std::nothrow) CCredential();
                if (_pCredential != nullptr)
                {
                    hr = _pCredential->Initialize(_cpus, s_rgCredProvFieldDescriptors, s_rgFieldStatePairs, pCredUser);
                    LOG_INFO(L"CProvider::_EnumerateCredentials: CCredential::Initialize retorno 0x%08X", hr);
                    if (FAILED(hr))
                    {
                        LOG_ERROR(L"CProvider::_EnumerateCredentials: Falla en CCredential::Initialize (0x%08X)", hr);
                        _pCredential->Release();
                        _pCredential = nullptr;
                    }
                }
                else
                {
                    hr = E_OUTOFMEMORY;
                    LOG_ERROR(L"CProvider::_EnumerateCredentials: E_OUTOFMEMORY al instanciar CCredential");
                }
                pCredUser->Release();
            }
        }
    }
    return hr;
}

HRESULT CSample_CreateInstance(_In_ REFIID riid, _Outptr_ void **ppv)
{
    HRESULT hr;
    CProvider *pProvider = new(std::nothrow) CProvider();
    if (pProvider)
    {
        hr = pProvider->QueryInterface(riid, ppv);
        pProvider->Release();
    }
    else
    {
        hr = E_OUTOFMEMORY;
        LOG_ERROR(L"CSample_CreateInstance: E_OUTOFMEMORY al instanciar CProvider");
    }
    return hr;
}
