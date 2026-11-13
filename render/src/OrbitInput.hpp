#pragma once
#include "render/Model.hpp"
#include <algorithm>
#include <cmath>
namespace render::detail {
inline OrbitCamera DragOrbit(OrbitCamera camera,double dx,double dy) {
    if(!std::isfinite(dx)||!std::isfinite(dy))return camera;
    camera.yaw=static_cast<float>(std::remainder(camera.yaw-std::clamp(dx,-10000.0,10000.0)*0.35,360.0));
    camera.pitch=static_cast<float>(std::clamp(camera.pitch+std::clamp(dy,-10000.0,10000.0)*0.35,-85.0,85.0));
    return camera;
}
inline OrbitCamera ZoomOrbit(OrbitCamera camera,double pixels) {
    if(!std::isfinite(pixels))return camera;
    camera.distance=static_cast<float>(std::clamp(camera.distance*std::exp(std::clamp(pixels,-1000.0,1000.0)*0.001),1.5,100.0));
    return camera;
}
}
