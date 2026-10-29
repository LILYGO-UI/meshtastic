#include "components/qr_code.hpp"

#include <array>
#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace lilygo_ui {
namespace {

constexpr int kSize          = static_cast<int>(kQrCodeSize);
constexpr int kDataCodewords = 19;  // Version 1-L, one block.
constexpr int kEccCodewords  = 7;

struct Builder {
    QrCodeMatrix modules{};
    std::array<bool, kQrCodeSize * kQrCodeSize> function{};

    static constexpr std::size_t index(int x, int y)
    {
        return static_cast<std::size_t>(y * kSize + x);
    }

    void set_function_module(int x, int y, bool dark)
    {
        if (x < 0 || y < 0 || x >= kSize || y >= kSize) return;
        modules[index(x, y)] = dark;
        function[index(x, y)] = true;
    }

    void draw_finder(int left, int top)
    {
        // The one-module white separator is included in this 9x9 footprint.
        // Marking the separator as function modules prevents data bits from
        // ever being written into the quiet border around a finder pattern.
        for (int dy = -1; dy <= 7; ++dy) {
            for (int dx = -1; dx <= 7; ++dx) {
                const bool inside = dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6;
                const bool dark = inside &&
                                  (dx == 0 || dx == 6 || dy == 0 || dy == 6 ||
                                   (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4));
                set_function_module(left + dx, top + dy, dark);
            }
        }
    }

    void reserve_format_information()
    {
        // Keep the format locations out of the payload.  They are overwritten
        // with the selected error-correction level and mask below.
        for (int i = 0; i < 15; ++i) {
            const int vertical_y = i < 6 ? i : (i < 8 ? i + 1 : kSize - 15 + i);
            const int horizontal_y = i < 8 ? kSize - i - 1 : (i < 9 ? 7 : 15 - i - 1);
            set_function_module(8, vertical_y, false);
            set_function_module(horizontal_y, 8, false);
        }
        set_function_module(kSize - 8, 8, true);  // fixed dark module
    }

    void draw_function_patterns()
    {
        draw_finder(0, 0);
        draw_finder(kSize - 7, 0);
        draw_finder(0, kSize - 7);

        for (int i = 8; i < kSize - 8; ++i) {
            if (!function[index(i, 6)]) set_function_module(i, 6, (i & 1) == 0);
            if (!function[index(6, i)]) set_function_module(6, i, (i & 1) == 0);
        }
        reserve_format_information();
    }

    static std::uint8_t gf_multiply(std::uint8_t x, std::uint8_t y)
    {
        std::uint8_t result = 0;
        for (int i = 0; i < 8; ++i) {
            if ((y & 1U) != 0U) result ^= x;
            const bool high = (x & 0x80U) != 0U;
            x = static_cast<std::uint8_t>(x << 1U);
            if (high) x ^= 0x1dU;  // x^8 + x^4 + x^3 + x^2 + 1 (0x11d)
            y = static_cast<std::uint8_t>(y >> 1U);
        }
        return result;
    }

    static std::array<std::uint8_t, kEccCodewords>
    compute_ecc(const std::array<std::uint8_t, kDataCodewords> &data)
    {
        // Generator polynomial for seven Reed-Solomon codewords over GF(256).
        constexpr std::array<std::uint8_t, kEccCodewords> generator{
            127, 122, 154, 164, 11, 68, 117};
        std::array<std::uint8_t, kEccCodewords> remainder{};
        for (const auto value : data) {
            const auto factor = static_cast<std::uint8_t>(value ^ remainder[0]);
            for (int i = 0; i < kEccCodewords - 1; ++i) {
                remainder[i] = static_cast<std::uint8_t>(remainder[i + 1] ^
                                                          gf_multiply(generator[i], factor));
            }
            remainder[kEccCodewords - 1] = gf_multiply(generator[kEccCodewords - 1], factor);
        }
        return remainder;
    }

    static std::uint32_t bch_remainder(std::uint32_t data, std::uint32_t generator)
    {
        int generator_degree = 0;
        for (auto value = generator; value != 0; value >>= 1U) ++generator_degree;
        data <<= static_cast<unsigned>(generator_degree - 1);
        while (true) {
            int data_degree = 0;
            for (auto value = data; value != 0; value >>= 1U) ++data_degree;
            if (data_degree < generator_degree) break;
            data ^= generator << static_cast<unsigned>(data_degree - generator_degree);
        }
        return data;
    }

