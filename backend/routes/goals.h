#pragma once
#include "crow.h"
#include <SQLiteCpp/SQLiteCpp.h>

void setupGoalsRoutes(crow::SimpleApp& app, SQLite::Database& db);
