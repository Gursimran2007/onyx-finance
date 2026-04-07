cmake_minimum_required(VERSION 3.15)
project(FinanceApp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS: help CMake find brew-installed packages
if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
endif()

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# SQLiteCpp (built from source)
set(SQLITECPP_RUN_CPPLINT OFF CACHE BOOL "" FORCE)
add_subdirectory(libs/SQLiteCpp)

include_directories(
    libs/crow
    libs/json/include
    libs/SQLiteCpp/include
    backend
    /opt/homebrew/include
    /usr/local/include
)

add_executable(finance_app
    backend/main.cpp
    backend/database/db.cpp
    backend/routes/transactions.cpp
    backend/routes/ai.cpp
    backend/routes/import.cpp
    backend/routes/auth.cpp
    backend/routes/portfolio.cpp
    backend/routes/news.cpp
    backend/routes/goals.cpp
)

target_link_libraries(finance_app
    CURL::libcurl
    SQLiteCpp
    sqlite3
    Threads::Threads
)
