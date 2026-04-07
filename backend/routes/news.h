#pragma once
#include "crow.h"
#include <SQLiteCpp/SQLiteCpp.h>

void setupNewsRoutes(crow::SimpleApp& app, SQLite::Database& db);
