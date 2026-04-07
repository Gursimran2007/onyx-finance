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

// Parse RSS XML into JSON articles (minimal parser, no external lib)
static json parseRSS(const std::string& xml, const std::string& source, const std::string& tag = "") {
    json arr = json::array();
    size_t pos = 0;
    auto extract = [&](const std::string& startTag, const std::string& endTag, size_t from) -> std::string {
        size_t s = xml.find(startTag, from);
        if (s == std::string::npos) return "";
        s += startTag.size();
        size_t e = xml.find(endTag, s);
        if (e == std::string::npos) return "";
        std::string val = xml.substr(s, e - s);
        // Strip CDATA
        if (val.rfind("<![CDATA[", 0) == 0) val = val.substr(9, val.size() - 12);
        return val;
    };

    while ((pos = xml.find("<item>", pos)) != std::string::npos) {
        size_t end = xml.find("</item>", pos);
        if (end == std::string::npos) break;
        std::string item = xml.substr(pos, end - pos);
        std::string title = extract("<title>", "</title>", 0);
        std::string desc  = extract("<description>", "</description>", 0);
        std::string link  = extract("<link>", "</link>", 0);
        std::string date  = extract("<pubDate>", "</pubDate>", 0);

        // Extract from item block
        auto exItem = [&](const std::string& st, const std::string& et) {
            size_t s = item.find(st);
            if (s == std::string::npos) return std::string("");
            s += st.size();
            size_t e = item.find(et, s);
            if (e == std::string::npos) return std::string("");
            std::string v = item.substr(s, e - s);
            if (v.rfind("<![CDATA[", 0) == 0) v = v.substr(9, v.size() - 12);
            return v;
        };

        title = exItem("<title>", "</title>");
        desc  = exItem("<description>", "</description>");
        link  = exItem("<link>", "</link>");
        date  = exItem("<pubDate>", "</pubDate>");

        if (title.empty() || title == source) { pos = end + 7; continue; }

        json article = {
            {"title",       title},
            {"description", desc},
            {"url",         link},
            {"publishedAt", date},
            {"source",      source}
        };
        if (!tag.empty()) article["portfolioTag"] = tag;
        arr.push_back(article);
        if ((int)arr.size() >= 6) break;
        pos = end + 7;
    }
    return arr;
}

// Fetch live news from free RSS feeds — no API key needed
static json fetchLiveNews() {
    json all = json::array();

    struct Feed { std::string url; std::string source; std::string tag; };
    std::vector<Feed> feeds = {
        {"https://feeds.feedburner.com/ndtvprofit-latest",              "NDTV Profit",      "Markets"},
        {"https://feeds.feedburner.com/ndtvprofit-topstories",          "NDTV Profit",      "Top Stories"},
        {"https://www.thehindu.com/business/markets/?service=rss",      "The Hindu",        "Markets"},
        {"https://timesofindia.indiatimes.com/rssfeeds/1898055.cms",    "Times of India",   "Business"},
    };

    for (auto& f : feeds) {
        std::string raw = httpGet(f.url);
        if (raw.empty()) continue;
        auto articles = parseRSS(raw, f.source);
        for (auto& a : articles) { a["tag"] = f.tag; all.push_back(a); }
    }

    return all;
}

// Fetch stock-specific news via Google News RSS (free, no key)
static json fetchStockNews(const std::string& symbol, const std::string& tag) {
    // URL encode the symbol
    std::string query = symbol + "+stock+NSE+India";
    std::string url = "https://news.google.com/rss/search?q=" + query + "&hl=en-IN&gl=IN&ceid=IN:en";
    std::string raw = httpGet(url);
    return parseRSS(raw, "Google News", tag);
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
    // Returns live RSS news + portfolio-specific news — no API key needed
    CROW_ROUTE(app, "/api/news").methods("GET"_method)
    ([&db](const crow::request& req) {
        // Get userId from session
        int userId = 0;
        auto auth = req.get_header_value("Authorization");
        if (auth.size() > 7 && auth.substr(0, 7) == "Bearer ")
            validateSession(db, auth.substr(7), userId);

        json result = json::array();

        // 1. Live general finance news from RSS feeds
        auto general = fetchLiveNews();
        for (auto& a : general) result.push_back(a);

        // 2. Portfolio-specific news via Google News RSS
        if (userId > 0) {
            auto symbols = getUserSymbols(db, userId);
            for (auto& sym : symbols) {
                auto stockNews = fetchStockNews(sym, sym);
                for (auto& a : stockNews) result.push_back(a);
            }
        }

        // 3. Fallback to mock if RSS fetch failed
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
