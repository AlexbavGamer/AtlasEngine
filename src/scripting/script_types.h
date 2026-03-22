#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

namespace Atlas::Scripting {

enum class ScriptFieldType : uint8_t {
    None = 0,
    Bool,
    Int,
    Float,
    String,
    Vec2,
    Vec3,
    Vec4,
};

using ScriptFieldValue = std::variant<std::monostate, bool, int, float, std::string, glm::vec2, glm::vec3, glm::vec4>;
using ScriptFieldMap = std::unordered_map<std::string, ScriptFieldValue>;

struct ScriptFieldDefinition {
    std::string name;
    ScriptFieldType type = ScriptFieldType::None;
    ScriptFieldValue defaultValue{};
};

inline ScriptFieldType getFieldType(const ScriptFieldValue& value) {
    if (std::holds_alternative<bool>(value)) return ScriptFieldType::Bool;
    if (std::holds_alternative<int>(value)) return ScriptFieldType::Int;
    if (std::holds_alternative<float>(value)) return ScriptFieldType::Float;
    if (std::holds_alternative<std::string>(value)) return ScriptFieldType::String;
    if (std::holds_alternative<glm::vec2>(value)) return ScriptFieldType::Vec2;
    if (std::holds_alternative<glm::vec3>(value)) return ScriptFieldType::Vec3;
    if (std::holds_alternative<glm::vec4>(value)) return ScriptFieldType::Vec4;
    return ScriptFieldType::None;
}

inline const char* getFieldTypeName(ScriptFieldType type) {
    switch (type) {
    case ScriptFieldType::Bool: return "bool";
    case ScriptFieldType::Int: return "int";
    case ScriptFieldType::Float: return "float";
    case ScriptFieldType::String: return "string";
    case ScriptFieldType::Vec2: return "vec2";
    case ScriptFieldType::Vec3: return "vec3";
    case ScriptFieldType::Vec4: return "vec4";
    default: return "none";
    }
}

} // namespace Atlas::Scripting
