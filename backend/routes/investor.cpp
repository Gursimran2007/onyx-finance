#include "routes/investor.h"
#include "database/db.h"
#include <nlohmann/json.hpp>
#include <string>
#include <cmath>

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

void createInvestorTables(SQLite::Database& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS ai_recommendations ("
        "id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "user_id     INTEGER NOT NULL,"
        "symbol      TEXT,"
        "action      TEXT,"
        "reason      TEXT,"
        "confidence  REAL,"
        "target_qty  REAL,"
        "target_price REAL,"
        "status      TEXT DEFAULT 'pending',"
        "created_at  TEXT DEFAULT (datetime('now'))"
        ");"
    );
    db.exec(
        "CREATE TABLE IF NOT EXISTS ai_portfolio_targets ("
        "id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "user_id     INTEGER NOT NULL,"
        "symbol      TEXT,"
        "target_pct  REAL,"
        "current_pct REAL DEFAULT 0"
        ");"
    );
}

void setupInvestorRoutes(crow::SimpleApp& app, SQLite::Database& db) {

    // ---------- OPTIONS preflight ----------
    CROW_ROUTE(app, "/api/investor/recommendations").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/investor/analyze").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/investor/approve/<int>").methods("OPTIONS"_method)
    ([](const crow::request&, int) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/investor/reject/<int>").methods("OPTIONS"_method)
    ([](const crow::request&, int) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/investor/targets").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/investor/performance").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    // ---------- GET /api/investor/recommendations ----------
    CROW_ROUTE(app, "/api/investor/recommendations").methods("GET"_method)
    ([&db](const crow::request& req) {
        std::string token = extractBearerToken(req);
        int userId = 0;
        if (token.empty() || !validateSession(db, token, userId)) {
            crow::response res(401, "{\"error\":\"unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        json arr = json::array();
        SQLite::Statement q(db,
            "SELECT id, symbol, action, reason, confidence, target_qty, target_price, status, created_at "
            "FROM ai_recommendations WHERE user_id = ? ORDER BY created_at DESC"
        );
        q.bind(1, userId);
        while (q.executeStep()) {
            json obj;
            obj["id"]          = q.getColumn(0).getInt();
            obj["symbol"]      = q.getColumn(1).getString();
            obj["action"]      = q.getColumn(2).getString();
            obj["reason"]      = q.getColumn(3).getString();
            obj["confidence"]  = q.getColumn(4).getDouble();
            obj["target_qty"]  = q.getColumn(5).getDouble();
            obj["target_price"]= q.getColumn(6).getDouble();
            obj["status"]      = q.getColumn(7).getString();
            obj["created_at"]  = q.getColumn(8).getString();
            arr.push_back(obj);
        }

        crow::response res(arr.dump());
        res.add_header("Content-Type", "application/json");
        addCORS(res);
        return res;
    });

    // ---------- POST /api/investor/analyze ----------
    CROW_ROUTE(app, "/api/investor/analyze").methods("POST"_method)
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
            // Get all stocks for user
            auto stocks = getStocks(db, userId);
            if (stocks.empty()) {
                crow::response res(200, "{\"recommendations\":[],\"message\":\"No stocks in portfolio\"}");
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }

            // Calculate total portfolio value (qty * avg_price as proxy)
            double totalValue = 0.0;
            for (auto& s : stocks) {
                double qty      = s.value("qty", 0.0);
                double avgPrice = s.value("avgPrice", 0.0);
                totalValue += qty * avgPrice;
            }

            if (totalValue <= 0) {
                crow::response res(200, "{\"recommendations\":[],\"message\":\"Portfolio value is zero\"}");
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }

            int numStocks = (int)stocks.size();
            double idealPct = 100.0 / numStocks; // equal-weight default

            // Check if user has custom targets
            std::map<std::string, double> targetMap;
            {
                SQLite::Statement tq(db,
                    "SELECT symbol, target_pct FROM ai_portfolio_targets WHERE user_id = ?"
                );
                tq.bind(1, userId);
                while (tq.executeStep()) {
                    targetMap[tq.getColumn(0).getString()] = tq.getColumn(1).getDouble();
                }
            }

            // Clear old pending recommendations
            {
                SQLite::Statement del(db,
                    "DELETE FROM ai_recommendations WHERE user_id = ? AND status = 'pending'"
                );
                del.bind(1, userId);
                del.exec();
            }

            json recommendations = json::array();

            for (auto& s : stocks) {
                std::string symbol = s.value("symbol", "");
                double qty         = s.value("qty", 0.0);
                double avgPrice    = s.value("avgPrice", 0.0);
                double stockValue  = qty * avgPrice;
                double currentPct  = (stockValue / totalValue) * 100.0;

                // Use custom target if set, otherwise equal-weight
                double targetPct = idealPct;
                if (targetMap.count(symbol)) {
                    targetPct = targetMap[symbol];
                }

                double deviation = currentPct - targetPct;

                // Update current_pct in targets table
                {
                    SQLite::Statement upd(db,
                        "UPDATE ai_portfolio_targets SET current_pct = ? WHERE user_id = ? AND symbol = ?"
                    );
                    upd.bind(1, currentPct);
                    upd.bind(2, userId);
                    upd.bind(3, symbol);
                    upd.exec();
                }

                std::string action;
                std::string reason;
                double confidence = 0.0;
                double targetQty = 0.0;

                if (deviation > 5.0) {
                    action = "SELL";
                    reason = symbol + " is overweight at " + std::to_string((int)std::round(currentPct))
                           + "% vs target " + std::to_string((int)std::round(targetPct))
                           + "%. Consider trimming position.";
                    confidence = std::min(0.95, 0.5 + (deviation / 100.0) * 2.0);
                    // How many shares to sell to reach target
                    double targetValue = totalValue * (targetPct / 100.0);
                    double excessValue = stockValue - targetValue;
                    if (avgPrice > 0) targetQty = std::round(excessValue / avgPrice);
                } else if (deviation < -5.0) {
                    action = "BUY";
                    reason = symbol + " is underweight at " + std::to_string((int)std::round(currentPct))
                           + "% vs target " + std::to_string((int)std::round(targetPct))
                           + "%. Consider adding to position.";
                    confidence = std::min(0.95, 0.5 + (std::abs(deviation) / 100.0) * 2.0);
                    double targetValue = totalValue * (targetPct / 100.0);
                    double deficitValue = targetValue - stockValue;
                    if (avgPrice > 0) targetQty = std::round(deficitValue / avgPrice);
                } else {
                    continue; // within tolerance
                }

                // Insert recommendation
                {
                    SQLite::Statement ins(db,
                        "INSERT INTO ai_recommendations (user_id, symbol, action, reason, confidence, target_qty, target_price, status) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, 'pending')"
                    );
                    ins.bind(1, userId);
                    ins.bind(2, symbol);
                    ins.bind(3, action);
                    ins.bind(4, reason);
                    ins.bind(5, confidence);
                    ins.bind(6, targetQty);
                    ins.bind(7, avgPrice);
                    ins.exec();
                }

                int recId = (int)db.getLastInsertRowid();
                json rec;
                rec["id"]          = recId;
                rec["symbol"]      = symbol;
                rec["action"]      = action;
                rec["reason"]      = reason;
                rec["confidence"]  = confidence;
                rec["target_qty"]  = targetQty;
                rec["target_price"]= avgPrice;
                rec["status"]      = "pending";
                recommendations.push_back(rec);
            }

            json result;
            result["recommendations"] = recommendations;
            result["totalValue"]      = totalValue;
            result["numStocks"]       = numStocks;

            crow::response res(result.dump());
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

    // ---------- POST /api/investor/approve/<id> ----------
    CROW_ROUTE(app, "/api/investor/approve/<int>").methods("POST"_method)
    ([&db](const crow::request& req, int recId) {
        std::string token = extractBearerToken(req);
        int userId = 0;
        if (token.empty() || !validateSession(db, token, userId)) {
            crow::response res(401, "{\"error\":\"unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        SQLite::Statement q(db,
            "UPDATE ai_recommendations SET status = 'approved' WHERE id = ? AND user_id = ?"
        );
        q.bind(1, recId);
        q.bind(2, userId);
        q.exec();

        crow::response res(200, "{\"status\":\"approved\"}");
        res.add_header("Content-Type", "application/json");
        addCORS(res);
        return res;
    });

    // ---------- POST /api/investor/reject/<id> ----------
    CROW_ROUTE(app, "/api/investor/reject/<int>").methods("POST"_method)
    ([&db](const crow::request& req, int recId) {
        std::string token = extractBearerToken(req);
        int userId = 0;
        if (token.empty() || !validateSession(db, token, userId)) {
            crow::response res(401, "{\"error\":\"unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        SQLite::Statement q(db,
            "UPDATE ai_recommendations SET status = 'rejected' WHERE id = ? AND user_id = ?"
        );
        q.bind(1, recId);
        q.bind(2, userId);
        q.exec();

        crow::response res(200, "{\"status\":\"rejected\"}");
        res.add_header("Content-Type", "application/json");
        addCORS(res);
        return res;
    });

    // ---------- GET /api/investor/targets ----------
    CROW_ROUTE(app, "/api/investor/targets").methods("GET"_method)
    ([&db](const crow::request& req) {
        std::string token = extractBearerToken(req);
        int userId = 0;
        if (token.empty() || !validateSession(db, token, userId)) {
            crow::response res(401, "{\"error\":\"unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        json arr = json::array();
        SQLite::Statement q(db,
            "SELECT id, symbol, target_pct, current_pct FROM ai_portfolio_targets WHERE user_id = ?"
        );
        q.bind(1, userId);
        while (q.executeStep()) {
            json obj;
            obj["id"]         = q.getColumn(0).getInt();
            obj["symbol"]     = q.getColumn(1).getString();
            obj["target_pct"] = q.getColumn(2).getDouble();
            obj["current_pct"]= q.getColumn(3).getDouble();
            arr.push_back(obj);
        }

        crow::response res(arr.dump());
        res.add_header("Content-Type", "application/json");
        addCORS(res);
        return res;
    });

    // ---------- POST /api/investor/targets ----------
    CROW_ROUTE(app, "/api/investor/targets").methods("POST"_method)
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
            if (!body.is_array()) {
                crow::response res(400, "{\"error\":\"expected array of targets\"}");
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }

            // Clear existing targets for user
            {
                SQLite::Statement del(db,
                    "DELETE FROM ai_portfolio_targets WHERE user_id = ?"
                );
                del.bind(1, userId);
                del.exec();
            }

            for (auto& t : body) {
                std::string symbol = t.value("symbol", "");
                double targetPct   = t.value("target_pct", 0.0);
                if (symbol.empty()) continue;

                SQLite::Statement ins(db,
                    "INSERT INTO ai_portfolio_targets (user_id, symbol, target_pct) VALUES (?, ?, ?)"
                );
                ins.bind(1, userId);
                ins.bind(2, symbol);
                ins.bind(3, targetPct);
                ins.exec();
            }

            crow::response res(200, "{\"status\":\"targets saved\"}");
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

    // ---------- GET /api/investor/performance ----------
    CROW_ROUTE(app, "/api/investor/performance").methods("GET"_method)
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
            auto stocks = getStocks(db, userId);
            double totalValue    = 0.0;
            double totalInvested = 0.0;
            json allocation      = json::array();

            // First pass: compute totals
            for (auto& s : stocks) {
                double qty      = s.value("qty", 0.0);
                double avgPrice = s.value("avgPrice", 0.0);
                double invested = qty * avgPrice;
                totalValue    += invested;
                totalInvested += invested;
            }

            // Load targets map
            std::map<std::string, double> targetMap;
            {
                SQLite::Statement tq(db,
                    "SELECT symbol, target_pct FROM ai_portfolio_targets WHERE user_id = ?"
                );
                tq.bind(1, userId);
                while (tq.executeStep()) {
                    targetMap[tq.getColumn(0).getString()] = tq.getColumn(1).getDouble();
                }
            }

            int numStocks = (int)stocks.size();
            double idealPct = numStocks > 0 ? 100.0 / numStocks : 0.0;

            // Second pass: build allocation array
            for (auto& s : stocks) {
                std::string symbol = s.value("symbol", "");
                double qty         = s.value("qty", 0.0);
                double avgPrice    = s.value("avgPrice", 0.0);
                double stockValue  = qty * avgPrice;
                double currentPct  = totalValue > 0 ? (stockValue / totalValue) * 100.0 : 0.0;
                double targetPct   = targetMap.count(symbol) ? targetMap[symbol] : idealPct;

                json alloc;
                alloc["symbol"]     = symbol;
                alloc["currentPct"] = std::round(currentPct * 100.0) / 100.0;
                alloc["targetPct"]  = std::round(targetPct * 100.0) / 100.0;
                alloc["value"]      = std::round(stockValue * 100.0) / 100.0;
                allocation.push_back(alloc);
            }

            double returns    = totalValue - totalInvested;
            double returnsPct = totalInvested > 0 ? (returns / totalInvested) * 100.0 : 0.0;

            json result;
            result["totalValue"]    = std::round(totalValue * 100.0) / 100.0;
            result["totalInvested"] = std::round(totalInvested * 100.0) / 100.0;
            result["returns"]       = std::round(returns * 100.0) / 100.0;
            result["returnsPct"]    = std::round(returnsPct * 100.0) / 100.0;
            result["allocation"]    = allocation;

            crow::response res(result.dump());
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
}
