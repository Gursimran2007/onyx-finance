#include "routes/auth.h"
#include "database/db.h"
#include "models/user.h"
#include <nlohmann/json.hpp>
#include <string>
#include <functional>

using json = nlohmann::json;

static void addCORS(crow::response& res) {
    res.add_header("Access-Control-Allow-Origin",  "*");
    res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

static std::string hashPassword(const std::string& password) {
    return std::to_string(std::hash<std::string>{}(password + "onyx_salt_2026"));
}

static std::string extractBearerToken(const crow::request& req) {
    std::string auth = req.get_header_value("Authorization");
    if (auth.substr(0, 7) == "Bearer ") {
        return auth.substr(7);
    }
    return "";
}

void setupAuthRoutes(crow::SimpleApp& app, SQLite::Database& db) {

    // ---------- OPTIONS preflight ----------
    CROW_ROUTE(app, "/api/auth/signup").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/auth/login").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/auth/logout").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    CROW_ROUTE(app, "/api/auth/me").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return res;
    });

    // ---------- POST /api/auth/signup ----------
    CROW_ROUTE(app, "/api/auth/signup").methods("POST"_method)
    ([&db](const crow::request& req) {
        try {
            auto body = json::parse(req.body);
            std::string email    = body.value("email", "");
            std::string name     = body.value("name",  "");
            std::string password = body.value("password", "");

            if (email.empty() || password.empty()) {
                crow::response res(400, "{\"error\":\"email and password required\"}");
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }

            std::string hash = hashPassword(password);
            bool ok = createUser(db, email, name, hash);
            if (!ok) {
                crow::response res(409, "{\"error\":\"email already exists\"}");
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }

            User user;
            getUserByEmail(db, email, user);
            std::string token = createSession(db, user.id);

            json resp = {{"token", token}, {"name", user.name}, {"email", user.email}};
            crow::response res(201, resp.dump());
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

    // ---------- POST /api/auth/login ----------
    CROW_ROUTE(app, "/api/auth/login").methods("POST"_method)
    ([&db](const crow::request& req) {
        try {
            auto body = json::parse(req.body);
            std::string email    = body.value("email", "");
            std::string password = body.value("password", "");

            if (email.empty() || password.empty()) {
                crow::response res(400, "{\"error\":\"email and password required\"}");
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }

            User user;
            if (!getUserByEmail(db, email, user)) {
                crow::response res(401, "{\"error\":\"invalid credentials\"}");
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }

            std::string hash = hashPassword(password);
            if (hash != user.passwordHash) {
                crow::response res(401, "{\"error\":\"invalid credentials\"}");
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }

            std::string token = createSession(db, user.id);
            json resp = {{"token", token}, {"name", user.name}, {"email", user.email}};
            crow::response res(200, resp.dump());
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

    // ---------- POST /api/auth/logout ----------
    CROW_ROUTE(app, "/api/auth/logout").methods("POST"_method)
    ([&db](const crow::request& req) {
        std::string token = extractBearerToken(req);
        if (!token.empty()) {
            SQLite::Statement q(db, "DELETE FROM sessions WHERE token = ?");
            q.bind(1, token);
            q.exec();
        }
        crow::response res(200, "{\"status\":\"logged out\"}");
        res.add_header("Content-Type", "application/json");
        addCORS(res);
        return res;
    });

    // ---------- GET /api/auth/me ----------
    CROW_ROUTE(app, "/api/auth/me").methods("GET"_method)
    ([&db](const crow::request& req) {
        std::string token = extractBearerToken(req);
        if (token.empty()) {
            crow::response res(401, "{\"error\":\"unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        int userId = 0;
        if (!validateSession(db, token, userId)) {
            crow::response res(401, "{\"error\":\"unauthorized\"}");
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        SQLite::Statement q(db, "SELECT email, name FROM users WHERE id = ?");
        q.bind(1, userId);
        if (q.executeStep()) {
            json resp = {
                {"email", q.getColumn(0).getString()},
                {"name",  q.getColumn(1).getString()}
            };
            crow::response res(200, resp.dump());
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }

        crow::response res(404, "{\"error\":\"user not found\"}");
        res.add_header("Content-Type", "application/json");
        addCORS(res);
        return res;
    });
}
