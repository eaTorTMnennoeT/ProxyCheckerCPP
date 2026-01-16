# ProxyChecker C++ Project

## Overview
Windows용 고속 멀티스레드 프록시 체커 프로그램. HTTP/HTTPS 프록시 검증 및 Chrome 브라우저 연동 지원.

## Tech Stack
- **Language**: C++ (Win32 API)
- **Build**: CMake + MinGW-w64
- **Chrome Integration**: Node.js + Puppeteer with Stealth Plugin

## Project Structure
```
ProxyCheckerCPP/
├── main.cpp              # 메인 소스 (Win32 GUI + 프록시 체크 로직)
├── CMakeLists.txt        # CMake 빌드 설정
├── build/                # 빌드 출력
│   ├── ProxyChecker.exe
│   ├── launch_chrome.js  # Chrome 실행 스크립트
│   └── node_modules/     # Puppeteer dependencies
├── dist/                 # 배포용 폴더
└── ProxyChecker_v1.0.zip # 배포 패키지
```

## Key Features
1. **멀티스레드 프록시 체크** - 최대 100개 동시 체크
2. **프록시 정보 조회** - IP, 국가, 도시, ISP, 익명성 판별
3. **Chrome 연동** - 선택한 프록시로 Chrome 실행 (자동 인증, 자동화 감지 우회)

## Chrome Integration Architecture
Chrome 프록시 연동은 Puppeteer를 통해 구현:

### 왜 Puppeteer인가?
- Chrome 137+ 에서 `--load-extension` 플래그 제거됨
- Chrome Extension으로는 다른 PC에서 자동 로드 불가
- Puppeteer의 `page.authenticate()`가 유일한 해결책

### Stealth Plugin
`puppeteer-extra-plugin-stealth` 사용하여 자동화 감지 우회:
- `navigator.webdriver` 숨김
- Chrome DevTools Protocol 흔적 제거
- bot.sannysoft.com 테스트 통과

## Build Instructions
```bash
cd ProxyCheckerCPP
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
mingw32-make
```

## Chrome Script Setup
```bash
cd build
npm install puppeteer-extra puppeteer-extra-plugin-stealth puppeteer
```

## Key Code Sections

### OnOpenInChrome() - main.cpp:1430-1480
프록시 선택 후 Chrome 실행하는 함수:
```cpp
void OnOpenInChrome() {
    // 1. 선택된 프록시 파싱
    ParseProxyComponents(proxy, host, port, user, pass);

    // 2. Node.js 스크립트 실행
    std::wstring cmdArgs = L"/c node \"" + scriptPath + L"\" " +
                           host + L" " + port + L" " + user + L" " + pass;
    ShellExecuteW(nullptr, L"open", L"cmd.exe", cmdArgs.c_str(), ...);
}
```

### launch_chrome.js
```javascript
const puppeteer = require('puppeteer-extra');
const StealthPlugin = require('puppeteer-extra-plugin-stealth');
puppeteer.use(StealthPlugin());

// 프록시 설정 + 자동 인증
await puppeteer.launch({
    args: [`--proxy-server=http://${host}:${port}`]
});
await page.authenticate({ username, password });
```

## Distribution
배포 시 필요한 파일:
- `ProxyChecker.exe`
- `launch_chrome.js`
- `node_modules/` (puppeteer-extra, stealth plugin)
- `README.txt`

사용자 요구사항: Node.js 설치 필요

## Proxy Format Support
- `IP:PORT`
- `http://IP:PORT`
- `http://USER:PASS@IP:PORT`

## Future Improvements
- [ ] Node.js 번들링 (pkg 또는 nexe)
- [ ] SOCKS5 프록시 지원
- [ ] 프록시 자동 갱신/구독 기능
- [ ] 다국어 지원
