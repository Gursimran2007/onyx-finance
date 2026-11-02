#include "routes/ai.h"
#include "database/db.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <map>

using json = nlohmann::json;

// ---- libcurl write callback ----
static size_t writeCallback(char* ptr, size_t size, size_t nmemb, std::string* data) {
    data->append(ptr, size * nmemb);
    return size * nmemb;
}

// ---- Call Groq API (free tier, OpenAI-compatible) ----
static std::string callGroq(const std::string& userMessage) {
    const char* apiKey = std::getenv("GROQ_API_KEY");
    if (!apiKey || std::string(apiKey).empty())
        throw std::runtime_error("AI Chat requires a GROQ_API_KEY. Get a free key at console.groq.com and restart the server with: GROQ_API_KEY=your_key ./build/finance_app");

    // Groq uses OpenAI-compatible format
    json payload = {
        {"model",      "llama-3.3-70b-versatile"},
        {"max_tokens", 1024},
        {"messages", {{
            {"role",    "user"},
            {"content", userMessage}
        }}}
    };
    std::string body = payload.dump();

    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl init failed");

    std::string responseStr;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, (std::string("Authorization: Bearer ") + apiKey).c_str());

    curl_easy_setopt(curl, CURLOPT_URL,           "https://api.groq.com/openai/v1/chat/completions");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,    headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &responseStr);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       30L);

    CURLcode code = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK)
        throw std::runtime_error(std::string("curl error: ") + curl_easy_strerror(code));

    // Groq response: choices[0].message.content
    auto respJson = json::parse(responseStr);
    if (respJson.contains("error"))
        throw std::runtime_error(respJson["error"]["message"].get<std::string>());

    return respJson["choices"][0]["message"]["content"].get<std::string>();
}

// ---- Build context string from transactions ----
static std::string buildContext(const std::vector<Transaction>& txns) {
    std::ostringstream ss;
    ss << "Here are my recent transactions:\n";
    for (auto& t : txns) {
        ss << "- [" << t.date << "] "
           << t.type << " | "
           << t.category << " | $"
           << t.amount << " | "
           << t.description << "\n";
    }
    return ss.str();
}

static void addCORS(crow::response& res) {
    res.add_header("Access-Control-Allow-Origin",  "*");
    res.add_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.add_header("Access-Control-Allow-Headers", "Content-Type");
}

void setupAIRoutes(crow::SimpleApp& app, SQLite::Database& db) {

    // OPTIONS preflight
    CROW_ROUTE(app, "/api/ai/analyze").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type");
        return res;
    });

    // POST /api/ai/analyze
    // Body: { "question": "why am I overspending?" }
    CROW_ROUTE(app, "/api/ai/analyze").methods("POST"_method)
    ([&db](const crow::request& req) {
        try {
            // Get userId from session
            int userId = 0;
            auto auth = req.get_header_value("Authorization");
            if (auth.size() > 7 && auth.substr(0, 7) == "Bearer ")
                validateSession(db, auth.substr(7), userId);

            auto body     = json::parse(req.body);
            auto question = body.value("question", "Give me a summary of my spending.");

            // Grab last 30 transactions for this user
            auto txns   = getRecentTransactions(db, userId, 30);
            auto context = buildContext(txns);

            // Compute quick summary stats for context
            double totalInc = 0, totalExp = 0;
            std::map<std::string, double> catSpend;
            for (auto& t : txns) {
                if (t.type == "income")  totalInc += t.amount;
                else { totalExp += t.amount; catSpend[t.category] += t.amount; }
            }
            std::string catBreakdown;
            for (auto& [cat, amt] : catSpend)
                catBreakdown += "  - " + cat + ": ₹" + std::to_string((int)amt) + "\n";

            std::string prompt =
                "You are **Onyx AI** — an expert Indian personal finance advisor built into the Onyx finance app.\n\n"
                "## USER'S FINANCIAL SNAPSHOT (last 30 transactions)\n"
                "- Total Income: ₹" + std::to_string((int)totalInc) + "\n"
                "- Total Expenses: ₹" + std::to_string((int)totalExp) + "\n"
                "- Net Savings: ₹" + std::to_string((int)(totalInc - totalExp)) + "\n"
                "- Savings Rate: " + (totalInc > 0 ? std::to_string((int)((totalInc - totalExp) / totalInc * 100)) : "0") + "%\n"
                "\n## SPENDING BY CATEGORY\n" + catBreakdown +
                "\n## RAW TRANSACTIONS\n" + context +
                "\n## USER QUESTION\n" + question +
                "\n\n## INSTRUCTIONS\n"
                "1. Answer in **structured format** using headers (##), bullet points, and bold for key figures\n"
                "2. Always use ₹ (Indian Rupees) — never $\n"
                "3. Reference specific transactions/categories from the data above\n"
                "4. Give **actionable, specific advice** (not generic)\n"
                "5. If asked about budgeting, suggest exact ₹ amounts per category based on their spending patterns\n"
                "6. If asked about tax, use Indian tax slabs (new regime) and mention Section 80C, 80D, HRA where relevant\n"
                "7. If asked about the app, explain Onyx features: Dashboard, Import (CSV/SMS/Portfolio), Portfolio tracker, News, Goals, Budget, Analytics, Report, AI Trader, AI Investor\n"
                "8. NEVER recommend specific stocks or guarantee returns. Say 'this is not financial advice' for investment questions\n"
                "9. Keep responses concise (under 300 words) but thorough\n"
                "10. Use a friendly, direct tone — like a knowledgeable friend, not a formal advisor\n";

            std::string reply = callGroq(prompt);

            json j = {{"reply", reply}};
            crow::response res(j.dump());
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;

        } catch (std::exception& e) {
            json j = {{"error", e.what()}};
            crow::response res(500, j.dump());
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }
    });
}
