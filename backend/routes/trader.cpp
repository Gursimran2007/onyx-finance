#include "routes/trader.h"
#include "database/db.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <string>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <mutex>

using json = nlohmann::json;

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

static size_t curlWriteCb(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total = size * nmemb;
    output->append(static_cast<char*>(contents), total);
    return total;
}

// ── NSE India session for price fetch ───────────────────────────────
static std::mutex  g_traderNseMtx;
static std::string g_traderNseCookieFile = "/tmp/onyx_trader_nse_cookies.txt";
static time_t      g_traderNseExpiry = 0;

static const char* TRADER_UA =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

static std::string traderHttpGet(const std::string& url,
                                  const std::string& cookieFile = "",
                                  const std::string& referer = "") {
    CURL* curl = curl_easy_init();
    std::string response;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, TRADER_UA);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        if (!cookieFile.empty()) {
            curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookieFile.c_str());
            curl_easy_setopt(curl, CURLOPT_COOKIEJAR,  cookieFile.c_str());
        }
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

static void warmTraderNSE() {
    std::lock_guard<std::mutex> lk(g_traderNseMtx);
    if (time(nullptr) < g_traderNseExpiry) return;
    traderHttpGet("https://www.nseindia.com/", g_traderNseCookieFile, "https://www.nseindia.com/");
    g_traderNseExpiry = time(nullptr) + 3600;
}

static double fetchPriceForSymbol(const std::string& symbol) {
    // Strip .NS/.BO suffix
    std::string nse = symbol;
    auto dot = nse.rfind('.');
    if (dot != std::string::npos) nse = nse.substr(0, dot);
    for (auto& c : nse) c = toupper((unsigned char)c);

    // Try NSE India
    warmTraderNSE();
    std::string url = "https://www.nseindia.com/api/quote-equity?symbol=" + nse;
    std::string raw = traderHttpGet(url, g_traderNseCookieFile, "https://www.nseindia.com/");
    if (!raw.empty()) {
        try {
            auto parsed = json::parse(raw);
            double price = parsed["priceInfo"]["lastPrice"].get<double>();
            if (price > 0) return price;
        } catch (...) {}
    }

    // Fallback 2: MoneyControl
    try {
        std::string mcUrl = "https://priceapi.moneycontrol.com/pricefeed/nse/equitycash/" + nse;
        std::string mcRaw = traderHttpGet(mcUrl);
        if (!mcRaw.empty()) {
            auto mc = json::parse(mcRaw);
            std::string pStr = mc["data"]["pricecurrent"].get<std::string>();
            double price = std::stod(pStr);
            if (price > 0) return price;
        }
    } catch (...) {}

    // Fallback 3: Yahoo Finance v8
    try {
        std::string yfUrl = "https://query2.finance.yahoo.com/v8/finance/chart/" + symbol + "?interval=1d&range=1d";
        std::string yfRaw = traderHttpGet(yfUrl);
        if (!yfRaw.empty()) {
            auto yf = json::parse(yfRaw);
            double price = yf["chart"]["result"][0]["meta"]["regularMarketPrice"].get<double>();
            if (price > 0) return price;
        }
    } catch (...) {}

    return -1.0;
}

// ── Table creation ──────────────────────────────────────────────────

void createAITraderTable(SQLite::Database& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS ai_trades ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user_id INTEGER NOT NULL,"
        "  symbol TEXT,"
        "  action TEXT,"
        "  qty REAL,"
        "  price REAL,"
        "  confidence REAL,"
        "  strategy TEXT,"
        "  pnl REAL DEFAULT 0,"
        "  status TEXT DEFAULT 'pending',"
        "  created_at TEXT DEFAULT (datetime('now'))"
        ")"
    );
}

void createAISettingsTable(SQLite::Database& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS ai_trader_settings ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user_id INTEGER NOT NULL UNIQUE,"
        "  capital REAL DEFAULT 100000,"
        "  risk_level TEXT DEFAULT 'moderate',"
        "  strategy TEXT DEFAULT 'momentum',"
        "  active INTEGER DEFAULT 0,"
        "  max_drawdown REAL DEFAULT 0.15,"
        "  stop_loss_pct REAL DEFAULT 0.03,"
        "  take_profit_pct REAL DEFAULT 0.06"
        ")"
    );
}

