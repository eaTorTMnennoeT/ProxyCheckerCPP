#include "ProxyChecker.h"
#include "resource.h"
#include <fstream>
#include <algorithm>
#include <chrono>

// Custom message for UI updates
#define WM_PROXY_UPDATE (WM_USER + 100)
#define WM_CHECK_COMPLETE (WM_USER + 101)

// ThreadPool implementation
ThreadPool::ThreadPool(size_t numThreads) {
    for (size_t i = 0; i < numThreads; ++i) {
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queueMutex);
                    condition.wait(lock, [this] { return stopFlag || !tasks.empty(); });
                    if (stopFlag && tasks.empty()) return;
                    task = std::move(tasks.front());
                    tasks.pop();
                    ++activeTasks;
                }
                task();
                --activeTasks;
                doneCondition.notify_all();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::wait() {
    std::unique_lock<std::mutex> lock(queueMutex);
    doneCondition.wait(lock, [this] { return tasks.empty() && activeTasks == 0; });
}

void ThreadPool::stop() {
    stopFlag = true;
    condition.notify_all();
    for (auto& worker : workers) {
        if (worker.joinable()) worker.join();
    }
}

// Utility functions
std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    return result;
}

std::string WideToUtf8(const std::wstring& str) {
    if (str.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

std::wstring Trim(const std::wstring& str) {
    size_t start = str.find_first_not_of(L" \t\r\n");
    size_t end = str.find_last_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return L"";
    return str.substr(start, end - start + 1);
}

std::vector<std::wstring> Split(const std::wstring& str, wchar_t delim) {
    std::vector<std::wstring> result;
    std::wstringstream ss(str);
    std::wstring item;
    while (std::getline(ss, item, delim)) {
        result.push_back(item);
    }
    return result;
}

// ProxyChecker implementation
ProxyChecker::ProxyChecker(HWND hwnd) : mainHwnd(hwnd) {
    std::thread([this] { FetchMyIp(); }).detach();
}

ProxyChecker::~ProxyChecker() {
    StopCheck();
}

void ProxyChecker::FetchMyIp() {
    try {
        std::wstring response = HttpRequest(L"https://httpbin.org/ip", L"", 10);
        std::wregex ipRegex(L"\"origin\"\\s*:\\s*\"([^\"]+)\"");
        std::wsmatch match;
        if (std::regex_search(response, match, ipRegex)) {
            myIp = match[1].str();
            // Remove any comma-separated values
            size_t pos = myIp.find(L',');
            if (pos != std::wstring::npos) {
                myIp = Trim(myIp.substr(0, pos));
            }
        }
    } catch (...) {
        myIp = L"";
    }
}

void ProxyChecker::LoadFromFile(const std::wstring& filename) {
    std::wifstream file(filename.c_str());
    if (!file.is_open()) return;

    proxies.clear();
    results.clear();
    std::wstring line;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (!line.empty()) {
            proxies.push_back(line);
        }
    }
    results.resize(proxies.size());
    for (size_t i = 0; i < proxies.size(); ++i) {
        results[i].proxy = proxies[i];
        results[i].index = static_cast<int>(i);
        results[i].status = L"Waiting";
    }
}

void ProxyChecker::LoadFromClipboard() {
    if (!OpenClipboard(mainHwnd)) return;

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData == nullptr) {
        CloseClipboard();
        return;
    }

    wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
    if (pszText == nullptr) {
        CloseClipboard();
        return;
    }

    std::wstring clipText(pszText);
    GlobalUnlock(hData);
    CloseClipboard();

    proxies.clear();
    results.clear();

    // Find proxy patterns: http://user:pass@ip:port or ip:port
    // More strict pattern: user:pass must not contain @ or whitespace
    std::wregex fullPattern(L"https?://[^:@\\s]+:[^:@\\s]+@(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}):(\\d{2,5})");
    std::wregex simplePattern(L"(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}):(\\d{2,5})");

    std::set<std::wstring> found;

    // First try to find full URL format (http://user:pass@ip:port)
    std::wsregex_iterator it(clipText.begin(), clipText.end(), fullPattern);
    std::wsregex_iterator end;

    for (; it != end; ++it) {
        std::wstring match = it->str();
        // Validate IP parts (0-255)
        std::wstring ip = (*it)[1].str();
        int port = std::stoi((*it)[2].str());
        if (port > 0 && port <= 65535) {
            found.insert(match);
        }
    }

    // If no full URL found, try simple ip:port format
    if (found.empty()) {
        std::wsregex_iterator it2(clipText.begin(), clipText.end(), simplePattern);
        for (; it2 != end; ++it2) {
            std::wstring match = it2->str();
            int port = std::stoi((*it2)[2].str());
            if (port > 0 && port <= 65535) {
                found.insert(match);
            }
        }
    }

    for (const auto& p : found) {
        proxies.push_back(p);
    }

    results.resize(proxies.size());
    for (size_t i = 0; i < proxies.size(); ++i) {
        results[i].proxy = proxies[i];
        results[i].index = static_cast<int>(i);
        results[i].status = L"Waiting";
    }
}

