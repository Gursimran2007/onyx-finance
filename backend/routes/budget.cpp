#include "routes/budget.h"
#include "database/db.h"
#include <nlohmann/json.hpp>
#include <ctime>
#include <string>
#include <vector>

using json = nlohmann::json;

static void addCORS(crow::response& res) {
    res.add_header("Access-Control-Allow-Origin", "*");
    res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

static int getUserId(SQLite::Database& db, const crow::request& req) {
    auto auth = req.get_header_value("Authorization");
    if (auth.size() > 7 && auth.substr(0, 7) == "Bearer ") {
        std::string token = auth.substr(7);
        int userId = -1;
        if (validateSession(db, token, userId)) {
            return userId;
        }
    }
    return -1;
}

void setupBudgetRoutes(crow::SimpleApp& app, SQLite::Database& db) {

    CROW_ROUTE(app, "/api/budgets").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        addCORS(res);
        return res;
    });

    CROW_ROUTE(app, "/api/budgets").methods("GET"_method)
    ([&db](const crow::request& req) {
        int userId = getUserId(db, req);
        if (userId == -1) {
            crow::response res(401);
            addCORS(res);
            res.write("{\"error\":\"Unauthorized\"}");
            return res;
        }

        time_t now = time(nullptr);
        struct tm* tm_info = localtime(&now);
        char monthBuf[8];
        strftime(monthBuf, sizeof(monthBuf), "%Y-%m", tm_info);
        std::string currentMonth(monthBuf);

        auto budgets = getBudgets(db, userId, currentMonth);

        json arr = json::array();
        for (auto& b : budgets) {
            double monthlyLimit = b["monthlyLimit"].get<double>();
            double spent        = b["spent"].get<double>();
            double percentage   = (monthlyLimit > 0) ? (spent / monthlyLimit * 100.0) : 0.0;
            std::string status;
            if (percentage > 100.0) {
                status = "over";
            } else if (percentage >= 80.0) {
                status = "warning";
            } else {
                status = "ok";
            }
            b["percentage"] = percentage;
            b["status"]     = status;
            arr.push_back(b);
        }

        crow::response res(200);
        addCORS(res);
        res.set_header("Content-Type", "application/json");
        res.write(arr.dump());
        return res;
    });

    CROW_ROUTE(app, "/api/budgets").methods("POST"_method)
    ([&db](const crow::request& req) {
        int userId = getUserId(db, req);
        if (userId == -1) {
            crow::response res(401);
            addCORS(res);
            res.write("{\"error\":\"Unauthorized\"}");
            return res;
        }

        try {
            auto body        = json::parse(req.body);
            std::string cat  = body.at("category").get<std::string>();
            double limit     = body.at("monthlyLimit").get<double>();
            upsertBudget(db, userId, cat, limit);
        } catch (...) {
            crow::response res(400);
            addCORS(res);
            res.write("{\"error\":\"Invalid request body\"}");
            return res;
        }

        crow::response res(200);
        addCORS(res);
        res.set_header("Content-Type", "application/json");
        res.write("{\"status\":\"saved\"}");
        return res;
    });

    CROW_ROUTE(app, "/api/budgets/<int>").methods("OPTIONS"_method)
    ([](const crow::request&, int) {
        crow::response res(204);
        addCORS(res);
        return res;
    });

    CROW_ROUTE(app, "/api/budgets/<int>").methods("DELETE"_method)
    ([&db](const crow::request& req, int id) {
        int userId = getUserId(db, req);
        if (userId == -1) {
            crow::response res(401);
            addCORS(res);
            res.write("{\"error\":\"Unauthorized\"}");
            return res;
        }

        deleteBudget(db, id, userId);

        crow::response res(200);
        addCORS(res);
        res.set_header("Content-Type", "application/json");
        res.write("{\"status\":\"deleted\"}");
        return res;
    });
}
