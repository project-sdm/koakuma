#ifndef PACK_HPP
#define PACK_HPP

#include <cstddef>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>
#include "types.hpp"

namespace pack {
    template<typename T>
    std::size_t pack_size(const T& val);

    template<typename T>
    void pack(const T& val, u8*& dest);

    template<typename T>
    void unpack(T& dest, const u8*& src);

    template<typename T>
    class PackSize {
        [[nodiscard]] std::size_t operator()(const T& val) const;
    };

    template<typename T>
    class Pack {
        void operator()(const T& val, u8*& dest) const;
    };

    template<typename T>
    struct Unpack {
        void operator()(T& dest, const u8*& src) const;
    };

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    struct PackSize<T> {
        [[nodiscard]] std::size_t operator()(const T& val) const {
            return sizeof(val);
        }
    };

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    struct Pack<T> {
        void operator()(const T& val, u8*& dest) const {
            std::memcpy(dest, &val, sizeof(val));
            dest += sizeof(val);
        }
    };

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    struct Unpack<T> {
        void operator()(T& dest, const u8*& src) const {
            std::memcpy(&dest, src, sizeof(dest));
            src += sizeof(dest);
        }
    };

    template<>
    struct PackSize<std::string> {
        [[nodiscard]] std::size_t operator()(const std::string& str) const {
            return pack_size<>(str.size()) + str.size();
        }
    };

    template<>
    struct Pack<std::string> {
        void operator()(const std::string& str, u8*& dest) const {
            std::size_t size = str.size();
            pack<>(size, dest);
            std::memcpy(dest, str.data(), size);
            dest += size;
        }
    };

    template<>
    struct Unpack<std::string> {
        void operator()(std::string& dest, const u8*& src) {
            std::size_t size = 0;
            unpack<>(size, src);

            dest.resize(size);
            std::memcpy(dest.data(), src, size);
            src += size;
        }
    };

    template<typename T>
    struct PackSize<std::vector<T>> {
        [[nodiscard]] std::size_t operator()(const std::vector<T>& vec) const {
            std::size_t out = pack_size<>(vec.size());

            for (const auto& el : vec)
                out += pack_size<>(el);

            return out;
        }
    };

    template<typename T>
    struct Pack<std::vector<T>> {
        void operator()(const std::vector<T>& vec, u8*& dest) const {
            pack<>(vec.size(), dest);

            for (const auto& el : vec)
                pack<>(el, dest);
        }
    };

    template<typename T>
    struct Unpack<std::vector<T>> {
        void operator()(std::vector<T>& dest, const u8*& src) const {
            std::size_t size = 0;
            unpack<>(size, src);

            dest = std::vector<T>(size);

            for (auto& dest_el : dest)
                unpack<>(dest_el, src);
        }
    };

    template<typename T>
    std::size_t pack_size(const T& val) {
        return PackSize<T>{}(val);
    }

    template<typename T>
    void pack(const T& val, u8*& dest) {
        Pack<T>{}(val, dest);
    }

    template<typename T>
    void unpack(T& dest, const u8*& src) {
        Unpack<T>{}(dest, src);
    }

    template<typename T>
    std::vector<u8> pack_alloc(const T& val) {
        std::vector<u8> data(pack_size<>(val));
        u8* ptr = data.data();
        pack<>(val, ptr);
        return data;
    }

    template<typename T>
    T unpack_alloc(const u8* src) {
        T val;
        unpack<>(val, src);
        return val;
    }

}  // namespace pack

#endif
