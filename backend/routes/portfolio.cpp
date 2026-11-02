#include "routes/portfolio.h"
#include "database/db.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <string>
#include <sstream>
#include <map>
#include <mutex>
#include <ctime>
#include <thread>
#include <chrono>

using json = nlohmann::json;

// ── Yahoo Finance crumb session (refreshed every 2 hours) ────────
static std::mutex  g_yfMutex;
static std::string g_yfCookieFile = "/tmp/onyx_yf_cookies.txt";
static std::string g_yfCrumb;
static time_t      g_yfCrumbExpiry = 0;

// ── NSE India session cookie jar ─────────────────────────────────
static std::mutex  g_nseMutex;
static std::string g_nseCookieFile = "/tmp/onyx_nse_cookies.txt";
static time_t      g_nseSessionExpiry = 0;

// ── 5-minute in-memory price cache (avoid hammering APIs) ────────
static std::mutex g_priceCacheMutex;
static std::map<std::string, std::pair<time_t, double>> g_priceCache;

// ── 30-minute history cache ───────────────────────────────────────
static std::mutex g_cacheMutex;
static std::map<std::string, std::pair<time_t, std::string>> g_historyCache;

static void addCORS(crow::response& res) {
    res.add_header("Access-Control-Allow-Origin",  "*");
    res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

static std::string extractBearerToken(const crow::request& req) {
    std::string auth = req.get_header_value("Authorization");
    if (auth.size() > 7 && auth.substr(0, 7) == "Bearer ") {
        return auth.substr(7);
    }
    return "";
}

static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total = size * nmemb;
    output->append(static_cast<char*>(contents), total);
    return total;
}

static const char* YF_UA =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

// Plain GET (used for price + MF)
static std::string httpGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string response;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, YF_UA);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.9");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return response;
}

// GET with persistent cookie jar (used for history which needs crumb auth)
static std::string httpGetWithCookies(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string response;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, YF_UA);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, g_yfCookieFile.c_str());
        curl_easy_setopt(curl, CURLOPT_COOKIEJAR,  g_yfCookieFile.c_str());
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: application/json,text/plain,*/*");
        headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.9");
        headers = curl_slist_append(headers, "Origin: https://finance.yahoo.com");
        headers = curl_slist_append(headers, "Referer: https://finance.yahoo.com/");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return response;
}

// Generic cookie-jar GET (reusable for both NSE and Yahoo)
static std::string httpGetWithCookieFile(const std::string& url,
                                          const std::string& cookieFile,
                                          const std::string& referer = "") {
    CURL* curl = curl_easy_init();
    std::string response;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, YF_UA);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookieFile.c_str());
        curl_easy_setopt(curl, CURLOPT_COOKIEJAR,  cookieFile.c_str());
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: application/json, text/plain, */*");
        headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.9");
        if (!referer.empty()) {
            std::string ref = "Referer: " + referer;
            headers = curl_slist_append(headers, ref.c_str());
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return response;
}

// Warms up NSE India session cookie (needed for API calls)
static void warmNSESession() {
    std::lock_guard<std::mutex> lk(g_nseMutex);
    if (time(nullptr) < g_nseSessionExpiry) return;
    // Visit main NSE page to get session cookies
    httpGetWithCookieFile("https://www.nseindia.com/", g_nseCookieFile,
                          "https://www.nseindia.com/");
    g_nseSessionExpiry = time(nullptr) + 3600; // 1-hour TTL
    std::cout << "[Portfolio] NSE session initialized\n";
}

// Fetch price from NSE India (primary source for .NS symbols)
static double fetchNSEPrice(const std::string& nseSymbol) {
    warmNSESession();
    std::string url = "https://www.nseindia.com/api/quote-equity?symbol=" + nseSymbol;
    std::string raw = httpGetWithCookieFile(url, g_nseCookieFile, "https://www.nseindia.com/");
    if (raw.empty()) return -1.0;
    try {
        auto parsed = json::parse(raw);
        double price = parsed["priceInfo"]["lastPrice"].get<double>();
        if (price > 0) return price;
    } catch (...) {}
    return -1.0;
}

// Returns valid Yahoo Finance crumb (refreshes session if expired)
static std::string getYFCrumb() {
    std::lock_guard<std::mutex> lk(g_yfMutex);
    if (!g_yfCrumb.empty() && time(nullptr) < g_yfCrumbExpiry) {
        return g_yfCrumb;
    }
    // Step 1: seed the cookie jar by visiting fc.yahoo.com
    httpGetWithCookies("https://fc.yahoo.com/");
    // Step 2: get crumb
    std::string crumb = httpGetWithCookies(
        "https://query2.finance.yahoo.com/v1/test/getcrumb");
    // Crumb is a short plain string (~10-15 chars), not JSON
    if (!crumb.empty() && crumb.find('{') == std::string::npos && crumb.size() < 40) {
        g_yfCrumb       = crumb;
        g_yfCrumbExpiry = time(nullptr) + 7200; // 2-hour TTL
    }
    return g_yfCrumb;
}

