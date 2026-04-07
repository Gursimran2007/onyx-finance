#pragma once
#include "crow.h"
#include <SQLiteCpp/SQLiteCpp.h>

void setupPortfolioRoutes(crow::SimpleApp& app, SQLite::Database& db);
