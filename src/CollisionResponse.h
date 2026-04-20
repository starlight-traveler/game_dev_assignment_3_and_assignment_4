#ifndef COLLISION_RESPONSE_H
#define COLLISION_RESPONSE_H

// forward declare the game object type so this header can stay tiny
// that way other files can include the callback type without pulling in all of gameobject
class GameObject;

// this is the shared function shape for collision callbacks
// utility stores pointers with this exact type and calls them when two objects overlap
// the two references are mutable so gameplay code can react by changing either object
using CollisionResponse = void (*)(GameObject&, GameObject&);

#endif