    void draw_format_information(int mask)
    {
        // Error correction level L has the two-bit format value 01.
        const auto data = static_cast<std::uint32_t>((1 << 3) | mask);
        const auto bits = static_cast<std::uint16_t>((data << 10U) |
                                                      bch_remainder(data, 0x537U)) ^
                          0x5412U;
        for (int i = 0; i < 15; ++i) {
            const bool dark = ((bits >> i) & 1U) != 0U;
            const int vertical_y = i < 6 ? i : (i < 8 ? i + 1 : kSize - 15 + i);
            const int horizontal_y = i < 8 ? kSize - i - 1 : (i < 9 ? 7 : 15 - i - 1);
            modules[index(8, vertical_y)] = dark;
            modules[index(horizontal_y, 8)] = dark;
        }
        modules[index(kSize - 8, 8)] = true;
    }

    static bool mask_bit(int mask, int x, int y)
    {
        switch (mask) {
            case 0: return ((x + y) & 1) == 0;
            case 1: return (y & 1) == 0;
            case 2: return x % 3 == 0;
            case 3: return (x + y) % 3 == 0;
            case 4: return ((x / 3 + y / 2) & 1) == 0;
            case 5: return (x * y) % 2 + (x * y) % 3 == 0;
            case 6: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
            case 7: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
            default: return false;
        }
    }

    void draw_codewords(const std::array<std::uint8_t, kDataCodewords + kEccCodewords> &codewords,
                        int mask)
    {
        int bit_index = 0;
        bool upward = true;
        for (int right = kSize - 1; right >= 1; right -= 2) {
            if (right == 6) --right;  // Skip the vertical timing column.
            for (int vert = 0; vert < kSize; ++vert) {
                const int y = upward ? kSize - 1 - vert : vert;
                for (int j = 0; j < 2; ++j) {
                    const int x = right - j;
                    if (function[index(x, y)]) continue;
                    bool dark = false;
                    if (bit_index < static_cast<int>(codewords.size() * 8U)) {
                        dark = ((codewords[bit_index >> 3] >> (7 - (bit_index & 7))) & 1U) != 0U;
                    }
                    if (mask_bit(mask, x, y)) dark = !dark;
                    modules[index(x, y)] = dark;
                    ++bit_index;
                }
            }
            upward = !upward;
        }
    }
};

}  // namespace

QrCodeMatrix qr_code_matrix(std::string_view payload)
{
    QrCodeMatrix empty{};
    if (payload.empty() || payload.size() > 17U) return empty;

    std::vector<bool> bits;
    bits.reserve(kDataCodewords * 8);
    const auto append_bits = [&bits](std::uint32_t value, int count) {
        for (int i = count - 1; i >= 0; --i) bits.push_back(((value >> i) & 1U) != 0U);
    };
    append_bits(0b0100U, 4);  // Byte mode.
    append_bits(static_cast<std::uint32_t>(payload.size()), 8);
    for (const auto byte : payload) append_bits(static_cast<std::uint8_t>(byte), 8);
    const auto capacity = static_cast<int>(kDataCodewords * 8);
    append_bits(0, std::min(4, capacity - static_cast<int>(bits.size())));
    while ((bits.size() & 7U) != 0U) bits.push_back(false);

    std::array<std::uint8_t, kDataCodewords> data{};
    for (int i = 0; i < kDataCodewords; ++i) {
        std::uint8_t value = 0;
        for (int bit = 0; bit < 8; ++bit) {
            value = static_cast<std::uint8_t>((value << 1U) | (bits[static_cast<std::size_t>(i * 8 + bit)] ? 1U : 0U));
        }
        data[static_cast<std::size_t>(i)] = value;
    }
    const auto data_end = static_cast<int>(bits.size() / 8U);
    for (int i = data_end; i < kDataCodewords; ++i) {
        data[static_cast<std::size_t>(i)] = ((i - data_end) & 1) == 0 ? 0xecU : 0x11U;
    }

    std::array<std::uint8_t, kDataCodewords + kEccCodewords> codewords{};
    for (int i = 0; i < kDataCodewords; ++i) codewords[static_cast<std::size_t>(i)] = data[static_cast<std::size_t>(i)];
    const auto ecc = Builder::compute_ecc(data);
    for (int i = 0; i < kEccCodewords; ++i)
        codewords[static_cast<std::size_t>(kDataCodewords + i)] = ecc[static_cast<std::size_t>(i)];

    Builder builder;
    builder.draw_function_patterns();
    constexpr int mask = 0;
    builder.draw_codewords(codewords, mask);
    builder.draw_format_information(mask);
    return builder.modules;
}

}  // namespace lilygo_ui
