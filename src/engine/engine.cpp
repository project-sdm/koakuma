#include "engine/engine.hpp"

Engine::Engine()
    : file_mgr{},
      buf_mgr{file_mgr} {}
