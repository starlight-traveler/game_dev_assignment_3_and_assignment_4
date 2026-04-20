#ifndef CONVEX_COLLISION_H
#define CONVEX_COLLISION_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <limits>

#include <glm/glm.hpp>

struct ConvexCollisionQueryResult {
    bool has_convex_data = false;
    bool intersecting = false;
    float separation_distance = 0.0f;
    glm::vec3 separating_axis = glm::vec3(0.0f);
    glm::vec3 closest_point_first = glm::vec3(0.0f);
    glm::vec3 closest_point_second = glm::vec3(0.0f);
};

namespace convex_collision {
namespace detail {
constexpr float kTolerance = 0.00001f;
constexpr float kToleranceSquared = kTolerance * kTolerance;
constexpr int kMaxIterations = 32;

struct SupportVertex {
    glm::vec3 minkowski = glm::vec3(0.0f);
    glm::vec3 point_first = glm::vec3(0.0f);
    glm::vec3 point_second = glm::vec3(0.0f);
};

struct Simplex {
    std::array<SupportVertex, 4> vertices{};
    std::size_t count = 0;
};

struct ClosestSimplexResult {
    glm::vec3 closest_point = glm::vec3(0.0f);
    std::array<float, 4> weights{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<int, 4> indices{-1, -1, -1, -1};
    std::size_t count = 0;
    bool contains_origin = false;
};

inline glm::vec3 normalize_or_fallback(const glm::vec3& value, const glm::vec3& fallback) {
    const float length_squared = glm::dot(value, value);
    if (length_squared <= kToleranceSquared) {
        return fallback;
    }
    return value / std::sqrt(length_squared);
}

template <typename SupportFirst, typename SupportSecond>
SupportVertex build_support_vertex(const SupportFirst& support_first,
                                   const SupportSecond& support_second,
                                   const glm::vec3& direction) {
    const glm::vec3 safe_direction = normalize_or_fallback(direction, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 point_first = support_first(safe_direction);
    const glm::vec3 point_second = support_second(-safe_direction);
    return SupportVertex{
        point_first - point_second,
        point_first,
        point_second};
}

inline bool simplex_contains_vertex(const Simplex& simplex, const SupportVertex& vertex) {
    for (std::size_t i = 0; i < simplex.count; ++i) {
        const glm::vec3 delta = simplex.vertices[i].minkowski - vertex.minkowski;
        if (glm::dot(delta, delta) <= kToleranceSquared) {
            return true;
        }
    }
    return false;
}

inline ClosestSimplexResult closest_point_line(const Simplex& simplex) {
    const glm::vec3& a = simplex.vertices[0].minkowski;
    const glm::vec3& b = simplex.vertices[1].minkowski;
    const glm::vec3 ab = b - a;
    const float ab_length_squared = glm::dot(ab, ab);
    if (ab_length_squared <= kToleranceSquared) {
        return ClosestSimplexResult{
            a,
            {1.0f, 0.0f, 0.0f, 0.0f},
            {0, -1, -1, -1},
            1,
            glm::dot(a, a) <= kToleranceSquared};
    }

    const float t = std::clamp(-glm::dot(a, ab) / ab_length_squared, 0.0f, 1.0f);
    if (t <= kTolerance) {
        return ClosestSimplexResult{
            a,
            {1.0f, 0.0f, 0.0f, 0.0f},
            {0, -1, -1, -1},
            1,
            glm::dot(a, a) <= kToleranceSquared};
    }
    if (t >= 1.0f - kTolerance) {
        return ClosestSimplexResult{
            b,
            {1.0f, 0.0f, 0.0f, 0.0f},
            {1, -1, -1, -1},
            1,
            glm::dot(b, b) <= kToleranceSquared};
    }

    const glm::vec3 closest = a + ab * t;
    return ClosestSimplexResult{
        closest,
        {1.0f - t, t, 0.0f, 0.0f},
        {0, 1, -1, -1},
        2,
        glm::dot(closest, closest) <= kToleranceSquared};
}

inline ClosestSimplexResult closest_point_triangle(const Simplex& simplex) {
    const glm::vec3& a = simplex.vertices[0].minkowski;
    const glm::vec3& b = simplex.vertices[1].minkowski;
    const glm::vec3& c = simplex.vertices[2].minkowski;

    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = -a;
    const float d1 = glm::dot(ab, ap);
    const float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        return ClosestSimplexResult{
            a,
            {1.0f, 0.0f, 0.0f, 0.0f},
            {0, -1, -1, -1},
            1,
            glm::dot(a, a) <= kToleranceSquared};
    }

    const glm::vec3 bp = -b;
    const float d3 = glm::dot(ab, bp);
    const float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        return ClosestSimplexResult{
            b,
            {1.0f, 0.0f, 0.0f, 0.0f},
            {1, -1, -1, -1},
            1,
            glm::dot(b, b) <= kToleranceSquared};
    }

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        const glm::vec3 closest = a + ab * v;
        return ClosestSimplexResult{
            closest,
            {1.0f - v, v, 0.0f, 0.0f},
            {0, 1, -1, -1},
            2,
            glm::dot(closest, closest) <= kToleranceSquared};
    }

