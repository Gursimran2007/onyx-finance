#pragma once
#include "crow.h"
#include <SQLiteCpp/SQLiteCpp.h>

void setupImportRoutes(crow::SimpleApp& app, SQLite::Database& db);
