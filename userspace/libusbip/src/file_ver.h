/*
 * Copyright (C) 2001 - 2023 Vadym Hrynchyshyn <vadimgrn@gmail.com>
 */

#pragma once

#include "..\dllspec.h"

#include <windows.h>
#include <string>

namespace win
{

class USBIP_API FileVersion 
{
public:
        FileVersion(std::wstring_view path);
        ~FileVersion();

        FileVersion(const FileVersion&) = delete;
        FileVersion& operator =(const FileVersion&) = delete;

        FileVersion(FileVersion&& obj) : m_impl(obj.release()) {}
        FileVersion& operator =(FileVersion&& obj);

        explicit operator bool () const;
        bool operator !() const;

        DWORD SetFile(std::wstring_view path);
        std::wstring VerLanguageName() const;

        void SetDefTranslation();

        void SetTranslation(WORD lang_id = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), UINT code_page = GetACP());
        void GetTranslation(WORD &lang_id, UINT &code_page) const;
                                              
        std::wstring_view GetCompanyName() const;
        std::wstring_view GetComments() const;
        std::wstring_view GetFileDescription() const;
        std::wstring_view GetFileVersion() const;
        std::wstring_view GetInternalName() const;
        std::wstring_view GetLegalCopyright() const;
        std::wstring_view GetLegalTrademarks() const;
        std::wstring_view GetPrivateBuild() const;
        std::wstring_view GetProductName() const;
        std::wstring_view GetProductVersion() const;
        std::wstring_view GetSpecialBuild() const;

private:
        class Impl;
        Impl *m_impl{}; // std::unique_ptr is not compatible with __declspec(dllexport) for the class

        Impl *release() {
                auto p = m_impl;
                m_impl = nullptr;
                return p;
        }
};

} // namespace win