    const glm::vec3 cp = -c;
    const float d5 = glm::dot(ab, cp);
    const float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        return ClosestSimplexResult{
            c,
            {1.0f, 0.0f, 0.0f, 0.0f},
            {2, -1, -1, -1},
            1,
            glm::dot(c, c) <= kToleranceSquared};
    }

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        const glm::vec3 closest = a + ac * w;
        return ClosestSimplexResult{
            closest,
            {1.0f - w, 0.0f, w, 0.0f},
            {0, 2, -1, -1},
            2,
            glm::dot(closest, closest) <= kToleranceSquared};
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        const glm::vec3 closest = b + (c - b) * w;
        return ClosestSimplexResult{
            closest,
            {1.0f - w, w, 0.0f, 0.0f},
            {1, 2, -1, -1},
            2,
            glm::dot(closest, closest) <= kToleranceSquared};
    }

    const float denominator = 1.0f / (va + vb + vc);
    const float v = vb * denominator;
    const float w = vc * denominator;
    const float u = 1.0f - v - w;
    const glm::vec3 closest = (a * u) + (b * v) + (c * w);
    return ClosestSimplexResult{
        closest,
        {u, v, w, 0.0f},
        {0, 1, 2, -1},
        3,
        glm::dot(closest, closest) <= kToleranceSquared};
}

inline bool origin_outside_face(const glm::vec3& a,
                                const glm::vec3& b,
                                const glm::vec3& c,
                                const glm::vec3& opposite) {
    glm::vec3 normal = glm::cross(b - a, c - a);
    if (glm::dot(normal, opposite - a) > 0.0f) {
        normal = -normal;
    }
    return glm::dot(normal, -a) > kTolerance;
}

inline ClosestSimplexResult closest_point_tetrahedron(const Simplex& simplex) {
    const std::array<std::array<int, 3>, 4> faces{{
        {{0, 1, 2}},
        {{0, 3, 1}},
        {{0, 2, 3}},
        {{1, 3, 2}},
    }};
    const std::array<int, 4> opposite_indices{{3, 2, 1, 0}};

    ClosestSimplexResult best{};
    float best_distance_squared = std::numeric_limits<float>::max();
    bool found_outside_face = false;

    for (std::size_t face_index = 0; face_index < faces.size(); ++face_index) {
        const auto& face = faces[face_index];
        const int opposite = opposite_indices[face_index];

        const glm::vec3& a = simplex.vertices[face[0]].minkowski;
        const glm::vec3& b = simplex.vertices[face[1]].minkowski;
        const glm::vec3& c = simplex.vertices[face[2]].minkowski;
        const glm::vec3& d = simplex.vertices[opposite].minkowski;

        if (!origin_outside_face(a, b, c, d)) {
            continue;
        }

        found_outside_face = true;
        Simplex face_simplex{};
        face_simplex.count = 3;
        face_simplex.vertices[0] = simplex.vertices[face[0]];
        face_simplex.vertices[1] = simplex.vertices[face[1]];
        face_simplex.vertices[2] = simplex.vertices[face[2]];

        ClosestSimplexResult candidate = closest_point_triangle(face_simplex);
        std::array<int, 4> mapped_indices{-1, -1, -1, -1};
        for (std::size_t i = 0; i < candidate.count; ++i) {
            mapped_indices[i] = face[static_cast<std::size_t>(candidate.indices[i])];
        }
        candidate.indices = mapped_indices;

        const float distance_squared = glm::dot(candidate.closest_point, candidate.closest_point);
        if (distance_squared < best_distance_squared) {
            best_distance_squared = distance_squared;
            best = candidate;
        }
    }

    if (!found_outside_face) {
        return ClosestSimplexResult{
            glm::vec3(0.0f),
            {0.25f, 0.25f, 0.25f, 0.25f},
            {0, 1, 2, 3},
            4,
            true};
    }

    return best;
}

