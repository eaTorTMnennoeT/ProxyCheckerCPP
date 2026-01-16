# Proxy Checker Pro

고속 멀티스레드 프록시 체커 (Windows)

![Screenshot](https://img.shields.io/badge/platform-Windows-blue)
![License](https://img.shields.io/badge/license-MIT-green)

## 기능

- **멀티스레드 프록시 체크** - 동시 최대 100개
- **익명성 판별** - Anonymous / Not Anonymous
- **IP 정보 조회** - 국가, 도시, ISP
- **Chrome 연동** - 프록시 자동 인증 (팝업 없음!)
- **자동 체크** - 주기적 재검증

## 지원 프록시 형식

```
IP:PORT
http://IP:PORT  
http://USER:PASS@IP:PORT
```

## 설치

### 요구 사항
- Windows 10/11
- Google Chrome
- [Node.js](https://nodejs.org) (Chrome 연동 시)

### 빌드

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### 실행

```bash
npm install puppeteer-core
./ProxyChecker.exe
```

## 사용법

1. **프록시 로드** - Open File 또는 Paste
2. **체크 시작** - Start Check 클릭
3. **Chrome 실행** - 프록시 우클릭 → Open in Chrome

## 파일 구조

```
ProxyChecker.exe      # 메인 프로그램
launch_chrome.js      # Chrome 실행 스크립트
node_modules/         # puppeteer-core
```

## License

MIT
