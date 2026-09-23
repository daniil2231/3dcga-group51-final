#pragma once
// Suppress warnings in third-party code.
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/gtx/string_cast.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
DISABLE_WARNINGS_POP()
#include <ostream>

namespace glm {

inline std::ostream& operator<<(std::ostream& stream, const glm::vec2& obj)
{
    return stream << glm::to_string(obj);
}

inline std::ostream& operator<<(std::ostream& stream, const glm::vec3& obj)
{
    return stream << glm::to_string(obj);
}

inline std::ostream& operator<<(std::ostream& stream, const glm::uvec3& obj)
{
    return stream << glm::to_string(obj);
}

inline std::ostream& operator<<(std::ostream& stream, const glm::uvec4& obj)
{
    return stream << glm::to_string(obj);
}

inline std::ostream& operator<<(std::ostream& stream, const glm::vec4& obj)
{
    return stream << glm::to_string(obj);
}

inline std::ostream& operator<<(std::ostream& stream, const glm::mat3 &mat)
{
    stream << "mat3(\n"
           << "  (" << mat[0].x << ", " << mat[0].y << ", " << mat[0].z << ")\n"
           << "  (" << mat[1].x << ", " << mat[1].y << ", " << mat[1].z << ")\n"
           << "  (" << mat[2].x << ", " << mat[2].y << ", " << mat[2].z << ")\n"
           << ")";
    return stream;
}

inline std::ostream& operator<<(std::ostream& stream, const glm::mat4& mat)
{
    stream << "mat4(\n"
        << "  (" << mat[0].x << ", " << mat[0].y << ", " << mat[0].z << ", " << mat[0].w << ")\n"
        << "  (" << mat[1].x << ", " << mat[1].y << ", " << mat[1].z << ", " << mat[1].w << ")\n"
        << "  (" << mat[2].x << ", " << mat[2].y << ", " << mat[2].z << ", " << mat[2].w << ")\n"
        << "  (" << mat[3].x << ", " << mat[3].y << ", " << mat[3].z << ", " << mat[3].w << ")\n"
        << ")";
    return stream;
}

inline std::ostream& operator<<(std::ostream& stream, const glm::quat& quat)
{
    stream << glm::to_string(quat);
    return stream;
}

//inline std::ostream& operator<<(std::ostream& stream, const glm::mat4x4& obj)
//{
//    return stream << glm::to_string(obj);
//}

}
