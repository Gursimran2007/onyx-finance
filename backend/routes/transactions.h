#pragma once
#include "crow.h"
#include <SQLiteCpp/SQLiteCpp.h>

void setupTransactionRoutes(crow::SimpleApp& app, SQLite::Database& db);
