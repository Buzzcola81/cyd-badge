#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <PNGdec.h>
#include <TFT_eSPI.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <new>
#include <time.h>
#include <vector>

#include "app_logic.h"

namespace
{
    TFT_eSPI tft;
    TFT_eSprite logoSprite = TFT_eSprite(&tft);
    Preferences prefs;
    WebServer server(80);
    DNSServer dnsServer;
    PNG png;

    String githubUser;
    String wifiSsid;
    String wifiPass;

    String profileName;
    String profileLogin;
    String profileAvatarUrl;
    int profileFollowers = -1;
    int profileFollowing = -1;
    int profileRepos = -1;
    int recentEvents = -1;
    int pushEvents = -1;
    int prEvents = -1;
    int issueEvents = -1;
    bool profileLoaded = false;

    std::vector<uint8_t> avatarPng;
    bool avatarLoaded = false;
    int16_t avatarDrawX = 140;
    int16_t avatarDrawY = 38;
    constexpr int16_t kAvatarSize = 92;
    uint16_t avatarLineBuffer[kAvatarSize];

    std::vector<uint8_t> versentLogoPng;
    bool versentLogoLoaded = false;
    constexpr int16_t kVersentLogoRenderSize = 72;
    constexpr int16_t kVersentLogoSourceMaxWidth = 320;
    uint16_t versentLogoLineBuffer[kVersentLogoSourceMaxWidth];
    uint16_t versentLogoPixels[kVersentLogoRenderSize * kVersentLogoRenderSize];
    bool logoSpriteReady = false;

    uint32_t lastCubeFrameMs = 0;
    float cubeAngle = 0.0f;
    constexpr int16_t kCubeAreaX = 128;
    constexpr int16_t kCubeAreaY = 196;
    constexpr int16_t kCubeAreaSize = 112;

    bool downloadToBuffer(const String &url, std::vector<uint8_t> &out, size_t maxBytes);
    void drawMultilineStatus(const String &line1, const String &line2 = "", const String &line3 = "");
    void drawProfile();

    struct Vec3
    {
        float x;
        float y;
        float z;
    };

    struct Vec2
    {
        int16_t x;
        int16_t y;
    };

    int versentLogoDraw(PNGDRAW *pDraw)
    {
        int16_t width = pDraw->iWidth;
        if (width > kVersentLogoSourceMaxWidth)
        {
            width = kVersentLogoSourceMaxWidth;
        }
        png.getLineAsRGB565(pDraw, versentLogoLineBuffer, PNG_RGB565_LITTLE_ENDIAN, 0x000000u);

        int targetY = (pDraw->y * kVersentLogoRenderSize) / png.getHeight();
        if (targetY < 0 || targetY >= kVersentLogoRenderSize)
        {
            return 1;
        }

        for (int sourceX = 0; sourceX < width; ++sourceX)
        {
            int targetX = (sourceX * kVersentLogoRenderSize) / width;
            if (targetX < 0 || targetX >= kVersentLogoRenderSize)
            {
                continue;
            }
            versentLogoPixels[(targetY * kVersentLogoRenderSize) + targetX] = versentLogoLineBuffer[sourceX];
        }
        return 1;
    }

    bool fetchVersentLogo()
    {
        versentLogoLoaded = false;
        versentLogoPng.clear();
        for (int i = 0; i < (kVersentLogoRenderSize * kVersentLogoRenderSize); ++i)
        {
            versentLogoPixels[i] = TFT_BLACK;
        }
        String url = "https://versent.com.au/wp-content/uploads/2025/12/cropped-V-mark-Green-270x270.png";
        if (!downloadToBuffer(url, versentLogoPng, 48 * 1024))
        {
            return false;
        }

        int openResult = png.openRAM(versentLogoPng.data(), (int32_t)versentLogoPng.size(), versentLogoDraw);
        if (openResult != PNG_SUCCESS)
        {
            versentLogoPng.clear();
            return false;
        }
        png.decode(nullptr, 0);
        png.close();

        versentLogoLoaded = true;
        return true;
    }

