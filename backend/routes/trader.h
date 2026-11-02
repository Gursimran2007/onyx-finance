#pragma once
#include "crow.h"
#include <SQLiteCpp/SQLiteCpp.h>
void createAITraderTable(SQLite::Database& db);
void createAISettingsTable(SQLite::Database& db);
void setupTraderRoutes(crow::SimpleApp& app, SQLite::Database& db);