inline ClosestSimplexResult closest_simplex_to_origin(const Simplex& simplex) {
    switch (simplex.count) {
    case 1:
        return ClosestSimplexResult{
            simplex.vertices[0].minkowski,
            {1.0f, 0.0f, 0.0f, 0.0f},
            {0, -1, -1, -1},
            1,
            glm::dot(simplex.vertices[0].minkowski, simplex.vertices[0].minkowski) <= kToleranceSquared};
    case 2:
        return closest_point_line(simplex);
    case 3:
        return closest_point_triangle(simplex);
    case 4:
        return closest_point_tetrahedron(simplex);
    default:
        return ClosestSimplexResult{};
    }
}

inline void apply_closest_result(Simplex& simplex, ClosestSimplexResult& result) {
    std::array<SupportVertex, 4> reduced_vertices{};
    std::array<float, 4> reduced_weights{0.0f, 0.0f, 0.0f, 0.0f};
    std::size_t reduced_count = 0;

    for (std::size_t i = 0; i < result.count; ++i) {
        const int source_index = result.indices[i];
        if (source_index < 0 || static_cast<std::size_t>(source_index) >= simplex.count) {
            continue;
        }
        const float weight = result.weights[i];
        if (weight <= kTolerance) {
            continue;
        }

        reduced_vertices[reduced_count] = simplex.vertices[static_cast<std::size_t>(source_index)];
        reduced_weights[reduced_count] = weight;
        ++reduced_count;
    }

    if (reduced_count == 0 && simplex.count > 0) {
        reduced_vertices[0] = simplex.vertices[0];
        reduced_weights[0] = 1.0f;
        reduced_count = 1;
        result.closest_point = simplex.vertices[0].minkowski;
    }

    simplex.vertices = reduced_vertices;
    simplex.count = reduced_count;
    result.weights = reduced_weights;
    result.count = reduced_count;
}

inline void fill_witness_points(const Simplex& simplex,
                                const ClosestSimplexResult& result,
                                ConvexCollisionQueryResult& query_result) {
    glm::vec3 closest_point_first(0.0f);
    glm::vec3 closest_point_second(0.0f);
    for (std::size_t i = 0; i < result.count && i < simplex.count; ++i) {
        closest_point_first += simplex.vertices[i].point_first * result.weights[i];
        closest_point_second += simplex.vertices[i].point_second * result.weights[i];
    }

    query_result.closest_point_first = closest_point_first;
    query_result.closest_point_second = closest_point_second;
}
}  // namespace detail

template <typename SupportFirst, typename SupportSecond>
ConvexCollisionQueryResult query(const SupportFirst& support_first,
                                 const SupportSecond& support_second,
                                 const glm::vec3& initial_direction) {
    ConvexCollisionQueryResult result{};
    result.has_convex_data = true;

    detail::Simplex simplex{};
    simplex.vertices[0] = detail::build_support_vertex(
        support_first, support_second, initial_direction);
    simplex.count = 1;

    detail::ClosestSimplexResult closest = detail::closest_simplex_to_origin(simplex);
    detail::apply_closest_result(simplex, closest);

    glm::vec3 search_direction = -closest.closest_point;
    if (glm::dot(search_direction, search_direction) <= detail::kToleranceSquared) {
        result.intersecting = true;
        result.separation_distance = 0.0f;
        detail::fill_witness_points(simplex, closest, result);
        return result;
    }

    for (int iteration = 0; iteration < detail::kMaxIterations; ++iteration) {
        const detail::SupportVertex support_vertex =
            detail::build_support_vertex(support_first, support_second, search_direction);
        if (detail::simplex_contains_vertex(simplex, support_vertex)) {
            break;
        }

        const float progress =
            glm::dot(support_vertex.minkowski, search_direction) -
            glm::dot(closest.closest_point, search_direction);
        if (progress <= detail::kTolerance) {
            break;
        }

        simplex.vertices[simplex.count++] = support_vertex;
        closest = detail::closest_simplex_to_origin(simplex);
        if (closest.contains_origin) {
            detail::apply_closest_result(simplex, closest);
            result.intersecting = true;
            result.separation_distance = 0.0f;
            detail::fill_witness_points(simplex, closest, result);
            return result;
        }

        detail::apply_closest_result(simplex, closest);
        search_direction = -closest.closest_point;
        if (glm::dot(search_direction, search_direction) <= detail::kToleranceSquared) {
            result.intersecting = true;
            result.separation_distance = 0.0f;
            detail::fill_witness_points(simplex, closest, result);
            return result;
        }
    }

    detail::fill_witness_points(simplex, closest, result);
    const glm::vec3 separating_delta =
        result.closest_point_second - result.closest_point_first;
    result.separation_distance = std::sqrt(glm::dot(separating_delta, separating_delta));
    result.separating_axis = detail::normalize_or_fallback(
        separating_delta,
        detail::normalize_or_fallback(search_direction, glm::vec3(1.0f, 0.0f, 0.0f)));
    return result;
}
}  // namespace convex_collision

#endif
