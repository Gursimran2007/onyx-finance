#include "routes/transactions.h"
#include "database/db.h"
#include "models/transaction.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

using json = nlohmann::json;

// Auto-update goals: recalculate saved_amount from net savings (income - expense)
static void recomputeGoals(SQLite::Database& db, int userId) {
    try {
        Summary s = getSummary(db, userId);
        double netSavings = s.balance; // income - expenses = available savings
        if (netSavings < 0) netSavings = 0;

        auto goals = getGoals(db, userId);
        if (goals.empty()) return;

        // Distribute net savings proportionally across goals by target amount
        double totalTarget = 0;
        for (auto& g : goals) totalTarget += g.value("targetAmount", 0.0);
        if (totalTarget <= 0) return;

        for (auto& g : goals) {
            double target = g.value("targetAmount", 0.0);
            double share  = (target / totalTarget) * netSavings;
            double capped = std::min(share, target); // don't exceed target
            updateGoalSaved(db, g["id"].get<int>(), capped);
        }
    } catch (...) {} // Silent — goals auto-update should never break transactions
}

static void addCORS(crow::response& res) {
    res.add_header("Access-Control-Allow-Origin",  "*");
    res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

// Extract userId from Bearer token, return -1 if invalid
static int getUserId(SQLite::Database& db, const crow::request& req) {
    auto auth = req.get_header_value("Authorization");
    if (auth.size() > 7 && auth.substr(0, 7) == "Bearer ") {
        int uid = -1;
        if (validateSession(db, auth.substr(7), uid)) return uid;
    }
    return -1;
}

void setupTransactionRoutes(crow::SimpleApp& app, SQLite::Database& db) {

    CROW_ROUTE(app, "/api/transactions").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/transactions").methods("GET"_method)
    ([&db](const crow::request& req) {
        int userId = getUserId(db, req);
        if (userId < 0) {
            crow::response res(401, "{\"error\":\"Unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res); return res;
        }
        auto txns = getAllTransactions(db, userId);
        json arr = json::array();
        for (auto& t : txns) {
            arr.push_back({
                {"id",          t.id},
                {"amount",      t.amount},
                {"category",    t.category},
                {"description", t.description},
                {"date",        t.date},
                {"type",        t.type}
            });
        }
        crow::response res(arr.dump());
        res.add_header("Content-Type", "application/json");
        addCORS(res); return res;
    });

    CROW_ROUTE(app, "/api/transactions").methods("POST"_method)
    ([&db](const crow::request& req) {
        int userId = getUserId(db, req);
        if (userId < 0) {
            crow::response res(401, "{\"error\":\"Unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res); return res;
        }
        try {
            auto body = json::parse(req.body);
            Transaction t;
            t.userId      = userId;
            t.amount      = body.value("amount",      0.0);
            t.category    = body.value("category",    "Other");
            t.description = body.value("description", "");
            t.date        = body.value("date",        "");
            t.type        = body.value("type",        "expense");

            if (t.date.empty() || t.amount == 0.0) {
                crow::response res(400, "{\"error\":\"amount and date required\"}");
                res.add_header("Content-Type", "application/json");
                addCORS(res); return res;
            }
            insertTransaction(db, t);
            recomputeGoals(db, userId); // Auto-update goals
            crow::response res(201, "{\"status\":\"created\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res); return res;
        } catch (std::exception& e) {
            crow::response res(400, std::string("{\"error\":\"") + e.what() + "\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res); return res;
        }
    });

    CROW_ROUTE(app, "/api/transactions/<int>").methods("OPTIONS"_method)
    ([](const crow::request&, int) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/transactions/<int>").methods("DELETE"_method)
    ([&db](const crow::request& req, int id) {
        int userId = getUserId(db, req);
        if (userId < 0) {
            crow::response res(401, "{\"error\":\"Unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res); return res;
        }
        deleteTransaction(db, id, userId);
        recomputeGoals(db, userId); // Auto-update goals
        crow::response res(200, "{\"status\":\"deleted\"}");
        res.add_header("Content-Type", "application/json");
        addCORS(res); return res;
    });

    CROW_ROUTE(app, "/api/summary").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/summary").methods("GET"_method)
    ([&db](const crow::request& req) {
        int userId = getUserId(db, req);
        if (userId < 0) {
            crow::response res(401, "{\"error\":\"Unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res); return res;
        }
        auto s = getSummary(db, userId);
        json j = {
            {"totalIncome",   s.totalIncome},
            {"totalExpenses", s.totalExpenses},
            {"balance",       s.balance}
        };
        crow::response res(j.dump());
        res.add_header("Content-Type", "application/json");
        addCORS(res); return res;
    });
}