    void drawSpinningCube()
    {
        if (versentLogoLoaded)
        {
            float srcCenter = (kVersentLogoRenderSize - 1) * 0.5f;
            float destCenter = (kCubeAreaSize - 1) * 0.5f;
            float cosA = cosf(cubeAngle);
            float sinA = sinf(cubeAngle);

            if (logoSpriteReady)
            {
                logoSprite.fillSprite(TFT_BLACK);
            }
            else
            {
                tft.fillRect(kCubeAreaX, kCubeAreaY, kCubeAreaSize, kCubeAreaSize, TFT_BLACK);
            }

            for (int destY = 0; destY < kCubeAreaSize; ++destY)
            {
                for (int destX = 0; destX < kCubeAreaSize; ++destX)
                {
                    float rx = destX - destCenter;
                    float ry = destY - destCenter;
                    float srcXf = (rx * cosA) + (ry * sinA) + srcCenter;
                    float srcYf = (-rx * sinA) + (ry * cosA) + srcCenter;

                    int srcX = (int)(srcXf + 0.5f);
                    int srcY = (int)(srcYf + 0.5f);
                    uint16_t color = TFT_BLACK;
                    if (srcX >= 0 && srcX < kVersentLogoRenderSize && srcY >= 0 && srcY < kVersentLogoRenderSize)
                    {
                        color = versentLogoPixels[(srcY * kVersentLogoRenderSize) + srcX];
                    }
                    if (logoSpriteReady)
                    {
                        logoSprite.drawPixel(destX, destY, color);
                    }
                    else
                    {
                        tft.drawPixel(kCubeAreaX + destX, kCubeAreaY + destY, color);
                    }
                }
            }

            if (logoSpriteReady)
            {
                logoSprite.pushSprite(kCubeAreaX, kCubeAreaY);
            }
            return;
        }

        tft.fillRect(kCubeAreaX, kCubeAreaY, kCubeAreaSize, kCubeAreaSize, TFT_BLACK);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setCursor(kCubeAreaX + 12, kCubeAreaY + 42);
        tft.print("V");
    }

    bool isLikelyBlack(uint16_t color)
    {
        return color == TFT_BLACK;
    }

    bool fetchPending = false;
    uint32_t lastFetchAttemptMs = 0;
    uint32_t nextGithubRetryMs = 0;
    uint8_t githubConsecutiveFailures = 0;
    String lastGithubError;
    uint32_t wifiConnectStartedMs = 0;
    uint32_t lastWifiRetryMs = 0;
    wl_status_t lastWifiStatus = WL_IDLE_STATUS;
    bool wifiStatusDirty = false;

    constexpr uint8_t kGithubErrorAfterFailures = 3;
    constexpr uint32_t kGithubFallbackRetryDelayMs = 15UL * 60UL * 1000UL;
    constexpr uint32_t kGithubMinRetryDelayMs = 3000;
    constexpr uint32_t kGithubMaxRetryDelayMs = 6UL * 60UL * 60UL * 1000UL;

    constexpr const char *kApName = "CYD-Badge-Setup";
    constexpr const char *kApPassword = "badgeconfig";
    constexpr const char *kPortalHost = "cyd-badge";

    String wifiStatusText(wl_status_t status)
    {
        switch (status)
        {
        case WL_NO_SSID_AVAIL:
            return "SSID not found";
        case WL_CONNECT_FAILED:
            return "Auth failed";
        case WL_CONNECTION_LOST:
            return "Connection lost";
        case WL_DISCONNECTED:
            return "Disconnected";
        case WL_CONNECTED:
            return "Connected";
        case WL_IDLE_STATUS:
        default:
            return "Connecting...";
        }
    }

    void addGitHubApiHeaders(HTTPClient &http)
    {
        http.addHeader("User-Agent", "cyd-badge");
        http.addHeader("Accept", "application/vnd.github+json");
        http.addHeader("X-GitHub-Api-Version", "2022-11-28");
    }

    bool hasReachedTime(uint32_t nowMs, uint32_t targetMs)
    {
        return (int32_t)(nowMs - targetMs) >= 0;
    }

    bool parseUint32(const String &value, uint32_t &out)
    {
        if (value.isEmpty())
        {
            return false;
        }
        char *end = nullptr;
        unsigned long parsed = strtoul(value.c_str(), &end, 10);
        if (end == value.c_str() || (end != nullptr && *end != '\0'))
        {
            return false;
        }
        out = (uint32_t)parsed;
        return true;
    }

