#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

class MemoryScanner
{
public:
    using Address = std::uintptr_t;

    struct ReadResult
    {
        Address address = 0;
        std::size_t requested_size = 0;
        std::vector<std::uint8_t> bytes;
        std::uint32_t error_code = 0;

        explicit operator bool() const noexcept
        {
            return error_code == 0 && bytes.size() == requested_size;
        }
    };

    static constexpr std::size_t MAX_READ_SIZE = 1024 * 1024;

    [[nodiscard]] static Address ExecutableBaseAddress() noexcept;
    [[nodiscard]] static std::optional<Address> ParseAddress(std::string_view text) noexcept;

    [[nodiscard]] ReadResult Read(Address address, std::size_t size) const;
    [[nodiscard]] ReadResult ReadExecutableOffset(Address offset, std::size_t size) const;

    template <typename Value>
    [[nodiscard]] std::optional<Value> ReadValue(Address address) const
    {
        static_assert(std::is_trivially_copyable_v<Value>, "MemoryScanner can only read trivially copyable values");
        static_assert(std::is_default_constructible_v<Value>, "MemoryScanner values must be default constructible");

        const auto result = Read(address, sizeof(Value));
        if (!result)
            return std::nullopt;

        Value value{};
        std::memcpy(&value, result.bytes.data(), sizeof(Value));
        return value;
    }
};
