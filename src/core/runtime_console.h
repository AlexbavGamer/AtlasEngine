#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace Atlas {

class RuntimeConsole {
public:
    enum class Level {
        Info,
        Warn,
        Error
    };

    struct Entry {
        Level level = Level::Info;
        std::string text;
    };

    static RuntimeConsole& instance() {
        static RuntimeConsole s_Instance;
        return s_Instance;
    }

    void add(Level level, const std::string& text) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Entries.push_back({level, text});
        if (m_Entries.size() > m_MaxEntries) {
            m_Entries.pop_front();
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Entries.clear();
    }

    std::vector<Entry> snapshot() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return std::vector<Entry>(m_Entries.begin(), m_Entries.end());
    }

private:
    RuntimeConsole() = default;

    mutable std::mutex m_Mutex;
    std::deque<Entry> m_Entries;
    size_t m_MaxEntries = 2000;
};

inline void ConsoleInfo(const std::string& text) {
    RuntimeConsole::instance().add(RuntimeConsole::Level::Info, text);
}

inline void ConsoleWarn(const std::string& text) {
    RuntimeConsole::instance().add(RuntimeConsole::Level::Warn, text);
}

inline void ConsoleError(const std::string& text) {
    RuntimeConsole::instance().add(RuntimeConsole::Level::Error, text);
}

} // namespace Atlas