// ── Routes ──────────────────────────────────────────────────────────

void setupTraderRoutes(crow::SimpleApp& app, SQLite::Database& db) {

    createAITraderTable(db);
    createAISettingsTable(db);
    std::srand((unsigned)std::time(nullptr));

    // ---------- OPTIONS preflight ----------
    CROW_ROUTE(app, "/api/trader/settings").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/trader/trades").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/trader/signal").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/trader/execute/<int>").methods("OPTIONS"_method)
    ([](const crow::request&, int) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/trader/trades/<int>").methods("OPTIONS"_method)
    ([](const crow::request&, int) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/trader/pnl").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    // ---------- GET /api/trader/settings ----------
    CROW_ROUTE(app, "/api/trader/settings").methods("GET"_method)
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
            SQLite::Statement q(db, "SELECT capital, risk_level, strategy, active, max_drawdown, stop_loss_pct, take_profit_pct FROM ai_trader_settings WHERE user_id = ?");
            q.bind(1, userId);
            if (q.executeStep()) {
                json resp = {
                    {"capital",        q.getColumn(0).getDouble()},
                    {"risk_level",     q.getColumn(1).getString()},
                    {"strategy",       q.getColumn(2).getString()},
                    {"active",         q.getColumn(3).getInt()},
                    {"max_drawdown",   q.getColumn(4).getDouble()},
                    {"stop_loss_pct",  q.getColumn(5).getDouble()},
                    {"take_profit_pct",q.getColumn(6).getDouble()}
                };
                crow::response res(resp.dump());
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }
            // Return defaults
            json resp = {
                {"capital", 100000}, {"risk_level", "moderate"}, {"strategy", "momentum"},
                {"active", 0}, {"max_drawdown", 0.15}, {"stop_loss_pct", 0.03}, {"take_profit_pct", 0.06}
            };
            crow::response res(resp.dump());
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        } catch (std::exception& e) {
            crow::response res(500, std::string("{\"error\":\"") + e.what() + "\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }
    });

    // ---------- POST /api/trader/settings ----------
    CROW_ROUTE(app, "/api/trader/settings").methods("POST"_method)
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
            double capital       = body.value("capital", 100000.0);
            std::string riskLvl  = body.value("risk_level", "moderate");
            std::string strategy = body.value("strategy", "momentum");
            int active           = body.value("active", 0);
            double maxDrawdown   = body.value("max_drawdown", 0.15);
            double stopLoss      = body.value("stop_loss_pct", 0.03);
            double takeProfit    = body.value("take_profit_pct", 0.06);

            SQLite::Statement upsert(db,
                "INSERT INTO ai_trader_settings (user_id, capital, risk_level, strategy, active, max_drawdown, stop_loss_pct, take_profit_pct) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
                "ON CONFLICT(user_id) DO UPDATE SET capital=excluded.capital, risk_level=excluded.risk_level, "
                "strategy=excluded.strategy, active=excluded.active, max_drawdown=excluded.max_drawdown, "
                "stop_loss_pct=excluded.stop_loss_pct, take_profit_pct=excluded.take_profit_pct");
            upsert.bind(1, userId);
            upsert.bind(2, capital);
            upsert.bind(3, riskLvl);
            upsert.bind(4, strategy);
            upsert.bind(5, active);
            upsert.bind(6, maxDrawdown);
            upsert.bind(7, stopLoss);
            upsert.bind(8, takeProfit);
            upsert.exec();

            crow::response res(200, "{\"status\":\"saved\"}");
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

    // ---------- GET /api/trader/trades ----------
    CROW_ROUTE(app, "/api/trader/trades").methods("GET"_method)
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
            SQLite::Statement q(db, "SELECT id, symbol, action, qty, price, confidence, strategy, pnl, status, created_at FROM ai_trades WHERE user_id = ? ORDER BY created_at DESC");
            q.bind(1, userId);
            json arr = json::array();
            while (q.executeStep()) {
                arr.push_back({
                    {"id",         q.getColumn(0).getInt()},
                    {"symbol",     q.getColumn(1).getString()},
                    {"action",     q.getColumn(2).getString()},
                    {"qty",        q.getColumn(3).getDouble()},
                    {"price",      q.getColumn(4).getDouble()},
                    {"confidence", q.getColumn(5).getDouble()},
                    {"strategy",   q.getColumn(6).getString()},
                    {"pnl",        q.getColumn(7).getDouble()},
                    {"status",     q.getColumn(8).getString()},
                    {"created_at", q.getColumn(9).getString()}
                });
            }
            crow::response res(arr.dump());
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        } catch (std::exception& e) {
            crow::response res(500, std::string("{\"error\":\"") + e.what() + "\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }
    });

    // ---------- POST /api/trader/signal ----------
    CROW_ROUTE(app, "/api/trader/signal").methods("POST"_method)
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
            if (symbol.empty()) {
                crow::response res(400, "{\"error\":\"symbol required\"}");
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }

            // Price can be passed from frontend (which fetches from /api/portfolio/price/)
            double price = body.value("price", -1.0);
            if (price <= 0) {
                // Fallback: try server-side fetch
                price = fetchPriceForSymbol(symbol);
            }
            if (price <= 0) {
                crow::response res(502, "{\"error\":\"could not fetch price for symbol\"}");
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }

            // Load user settings
            double capital      = 100000;
            std::string riskLvl = "moderate";
            std::string strategy = "momentum";
            double maxDrawdown  = 0.15;
            {
                SQLite::Statement sq(db, "SELECT capital, risk_level, strategy, max_drawdown FROM ai_trader_settings WHERE user_id = ?");
                sq.bind(1, userId);
                if (sq.executeStep()) {
                    capital     = sq.getColumn(0).getDouble();
                    riskLvl     = sq.getColumn(1).getString();
                    strategy    = sq.getColumn(2).getString();
                    maxDrawdown = sq.getColumn(3).getDouble();
                }
            }

            // Try to get avgPrice from stocks table
            double avgPrice = price; // fallback
            {
                // Strip suffix for matching
                std::string sym = symbol;
                auto dot = sym.rfind('.');
                if (dot != std::string::npos) sym = sym.substr(0, dot);
                for (auto& c : sym) c = toupper((unsigned char)c);

                SQLite::Statement sq(db, "SELECT avg_price FROM stocks WHERE user_id = ? AND (symbol = ? OR symbol LIKE ? OR symbol LIKE ?)");
                sq.bind(1, userId);
                sq.bind(2, symbol);
                sq.bind(3, sym + ".%");
                sq.bind(4, "%" + sym + "%");
                if (sq.executeStep()) {
                    avgPrice = sq.getColumn(0).getDouble();
                }
            }

            // Determine action and confidence based on strategy
            std::string action = "BUY";
            double confidence = 0.5;

            if (strategy == "momentum") {
                if (price > avgPrice) {
                    action = "BUY";
                    double pctAbove = ((price - avgPrice) / avgPrice) * 100.0;
                    confidence = std::min(0.95, 0.5 + pctAbove / 100.0);
                } else {
                    action = "SELL";
                    double pctBelow = ((avgPrice - price) / avgPrice) * 100.0;
                    confidence = std::min(0.95, 0.4 + pctBelow / 100.0);
                }
            } else if (strategy == "mean_reversion") {
                if (price < avgPrice) {
                    action = "BUY";  // oversold
                    double pctBelow = ((avgPrice - price) / avgPrice) * 100.0;
                    confidence = std::min(0.95, 0.5 + pctBelow / 50.0);
                } else if (price > avgPrice * 1.15) {
                    action = "SELL"; // overbought
                    double pctAbove = ((price - avgPrice) / avgPrice) * 100.0;
                    confidence = std::min(0.95, 0.5 + (pctAbove - 15.0) / 50.0);
                } else {
                    action = "HOLD";
                    confidence = 0.3;
                }
            } else {
                // "random" strategy
                action = (std::rand() % 2 == 0) ? "BUY" : "SELL";
                confidence = 0.5;
            }

            // Risk sizing: qty = (capital * risk_pct) / price
            double riskPct = 0.05; // moderate default
            if (riskLvl == "conservative") riskPct = 0.02;
            else if (riskLvl == "aggressive") riskPct = 0.10;

            double qty = std::floor((capital * riskPct) / price);
            if (qty < 1) qty = 1;

            // Check max drawdown: if total unrealized loss > max_drawdown * capital, confidence = 0
            {
                SQLite::Statement pq(db, "SELECT COALESCE(SUM(pnl), 0) FROM ai_trades WHERE user_id = ? AND status = 'executed'");
                pq.bind(1, userId);
                if (pq.executeStep()) {
                    double totalPnl = pq.getColumn(0).getDouble();
                    if (totalPnl < 0 && std::abs(totalPnl) > maxDrawdown * capital) {
                        confidence = 0;
                    }
                }
            }

            // Insert trade as signal
            SQLite::Statement ins(db,
                "INSERT INTO ai_trades (user_id, symbol, action, qty, price, confidence, strategy, status) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, 'signal')");
            ins.bind(1, userId);
            ins.bind(2, symbol);
            ins.bind(3, action);
            ins.bind(4, qty);
            ins.bind(5, price);
            ins.bind(6, confidence);
            ins.bind(7, strategy);
            ins.exec();

            int tradeId = (int)db.getLastInsertRowid();

            json resp = {
                {"id", tradeId}, {"symbol", symbol}, {"action", action},
                {"qty", qty}, {"price", price}, {"confidence", confidence},
                {"strategy", strategy}, {"status", "signal"}
            };
            crow::response res(200, resp.dump());
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        } catch (std::exception& e) {
            crow::response res(500, std::string("{\"error\":\"") + e.what() + "\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }
    });

    // ---------- POST /api/trader/execute/<id> ----------
    CROW_ROUTE(app, "/api/trader/execute/<int>").methods("POST"_method)
    ([&db](const crow::request& req, int tradeId) {
        std::string token = extractBearerToken(req);
        int userId = 0;
        if (token.empty() || !validateSession(db, token, userId)) {
            crow::response res(401, "{\"error\":\"unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        try {
            SQLite::Statement upd(db, "UPDATE ai_trades SET status = 'executed' WHERE id = ? AND user_id = ? AND status = 'signal'");
            upd.bind(1, tradeId);
            upd.bind(2, userId);
            int rows = upd.exec();

            if (rows == 0) {
                crow::response res(404, "{\"error\":\"trade not found or already executed\"}");
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }

            crow::response res(200, "{\"status\":\"executed\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        } catch (std::exception& e) {
            crow::response res(500, std::string("{\"error\":\"") + e.what() + "\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }
    });

    // ---------- DELETE /api/trader/trades/<id> ----------
    CROW_ROUTE(app, "/api/trader/trades/<int>").methods("DELETE"_method)
    ([&db](const crow::request& req, int tradeId) {
        std::string token = extractBearerToken(req);
        int userId = 0;
        if (token.empty() || !validateSession(db, token, userId)) {
            crow::response res(401, "{\"error\":\"unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        SQLite::Statement del(db, "DELETE FROM ai_trades WHERE id = ? AND user_id = ?");
        del.bind(1, tradeId);
        del.bind(2, userId);
        del.exec();

        crow::response res(200, "{\"status\":\"deleted\"}");
        res.add_header("Content-Type", "application/json");
        addCORS(res);
        return res;
    });

    // ---------- GET /api/trader/pnl ----------
    CROW_ROUTE(app, "/api/trader/pnl").methods("GET"_method)
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
            double totalPnl = 0;
            int totalTrades = 0, wins = 0, losses = 0;

            SQLite::Statement q(db, "SELECT pnl FROM ai_trades WHERE user_id = ? AND status = 'executed'");
            q.bind(1, userId);
            while (q.executeStep()) {
                double pnl = q.getColumn(0).getDouble();
                totalPnl += pnl;
                totalTrades++;
                if (pnl >= 0) wins++;
                else losses++;
            }

            double winRate = totalTrades > 0 ? (double)wins / (double)totalTrades : 0;

            json resp = {
                {"totalPnl", totalPnl},
                {"totalTrades", totalTrades},
                {"wins", wins},
                {"losses", losses},
                {"winRate", winRate}
            };
            crow::response res(resp.dump());
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        } catch (std::exception& e) {
            crow::response res(500, std::string("{\"error\":\"") + e.what() + "\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }
    });
}
