#include "database/db.h"
#include <stdexcept>
#include <cstdlib>
#include <ctime>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

void initDB(SQLite::Database& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS transactions ("
        "id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "user_id     INTEGER NOT NULL DEFAULT 0,"
        "amount      REAL    NOT NULL,"
        "category    TEXT    NOT NULL DEFAULT 'Other',"
        "description TEXT    NOT NULL DEFAULT '',"
        "date        TEXT    NOT NULL,"
        "type        TEXT    NOT NULL DEFAULT 'expense'"
        ");"
    );
    // Add user_id column to existing DBs that don't have it
    try { db.exec("ALTER TABLE transactions ADD COLUMN user_id INTEGER NOT NULL DEFAULT 0;"); } catch (...) {}
}

void insertTransaction(SQLite::Database& db, const Transaction& t) {
    SQLite::Statement q(db,
        "INSERT INTO transactions (user_id, amount, category, description, date, type) "
        "VALUES (?, ?, ?, ?, ?, ?)"
    );
    q.bind(1, t.userId);
    q.bind(2, t.amount);
    q.bind(3, t.category);
    q.bind(4, t.description);
    q.bind(5, t.date);
    q.bind(6, t.type);
    q.exec();
}

std::vector<Transaction> getAllTransactions(SQLite::Database& db, int userId) {
    std::vector<Transaction> results;
    SQLite::Statement q(db,
        "SELECT id, user_id, amount, category, description, date, type "
        "FROM transactions WHERE user_id = ? ORDER BY date DESC, id DESC"
    );
    q.bind(1, userId);
    while (q.executeStep()) {
        Transaction t;
        t.id          = q.getColumn(0).getInt();
        t.userId      = q.getColumn(1).getInt();
        t.amount      = q.getColumn(2).getDouble();
        t.category    = q.getColumn(3).getString();
        t.description = q.getColumn(4).getString();
        t.date        = q.getColumn(5).getString();
        t.type        = q.getColumn(6).getString();
        results.push_back(t);
    }
    return results;
}

std::vector<Transaction> getRecentTransactions(SQLite::Database& db, int userId, int limit) {
    std::vector<Transaction> results;
    SQLite::Statement q(db,
        "SELECT id, user_id, amount, category, description, date, type "
        "FROM transactions WHERE user_id = ? ORDER BY date DESC, id DESC LIMIT ?"
    );
    q.bind(1, userId);
    q.bind(2, limit);
    while (q.executeStep()) {
        Transaction t;
        t.id          = q.getColumn(0).getInt();
        t.userId      = q.getColumn(1).getInt();
        t.amount      = q.getColumn(2).getDouble();
        t.category    = q.getColumn(3).getString();
        t.description = q.getColumn(4).getString();
        t.date        = q.getColumn(5).getString();
        t.type        = q.getColumn(6).getString();
        results.push_back(t);
    }
    return results;
}

void deleteTransaction(SQLite::Database& db, int id, int userId) {
    SQLite::Statement q(db, "DELETE FROM transactions WHERE id = ? AND user_id = ?");
    q.bind(1, id);
    q.bind(2, userId);
    q.exec();
}

Summary getSummary(SQLite::Database& db, int userId) {
    Summary s{0, 0, 0};
    SQLite::Statement inc(db,
        "SELECT COALESCE(SUM(amount),0) FROM transactions WHERE user_id = ? AND type='income'"
    );
    inc.bind(1, userId);
    if (inc.executeStep()) s.totalIncome = inc.getColumn(0).getDouble();

    SQLite::Statement exp(db,
        "SELECT COALESCE(SUM(amount),0) FROM transactions WHERE user_id = ? AND type='expense'"
    );
    exp.bind(1, userId);
    if (exp.executeStep()) s.totalExpenses = exp.getColumn(0).getDouble();

    s.balance = s.totalIncome - s.totalExpenses;
    return s;
}

// ---- User management ----

void createUserTable(SQLite::Database& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS users ("
        "id            INTEGER PRIMARY KEY AUTOINCREMENT,"
        "email         TEXT    NOT NULL UNIQUE,"
        "name          TEXT    NOT NULL DEFAULT '',"
        "password_hash TEXT    NOT NULL,"
        "created_at    TEXT    NOT NULL DEFAULT (datetime('now'))"
        ");"
    );
    db.exec(
        "CREATE TABLE IF NOT EXISTS sessions ("
        "token      TEXT PRIMARY KEY,"
        "user_id    INTEGER NOT NULL,"
        "expires_at TEXT    NOT NULL"
        ");"
    );
}

bool createUser(SQLite::Database& db, const std::string& email, const std::string& name, const std::string& passwordHash) {
    try {
        SQLite::Statement q(db,
            "INSERT INTO users (email, name, password_hash) VALUES (?, ?, ?)"
        );
        q.bind(1, email);
        q.bind(2, name);
        q.bind(3, passwordHash);
        q.exec();
        return true;
    } catch (SQLite::Exception&) {
        return false;
    }
}

bool getUserByEmail(SQLite::Database& db, const std::string& email, User& out) {
    SQLite::Statement q(db,
        "SELECT id, email, name, password_hash, created_at FROM users WHERE email = ?"
    );
    q.bind(1, email);
    if (q.executeStep()) {
        out.id           = q.getColumn(0).getInt();
        out.email        = q.getColumn(1).getString();
        out.name         = q.getColumn(2).getString();
        out.passwordHash = q.getColumn(3).getString();
        out.createdAt    = q.getColumn(4).getString();
        return true;
    }
    return false;
}

