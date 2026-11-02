# Onyx Finance

**A personal-finance web app for India with a C++ backend: transactions, bank-statement import, budgets, goals,
a live stock and mutual-fund portfolio, market news, and AI-assisted investing and trading workspaces.**

The server is a single C++17 binary (Crow + SQLite) that serves a vanilla HTML/JS frontend and a JSON API.

## Features

- **Transactions and import.** Add transactions by hand, import bank-statement CSVs (HDFC, SBI, ICICI, Axis or a
  generic layout, detected from the headers) or paste UPI SMS messages to parse them.
- **Budgets, goals and reports.** Category budgets with alerts, savings goals with progress, analytics charts and a
  monthly report.
- **Portfolio.** Stocks priced live from NSE India (with session-cookie handling, a Yahoo Finance fallback and a
  five-minute price cache) plus mutual-fund NAVs, price history, P&L and a growth chart.
- **Market news.** Ten Indian market and personal-finance RSS sources, tagged by the stocks in your portfolio.
- **Investor workspace.** Rebalancing recommendations against equal-weight or custom allocation targets, which you
  approve or reject, plus performance tracking.
- **Trader workspace.** Strategy settings (momentum, stop-loss, take-profit), signals, a simulated trade log and P&L.
  No broker is connected; trades are paper trades.
- **AI chat and analysis** over your own data, through Groq's OpenAI-compatible API (`llama-3.3-70b-versatile`).
- **Tax reminders** for upcoming Indian filing and payment deadlines.
- **Accounts** with every user's data kept separate.

## Tech stack

C++17 · [Crow](https://github.com/CrowCpp/Crow) (HTTP) · SQLite via [SQLiteCpp](https://github.com/SRombauts/SQLiteCpp) ·
[nlohmann/json](https://github.com/nlohmann/json) · libcurl · CMake · vanilla HTML/CSS/JS · Docker (configs for Fly.io,
Render and Railway)

## Run it

Needs CMake, a C++17 compiler, libcurl and Asio (`brew install asio` or `apt install libasio-dev`).

```bash
git clone --recursive https://github.com/Gursimran2007/onyx-finance.git
cd onyx-finance
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/finance_app                       # http://localhost:8080
GROQ_API_KEY=your_key ./build/finance_app  # optional: turns on the AI features
```

The server creates `finance.db` in the folder it runs from. The CSV files in the repo root are synthetic sample
statements you can import.

## Layout

```
backend/main.cpp        server setup, static pages, route registration
backend/database/       schema and helpers
backend/routes/         auth, transactions, import, budget, goals, portfolio, news + tax, ai, investor, trader
frontend/               one HTML page per screen, shared app.js and style.css
libs/                   Crow, SQLiteCpp (submodule), nlohmann/json
```
