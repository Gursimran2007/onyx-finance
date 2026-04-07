#include "routes/news.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <cstdlib>
#include <string>

using json = nlohmann::json;

static void addCORS(crow::response& res) {
    res.add_header("Access-Control-Allow-Origin",  "*");
    res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total = size * nmemb;
    output->append(static_cast<char*>(contents), total);
    return total;
}

static std::string httpGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string response;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    return response;
}

static json getMockNews() {
    return json::array({
        {
            {"title", "Sensex surges 500 points as FII inflows return"},
            {"description", "Foreign institutional investors returned to Indian markets, pushing the Sensex up by 500 points in early trade."},
            {"url", "https://economictimes.indiatimes.com"},
            {"publishedAt", "2026-04-07T09:00:00Z"},
            {"source", "Economic Times"}
        },
        {
            {"title", "RBI holds repo rate steady at 6.5%"},
            {"description", "The Reserve Bank of India kept its benchmark lending rate unchanged at 6.5% in its latest monetary policy meeting."},
            {"url", "https://livemint.com"},
            {"publishedAt", "2026-04-07T08:30:00Z"},
            {"source", "LiveMint"}
        },
        {
            {"title", "India's GDP growth forecast revised upward to 7.2% for FY2026"},
            {"description", "The IMF revised India's GDP growth forecast upward citing strong domestic consumption and investment."},
            {"url", "https://businessstandard.com"},
            {"publishedAt", "2026-04-06T18:00:00Z"},
            {"source", "Business Standard"}
        },
        {
            {"title", "Nifty IT index gains 2% on strong Q4 earnings expectations"},
            {"description", "Technology stocks rallied on the NSE as investors anticipated strong fourth-quarter results from major IT firms."},
            {"url", "https://financialexpress.com"},
            {"publishedAt", "2026-04-06T15:00:00Z"},
            {"source", "Financial Express"}
        },
        {
            {"title", "Gold prices hit all-time high in Indian markets"},
            {"description", "Gold prices in India crossed the Rs 75,000 per 10 grams mark for the first time amid global uncertainty."},
            {"url", "https://moneycontrol.com"},
            {"publishedAt", "2026-04-06T12:00:00Z"},
            {"source", "Moneycontrol"}
        }
    });
}

void setupNewsRoutes(crow::SimpleApp& app) {

    // ---------- OPTIONS preflight ----------
    CROW_ROUTE(app, "/api/news").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/tax/reminders").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    // ---------- GET /api/news ----------
    CROW_ROUTE(app, "/api/news").methods("GET"_method)
    ([]() {
        const char* apiKey = std::getenv("NEWS_API_KEY");

        if (!apiKey || std::string(apiKey).empty()) {
            json arr = getMockNews();
            crow::response res(arr.dump());
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        std::string url = std::string("https://newsapi.org/v2/top-headlines?country=in&category=business&pageSize=10&apiKey=") + apiKey;
        std::string raw = httpGet(url);

        try {
            auto parsed = json::parse(raw);
            json arr = json::array();
            for (auto& article : parsed["articles"]) {
                json item;
                item["title"]       = article.value("title",       "");
                item["description"] = article.value("description", "");
                item["url"]         = article.value("url",         "");
                item["publishedAt"] = article.value("publishedAt", "");
                item["source"]      = article["source"].value("name", "");
                arr.push_back(item);
            }
            crow::response res(arr.dump());
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        } catch (std::exception&) {
            // Fall back to mock data on parse failure
            json arr = getMockNews();
            crow::response res(arr.dump());
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }
    });

    // ---------- GET /api/tax/reminders ----------
    CROW_ROUTE(app, "/api/tax/reminders").methods("GET"_method)
    ([]() {
        json arr = json::array({
            {
                {"title", "ITR Filing Deadline"},
                {"date", "2026-07-31"},
                {"description", "Last date to file Income Tax Return for FY 2025-26"},
                {"urgent", false}
            },
            {
                {"title", "Advance Tax Q1"},
                {"date", "2026-06-15"},
                {"description", "First installment of advance tax payment"},
                {"urgent", false}
            },
            {
                {"title", "GST Monthly Return"},
                {"date", "2026-04-20"},
                {"description", "GSTR-3B filing for March 2026"},
                {"urgent", true}
            },
            {
                {"title", "TDS Deposit"},
                {"date", "2026-04-07"},
                {"description", "TDS deducted in March 2026 to be deposited"},
                {"urgent", true}
            },
            {
                {"title", "Form 16 Issuance"},
                {"date", "2026-06-15"},
                {"description", "Employers must issue Form 16 to employees"},
                {"urgent", false}
            }
        });

        crow::response res(arr.dump());
        res.add_header("Content-Type", "application/json");
        addCORS(res);
        return res;
    });
}
