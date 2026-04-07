#include "database/db.h"
#include <stdexcept>

void initDB(SQLite::Database& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS transactions ("
        "id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "amount      REAL    NOT NULL,"
        "category    TEXT    NOT NULL DEFAULT 'Other',"
        "description TEXT    NOT NULL DEFAULT '',"
        "date        TEXT    NOT NULL,"
        "type        TEXT    NOT NULL DEFAULT 'expense'"
        ");"
    );
}

void insertTransaction(SQLite::Database& db, const Transaction& t) {
    SQLite::Statement q(db,
        "INSERT INTO transactions (amount, category, description, date, type) "
        "VALUES (?, ?, ?, ?, ?)"
    );
    q.bind(1, t.amount);
    q.bind(2, t.category);
    q.bind(3, t.description);
    q.bind(4, t.date);
    q.bind(5, t.type);
    q.exec();
}

std::vector<Transaction> getAllTransactions(SQLite::Database& db) {
    std::vector<Transaction> results;
    SQLite::Statement q(db,
        "SELECT id, amount, category, description, date, type "
        "FROM transactions ORDER BY date DESC, id DESC"
    );
    while (q.executeStep()) {
        Transaction t;
        t.id          = q.getColumn(0).getInt();
        t.amount      = q.getColumn(1).getDouble();
        t.category    = q.getColumn(2).getString();
        t.description = q.getColumn(3).getString();
        t.date        = q.getColumn(4).getString();
        t.type        = q.getColumn(5).getString();
        results.push_back(t);
    }
    return results;
}

std::vector<Transaction> getRecentTransactions(SQLite::Database& db, int limit) {
    std::vector<Transaction> results;
    SQLite::Statement q(db,
        "SELECT id, amount, category, description, date, type "
        "FROM transactions ORDER BY date DESC, id DESC LIMIT ?"
    );
    q.bind(1, limit);
    while (q.executeStep()) {
        Transaction t;
        t.id          = q.getColumn(0).getInt();
        t.amount      = q.getColumn(1).getDouble();
        t.category    = q.getColumn(2).getString();
        t.description = q.getColumn(3).getString();
        t.date        = q.getColumn(4).getString();
        t.type        = q.getColumn(5).getString();
        results.push_back(t);
    }
    return results;
}

void deleteTransaction(SQLite::Database& db, int id) {
    SQLite::Statement q(db, "DELETE FROM transactions WHERE id = ?");
    q.bind(1, id);
    q.exec();
}

Summary getSummary(SQLite::Database& db) {
    Summary s{0, 0, 0};

    SQLite::Statement inc(db,
        "SELECT COALESCE(SUM(amount),0) FROM transactions WHERE type='income'"
    );
    if (inc.executeStep()) s.totalIncome = inc.getColumn(0).getDouble();

    SQLite::Statement exp(db,
        "SELECT COALESCE(SUM(amount),0) FROM transactions WHERE type='expense'"
    );
    if (exp.executeStep()) s.totalExpenses = exp.getColumn(0).getDouble();

    s.balance = s.totalIncome - s.totalExpenses;
    return s;
}