    int monthIndex(const String &mon)
    {
        if (mon == "Jan")
            return 1;
        if (mon == "Feb")
            return 2;
        if (mon == "Mar")
            return 3;
        if (mon == "Apr")
            return 4;
        if (mon == "May")
            return 5;
        if (mon == "Jun")
            return 6;
        if (mon == "Jul")
            return 7;
        if (mon == "Aug")
            return 8;
        if (mon == "Sep")
            return 9;
        if (mon == "Oct")
            return 10;
        if (mon == "Nov")
            return 11;
        if (mon == "Dec")
            return 12;
        return 0;
    }

    bool isLeapYear(int year)
    {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    }

    int daysInMonth(int year, int month)
    {
        static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month == 2)
        {
            return isLeapYear(year) ? 29 : 28;
        }
        return days[month - 1];
    }

    bool parseGithubDateEpoch(const String &dateHeader, uint32_t &epochOut)
    {
        // Expects RFC1123 date like: Thu, 06 Aug 2026 12:34:56 GMT
        if (dateHeader.length() < 29)
        {
            return false;
        }

        int comma = dateHeader.indexOf(',');
        if (comma < 0 || comma + 2 >= (int)dateHeader.length())
        {
            return false;
        }

        String rest = dateHeader.substring(comma + 2);
        int p1 = rest.indexOf(' ');
        if (p1 < 0)
            return false;
        int p2 = rest.indexOf(' ', p1 + 1);
        if (p2 < 0)
            return false;
        int p3 = rest.indexOf(' ', p2 + 1);
        if (p3 < 0)
            return false;
        int p4 = rest.indexOf(' ', p3 + 1);
        if (p4 < 0)
            return false;

        String dayStr = rest.substring(0, p1);
        String monStr = rest.substring(p1 + 1, p2);
        String yearStr = rest.substring(p2 + 1, p3);
        String timeStr = rest.substring(p3 + 1, p4);
        String zoneStr = rest.substring(p4 + 1);
        zoneStr.trim();
        if (zoneStr != "GMT")
        {
            return false;
        }

        uint32_t day = 0;
        uint32_t year = 0;
        if (!parseUint32(dayStr, day) || !parseUint32(yearStr, year))
        {
            return false;
        }
        int month = monthIndex(monStr);
        if (month == 0 || day == 0 || day > (uint32_t)daysInMonth((int)year, month))
        {
            return false;
        }

        int c1 = timeStr.indexOf(':');
        int c2 = timeStr.indexOf(':', c1 + 1);
        if (c1 < 0 || c2 < 0)
        {
            return false;
        }
        uint32_t hour = 0;
        uint32_t minute = 0;
        uint32_t second = 0;
        if (!parseUint32(timeStr.substring(0, c1), hour) ||
            !parseUint32(timeStr.substring(c1 + 1, c2), minute) ||
            !parseUint32(timeStr.substring(c2 + 1), second))
        {
            return false;
        }
        if (hour > 23 || minute > 59 || second > 59)
        {
            return false;
        }

        uint32_t daysSinceEpoch = 0;
        for (int y = 1970; y < (int)year; ++y)
        {
            daysSinceEpoch += isLeapYear(y) ? 366 : 365;
        }
        for (int m = 1; m < month; ++m)
        {
            daysSinceEpoch += daysInMonth((int)year, m);
        }
        daysSinceEpoch += (day - 1);

        uint32_t epoch = (daysSinceEpoch * 86400UL) + (hour * 3600UL) + (minute * 60UL) + second;
        epochOut = epoch;
        return true;
    }

    uint32_t computeRetryDelayMs(const String &retryAfterHeader, const String &rateLimitResetHeader, const String &dateHeader)
    {
        uint32_t retryAfterSec = 0;
        if (parseUint32(retryAfterHeader, retryAfterSec) && retryAfterSec > 0)
        {
            uint32_t delay = retryAfterSec * 1000UL;
            if (delay < kGithubMinRetryDelayMs)
            {
                delay = kGithubMinRetryDelayMs;
            }
            if (delay > kGithubMaxRetryDelayMs)
            {
                delay = kGithubMaxRetryDelayMs;
            }
            return delay;
        }

        uint32_t resetEpoch = 0;
        uint32_t dateEpoch = 0;
        if (parseUint32(rateLimitResetHeader, resetEpoch) && parseGithubDateEpoch(dateHeader, dateEpoch) && resetEpoch > dateEpoch)
        {
            uint32_t deltaSec = resetEpoch - dateEpoch;
            uint32_t delay = deltaSec * 1000UL;
            if (delay < kGithubMinRetryDelayMs)
            {
                delay = kGithubMinRetryDelayMs;
            }
            if (delay > kGithubMaxRetryDelayMs)
            {
                delay = kGithubMaxRetryDelayMs;
            }
            return delay;
        }

        time_t nowEpoch = time(nullptr);
        if (parseUint32(rateLimitResetHeader, resetEpoch) && nowEpoch > 1600000000 && resetEpoch > (uint32_t)nowEpoch)
        {
            uint32_t deltaSec = resetEpoch - (uint32_t)nowEpoch;
            uint32_t delay = deltaSec * 1000UL;
            if (delay < kGithubMinRetryDelayMs)
            {
                delay = kGithubMinRetryDelayMs;
            }
            if (delay > kGithubMaxRetryDelayMs)
            {
                delay = kGithubMaxRetryDelayMs;
            }
            return delay;
        }

        return kGithubFallbackRetryDelayMs;
    }

    uint32_t parseRateLimitRemaining(const String &remainingHeader)
    {
        uint32_t remaining = 0;
        if (parseUint32(remainingHeader, remaining))
        {
            return remaining;
        }
        return UINT32_MAX;
    }

    void scheduleGitHubRetry(uint32_t delayMs, const String &reason)
    {
        if (delayMs < kGithubMinRetryDelayMs)
        {
            delayMs = kGithubMinRetryDelayMs;
        }
        if (delayMs > kGithubMaxRetryDelayMs)
        {
            delayMs = kGithubMaxRetryDelayMs;
        }

        fetchPending = true;
        nextGithubRetryMs = millis() + delayMs;
        lastGithubError = reason;
        Serial.printf("github retry in %lu ms (%s)\n", (unsigned long)delayMs, reason.c_str());
    }

    void handleGitHubFailureUi(int httpCode, const String &detail)
    {
        if (profileLoaded && githubConsecutiveFailures < kGithubErrorAfterFailures)
        {
            // Keep last known good profile visible for transient retry failures.
            drawProfile();
            return;
        }

        String detailLine = detail;
        if (detailLine.length() > 22)
        {
            detailLine = detailLine.substring(0, 22);
        }
        drawMultilineStatus("GitHub fetch failed", "HTTP " + String(httpCode), detailLine);
    }

    void drawWifiProgress()
    {
        if (wifiSsid.isEmpty())
        {
            drawMultilineStatus("No Wi-Fi configured", "Open setup portal", "and save SSID/pass");
            return;
        }

        String line1 = "Wi-Fi: " + wifiStatusText(WiFi.status());
        String line2 = wifiSsid;
        String line3 = "Open http://" + String(kPortalHost);
        drawMultilineStatus(line1, line2, line3);
    }

    int avatarPngDraw(PNGDRAW *pDraw)
    {
        int16_t width = pDraw->iWidth;
        if (width > kAvatarSize)
        {
            width = kAvatarSize;
        }
        png.getLineAsRGB565(pDraw, avatarLineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
        tft.pushImage(avatarDrawX, avatarDrawY + pDraw->y, width, 1, avatarLineBuffer);
        return 1;
    }

    bool downloadToBuffer(const String &url, std::vector<uint8_t> &out, size_t maxBytes)
    {
        out.clear();

        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient http;
        if (!http.begin(client, url))
        {
            return false;
        }

        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.addHeader("User-Agent", "cyd-badge");
        http.addHeader("Accept", "image/png,image/*;q=0.8,*/*;q=0.5");
        int code = http.GET();
        if (code != 200)
        {
            Serial.printf("avatar http code: %d\n", code);
            http.end();
            return false;
        }

        WiFiClient *stream = http.getStreamPtr();
        int contentLen = http.getSize();
        if (contentLen > 0 && (size_t)contentLen > maxBytes)
        {
            http.end();
            return false;
        }

        try
        {
            if (contentLen > 0)
            {
                out.reserve((size_t)contentLen);
            }
            else
            {
                out.reserve(maxBytes > 16384 ? 16384 : maxBytes);
            }
        }
        catch (const std::bad_alloc &)
        {
            http.end();
            out.clear();
            return false;
        }

        uint8_t temp[256];
        while (http.connected() && (contentLen > 0 || contentLen == -1))
        {
            size_t available = stream->available();
            if (available == 0)
            {
                delay(1);
                continue;
            }

            size_t toRead = available;
            if (toRead > sizeof(temp))
            {
                toRead = sizeof(temp);
            }

            int bytesRead = stream->readBytes(temp, toRead);
            if (bytesRead <= 0)
            {
                delay(1);
                continue;
            }

            if (out.size() + (size_t)bytesRead > maxBytes)
            {
                http.end();
                out.clear();
                return false;
            }

            try
            {
                out.insert(out.end(), temp, temp + bytesRead);
            }
            catch (const std::bad_alloc &)
            {
                http.end();
                out.clear();
                return false;
            }

            if (contentLen > 0)
            {
                contentLen -= bytesRead;
            }
        }

        http.end();
        return !out.empty();
    }

    bool fetchContributionStats()
    {
        recentEvents = -1;
        pushEvents = 0;
        prEvents = 0;
        issueEvents = 0;

        String url = "https://api.github.com/users/" + githubUser + "/events/public?per_page=12";
        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient http;
        if (!http.begin(client, url))
        {
            return false;
        }

        addGitHubApiHeaders(http);
        int code = http.GET();
        if (code != 200)
        {
            http.end();
            return false;
        }

        String body = http.getString();
        http.end();

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err)
        {
            return false;
        }

        if (!doc.is<JsonArray>())
        {
            return false;
        }

        JsonArray arr = doc.as<JsonArray>();
        recentEvents = arr.size();

        for (JsonObject ev : arr)
        {
            String type = ev["type"].as<String>();
            if (type == "PushEvent")
            {
                pushEvents++;
            }
            else if (type == "PullRequestEvent")
            {
                prEvents++;
            }
            else if (type == "IssuesEvent")
            {
                issueEvents++;
            }
        }

        return true;
    }

    bool fetchAvatar()
    {
        avatarLoaded = false;
        avatarPng.clear();

        String url = profileAvatarUrl;
        if (url.isEmpty())
        {
            url = "https://github.com/" + githubUser + ".png?size=96";
        }
        else
        {
            if (url.indexOf('?') >= 0)
            {
                url += "&s=96";
            }
            else
            {
                url += "?s=96";
            }
        }

        if (!downloadToBuffer(url, avatarPng, 80 * 1024))
        {
            Serial.println("avatar download failed");
            return false;
        }

        avatarLoaded = true;
        Serial.printf("avatar bytes: %d\n", (int)avatarPng.size());
        return true;
    }

    void drawMultilineStatus(const String &line1, const String &line2, const String &line3)
    {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setTextFont(2);
        tft.setTextSize(1);
        tft.setCursor(8, 8);
        tft.println(kProjectName);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setCursor(8, 36);
        tft.println(line1);
        if (line2.length() > 0)
        {
            tft.setCursor(8, 56);
            tft.println(line2);
        }
        if (line3.length() > 0)
        {
            tft.setCursor(8, 76);
            tft.println(line3);
        }
    }

    void drawProfile()
    {
        tft.fillScreen(TFT_BLACK);
        tft.setTextFont(2);
        tft.setTextSize(1);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setCursor(8, 8);
        tft.println(kProjectName);

        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setCursor(8, 36);
        tft.print("GitHub");
        tft.setCursor(8, 54);
        tft.println(profileLogin.length() > 0 ? profileLogin : githubUser);

        if (profileName.length() > 0)
        {
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.setCursor(8, 74);
            tft.print("Name ");
            tft.println(profileName);
        }

        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setCursor(8, 104);
        tft.print("Repos ");
        tft.println(profileRepos);

        tft.setCursor(8, 122);
        tft.print("Followers ");
        tft.println(profileFollowers);

        tft.setCursor(8, 140);
        tft.print("Following ");
        tft.println(profileFollowing);

        tft.setTextColor(TFT_ORANGE, TFT_BLACK);
        tft.setCursor(8, 168);
        tft.print("Events12 ");
        tft.println(recentEvents);
        tft.setCursor(8, 186);
        tft.print("Push ");
        tft.print(pushEvents);
        tft.print(" PR ");
        tft.print(prEvents);
        tft.print(" Iss ");
        tft.println(issueEvents);

        tft.drawRect(avatarDrawX - 2, avatarDrawY - 2, kAvatarSize + 4, kAvatarSize + 4, TFT_DARKGREY);
        tft.fillRect(avatarDrawX, avatarDrawY, kAvatarSize, kAvatarSize, TFT_BLACK);

        if (avatarLoaded && !avatarPng.empty())
        {
            int openResult = png.openRAM(avatarPng.data(), (int32_t)avatarPng.size(), avatarPngDraw);
            if (openResult == PNG_SUCCESS)
            {
                png.decode(nullptr, 0);
                png.close();
            }
            else
            {
                tft.setTextColor(TFT_RED, TFT_BLACK);
                tft.setCursor(avatarDrawX + 8, avatarDrawY + 40);
                tft.print("No avatar");
            }
        }
        else
        {
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.setCursor(avatarDrawX + 8, avatarDrawY + 40);
            tft.print("No avatar");
        }

        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.setCursor(8, 232);
        tft.println("Setup portal:");
        tft.setCursor(8, 250);
        tft.print("http://");
        tft.println(kPortalHost);

        drawSpinningCube();
    }

    String htmlEscape(const String &input)
    {
        String s = input;
        s.replace("&", "&amp;");
        s.replace("<", "&lt;");
        s.replace(">", "&gt;");
        s.replace("\"", "&quot;");
        return s;
    }

    String buildPage(const String &msg = "")
    {
        String apIp = WiFi.softAPIP().toString();
        String staIp = WiFi.isConnected() ? WiFi.localIP().toString() : String("Not connected");
        String escapedUser = htmlEscape(githubUser);
        String escapedSsid = htmlEscape(wifiSsid);

        String page;
        page += "<!doctype html><html><head><meta charset='utf-8'>";
        page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
        page += "<title>CYD Badge Setup</title>";
        page += "<style>body{font-family:Segoe UI,Arial,sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:20px;}";
        page += ".card{max-width:560px;margin:auto;background:#1e293b;border:1px solid #334155;border-radius:12px;padding:20px;}";
        page += "input{width:100%;padding:10px;border-radius:8px;border:1px solid #475569;background:#0b1220;color:#e2e8f0;margin:6px 0 12px;}";
        page += "button{padding:10px 14px;border:0;border-radius:8px;background:#06b6d4;color:#001018;font-weight:700;}";
        page += "small{color:#94a3b8;} .msg{color:#22c55e;font-weight:600;}</style></head><body>";
        page += "<div class='card'><h2>CYD Badge Setup</h2>";
        if (msg.length() > 0)
        {
            page += "<p class='msg'>" + htmlEscape(msg) + "</p>";
        }
        page += "<p><small>AP SSID: " + String(kApName) + " | AP IP: " + apIp + "</small><br>";
        page += "<small>STA IP: " + staIp + "</small></p>";
        page += "<p><small>Open: <b>http://" + String(kPortalHost) + "</b> or <b>http://" + String(kPortalHost) + ".local</b></small></p>";
        page += "<form method='POST' action='/save'>";
        page += "<label>GitHub Username</label>";
        page += "<input name='github' value='" + escapedUser + "' placeholder='octocat' required>";
        page += "<label>Wi-Fi SSID</label>";
        page += "<input name='ssid' value='" + escapedSsid + "' placeholder='Optional, needed for live GitHub fetch'>";
        page += "<label>Wi-Fi Password</label>";
        page += "<input name='pass' type='password' value='' placeholder='Leave blank to keep existing'>";
        page += "<button type='submit'>Save and Refresh Stats</button></form>";
        page += "</div></body></html>";
        return page;
    }

    void loadSettings()
    {
        prefs.begin("badgecfg", true);
        githubUser = prefs.getString("gh", "");
        wifiSsid = prefs.getString("ssid", "");
        wifiPass = prefs.getString("pwd", "");
        prefs.end();
    }

    void saveSettings()
    {
        prefs.begin("badgecfg", false);
        prefs.putString("gh", githubUser);
        prefs.remove("ght");
        prefs.putString("ssid", wifiSsid);
        prefs.putString("pwd", wifiPass);
        prefs.end();
    }

    void connectStaIfConfigured()
    {
        if (wifiSsid.isEmpty())
        {
            return;
        }

        WiFi.disconnect(true, true);
        delay(100);
        WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
        wifiConnectStartedMs = millis();
        lastWifiRetryMs = wifiConnectStartedMs;
        lastWifiStatus = WiFi.status();
        wifiStatusDirty = true;
    }

    bool fetchGitHubStats()
    {
        if (githubUser.isEmpty())
        {
            githubConsecutiveFailures = 0;
            nextGithubRetryMs = 0;
            drawMultilineStatus("No GitHub user set", "Open setup portal", "to configure.");
            return false;
        }

        if (!WiFi.isConnected())
        {
            drawMultilineStatus("Wi-Fi not connected", "Set SSID/password in", "setup portal.");
            return false;
        }

        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient http;
        String url = "https://api.github.com/users/" + githubUser;
        if (!http.begin(client, url))
        {
            githubConsecutiveFailures++;
            handleGitHubFailureUi(0, "HTTP begin failed");
            scheduleGitHubRetry(kGithubFallbackRetryDelayMs, "http begin failed");
            return false;
        }

        const char *headers[] = {"X-RateLimit-Reset", "Retry-After", "Date", "X-RateLimit-Remaining"};
        http.collectHeaders(headers, 4);
        addGitHubApiHeaders(http);
        int code = http.GET();
        String retryAfterHeader = http.header("Retry-After");
        String resetHeader = http.header("X-RateLimit-Reset");
        String dateHeader = http.header("Date");
        String remainingHeader = http.header("X-RateLimit-Remaining");
        uint32_t rateRemaining = parseRateLimitRemaining(remainingHeader);
        if (code != 200)
        {
            String body = http.getString();
            String detail = "Check username/network";

            JsonDocument errDoc;
            if (deserializeJson(errDoc, body) == DeserializationError::Ok)
            {
                String apiMessage = errDoc["message"].isNull() ? String("") : errDoc["message"].as<String>();
                if (apiMessage.length() > 0)
                {
                    detail = apiMessage;
                }
            }

            githubConsecutiveFailures++;
            uint32_t retryDelayMs = computeRetryDelayMs(retryAfterHeader, resetHeader, dateHeader);
            if (rateRemaining == 0)
            {
                detail = "Rate limited";
            }
            handleGitHubFailureUi(code, detail);
            scheduleGitHubRetry(retryDelayMs, "api error " + String(code));
            http.end();
            return false;
        }

        String body = http.getString();
        http.end();

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err)
        {
            githubConsecutiveFailures++;
            drawMultilineStatus("JSON parse failed", err.c_str());
            scheduleGitHubRetry(kGithubFallbackRetryDelayMs, "json parse failed");
            return false;
        }

        profileLogin = doc["login"].as<String>();
        profileName = doc["name"].isNull() ? String("") : doc["name"].as<String>();
        profileAvatarUrl = doc["avatar_url"].isNull() ? String("") : doc["avatar_url"].as<String>();
        profileRepos = doc["public_repos"].as<int>();
        profileFollowers = doc["followers"].as<int>();
        profileFollowing = doc["following"].as<int>();

        if (rateRemaining == UINT32_MAX || rateRemaining > 1)
        {
            fetchContributionStats();
        }
        else
        {
            recentEvents = -1;
            pushEvents = 0;
            prEvents = 0;
            issueEvents = 0;
        }
        fetchAvatar();
        fetchVersentLogo();

        githubConsecutiveFailures = 0;
        nextGithubRetryMs = 0;
        lastGithubError = "";
        profileLoaded = true;
        drawProfile();
        return true;
    }

    void handleRoot()
    {
        server.send(200, "text/html", buildPage());
    }

    void handleSave()
    {
        if (server.hasArg("github"))
        {
            githubUser = server.arg("github");
            githubUser.trim();
        }
        if (server.hasArg("ssid"))
        {
            wifiSsid = server.arg("ssid");
            wifiSsid.trim();
        }
        if (server.hasArg("pass"))
        {
            String postedPass = server.arg("pass");
            if (!postedPass.isEmpty())
            {
                wifiPass = postedPass;
            }
        }

        saveSettings();
        if (!wifiSsid.isEmpty())
        {
            connectStaIfConfigured();
        }
        githubConsecutiveFailures = 0;
        nextGithubRetryMs = 0;
        fetchPending = true;
        server.send(200, "text/html", buildPage("Saved. Device will refresh stats shortly."));
    }

    void startWebServer()
    {
        server.on("/", HTTP_GET, handleRoot);
        server.on("/save", HTTP_POST, handleSave);
        server.on("/generate_204", HTTP_GET, []()
                  {
            server.sendHeader("Location", String("http://") + kPortalHost, true);
            server.send(302, "text/plain", ""); });
        server.on("/hotspot-detect.html", HTTP_GET, []()
                  {
            server.sendHeader("Location", String("http://") + kPortalHost, true);
            server.send(302, "text/plain", ""); });
        server.on("/connecttest.txt", HTTP_GET, []()
                  {
            server.sendHeader("Location", String("http://") + kPortalHost, true);
            server.send(302, "text/plain", ""); });
        server.on("/ncsi.txt", HTTP_GET, []()
                  {
            server.sendHeader("Location", String("http://") + kPortalHost, true);
            server.send(302, "text/plain", ""); });
        server.onNotFound([]()
                          {
            server.sendHeader("Location", String("http://") + kPortalHost, true);
            server.send(302, "text/plain", ""); });
        server.begin();
    }
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println(kProjectName);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.init();
    tft.setRotation(0);
    logoSprite.setColorDepth(16);
    logoSpriteReady = logoSprite.createSprite(kCubeAreaSize, kCubeAreaSize) != nullptr;
    drawMultilineStatus("Starting setup portal...");

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(kApName, kApPassword);
    dnsServer.start(53, "*", WiFi.softAPIP());

    if (MDNS.begin(kPortalHost))
    {
        MDNS.addService("http", "tcp", 80);
    }

    loadSettings();
    connectStaIfConfigured();
    startWebServer();

    if (wifiSsid.isEmpty())
    {
        drawMultilineStatus("Connect to Wi-Fi:", String(kApName), "Open http://" + String(kPortalHost));
    }
    else
    {
        drawWifiProgress();
    }
    fetchPending = true;
}

