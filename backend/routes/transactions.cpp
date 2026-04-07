#include "routes/transactions.h"
#include "database/db.h"
#include "models/transaction.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Helper: add CORS headers to every response
static void addCORS(crow::response& res) {
    res.add_header("Access-Control-Allow-Origin",  "*");
    res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    res.add_header("Access-Control-Allow-Headers", "Content-Type");
}

void setupTransactionRoutes(crow::SimpleApp& app, SQLite::Database& db) {

    // ---------- OPTIONS preflight (CORS) ----------
    CROW_ROUTE(app, "/api/transactions").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type");
        return res;
    });

    // ---------- GET /api/transactions ----------
    CROW_ROUTE(app, "/api/transactions").methods("GET"_method)
    ([&db]() {
        auto txns = getAllTransactions(db);
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
        addCORS(res);
        return res;
    });

    // ---------- POST /api/transactions ----------
    CROW_ROUTE(app, "/api/transactions").methods("POST"_method)
    ([&db](const crow::request& req) {
        try {
            auto body = json::parse(req.body);
            Transaction t;
            t.amount      = body.value("amount",      0.0);
            t.category    = body.value("category",    "Other");
            t.description = body.value("description", "");
            t.date        = body.value("date",        "");
            t.type        = body.value("type",        "expense");

            if (t.date.empty() || t.amount == 0.0) {
                crow::response res(400, "{\"error\":\"amount and date required\"}");
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }

            insertTransaction(db, t);
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

    // ---------- DELETE /api/transactions/<id> ----------
    CROW_ROUTE(app, "/api/transactions/<int>").methods("DELETE"_method)
    ([&db](int id) {
        deleteTransaction(db, id);
        crow::response res(200, "{\"status\":\"deleted\"}");
        res.add_header("Content-Type", "application/json");
        addCORS(res);
        return res;
    });

    // ---------- GET /api/summary ----------
    CROW_ROUTE(app, "/api/summary").methods("GET"_method)
    ([&db]() {
        auto s = getSummary(db);
        json j = {
            {"totalIncome",   s.totalIncome},
            {"totalExpenses", s.totalExpenses},
            {"balance",       s.balance}
        };
        crow::response res(j.dump());
        res.add_header("Content-Type", "application/json");
        addCORS(res);
        return res;
    });
}
