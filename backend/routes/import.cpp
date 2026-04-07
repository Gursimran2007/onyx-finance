#include "routes/import.h"
#include "database/db.h"
#include "models/transaction.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

static void addCORS(crow::response& res) {
    res.add_header("Access-Control-Allow-Origin",  "*");
    res.add_header("Access-Control-Allow-Methods", "POST, OPTIONS");
    res.add_header("Access-Control-Allow-Headers", "Content-Type");
}

// Trim whitespace and quotes from a CSV field
static std::string trimField(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n\"");
    size_t end   = s.find_last_not_of(" \t\r\n\"");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

// Split a CSV line into fields (handles quoted commas)
static std::vector<std::string> splitCSV(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;
    for (char c : line) {
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ',' && !inQuotes) {
            fields.push_back(trimField(field));
            field.clear();
        } else {
            field += c;
        }
    }
    fields.push_back(trimField(field));
    return fields;
}

// Guess if a string looks like a date (basic check)
static bool looksLikeDate(const std::string& s) {
    for (char c : s) if (c == '/' || c == '-') return true;
    return false;
}

// Convert common Indian bank date formats to YYYY-MM-DD
static std::string normalizeDate(const std::string& raw) {
    // Try DD/MM/YYYY or DD-MM-YYYY
    if (raw.size() == 10) {
        char sep = (raw[2] == '/') ? '/' : '-';
        if (raw[2] == sep && raw[5] == sep) {
            return raw.substr(6,4) + "-" + raw.substr(3,2) + "-" + raw.substr(0,2);
        }
        // Already YYYY-MM-DD
        if (raw[4] == sep) return raw;
    }
    // DD/MM/YY → assume 20YY
    if (raw.size() == 8) {
        char sep = (raw[2] == '/') ? '/' : '-';
        if (raw[2] == sep && raw[5] == sep) {
            return "20" + raw.substr(6,2) + "-" + raw.substr(3,2) + "-" + raw.substr(0,2);
        }
    }
    return raw; // Return as-is if unknown
}

// Guess category from description
static std::string guessCategory(const std::string& desc) {
    std::string d = desc;
    std::transform(d.begin(), d.end(), d.begin(), ::tolower);
    if (d.find("salary") != std::string::npos || d.find("payroll") != std::string::npos) return "Salary";
    if (d.find("swiggy") != std::string::npos || d.find("zomato") != std::string::npos ||
        d.find("food")   != std::string::npos || d.find("cafe")   != std::string::npos ||
        d.find("restaurant") != std::string::npos) return "Food";
    if (d.find("uber")   != std::string::npos || d.find("ola")    != std::string::npos ||
        d.find("metro")  != std::string::npos || d.find("petrol") != std::string::npos ||
        d.find("fuel")   != std::string::npos) return "Transport";
    if (d.find("rent")   != std::string::npos || d.find("housing") != std::string::npos) return "Rent";
    if (d.find("netflix") != std::string::npos || d.find("spotify") != std::string::npos ||
        d.find("prime")  != std::string::npos || d.find("hotstar") != std::string::npos) return "Entertainment";
    if (d.find("hospital") != std::string::npos || d.find("pharmacy") != std::string::npos ||
        d.find("medical")  != std::string::npos || d.find("apollo") != std::string::npos) return "Health";
    if (d.find("amazon")   != std::string::npos || d.find("flipkart") != std::string::npos ||
        d.find("myntra")   != std::string::npos || d.find("shopping") != std::string::npos) return "Shopping";
    if (d.find("electricity") != std::string::npos || d.find("broadband") != std::string::npos ||
        d.find("recharge")    != std::string::npos || d.find("bill")  != std::string::npos) return "Utilities";
    return "Other";
}

