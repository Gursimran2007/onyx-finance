#include "routes/goals.h"
#include "database/db.h"
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

static void addCORS(crow::response& res) {
    res.add_header("Access-Control-Allow-Origin",  "*");
    res.add_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
    res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

static std::string extractBearerToken(const crow::request& req) {
    std::string auth = req.get_header_value("Authorization");
    if (auth.size() > 7 && auth.substr(0, 7) == "Bearer ") {
        return auth.substr(7);
    }
    return "";
}

void setupGoalsRoutes(crow::SimpleApp& app, SQLite::Database& db) {

    // ---------- OPTIONS preflight ----------
    CROW_ROUTE(app, "/api/goals").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/goals/<int>").methods("OPTIONS"_method)
    ([](const crow::request&, int) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    // ---------- GET /api/goals ----------
    CROW_ROUTE(app, "/api/goals").methods("GET"_method)
    ([&db](const crow::request& req) {
        std::string token = extractBearerToken(req);
        int userId = 0;
        if (token.empty() || !validateSession(db, token, userId)) {
            crow::response res(401, "{\"error\":\"unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        auto goals = getGoals(db, userId);
        json arr = json::array();
        for (auto& g : goals) arr.push_back(g);

        crow::response res(arr.dump());
        res.add_header("Content-Type", "application/json");
        addCORS(res);
        return res;
    });

    // ---------- POST /api/goals ----------
    CROW_ROUTE(app, "/api/goals").methods("POST"_method)
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
            std::string name       = body.value("name",         "");
            double targetAmount    = body.value("targetAmount",  0.0);
            std::string targetDate = body.value("targetDate",   "");

            if (name.empty()) {
                crow::response res(400, "{\"error\":\"name required\"}");
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }

            insertGoal(db, userId, name, targetAmount, targetDate);
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

    // ---------- PATCH /api/goals/<id> ----------
    CROW_ROUTE(app, "/api/goals/<int>").methods("PATCH"_method)
    ([&db](const crow::request& req, int goalId) {
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
            double saved = body.value("saved", 0.0);
            updateGoalSaved(db, goalId, saved);

            crow::response res(200, "{\"status\":\"updated\"}");
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

    // ---------- DELETE /api/goals/<id> ----------
    CROW_ROUTE(app, "/api/goals/<int>").methods("DELETE"_method)
    ([&db](const crow::request& req, int goalId) {
        std::string token = extractBearerToken(req);
        int userId = 0;
        if (token.empty() || !validateSession(db, token, userId)) {
            crow::response res(401, "{\"error\":\"unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        deleteGoal(db, goalId);
        crow::response res(200, "{\"status\":\"deleted\"}");
        res.add_header("Content-Type", "application/json");
        addCORS(res);
        return res;
    });
}
