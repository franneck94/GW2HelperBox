#include "MemoryScanner.h"

#include <windows.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <string>

namespace
{
    bool is_readable_protection(const DWORD protection)
    {
        if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;

        switch (protection & 0xff)
        {
        case PAGE_READONLY:
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;
        }
    }

    bool is_readable_range(const MemoryScanner::Address address, const std::size_t size,
                           std::uint32_t &error_code)
    {
        if (size == 0)
            return true;

        if (address == 0 || size > (std::numeric_limits<MemoryScanner::Address>::max)() - address)
        {
            error_code = ERROR_INVALID_ADDRESS;
            return false;
        }

        const auto end = address + size;
        auto current = address;
        while (current < end)
        {
            MEMORY_BASIC_INFORMATION region{};
            if (VirtualQuery(reinterpret_cast<const void *>(current), &region, sizeof(region)) == 0)
            {
                error_code = GetLastError();
                return false;
            }

            if (region.State != MEM_COMMIT || !is_readable_protection(region.Protect))
            {
                error_code = ERROR_NOACCESS;
                return false;
            }

            const auto region_base = reinterpret_cast<MemoryScanner::Address>(region.BaseAddress);
            if (region.RegionSize > (std::numeric_limits<MemoryScanner::Address>::max)() - region_base)
            {
                error_code = ERROR_INVALID_ADDRESS;
                return false;
            }

            const auto region_end = region_base + region.RegionSize;
            if (current < region_base || current >= region_end)
            {
                error_code = ERROR_INVALID_ADDRESS;
                return false;
            }

            current = (std::min)(end, region_end);
        }

        return true;
    }
}

MemoryScanner::Address MemoryScanner::ExecutableBaseAddress() noexcept
{
    return reinterpret_cast<Address>(GetModuleHandleW(nullptr));
}

std::optional<MemoryScanner::Address> MemoryScanner::ParseAddress(const std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return std::nullopt;

    const auto last = text.find_last_not_of(" \t\r\n");
    const auto trimmed = text.substr(first, last - first + 1);
    if (trimmed.front() == '-')
        return std::nullopt;

    const auto hexadecimal = trimmed.size() > 2 && trimmed[0] == '0' &&
                             (trimmed[1] == 'x' || trimmed[1] == 'X');
    const auto digits = hexadecimal ? trimmed.substr(2) : trimmed;
    if (digits.empty())
        return std::nullopt;

    const std::string value(digits);
    char *end = nullptr;
    errno = 0;
    const auto parsed = std::strtoull(value.c_str(), &end, hexadecimal ? 16 : 10);
    if (errno == ERANGE || end == value.c_str() || *end != '\0' ||
        parsed > (std::numeric_limits<Address>::max)())
        return std::nullopt;

    return static_cast<Address>(parsed);
}

MemoryScanner::ReadResult MemoryScanner::Read(const Address address, const std::size_t size) const
{
    ReadResult result;
    result.address = address;
    result.requested_size = size;

    if (size > MAX_READ_SIZE)
    {
        result.error_code = ERROR_BAD_LENGTH;
        return result;
    }

    if (!is_readable_range(address, size, result.error_code))
        return result;

    result.bytes.resize(size);
    if (size == 0)
        return result;

    SIZE_T bytes_read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(address),
                           result.bytes.data(), size, &bytes_read) || bytes_read != size)
    {
        result.error_code = GetLastError();
        if (result.error_code == ERROR_SUCCESS)
            result.error_code = ERROR_PARTIAL_COPY;
        result.bytes.resize(bytes_read);
    }

    return result;
}

MemoryScanner::ReadResult MemoryScanner::ReadExecutableOffset(const Address offset, const std::size_t size) const
{
    const auto base = ExecutableBaseAddress();
    if (base == 0 || offset > (std::numeric_limits<Address>::max)() - base)
    {
        ReadResult result;
        result.requested_size = size;
        result.error_code = ERROR_INVALID_ADDRESS;
        return result;
    }

    return Read(base + offset, size);
}
