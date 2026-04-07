#pragma once
#include <vector>
#include <string>
#include <SQLiteCpp/SQLiteCpp.h>
#include "models/transaction.h"
#include "models/user.h"
#include <nlohmann/json.hpp>

// Initialize database and create tables
void initDB(SQLite::Database& db);

// Insert a new transaction
void insertTransaction(SQLite::Database& db, const Transaction& t);

// Get all transactions (most recent first)
std::vector<Transaction> getAllTransactions(SQLite::Database& db);

// Get last N transactions (for AI context)
std::vector<Transaction> getRecentTransactions(SQLite::Database& db, int limit = 30);

// Delete a transaction by ID
void deleteTransaction(SQLite::Database& db, int id);

// Get summary: total income, total expenses, balance
struct Summary {
    double totalIncome;
    double totalExpenses;
    double balance;
};
Summary getSummary(SQLite::Database& db);

// User management
void createUserTable(SQLite::Database& db);
bool createUser(SQLite::Database& db, const std::string& email, const std::string& name, const std::string& passwordHash);
bool getUserByEmail(SQLite::Database& db, const std::string& email, User& out);
std::string createSession(SQLite::Database& db, int userId);
bool validateSession(SQLite::Database& db, const std::string& token, int& userId);
void cleanExpiredSessions(SQLite::Database& db);

// Portfolio tables
void createPortfolioTables(SQLite::Database& db);
void upsertStock(SQLite::Database& db, int userId, const std::string& symbol, const std::string& name, double qty, double avgPrice);
std::vector<nlohmann::json> getStocks(SQLite::Database& db, int userId);
void deleteStock(SQLite::Database& db, int userId, int id);

// Goals
void createGoalsTable(SQLite::Database& db);
void insertGoal(SQLite::Database& db, int userId, const std::string& name, double targetAmount, const std::string& targetDate);
std::vector<nlohmann::json> getGoals(SQLite::Database& db, int userId);
void updateGoalSaved(SQLite::Database& db, int goalId, double saved);
void deleteGoal(SQLite::Database& db, int goalId);
