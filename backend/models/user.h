#pragma once
#include <string>

struct User {
    int id;
    std::string email;
    std::string name;
    std::string passwordHash;
    std::string createdAt;
};
