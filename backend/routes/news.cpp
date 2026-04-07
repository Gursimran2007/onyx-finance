#include "routes/news.h"
#include "database/db.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <cstdlib>
#include <string>
#include <vector>
#include <sstream>

using json = nlohmann::json;

static void addCORS(crow::response& res) {
    res.add_header("Access-Control-Allow-Origin",  "*");
    res.add_header("Access-Control-Allow-Methods", "GET, OPTIONS");
    res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    output->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

static std::string httpGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string response;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL,           url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT,       10L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT,     "Mozilla/5.0");
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    return response;
}

// Fetch news for a given query from NewsAPI
static json fetchNews(const std::string& apiKey, const std::string& query, int pageSize = 5) {
    std::string url = "https://newsapi.org/v2/everything?"
                      "language=en&sortBy=publishedAt&pageSize=" + std::to_string(pageSize) +
                      "&apiKey=" + apiKey +
                      "&q=" + query;
    std::string raw = httpGet(url);
    json arr = json::array();
    try {
        auto parsed = json::parse(raw);
        if (!parsed.contains("articles")) return arr;
        for (auto& a : parsed["articles"]) {
            if (a.value("title","") == "[Removed]") continue;
            arr.push_back({
                {"title",       a.value("title",       "")},
                {"description", a.value("description", "")},
                {"url",         a.value("url",         "")},
                {"publishedAt", a.value("publishedAt", "")},
                {"source",      a["source"].value("name","")},
                {"tag",         query}
            });
        }
    } catch (...) {}
    return arr;
}

static json getMockNews() {
    return json::array({
        {{"title","Sensex surges 500 points as FII inflows return"},{"description","Foreign institutional investors returned to Indian markets."},{"url","https://economictimes.indiatimes.com"},{"publishedAt","2026-04-07T09:00:00Z"},{"source","Economic Times"},{"tag","India markets"}},
        {{"title","RBI holds repo rate steady at 6.5%"},{"description","The Reserve Bank of India kept its benchmark lending rate unchanged."},{"url","https://livemint.com"},{"publishedAt","2026-04-07T08:30:00Z"},{"source","LiveMint"},{"tag","RBI"}},
        {{"title","India's GDP growth forecast revised upward to 7.2%"},{"description","The IMF revised India's GDP growth forecast upward citing strong domestic consumption."},{"url","https://businessstandard.com"},{"publishedAt","2026-04-06T18:00:00Z"},{"source","Business Standard"},{"tag","Economy"}},
        {{"title","Nifty IT index gains 2% on strong Q4 earnings expectations"},{"description","Technology stocks rallied on the NSE amid strong fourth-quarter result expectations."},{"url","https://financialexpress.com"},{"publishedAt","2026-04-06T15:00:00Z"},{"source","Financial Express"},{"tag","Nifty IT"}},
        {{"title","Gold prices hit all-time high in Indian markets"},{"description","Gold prices in India crossed Rs 75,000 per 10 grams for the first time."},{"url","https://moneycontrol.com"},{"publishedAt","2026-04-06T12:00:00Z"},{"source","Moneycontrol"},{"tag","Gold"}}
    });
}

// Get user's stock symbols from DB
static std::vector<std::string> getUserSymbols(SQLite::Database& db, int userId) {
    std::vector<std::string> symbols;
    try {
        auto stocks = getStocks(db, userId);
        for (auto& s : stocks) {
            std::string sym = s["symbol"].get<std::string>();
            // Strip .NS / .BO suffix for cleaner search queries
            auto dot = sym.find('.');
            if (dot != std::string::npos) sym = sym.substr(0, dot);
            symbols.push_back(sym);
        }
    } catch (...) {}
    return symbols;
}

void setupNewsRoutes(crow::SimpleApp& app, SQLite::Database& db) {

    CROW_ROUTE(app, "/api/news").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    // GET /api/news
    // Returns general Indian finance news + portfolio-specific news if user has stocks
    CROW_ROUTE(app, "/api/news").methods("GET"_method)
    ([&db](const crow::request& req) {
        const char* apiKey = std::getenv("NEWS_API_KEY");

        if (!apiKey || std::string(apiKey).empty()) {
            crow::response res(getMockNews().dump());
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        // Get userId from session
        int userId = 0;
        auto auth = req.get_header_value("Authorization");
        if (auth.size() > 7 && auth.substr(0, 7) == "Bearer ")
            validateSession(db, auth.substr(7), userId);

        json result = json::array();

        // 1. General Indian business news (always shown)
        auto general = fetchNews(apiKey, "India stock market OR NSE OR BSE OR Sensex OR Nifty", 6);
        for (auto& a : general) result.push_back(a);

        // 2. Portfolio-specific news (if user has stocks)
        if (userId > 0) {
            auto symbols = getUserSymbols(db, userId);
            for (auto& sym : symbols) {
                auto stockNews = fetchNews(apiKey, sym + " stock India", 2);
                for (auto& a : stockNews) {
                    // Tag it as portfolio news
                    a["portfolioTag"] = sym;
                    result.push_back(a);
                }
            }
        }

        // 3. Fallback to mock if nothing came back
        if (result.empty()) result = getMockNews();

        crow::response res(result.dump());
        res.add_header("Content-Type", "application/json");
        addCORS(res);
        return res;
    });

    // GET /api/tax/reminders
    CROW_ROUTE(app, "/api/tax/reminders").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/tax/reminders").methods("GET"_method)
    ([]() {
        json arr = json::array({
            {{"title","ITR Filing Deadline"},       {"date","2026-07-31"},{"description","Last date to file Income Tax Return for FY 2025-26"},{"urgent",false}},
            {{"title","Advance Tax Q1"},            {"date","2026-06-15"},{"description","First installment of advance tax payment due"},{"urgent",false}},
            {{"title","GST Monthly Return"},        {"date","2026-04-20"},{"description","GSTR-3B filing for March 2026"},{"urgent",true}},
            {{"title","TDS Deposit"},               {"date","2026-04-07"},{"description","TDS deducted in March 2026 to be deposited"},{"urgent",true}},
            {{"title","Form 16 Issuance"},          {"date","2026-06-15"},{"description","Employers must issue Form 16 to employees by this date"},{"urgent",false}},
            {{"title","Advance Tax Q2"},            {"date","2026-09-15"},{"description","Second installment of advance tax payment due"},{"urgent",false}},
            {{"title","TDS Return Q1 (26Q/24Q)"},   {"date","2026-07-31"},{"description","Filing TDS return for April–June 2026 quarter"},{"urgent",false}}
        });
        crow::response res(arr.dump());
        res.add_header("Content-Type", "application/json");
        addCORS(res);
        return res;
    });
}
