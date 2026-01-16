/**
 * Chrome Proxy Launcher with Auto Authentication
 * Uses Puppeteer + Stealth to handle proxy auth without detection
 *
 * Usage: node launch_chrome.js <host> <port> <user> <pass> [url]
 */

const puppeteer = require('puppeteer-extra');
const StealthPlugin = require('puppeteer-extra-plugin-stealth');
puppeteer.use(StealthPlugin());

const args = process.argv.slice(2);
if (args.length < 4) {
  console.error('Usage: node launch_chrome.js <host> <port> <user> <pass> [url]');
  process.exit(1);
}

const [host, port, user, pass, url = 'https://httpbin.org/ip'] = args;

// Find Chrome executable
const chromePaths = [
  'C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Google/Chrome/Application/chrome.exe',
  process.env.LOCALAPPDATA + '/Google/Chrome/Application/chrome.exe'
];

function findChrome() {
  const fs = require('fs');
  for (const p of chromePaths) {
    if (fs.existsSync(p)) return p;
  }
  return null;
}

(async () => {
  try {
    const chromePath = findChrome();
    if (!chromePath) {
      console.error('Chrome not found');
      process.exit(1);
    }

    const browser = await puppeteer.launch({
      executablePath: chromePath,
      headless: false,
      ignoreDefaultArgs: ['--enable-automation'],
      args: [
        `--proxy-server=http://${host}:${port}`,
        '--no-first-run',
        '--start-maximized',
        '--disable-blink-features=AutomationControlled',
        '--disable-infobars'
      ],
      defaultViewport: null
    });

    const page = await browser.newPage();

    // Auto authenticate proxy - no popup!
    await page.authenticate({
      username: user,
      password: pass
    });

    await page.goto(url, { waitUntil: 'domcontentloaded', timeout: 30000 }).catch(e => {
      console.log('Page load warning:', e.message);
    });

    console.log('OK');
    // Keep browser open
  } catch (err) {
    console.error('Error:', err.message);
    process.exit(1);
  }
})();