void loop()
{
    dnsServer.processNextRequest();
    server.handleClient();
    uint32_t nowMs = millis();

    wl_status_t wifiStatus = WiFi.status();
    if (wifiStatus != lastWifiStatus)
    {
        lastWifiStatus = wifiStatus;
        wifiStatusDirty = true;
        Serial.printf("wifi status: %d\n", (int)wifiStatus);
    }

    if (fetchPending && !wifiSsid.isEmpty() && !WiFi.isConnected())
    {
        if (wifiStatusDirty)
        {
            drawWifiProgress();
            wifiStatusDirty = false;
        }

        if (nowMs - lastWifiRetryMs > 15000)
        {
            Serial.println("wifi retrying...");
            connectStaIfConfigured();
        }
    }

    if (fetchPending && WiFi.isConnected() && nowMs - lastFetchAttemptMs > 1500)
    {
        if (nextGithubRetryMs == 0 || hasReachedTime(nowMs, nextGithubRetryMs))
        {
            lastFetchAttemptMs = nowMs;
            fetchPending = false;
            fetchGitHubStats();
        }
    }

    if (profileLoaded && nowMs - lastCubeFrameMs > 60)
    {
        lastCubeFrameMs = nowMs;
        cubeAngle += 0.04f;
        if (cubeAngle > 6.28318f)
        {
            cubeAngle -= 6.28318f;
        }
        drawSpinningCube();
    }

    delay(10);
}
