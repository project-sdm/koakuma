#ifndef ENGINE_HPP
#define ENGINE_HPP

#include "buffer_manager.hpp"
#include "file_manager.hpp"

struct Engine {
    FileManager file_mgr;
    BufferManager buf_mgr;

    Engine();
};
#endif
