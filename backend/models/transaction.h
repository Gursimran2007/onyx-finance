#pragma once
#include <string>

struct Transaction {
    int id;
    int userId;             // owner
    double amount;          // positive = income, negative = expense
    std::string category;   // Food, Rent, Salary, Transport, Other
    std::string description;
    std::string date;       // YYYY-MM-DD
    std::string type;       // "income" or "expense"
};