static std::string generateToken() {
    const char hex[] = "0123456789abcdef";
    std::string token(64, ' ');
    srand(static_cast<unsigned int>(time(nullptr)));
    for (auto& c : token) c = hex[rand() % 16];
    return token;
}

std::string createSession(SQLite::Database& db, int userId) {
    std::string token = generateToken();
    SQLite::Statement q(db,
        "INSERT INTO sessions (token, user_id, expires_at) "
        "VALUES (?, ?, datetime('now', '+7 days'))"
    );
    q.bind(1, token);
    q.bind(2, userId);
    q.exec();
    return token;
}

bool validateSession(SQLite::Database& db, const std::string& token, int& userId) {
    SQLite::Statement q(db,
        "SELECT user_id FROM sessions WHERE token = ? AND expires_at > datetime('now')"
    );
    q.bind(1, token);
    if (q.executeStep()) {
        userId = q.getColumn(0).getInt();
        return true;
    }
    return false;
}

void cleanExpiredSessions(SQLite::Database& db) {
    db.exec("DELETE FROM sessions WHERE expires_at <= datetime('now')");
}

// ---- Portfolio tables ----

void createPortfolioTables(SQLite::Database& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS stocks ("
        "id         INTEGER PRIMARY KEY AUTOINCREMENT,"
        "user_id    INTEGER NOT NULL,"
        "symbol     TEXT    NOT NULL,"
        "name       TEXT    NOT NULL DEFAULT '',"
        "qty        REAL    NOT NULL DEFAULT 0,"
        "avg_price  REAL    NOT NULL DEFAULT 0"
        ");"
    );
}

void upsertStock(SQLite::Database& db, int userId, const std::string& symbol, const std::string& name, double qty, double avgPrice) {
    SQLite::Statement q(db,
        "INSERT OR REPLACE INTO stocks (user_id, symbol, name, qty, avg_price) VALUES (?, ?, ?, ?, ?)"
    );
    q.bind(1, userId);
    q.bind(2, symbol);
    q.bind(3, name);
    q.bind(4, qty);
    q.bind(5, avgPrice);
    q.exec();
}

std::vector<json> getStocks(SQLite::Database& db, int userId) {
    std::vector<json> results;
    SQLite::Statement q(db,
        "SELECT id, symbol, name, qty, avg_price FROM stocks WHERE user_id = ?"
    );
    q.bind(1, userId);
    while (q.executeStep()) {
        json obj;
        obj["id"]       = q.getColumn(0).getInt();
        obj["symbol"]   = q.getColumn(1).getString();
        obj["name"]     = q.getColumn(2).getString();
        obj["qty"]      = q.getColumn(3).getDouble();
        obj["avgPrice"] = q.getColumn(4).getDouble();
        results.push_back(obj);
    }
    return results;
}

void deleteStock(SQLite::Database& db, int userId, int id) {
    SQLite::Statement q(db, "DELETE FROM stocks WHERE id = ? AND user_id = ?");
    q.bind(1, id);
    q.bind(2, userId);
    q.exec();
}

// ---- Goals ----

void createGoalsTable(SQLite::Database& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS goals ("
        "id            INTEGER PRIMARY KEY AUTOINCREMENT,"
        "user_id       INTEGER NOT NULL,"
        "name          TEXT    NOT NULL,"
        "target_amount REAL    NOT NULL DEFAULT 0,"
        "saved_amount  REAL    NOT NULL DEFAULT 0,"
        "target_date   TEXT    NOT NULL DEFAULT ''"
        ");"
    );
}

void insertGoal(SQLite::Database& db, int userId, const std::string& name, double targetAmount, const std::string& targetDate) {
    SQLite::Statement q(db,
        "INSERT INTO goals (user_id, name, target_amount, target_date) VALUES (?, ?, ?, ?)"
    );
    q.bind(1, userId);
    q.bind(2, name);
    q.bind(3, targetAmount);
    q.bind(4, targetDate);
    q.exec();
}

std::vector<json> getGoals(SQLite::Database& db, int userId) {
    std::vector<json> results;
    SQLite::Statement q(db,
        "SELECT id, name, target_amount, saved_amount, target_date FROM goals WHERE user_id = ?"
    );
    q.bind(1, userId);
    while (q.executeStep()) {
        json obj;
        obj["id"]           = q.getColumn(0).getInt();
        obj["name"]         = q.getColumn(1).getString();
        obj["targetAmount"] = q.getColumn(2).getDouble();
        obj["savedAmount"]  = q.getColumn(3).getDouble();
        obj["targetDate"]   = q.getColumn(4).getString();
        results.push_back(obj);
    }
    return results;
}

void updateGoalSaved(SQLite::Database& db, int goalId, double saved) {
    SQLite::Statement q(db, "UPDATE goals SET saved_amount = ? WHERE id = ?");
    q.bind(1, saved);
    q.bind(2, goalId);
    q.exec();
}

void deleteGoal(SQLite::Database& db, int goalId) {
    SQLite::Statement q(db, "DELETE FROM goals WHERE id = ?");
    q.bind(1, goalId);
    q.exec();
}
