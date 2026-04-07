#include "crow.h"
#include <SQLiteCpp/SQLiteCpp.h>
#include "database/db.h"
#include "routes/transactions.h"
#include "routes/ai.h"
#include "routes/import.h"
#include <iostream>
#include <fstream>
#include <sstream>

// Serve a static file from the frontend folder
static crow::response serveFile(const std::string& path, const std::string& contentType) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return crow::response(404, "File not found: " + path);
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    crow::response res(ss.str());
    res.add_header("Content-Type", contentType);
    return res;
}

int main() {
    // Init database
    SQLite::Database db("finance.db", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    initDB(db);
    std::cout << "[DB] finance.db ready\n";

    crow::SimpleApp app;

    // ---- Static frontend routes ----
    CROW_ROUTE(app, "/")([]() {
        return serveFile("frontend/index.html", "text/html");
    });
    CROW_ROUTE(app, "/add")([]() {
        return serveFile("frontend/add.html", "text/html");
    });
    CROW_ROUTE(app, "/chat")([]() {
        return serveFile("frontend/chat.html", "text/html");
    });
    CROW_ROUTE(app, "/style.css")([]() {
        return serveFile("frontend/style.css", "text/css");
    });
    CROW_ROUTE(app, "/app.js")([]() {
        return serveFile("frontend/app.js", "application/javascript");
    });
    CROW_ROUTE(app, "/import")([]() {
        return serveFile("frontend/import.html", "text/html");
    });

    // ---- API routes ----
    setupTransactionRoutes(app, db);
    setupAIRoutes(app, db);
    setupImportRoutes(app, db);

    std::cout << "[Server] Running on http://localhost:8080\n";
    app.port(8080).multithreaded().run();
    return 0;
}
