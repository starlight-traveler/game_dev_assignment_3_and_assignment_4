/**
 * @file CollisionResponse.h
 * @brief Shared collision callback type used by the engine and end-user code
 */
#ifndef COLLISION_RESPONSE_H
#define COLLISION_RESPONSE_H

class GameObject;

/**
 * @brief Function pointer signature for pairwise collision responses
 */
using CollisionResponse = void (*)(GameObject&, GameObject&);

#endif
