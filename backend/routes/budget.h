#pragma once
#include "crow.h"
#include <SQLiteCpp/SQLiteCpp.h>

void setupBudgetRoutes(crow::SimpleApp& app, SQLite::Database& db);
