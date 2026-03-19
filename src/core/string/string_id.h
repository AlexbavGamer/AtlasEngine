#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

namespace Atlas {

class StringID {
public:
    using ID = uint64_t;

    StringID() : m_ID(0) {}

    explicit StringID(const std::string& str) 
        : m_ID(getOrCreate(str)) {}

    explicit StringID(ID id) : m_ID(id) {}

    StringID(const char* str) 
        : m_ID(getOrCreate(std::string(str))) {}

    ID getID() const { return m_ID; }

    bool operator==(const StringID& other) const { return m_ID == other.m_ID; }
    bool operator!=(const StringID& other) const { return m_ID != other.m_ID; }
    bool operator<(const StringID& other) const { return m_ID < other.m_ID; }

    explicit operator bool() const { return m_ID != 0; }

    static const StringID& null() {
        static StringID nullID(StringID::ID(0));
        return nullID;
    }

private:
    ID m_ID;

    static ID getOrCreate(const std::string& str) {
        static std::unordered_map<std::string, ID> stringToID;
        static ID nextID = 1;

        auto it = stringToID.find(str);
        if (it != stringToID.end()) {
            return it->second;
        }

        ID id = nextID++;
        stringToID[str] = id;
        return id;
    }
};

}

namespace std {
    template<>
    struct hash<Atlas::StringID> {
        size_t operator()(const Atlas::StringID& id) const {
            return std::hash<Atlas::StringID::ID>()(id.getID());
        }
    };
}
