#pragma once
#include <Windows.h>
#include <winhttp.h>
#include <string>
#include <utilities/diag.hpp>

namespace features::changer::detail {
    struct sync_http_result {
        DWORD status{};
        DWORD error{};
        std::string body{};
        bool ok() const { return error == 0 && status >= 200 && status < 300; }
    };

    struct sync_winhttp_api {
        HMODULE module{};
        decltype(&::WinHttpOpen) open{};
        decltype(&::WinHttpSetTimeouts) timeouts{};
        decltype(&::WinHttpConnect) connect{};
        decltype(&::WinHttpOpenRequest) request{};
        decltype(&::WinHttpSendRequest) send{};
        decltype(&::WinHttpReceiveResponse) receive{};
        decltype(&::WinHttpQueryHeaders) headers{};
        decltype(&::WinHttpReadData) read{};
        decltype(&::WinHttpCloseHandle) close{};
        bool ready{};

        sync_winhttp_api() {
            module = LoadLibraryW(L"winhttp.dll");
            if (!module) return;
#define SYNC_LOAD(member, symbol) member = reinterpret_cast<decltype(member)>(GetProcAddress(module, #symbol))
            SYNC_LOAD(open, WinHttpOpen);
            SYNC_LOAD(timeouts, WinHttpSetTimeouts);
            SYNC_LOAD(connect, WinHttpConnect);
            SYNC_LOAD(request, WinHttpOpenRequest);
            SYNC_LOAD(send, WinHttpSendRequest);
            SYNC_LOAD(receive, WinHttpReceiveResponse);
            SYNC_LOAD(headers, WinHttpQueryHeaders);
            SYNC_LOAD(read, WinHttpReadData);
            SYNC_LOAD(close, WinHttpCloseHandle);
#undef SYNC_LOAD
            ready = open && timeouts && connect && request && send && receive && headers && read && close;
        }
        // Keep the DLL reference for the process lifetime, including late worker cleanup.
    };

    inline sync_http_result http_post_json(const std::string& body) {
        static sync_winhttp_api api;
        sync_http_result out;
        if (!api.ready) {
            out.error = ERROR_PROC_NOT_FOUND;
            diag::write(diag::level::error, "[skin-sync] WinHTTP exports unavailable");
            return out;
        }
        struct handle {
            HINTERNET value;
            decltype(&::WinHttpCloseHandle) close;
            ~handle() { if (value) close(value); }
        };
        const auto fail = [&](const char* stage, DWORD error) {
            out.error = error ? error : ERROR_GEN_FAILURE;
            out.body.clear();
            diag::writef(diag::level::warning,
                "[skin-sync] transport stage=%s win32=%lu http=%lu", stage, out.error, out.status);
        };
        if (body.size() > 2u * 1024u * 1024u) {
            fail("request-size", ERROR_BUFFER_OVERFLOW);
            return out;
        }
        handle session{api.open(L"MintalySync/1.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), api.close};
        if (!session.value && GetLastError() == ERROR_INVALID_PARAMETER) {
            session.value = api.open(L"MintalySync/1.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        }
        if (!session.value) { fail("open", GetLastError()); return out; }
        if (!api.timeouts(session.value, 5000, 5000, 5000, 5000)) {
            fail("timeouts", GetLastError()); return out;
        }
        handle connection{api.connect(session.value, L"flasskdev.alwaysdata.net",
            INTERNET_DEFAULT_HTTPS_PORT, 0), api.close};
        if (!connection.value) { fail("connect", GetLastError()); return out; }
        handle request{api.request(connection.value, L"POST", L"/api/v1/index.php", nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE), api.close};
        if (!request.value) { fail("request", GetLastError()); return out; }
        // Use Windows certificate validation. Never ignore certificate errors.
        const wchar_t* headers = L"Content-Type: application/json; charset=utf-8\r\nAccept: application/json\r\n";
        if (!api.send(request.value, headers, static_cast<DWORD>(-1L),
            const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
            static_cast<DWORD>(body.size()), 0)) {
            fail("send", GetLastError()); return out;
        }
        if (!api.receive(request.value, nullptr)) { fail("receive", GetLastError()); return out; }
        DWORD size = sizeof(out.status);
        if (!api.headers(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &out.status, &size, WINHTTP_NO_HEADER_INDEX)) {
            fail("status", GetLastError()); return out;
        }
        const ULONGLONG started = GetTickCount64();
        for (;;) {
            if (GetTickCount64() - started > 20000) {
                fail("read-deadline", ERROR_TIMEOUT); return out;
            }
            char buffer[8192];
            DWORD read = 0;
            if (!api.read(request.value, buffer, sizeof(buffer), &read)) {
                fail("read", GetLastError()); return out;
            }
            if (!read) break;
            if (out.body.size() + read > 4u * 1024u * 1024u) {
                fail("response-size", ERROR_BUFFER_OVERFLOW); return out;
            }
            out.body.append(buffer, read);
        }
        if (!out.ok()) {
            diag::writef(diag::level::warning, "[skin-sync] HTTP status=%lu bytes=%zu",
                out.status, out.body.size());
        }
        return out;
    }
}
