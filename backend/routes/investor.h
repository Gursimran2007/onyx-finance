#pragma once
#include "crow.h"
#include <SQLiteCpp/SQLiteCpp.h>
void createInvestorTables(SQLite::Database& db);
void setupInvestorRoutes(crow::SimpleApp& app, SQLite::Database& db);