void ProxyChecker::ParseProxyUrl(const std::wstring& proxy, std::wstring& host, int& port, std::wstring& user, std::wstring& pass) {
    std::wstring url = proxy;

    // Remove protocol
    if (url.find(L"http://") == 0) url = url.substr(7);
    else if (url.find(L"https://") == 0) url = url.substr(8);

    // Check for auth
    size_t atPos = url.find(L'@');
    if (atPos != std::wstring::npos) {
        std::wstring auth = url.substr(0, atPos);
        url = url.substr(atPos + 1);

        size_t colonPos = auth.find(L':');
        if (colonPos != std::wstring::npos) {
            user = auth.substr(0, colonPos);
            pass = auth.substr(colonPos + 1);
        }
    }

    // Parse host:port
    size_t colonPos = url.find(L':');
    if (colonPos != std::wstring::npos) {
        host = url.substr(0, colonPos);
        port = std::stoi(url.substr(colonPos + 1));
    } else {
        host = url;
        port = 80;
    }
}

std::wstring ProxyChecker::HttpRequest(const std::wstring& url, const std::wstring& proxy, int timeout) {
    std::wstring result;

    HINTERNET hSession = nullptr;
    HINTERNET hConnect = nullptr;
    HINTERNET hRequest = nullptr;

    try {
        // Parse URL
        URL_COMPONENTS urlComp = {0};
        urlComp.dwStructSize = sizeof(urlComp);
        wchar_t hostName[256] = {0};
        wchar_t urlPath[1024] = {0};
        urlComp.lpszHostName = hostName;
        urlComp.dwHostNameLength = 256;
        urlComp.lpszUrlPath = urlPath;
        urlComp.dwUrlPathLength = 1024;

        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &urlComp)) {
            throw std::runtime_error("Invalid URL");
        }

        DWORD accessType = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
        LPCWSTR proxyName = WINHTTP_NO_PROXY_NAME;
        std::wstring proxyStr;

        if (!proxy.empty()) {
            std::wstring host;
            int port;
            std::wstring user, pass;
            ParseProxyUrl(proxy, host, port, user, pass);
            proxyStr = host + L":" + std::to_wstring(port);
            accessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
            proxyName = proxyStr.c_str();
        }

        hSession = WinHttpOpen(L"ProxyChecker/1.0", accessType, proxyName, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) throw std::runtime_error("Failed to open session");

        // Set timeouts
        DWORD timeoutMs = timeout * 1000;
        WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

        hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
        if (!hConnect) throw std::runtime_error("Failed to connect");

        DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) throw std::runtime_error("Failed to open request");

        // Set proxy auth if needed
        if (!proxy.empty()) {
            std::wstring host;
            int port;
            std::wstring user, pass;
            ParseProxyUrl(proxy, host, port, user, pass);

            if (!user.empty()) {
                WinHttpSetCredentials(hRequest, WINHTTP_AUTH_TARGET_PROXY, WINHTTP_AUTH_SCHEME_BASIC, user.c_str(), pass.c_str(), nullptr);
            }
        }

        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            throw std::runtime_error("Failed to send request");
        }

        if (!WinHttpReceiveResponse(hRequest, nullptr)) {
            throw std::runtime_error("Failed to receive response");
        }

        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &statusCode, &statusCodeSize, nullptr);

        if (statusCode != 200) {
            throw std::runtime_error("HTTP error");
        }

        // Read response
        std::string response;
        DWORD bytesAvailable = 0;
        do {
            WinHttpQueryDataAvailable(hRequest, &bytesAvailable);
            if (bytesAvailable > 0) {
                std::vector<char> buffer(bytesAvailable + 1, 0);
                DWORD bytesRead = 0;
                WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead);
                response.append(buffer.data(), bytesRead);
            }
        } while (bytesAvailable > 0);

        result = Utf8ToWide(response);

    } catch (...) {
        result = L"";
    }

    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);

    return result;
}

