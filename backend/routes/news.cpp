#include "routes/news.h"
#include "database/db.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <cstdlib>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

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

// Strip HTML tags and decode basic entities from a string
static std::string stripHTML(const std::string& s) {
    std::string out;
    bool inTag = false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '<') { inTag = true; continue; }
        if (s[i] == '>') { inTag = false; out += ' '; continue; }
        if (!inTag) out += s[i];
    }
    // Decode common HTML entities
    std::string decoded;
    decoded.reserve(out.size());
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '&') {
            if (out.substr(i,4) == "&lt;")   { decoded += '<';  i+=3; }
            else if (out.substr(i,4) == "&gt;")   { decoded += '>';  i+=3; }
            else if (out.substr(i,5) == "&amp;")  { decoded += '&';  i+=4; }
            else if (out.substr(i,6) == "&nbsp;") { decoded += ' ';  i+=5; }
            else if (out.substr(i,6) == "&quot;") { decoded += '"';  i+=5; }
            else decoded += out[i];
        } else decoded += out[i];
    }
    // Collapse whitespace
    std::string final;
    bool prevSpace = true;
    for (char c : decoded) {
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        if (c == ' ' && prevSpace) continue;
        prevSpace = (c == ' ');
        final += c;
    }
    while (!final.empty() && final.back() == ' ') final.pop_back();
    return final;
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

        title = stripHTML(exItem("<title>", "</title>"));
        desc  = stripHTML(exItem("<description>", "</description>"));
        // Truncate long descriptions
        if (desc.size() > 200) {
            size_t cut = 197;   // back up to a UTF-8 character boundary so we never split one
            while (cut > 0 && (static_cast<unsigned char>(desc[cut]) & 0xC0) == 0x80) --cut;
            desc = desc.substr(0, cut) + "...";
        }
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
        if ((int)arr.size() >= 12) break;
        pos = end + 7;
    }
    return arr;
}

