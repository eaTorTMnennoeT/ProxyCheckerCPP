// Proxy credentials - will be replaced dynamically
var PROXY_USER = "USERNAME_PLACEHOLDER";
var PROXY_PASS = "PASSWORD_PLACEHOLDER";

chrome.webRequest.onAuthRequired.addListener(
  function(details) {
    console.log("Auth required for: " + details.url);
    return {
      authCredentials: {
        username: PROXY_USER,
        password: PROXY_PASS
      }
    };
  },
  { urls: ["<all_urls>"] },
  ["blocking"]
);

console.log("Proxy Auto Auth extension loaded");