/*
  POST /api/import/csv
  Body (JSON):
  {
    "csv": "Date,Description,Debit,Credit\n01/04/2026,Salary,,50000\n..."
    "bank": "hdfc"   // optional hint: hdfc | sbi | icici | axis | generic
  }

  Returns: { "imported": N, "skipped": K }
*/
void setupImportRoutes(crow::SimpleApp& app, SQLite::Database& db) {

    CROW_ROUTE(app, "/api/import/csv").methods("OPTIONS"_method)
    ([](const crow::request&) {
        crow::response res(204);
        res.add_header("Access-Control-Allow-Origin",  "*");
        res.add_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type");
        return res;
    });

    CROW_ROUTE(app, "/api/import/csv").methods("POST"_method)
    ([&db](const crow::request& req) {
        try {
            auto body   = json::parse(req.body);
            std::string csvData = body.value("csv", "");
            std::string bank    = body.value("bank", "generic");

            if (csvData.empty()) {
                json j = {{"error", "No CSV data provided"}};
                crow::response res(400, j.dump());
                res.add_header("Content-Type", "application/json");
                addCORS(res);
                return res;
            }

            std::istringstream stream(csvData);
            std::string line;
            int imported = 0, skipped = 0;

            // Read header line to detect column positions
            std::getline(stream, line);
            auto headers = splitCSV(line);

            // Find column indices for common bank formats
            int dateIdx   = -1, descIdx  = -1;
            int debitIdx  = -1, creditIdx = -1, amountIdx = -1;

            for (int i = 0; i < (int)headers.size(); i++) {
                std::string h = headers[i];
                std::transform(h.begin(), h.end(), h.begin(), ::tolower);
                if (h.find("date")        != std::string::npos) dateIdx   = i;
                if (h.find("narration")   != std::string::npos ||
                    h.find("description") != std::string::npos ||
                    h.find("particulars") != std::string::npos ||
                    h.find("details")     != std::string::npos) descIdx = i;
                if (h.find("debit")       != std::string::npos ||
                    h.find("withdrawal")  != std::string::npos) debitIdx  = i;
                if (h.find("credit")      != std::string::npos ||
                    h.find("deposit")     != std::string::npos) creditIdx = i;
                if (h.find("amount")      != std::string::npos) amountIdx = i;
            }

            // Fallback to positional guessing if headers unrecognized
            if (dateIdx == -1) dateIdx = 0;
            if (descIdx == -1) descIdx = 1;

            // Parse data rows
            while (std::getline(stream, line)) {
                if (line.empty() || line.find_first_not_of(",\r\n ") == std::string::npos) continue;
                auto cols = splitCSV(line);
                if ((int)cols.size() <= std::max({dateIdx, descIdx})) { skipped++; continue; }

                std::string dateRaw = (dateIdx < (int)cols.size()) ? cols[dateIdx] : "";
                std::string desc    = (descIdx < (int)cols.size()) ? cols[descIdx] : "";
                if (!looksLikeDate(dateRaw)) { skipped++; continue; }

                double debit = 0, credit = 0;

                if (debitIdx != -1 && debitIdx < (int)cols.size() && !cols[debitIdx].empty())
                    try { debit = std::stod(cols[debitIdx]); } catch (...) {}
                if (creditIdx != -1 && creditIdx < (int)cols.size() && !cols[creditIdx].empty())
                    try { credit = std::stod(cols[creditIdx]); } catch (...) {}

                // Single amount column — positive = credit, negative = debit
                if (amountIdx != -1 && debitIdx == -1 && creditIdx == -1 &&
                    amountIdx < (int)cols.size() && !cols[amountIdx].empty()) {
                    double amt = 0;
                    try { amt = std::stod(cols[amountIdx]); } catch (...) {}
                    if (amt > 0) credit = amt;
                    else         debit  = -amt;
                }

                if (debit == 0 && credit == 0) { skipped++; continue; }

                Transaction t;
                t.date        = normalizeDate(dateRaw);
                t.description = desc;
                t.category    = guessCategory(desc);

                if (credit > 0) {
                    t.amount = credit;
                    t.type   = "income";
                } else {
                    t.amount = debit;
                    t.type   = "expense";
                }

                insertTransaction(db, t);
                imported++;
            }

            json j = {{"imported", imported}, {"skipped", skipped}};
            crow::response res(j.dump());
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;

        } catch (std::exception& e) {
            json j = {{"error", e.what()}};
            crow::response res(500, j.dump());
            res.add_header("Content-Type", "application/json");
            addCORS(res);
            return res;
        }
    });
}
