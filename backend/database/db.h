#pragma once
#include <vector>
#include <string>
#include <SQLiteCpp/SQLiteCpp.h>
#include "models/transaction.h"

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
