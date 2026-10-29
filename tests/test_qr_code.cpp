#include "components/qr_code.hpp"

#include <cassert>

namespace {

bool at(const lilygo_ui::QrCodeMatrix &matrix, int x, int y)
{
    return matrix[static_cast<std::size_t>(y * static_cast<int>(lilygo_ui::kQrCodeSize) + x)];
}

void assert_finder(const lilygo_ui::QrCodeMatrix &matrix, int left, int top)
{
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            const bool expected = x == 0 || x == 6 || y == 0 || y == 6 ||
                                  (x >= 2 && x <= 4 && y >= 2 && y <= 4);
            assert(at(matrix, left + x, top + y) == expected);
        }
    }
}

}  // namespace

int main()
{
    const auto matrix = lilygo_ui::qr_code_matrix("!12345678");
    assert_finder(matrix, 0, 0);
    assert_finder(matrix, 14, 0);
    assert_finder(matrix, 0, 14);
    assert(at(matrix, 13, 8));  // fixed dark format module

    const auto empty = lilygo_ui::qr_code_matrix("");
    for (const bool module : empty) assert(!module);
    return 0;
}
