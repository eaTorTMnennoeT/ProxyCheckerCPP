#pragma once

#include <windows.h>
#include <winhttp.h>
#include <commctrl.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <set>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <functional>
#include <condition_variable>
#include <regex>
#include <sstream>
#include <chrono>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comctl32.lib")

struct ProxyInfo {
    std::wstring proxy;
    std::wstring status;
    std::wstring anonymity;
    std::wstring responseTime;
    std::wstring externalIp;
    std::wstring country;
    std::wstring city;
    std::wstring isp;
    int index = 0;
    bool success = false;
    int failCount = 0;
    // Uptime tracking
    std::chrono::system_clock::time_point firstSuccessTime;
    std::chrono::system_clock::time_point lastCheckTime;
    bool hasFirstSuccess = false;
};

class ThreadPool {
public:
    ThreadPool(size_t numThreads);
    ~ThreadPool();

    template<class F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            tasks.emplace(std::forward<F>(f));
        }
        condition.notify_one();
    }

    void wait();
    void stop();

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stopFlag{false};
    std::atomic<int> activeTasks{0};
    std::condition_variable doneCondition;
};

class ProxyChecker {
public:
    ProxyChecker(HWND hwnd);
    ~ProxyChecker();

    void LoadFromFile(const std::wstring& filename);
    void LoadFromClipboard();
    void StartCheck(int timeout, int threads);
    void RecheckFailed(int timeout, int threads);
    void RecheckIndices(const std::vector<int>& indices, int timeout, int threads);
    void StopCheck();
    void Clear();
    void CopyWorking();
    void SaveWorking(const std::wstring& filename);

    const std::vector<ProxyInfo>& GetResults() const { return results; }
    int GetTotal() const { return static_cast<int>(proxies.size()); }
    int GetCompleted() const { return completed.load(); }
    int GetSuccess() const { return successCount.load(); }
    int GetAnonymous() const { return anonymousCount.load(); }
    bool IsRunning() const { return running.load(); }
    std::wstring GetMyIp() const { return myIp; }

private:
    void CheckProxy(const std::wstring& proxy, int index, int timeout);
    void GetIpInfo(ProxyInfo& info);
    void FetchMyIp();
    std::wstring HttpRequest(const std::wstring& url, const std::wstring& proxy = L"", int timeout = 5);
    void ParseProxyUrl(const std::wstring& proxy, std::wstring& host, int& port, std::wstring& user, std::wstring& pass);
    void UpdateUI(const ProxyInfo& info);

    HWND mainHwnd;
    std::vector<std::wstring> proxies;
    std::vector<ProxyInfo> results;
    std::mutex resultsMutex;
    std::atomic<int> completed{0};
    std::atomic<int> successCount{0};
    std::atomic<int> anonymousCount{0};
    std::atomic<bool> running{false};
    std::atomic<int> recheckTotal{0};
    std::wstring myIp;
    std::unique_ptr<ThreadPool> threadPool;
};

// Utility functions
std::wstring Utf8ToWide(const std::string& str);
std::string WideToUtf8(const std::wstring& str);
std::wstring Trim(const std::wstring& str);
std::vector<std::wstring> Split(const std::wstring& str, wchar_t delim);