void setupPortfolioRoutes(crow::SimpleApp& app, SQLite::Database& db) {
    // Pre-warm Yahoo Finance crumb in background so first history request works
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        getYFCrumb();
        std::cout << "[Portfolio] Yahoo Finance crumb initialized\n";
    }).detach();

    // ---------- OPTIONS preflight ----------
    CROW_ROUTE(app, "/api/portfolio/stocks").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/portfolio/stocks/<int>").methods("OPTIONS"_method)
    ([](const crow::request&, int) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/portfolio/price/<string>").methods("OPTIONS"_method)
    ([](const crow::request&, std::string) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/portfolio/mf/<string>").methods("OPTIONS"_method)
    ([](const crow::request&, std::string) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/portfolio/history/<string>").methods("OPTIONS"_method)
    ([](const crow::request&, std::string) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    // ---------- GET /api/portfolio/stocks ----------
    CROW_ROUTE(app, "/api/portfolio/stocks").methods("GET"_method)
    ([&db](const crow::request& req) {
        std::string token = extractBearerToken(req);
        int userId = 0;
        if (token.empty() || !validateSession(db, token, userId)) {
            crow::response res(401, "{\"error\":\"unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        auto stocks = getStocks(db, userId);
        json arr = json::array();
        for (auto& s : stocks) arr.push_back(s);

        crow::response res(arr.dump());
        res.add_header("Content-Type", "application/json");
        addCORS(res);
        return res;
    });

    // ---------- POST /api/portfolio/stocks ----------
    CROW_ROUTE(app, "/api/portfolio/stocks").methods("POST"_method)
    ([&db](const crow::request& req) {
        std::string token = extractBearerToken(req);
        int userId = 0;
        if (token.empty() || !validateSession(db, token, userId)) {
            crow::response res(401, "{\"error\":\"unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        try {
            auto body = json::parse(req.body);
            std::string symbol = body.value("symbol", "");
            std::string name   = body.value("name",   "");
            double qty         = body.value("qty",      0.0);
            double avgPrice    = body.value("avgPrice", 0.0);

            if (symbol.empty()) {
                crow::response res(400, "{\"error\":\"symbol required\"}");
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }

            upsertStock(db, userId, symbol, name, qty, avgPrice);
            crow::response res(201, "{\"status\":\"created\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        } catch (std::exception& e) {
            crow::response res(400, std::string("{\"error\":\"") + e.what() + "\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }
    });

    // ---------- DELETE /api/portfolio/stocks/<id> ----------
    CROW_ROUTE(app, "/api/portfolio/stocks/<int>").methods("DELETE"_method)
    ([&db](const crow::request& req, int id) {
        std::string token = extractBearerToken(req);
        int userId = 0;
        if (token.empty() || !validateSession(db, token, userId)) {
            crow::response res(401, "{\"error\":\"unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        deleteStock(db, userId, id);
        crow::response res(200, "{\"status\":\"deleted\"}");
        res.add_header("Content-Type", "application/json");
        addCORS(res);
        return res;
    });

    // ---------- GET /api/portfolio/price/<symbol> ----------
    CROW_ROUTE(app, "/api/portfolio/price/<string>").methods("GET"_method)
    ([&db](const crow::request& req, std::string symbol) {
        std::string token = extractBearerToken(req);
        int userId = 0;
        if (token.empty() || !validateSession(db, token, userId)) {
            crow::response res(401, "{\"error\":\"unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        // --- Check 5-minute price cache first ---
        {
            std::lock_guard<std::mutex> lk(g_priceCacheMutex);
            auto it = g_priceCache.find(symbol);
            if (it != g_priceCache.end() && time(nullptr) < it->second.first) {
                double cached = it->second.second;
                json resp = {{"symbol", symbol}, {"price", cached}, {"currency", "INR"}, {"cached", true}};
                crow::response res(resp.dump());
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }
        }

        // --- Try NSE India (primary — works for all valid NSE symbols) ---
        // Strip .NS / .BO suffix: RELIANCE.NS → RELIANCE
        std::string nseSymbol = symbol;
        {
            auto dotPos = nseSymbol.rfind('.');
            if (dotPos != std::string::npos) nseSymbol = nseSymbol.substr(0, dotPos);
            for (auto& c : nseSymbol) c = toupper((unsigned char)c);
        }

        double price = fetchNSEPrice(nseSymbol);

        // --- Fallback: MoneyControl price API ---
        if (price <= 0) {
            try {
                std::string mcUrl = "https://priceapi.moneycontrol.com/pricefeed/nse/equitycash/" + nseSymbol;
                std::string mcRaw = httpGet(mcUrl);
                if (!mcRaw.empty()) {
                    auto mc = json::parse(mcRaw);
                    std::string hp = mc["data"].value("HP", "");
                    if (hp.empty()) hp = mc["data"].value("pricecurrent", "");
                    if (!hp.empty()) price = std::stod(hp);
                }
            } catch (...) {}
        }

        // --- Fallback: Yahoo Finance v8 ---
        if (price <= 0) {
            try {
                std::string yfUrl = "https://query1.finance.yahoo.com/v8/finance/chart/" + symbol + "?interval=1d&range=1d";
                std::string yfRaw = httpGet(yfUrl);
                if (!yfRaw.empty()) {
                    auto yf = json::parse(yfRaw);
                    price = yf["chart"]["result"][0]["meta"]["regularMarketPrice"].get<double>();
                }
            } catch (...) {}
        }

        if (price > 0) {
            // Cache it for 5 minutes
            {
                std::lock_guard<std::mutex> lk(g_priceCacheMutex);
                g_priceCache[symbol] = { time(nullptr) + 300, price };
            }
            json resp = {{"symbol", symbol}, {"price", price}, {"currency", "INR"}};
            crow::response res(resp.dump());
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        // All sources failed
        crow::response res(502, "{\"error\":\"price unavailable — check symbol is a valid NSE ticker (e.g. RELIANCE.NS)\"}");
        res.add_header("Content-Type", "application/json");
        addCORS(res);
        return res;
    });

    // ---------- GET /api/portfolio/history/<symbol>?range=1y ----------
    CROW_ROUTE(app, "/api/portfolio/history/<string>").methods("GET"_method)
    ([&db](const crow::request& req, std::string symbol) {
        std::string token = extractBearerToken(req);
        int userId = 0;
        if (token.empty() || !validateSession(db, token, userId)) {
            crow::response res(401, "{\"error\":\"unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        std::string range = "1y";
        auto qr = req.url_params.get("range");
        if (qr) range = std::string(qr);

        // Check 30-min in-memory cache first (prevents Yahoo rate-limiting)
        std::string cacheKey = symbol + "_" + range;
        {
            std::lock_guard<std::mutex> lk(g_cacheMutex);
            auto it = g_historyCache.find(cacheKey);
            if (it != g_historyCache.end() && time(nullptr) < it->second.first) {
                crow::response res(it->second.second);
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }
        }

        // Get crumb for authenticated request
        std::string crumb = getYFCrumb();

        // Use query2 + crumb for history (avoids 429 rate-limiting)
        std::string url = "https://query2.finance.yahoo.com/v8/finance/chart/" + symbol
                        + "?interval=1wk&range=" + range;
        if (!crumb.empty()) url += "&crumb=" + crumb;
        std::string raw = httpGetWithCookies(url);

        try {
            auto parsed   = json::parse(raw);
            auto& result  = parsed["chart"]["result"][0];
            auto& tsArr   = result["timestamp"];
            auto& closes  = result["indicators"]["quote"][0]["close"];

            json arr = json::array();
            for (size_t i = 0; i < tsArr.size() && i < closes.size(); i++) {
                if (closes[i].is_null()) continue;
                long long ts = tsArr[i].get<long long>();
                time_t t = (time_t)ts;
                char dateBuf[16];
                struct tm* tm_info = gmtime(&t);
                strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", tm_info);
                arr.push_back({ {"date", std::string(dateBuf)},
                                {"close", closes[i].get<double>()} });
            }

            std::string body = arr.dump();
            // Store in cache for 30 minutes
            {
                std::lock_guard<std::mutex> lk(g_cacheMutex);
                g_historyCache[cacheKey] = { time(nullptr) + 1800, body };
            }

            crow::response res(body);
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        } catch (std::exception& e) {
            crow::response res(502, std::string("{\"error\":\"") + e.what() + "\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }
    });

    // ---------- GET /api/portfolio/mf/<scheme_code> ----------
    CROW_ROUTE(app, "/api/portfolio/mf/<string>").methods("GET"_method)
    ([&db](const crow::request& req, std::string schemeCode) {
        std::string token = extractBearerToken(req);
        int userId = 0;
        if (token.empty() || !validateSession(db, token, userId)) {
            crow::response res(401, "{\"error\":\"unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        std::string url = "https://api.mfapi.in/mf/" + schemeCode + "/latest";
        std::string raw = httpGet(url);

        try {
            auto parsed = json::parse(raw);
            std::string nav  = parsed["data"][0].value("nav",  "0");
            std::string date = parsed["data"][0].value("date", "");

            json resp = {{"schemeCode", schemeCode}, {"nav", nav}, {"date", date}};
            crow::response res(resp.dump());
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        } catch (std::exception& e) {
            crow::response res(502, std::string("{\"error\":\"failed to fetch NAV: ") + e.what() + "\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }
    });
}
