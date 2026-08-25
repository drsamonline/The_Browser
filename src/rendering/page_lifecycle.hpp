#pragma once
#include "url.hpp"
#include <string>
namespace aetheris::rendering {
enum class PageLoadState { Idle, Loading, Complete, Failed };
struct PageLifecycleState { PageLoadState state { PageLoadState::Idle }; Url url; std::string message; };
}
