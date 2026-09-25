#ifndef SCENE_FORWARD_DECLARATIONS_HPP
#define SCENE_FORWARD_DECLARATIONS_HPP

//-----------------------------------------------------------------------------
// System headers
#include <functional>

//-----------------------------------------------------------------------------
// Library Forward declarations

namespace BW
{
class SceneObject;
class SceneProvider;
class Scene;
class ISceneView;
class ISceneObjectOperation;
class SceneListener;
class IntersectionSet;


typedef std::function< void(const SceneObject&) > ConstSceneObjectCallback;
typedef std::function< void(SceneObject&) > SceneObjectCallback;

namespace SceneTypeSystem
{

}

} // namespace BW

#endif // SCENE_FORWARD_DECLARATIONS_HPP