void ProxyChecker::CheckProxy(const std::wstring& proxy, int index, int timeout) {
    if (!running) return;

    ProxyInfo info;
    info.proxy = proxy;
    info.index = index;

    // Preserve previous failCount and uptime tracking
    {
        std::lock_guard<std::mutex> lock(resultsMutex);
        if (index < static_cast<int>(results.size())) {
            info.failCount = results[index].failCount;
            info.firstSuccessTime = results[index].firstSuccessTime;
            info.hasFirstSuccess = results[index].hasFirstSuccess;
        }
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    try {
        std::wstring response = HttpRequest(L"https://httpbin.org/ip", proxy, timeout);

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        if (!response.empty()) {
            std::wregex ipRegex(L"\"origin\"\\s*:\\s*\"([^\"]+)\"");
            std::wsmatch match;
            if (std::regex_search(response, match, ipRegex)) {
                info.externalIp = match[1].str();
                size_t pos = info.externalIp.find(L',');
                if (pos != std::wstring::npos) {
                    info.externalIp = Trim(info.externalIp.substr(0, pos));
                }

                info.status = L"Success";
                info.responseTime = std::to_wstring(duration) + L"ms";
                info.success = true;

                // Record first success time for uptime tracking
                if (!info.hasFirstSuccess) {
                    info.firstSuccessTime = std::chrono::system_clock::now();
                    info.hasFirstSuccess = true;
                }
                info.lastCheckTime = std::chrono::system_clock::now();

                // Check anonymity
                if (!myIp.empty() && info.externalIp != myIp) {
                    info.anonymity = L"Anonymous";
                    anonymousCount++;
                } else {
                    info.anonymity = L"Not Anon";
                }

                successCount++;

                // Get IP info
                GetIpInfo(info);
            } else {
                info.status = L"Parse Error";
                info.failCount++;
                info.lastCheckTime = std::chrono::system_clock::now();
            }
        } else {
            info.status = L"Failed";
            info.failCount++;
            info.lastCheckTime = std::chrono::system_clock::now();
        }
    } catch (...) {
        info.status = L"Error";
        info.failCount++;
        info.lastCheckTime = std::chrono::system_clock::now();
    }

    completed++;

    {
        std::lock_guard<std::mutex> lock(resultsMutex);
        if (index < static_cast<int>(results.size())) {
            results[index] = info;
        }
    }

    UpdateUI(info);
}

void ProxyChecker::GetIpInfo(ProxyInfo& info) {
    try {
        std::wstring url = L"http://ip-api.com/json/" + info.externalIp + L"?lang=en";
        std::wstring response = HttpRequest(url, L"", 5);

        if (!response.empty()) {
            // Parse country
            std::wregex countryRegex(L"\"country\"\\s*:\\s*\"([^\"]+)\"");
            std::wsmatch match;
            if (std::regex_search(response, match, countryRegex)) {
                info.country = match[1].str();
            }

            // Parse city
            std::wregex cityRegex(L"\"city\"\\s*:\\s*\"([^\"]+)\"");
            if (std::regex_search(response, match, cityRegex)) {
                info.city = match[1].str();
            }

            // Parse ISP
            std::wregex ispRegex(L"\"isp\"\\s*:\\s*\"([^\"]+)\"");
            if (std::regex_search(response, match, ispRegex)) {
                info.isp = match[1].str();
            }
        }
    } catch (...) {}
}

void ProxyChecker::UpdateUI(const ProxyInfo& info) {
    // Post message to main window for UI update
    ProxyInfo* pInfo = new ProxyInfo(info);
    PostMessage(mainHwnd, WM_PROXY_UPDATE, 0, reinterpret_cast<LPARAM>(pInfo));
}

void ProxyChecker::StartCheck(int timeout, int threads) {
    if (running || proxies.empty()) return;

    running = true;
    completed = 0;
    successCount = 0;
    anonymousCount = 0;

    // Reset results
    for (size_t i = 0; i < results.size(); ++i) {
        results[i].status = L"Waiting";
        results[i].anonymity = L"-";
        results[i].responseTime = L"-";
        results[i].externalIp = L"-";
        results[i].country = L"-";
        results[i].city = L"-";
        results[i].isp = L"-";
        results[i].success = false;
    }

    threadPool = std::make_unique<ThreadPool>(threads);

    for (size_t i = 0; i < proxies.size(); ++i) {
        threadPool->enqueue([this, i, timeout] {
            CheckProxy(proxies[i], static_cast<int>(i), timeout);
        });
    }

    // Start completion checker thread
    std::thread([this] {
        while (running && completed < static_cast<int>(proxies.size())) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        running = false;
        PostMessage(mainHwnd, WM_CHECK_COMPLETE, 0, 0);
    }).detach();
}

void ProxyChecker::StopCheck() {
    running = false;
    if (threadPool) {
        threadPool->stop();
        threadPool.reset();
    }
}

void ProxyChecker::Clear() {
    StopCheck();
    proxies.clear();
    results.clear();
    completed = 0;
    successCount = 0;
    anonymousCount = 0;
}

void ProxyChecker::CopyWorking() {
    std::wstring text;
    for (const auto& result : results) {
        if (result.success) {
            text += result.proxy + L"\r\n";
        }
    }

    if (text.empty()) return;

    if (!OpenClipboard(mainHwnd)) return;
    EmptyClipboard();

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (hMem) {
        wchar_t* pMem = static_cast<wchar_t*>(GlobalLock(hMem));
        wcscpy_s(pMem, text.size() + 1, text.c_str());
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
    }
    CloseClipboard();
}

void ProxyChecker::SaveWorking(const std::wstring& filename) {
    std::wofstream file(filename.c_str());
    if (!file.is_open()) return;

    for (const auto& result : results) {
        if (result.success) {
            file << result.proxy << L"\n";
        }
    }
}

void ProxyChecker::RecheckFailed(int timeout, int threads) {
    if (running || proxies.empty()) return;

    // Collect failed indices
    std::vector<int> failedIndices;
    for (size_t i = 0; i < results.size(); ++i) {
        if (!results[i].success && results[i].status != L"Waiting") {
            failedIndices.push_back(static_cast<int>(i));
        }
    }

    if (failedIndices.empty()) return;

    RecheckIndices(failedIndices, timeout, threads);
}

void ProxyChecker::RecheckIndices(const std::vector<int>& indices, int timeout, int threads) {
    if (running || indices.empty()) return;

    running = true;
    completed = 0;
    recheckTotal = static_cast<int>(indices.size());

    // Recalculate success and anonymous counts from current results
    successCount = 0;
    anonymousCount = 0;
    for (const auto& r : results) {
        if (r.success) {
            successCount++;
            if (r.anonymity == L"Anonymous") {
                anonymousCount++;
            }
        }
    }

    // Reset status for rechecked proxies only (and subtract from counts if was success)
    for (int idx : indices) {
        if (idx >= 0 && idx < static_cast<int>(results.size())) {
            // If this proxy was previously successful, decrement counts
            if (results[idx].success) {
                successCount--;
                if (results[idx].anonymity == L"Anonymous") {
                    anonymousCount--;
                }
            }
            results[idx].status = L"Waiting";
            results[idx].anonymity = L"-";
            results[idx].responseTime = L"-";
            results[idx].externalIp = L"-";
            results[idx].country = L"-";
            results[idx].city = L"-";
            results[idx].isp = L"-";
            results[idx].success = false;
            // failCount is preserved
        }
    }

    threadPool = std::make_unique<ThreadPool>(threads);

    for (int idx : indices) {
        if (idx >= 0 && idx < static_cast<int>(proxies.size())) {
            threadPool->enqueue([this, idx, timeout] {
                CheckProxy(proxies[idx], idx, timeout);
            });
        }
    }

    // Start completion checker thread
    int totalToCheck = static_cast<int>(indices.size());
    std::thread([this, totalToCheck] {
        while (running && completed < totalToCheck) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        running = false;
        recheckTotal = 0;
        PostMessage(mainHwnd, WM_CHECK_COMPLETE, 0, 0);
    }).detach();
}
