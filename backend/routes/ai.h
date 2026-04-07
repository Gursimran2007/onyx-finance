#pragma once
#include "crow.h"
#include <SQLiteCpp/SQLiteCpp.h>

void setupAIRoutes(crow::SimpleApp& app, SQLite::Database& db);
