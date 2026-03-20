#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "RtsUnit.h"

namespace {
/**
 * @brief Object type for function-pointer-based update benchmarking
 */
struct FunctionObject {
    glm::vec3 position;
    glm::vec3 linear_velocity;
    glm::vec3 angular_velocity;
    Quaternion rotation;
    void (*update_fn)(FunctionObject&, float);
};

/**
 * @brief Integrates one frame for a function-pointer object
 * @param object Target object
 * @param delta_seconds Delta time in seconds
 */
void update_function_object(FunctionObject& object, float delta_seconds) {
    object.position += object.linear_velocity * delta_seconds;

    const float speed = glm::length(object.angular_velocity);
    if (speed <= 0.000001f || delta_seconds <= 0.0f) {
        return;
    }

    const glm::vec3 axis = object.angular_velocity / speed;
    const float angle = speed * delta_seconds;
    const Quaternion delta_rotation(axis, angle);
    object.rotation = delta_rotation * object.rotation;
    object.rotation.normalize();
}

/**
 * @brief Benchmarks virtual dispatch update cost
 * @param object_count Number of objects
 * @param iteration_count Number of iterations
 * @param delta_seconds Delta time in seconds
 * @return Duration samples in microseconds
 */
std::vector<double> benchmark_virtual_dispatch(std::size_t object_count,
                                               std::size_t iteration_count,
                                               float delta_seconds) {
    std::vector<std::unique_ptr<GameObject>> objects;
    objects.reserve(object_count);
    for (std::size_t i = 0; i < object_count; ++i) {
        objects.push_back(std::make_unique<RtsUnit>(
            static_cast<std::uint32_t>(i),
            glm::vec3(static_cast<float>(i), 0.0f, static_cast<float>(-i)),
            glm::vec3(0.5f, 0.0f, 0.2f),
            glm::vec3(0.0f, glm::half_pi<float>(), 0.0f)));
    }

    std::vector<double> samples_microseconds;
    samples_microseconds.reserve(iteration_count);
    for (std::size_t iteration = 0; iteration < iteration_count; ++iteration) {
        const auto start_time = std::chrono::steady_clock::now();
        for (const std::unique_ptr<GameObject>& object : objects) {
            object->update(delta_seconds);
        }
        const auto end_time = std::chrono::steady_clock::now();
        const double elapsed_us = std::chrono::duration<double, std::micro>(end_time - start_time).count();
        samples_microseconds.push_back(elapsed_us);
    }
    return samples_microseconds;
}

/**
 * @brief Benchmarks function-pointer update cost
 * @param object_count Number of objects
 * @param iteration_count Number of iterations
 * @param delta_seconds Delta time in seconds
 * @return Duration samples in microseconds
 */
std::vector<double> benchmark_function_pointer(std::size_t object_count,
                                               std::size_t iteration_count,
                                               float delta_seconds) {
    std::vector<FunctionObject> objects;
    objects.reserve(object_count);
    for (std::size_t i = 0; i < object_count; ++i) {
        FunctionObject object{};
        object.position = glm::vec3(static_cast<float>(i), 0.0f, static_cast<float>(-i));
        object.linear_velocity = glm::vec3(0.5f, 0.0f, 0.2f);
        object.angular_velocity = glm::vec3(0.0f, glm::half_pi<float>(), 0.0f);
        object.rotation = Quaternion(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f);
        object.update_fn = update_function_object;
        objects.push_back(object);
    }

    std::vector<double> samples_microseconds;
    samples_microseconds.reserve(iteration_count);
    for (std::size_t iteration = 0; iteration < iteration_count; ++iteration) {
        const auto start_time = std::chrono::steady_clock::now();
        for (FunctionObject& object : objects) {
            object.update_fn(object, delta_seconds);
        }
        const auto end_time = std::chrono::steady_clock::now();
        const double elapsed_us = std::chrono::duration<double, std::micro>(end_time - start_time).count();
        samples_microseconds.push_back(elapsed_us);
    }
    return samples_microseconds;
}

/**
 * @brief Writes benchmark timing vectors to a CSV file
 * @param csv_path Output CSV file path
 * @param virtual_samples Virtual dispatch samples
 * @param function_samples Function pointer samples
 * @return True when write succeeds
 */
bool write_csv(const std::string& csv_path,
               const std::vector<double>& virtual_samples,
               const std::vector<double>& function_samples) {
    if (virtual_samples.size() != function_samples.size()) {
        return false;
    }

    std::ofstream file(csv_path);
    if (!file.is_open()) {
        return false;
    }

    file << "iteration,virtual_dispatch_us,function_pointer_us\n";
    for (std::size_t i = 0; i < virtual_samples.size(); ++i) {
        file << i << "," << virtual_samples[i] << "," << function_samples[i] << "\n";
    }
    return true;
}
}  // namespace

/**
 * @brief Runs objective B benchmark and writes CSV output
 * @param argc Argument count
 * @param argv Argument values
 * @return Zero on success otherwise non-zero
 */
int main(int argc, char** argv) {
    const std::size_t object_count = 50;
    const std::size_t iteration_count = 10000;
    const float delta_seconds = 0.016f;
    const std::string csv_path = (argc > 1 && argv && argv[1]) ? argv[1] : "objective_b_benchmark.csv";

    const std::vector<double> virtual_samples =
        benchmark_virtual_dispatch(object_count, iteration_count, delta_seconds);
    const std::vector<double> function_samples =
        benchmark_function_pointer(object_count, iteration_count, delta_seconds);

    if (!write_csv(csv_path, virtual_samples, function_samples)) {
        std::cerr << "Failed to write benchmark csv to " << csv_path << "\n";
        return 1;
    }

    std::cout << "Benchmark completed\n";
    std::cout << "Objects: " << object_count << "\n";
    std::cout << "Iterations: " << iteration_count << "\n";
    std::cout << "CSV: " << csv_path << "\n";
    return 0;
}