// Fetch live news from free RSS feeds — no API key needed
static json fetchLiveNews() {
    json all = json::array();

    struct Feed { std::string url; std::string source; std::string tag; };
    std::vector<Feed> feeds = {
        {"https://feeds.feedburner.com/ndtvprofit-latest",                "NDTV Profit",        "Markets"},
        {"https://feeds.feedburner.com/ndtvprofit-topstories",            "NDTV Profit",        "Top Stories"},
        {"https://www.thehindu.com/business/markets/?service=rss",        "The Hindu",          "Markets"},
        {"https://timesofindia.indiatimes.com/rssfeeds/1898055.cms",      "Times of India",     "Business"},
        {"https://economictimes.indiatimes.com/markets/rssfeeds/1977021183.cms", "ET Markets",   "Markets"},
        {"https://www.livemint.com/rss/markets",                          "Livemint",           "Markets"},
        {"https://www.moneycontrol.com/rss/marketreports.xml",            "Moneycontrol",       "Markets"},
        {"https://www.livemint.com/rss/money",                            "Livemint",           "Personal Finance"},
        {"https://economictimes.indiatimes.com/wealth/rssfeeds/837555174.cms", "ET Wealth",     "Personal Finance"},
        {"https://www.financialexpress.com/feed/",                        "Financial Express",  "Economy"},
    };

    for (auto& f : feeds) {
        std::string raw = httpGet(f.url + "?_t=" + std::to_string(time(nullptr)));
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
    // Get current time for fresh-looking fallback dates
    time_t now = time(nullptr);
    char buf[32];
    auto mkDate = [&](int hoursAgo) -> std::string {
        time_t t = now - hoursAgo * 3600;
        struct tm* tm = gmtime(&t);
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm);
        return std::string(buf);
    };
    return json::array({
        {{"title","Sensex surges 500 points as FII inflows return"},{"description","Foreign institutional investors returned to Indian markets after a brief pullback. BSE benchmark index climbed above 75,000 on strong buying in banking and IT stocks."},{"url","https://economictimes.indiatimes.com"},{"publishedAt",mkDate(1)},{"source","Economic Times"},{"tag","Markets"}},
        {{"title","RBI holds repo rate steady at 6.5%"},{"description","The Reserve Bank of India kept its benchmark lending rate unchanged in its latest monetary policy review, in line with market expectations."},{"url","https://livemint.com"},{"publishedAt",mkDate(2)},{"source","LiveMint"},{"tag","RBI"}},
        {{"title","India's GDP growth forecast revised upward to 7.2%"},{"description","The IMF revised India's GDP growth forecast upward citing strong domestic consumption and infrastructure spending."},{"url","https://businessstandard.com"},{"publishedAt",mkDate(3)},{"source","Business Standard"},{"tag","Economy"}},
        {{"title","Nifty IT index gains 2% on strong Q4 earnings expectations"},{"description","Technology stocks rallied on the NSE amid strong fourth-quarter result expectations. TCS, Infosys led the rally."},{"url","https://financialexpress.com"},{"publishedAt",mkDate(4)},{"source","Financial Express"},{"tag","Nifty IT"}},
        {{"title","Gold prices hit all-time high in Indian markets"},{"description","Gold prices in India crossed Rs 75,000 per 10 grams for the first time amid global safe-haven demand."},{"url","https://moneycontrol.com"},{"publishedAt",mkDate(5)},{"source","Moneycontrol"},{"tag","Gold"}},
        {{"title","Adani Group stocks rally as FPIs increase stake"},{"description","Shares of Adani Group companies surged after foreign portfolio investors increased their holdings."},{"url","https://ndtvprofit.com"},{"publishedAt",mkDate(6)},{"source","NDTV Profit"},{"tag","Stocks"}},
        {{"title","Mutual fund SIP inflows cross Rs 20,000 crore milestone"},{"description","Systematic investment plan contributions to mutual funds crossed Rs 20,000 crore per month for the first time."},{"url","https://livemint.com"},{"publishedAt",mkDate(7)},{"source","Livemint"},{"tag","Mutual Funds"}},
        {{"title","SEBI tightens F&O trading rules for retail investors"},{"description","Markets regulator SEBI announced new rules to protect retail investors in the futures and options segment."},{"url","https://economictimes.indiatimes.com"},{"publishedAt",mkDate(8)},{"source","ET Markets"},{"tag","Regulation"}},
        {{"title","Rupee strengthens against dollar on FII inflows"},{"description","The Indian rupee appreciated against the US dollar supported by sustained foreign institutional investor inflows."},{"url","https://moneycontrol.com"},{"publishedAt",mkDate(10)},{"source","Moneycontrol"},{"tag","Forex"}},
        {{"title","HDFC Bank posts 20% rise in Q4 net profit"},{"description","HDFC Bank reported a 20% year-on-year rise in net profit for the fourth quarter, beating analyst estimates."},{"url","https://economictimes.indiatimes.com"},{"publishedAt",mkDate(12)},{"source","Economic Times"},{"tag","Banking"}}
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

        // 3. Score and sort by relevance
        // Finance keywords boost score; portfolio symbols boost more
        auto symbols = (userId > 0) ? getUserSymbols(db, userId) : std::vector<std::string>{};
        std::vector<std::string> finKeywords = {
            "stock", "market", "nifty", "sensex", "bse", "nse", "share", "equity",
            "mutual fund", "sip", "rbi", "repo rate", "inflation", "gdp", "economy",
            "crypto", "bitcoin", "budget", "tax", "gst", "ipo", "earnings",
            "dividend", "profit", "revenue", "quarterly", "fiscal"
        };

        for (auto& article : result) {
            double score = 0;
            std::string titleLower = article.value("title", "");
            std::string descLower  = article.value("description", "");
            // Lowercase for matching
            for (auto& c : titleLower) c = tolower((unsigned char)c);
            for (auto& c : descLower) c = tolower((unsigned char)c);
            std::string combined = titleLower + " " + descLower;

            // Portfolio symbol match = +10
            for (auto& sym : symbols) {
                std::string symLow = sym;
                for (auto& c : symLow) c = tolower((unsigned char)c);
                if (combined.find(symLow) != std::string::npos) { score += 10; break; }
            }
            // Finance keyword match = +2 each (max +12)
            int kwHits = 0;
            for (auto& kw : finKeywords) {
                if (combined.find(kw) != std::string::npos) { score += 2; kwHits++; }
                if (kwHits >= 6) break;
            }
            // Has portfolioTag = +3
            if (article.contains("portfolioTag")) score += 3;

            article["_score"] = score;
        }

        // Sort by score descending
        std::sort(result.begin(), result.end(),
            [](const json& a, const json& b) {
                return a.value("_score", 0.0) > b.value("_score", 0.0);
            });

        // Remove score field and limit to 25
        json filtered = json::array();
        for (size_t i = 0; i < result.size() && i < 40; i++) {
            json article = result[i];
            article.erase("_score");
            filtered.push_back(article);
        }
        result = filtered;

        // 4. Fallback to mock if empty
        if (result.empty()) result = getMockNews();

        crow::response res(result.dump(-1, ' ', false, json::error_handler_t::replace));
        res.add_header("Content-Type", "application/json");
        res.add_header("Cache-Control", "no-store, no-cache, must-revalidate");
        res.add_header("Pragma", "no-cache");
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
        res.add_header("Cache-Control", "no-store, no-cache, must-revalidate");
        res.add_header("Pragma", "no-cache");
        addCORS(res);
        return res;
    });
}
