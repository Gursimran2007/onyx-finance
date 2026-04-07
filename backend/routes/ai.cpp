#include "routes/ai.h"
#include "database/db.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

// ---- libcurl write callback ----
static size_t writeCallback(char* ptr, size_t size, size_t nmemb, std::string* data) {
    data->append(ptr, size * nmemb);
    return size * nmemb;
}

// ---- Call Groq API (free tier, OpenAI-compatible) ----
static std::string callGroq(const std::string& userMessage) {
    const char* apiKey = std::getenv("GROQ_API_KEY");
    if (!apiKey) throw std::runtime_error("GROQ_API_KEY not set");

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
            auto body     = json::parse(req.body);
            auto question = body.value("question", "Give me a summary of my spending.");

            // Grab last 30 transactions for context
            auto txns   = getRecentTransactions(db, 30);
            auto context = buildContext(txns);

            std::string prompt =
                context +
                "\nUser question: " + question +
                "\n\nYou are a personal finance assistant. "
                "Answer concisely and give actionable advice. "
                "Use bullet points where helpful.";

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
