#pragma once
#include "crow.h"
#include <SQLiteCpp/SQLiteCpp.h>

void setupAuthRoutes(crow::SimpleApp& app, SQLite::Database& db);
