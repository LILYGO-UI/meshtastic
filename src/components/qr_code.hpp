#ifndef LILYGO_UI_MESHTASTIC_QR_CODE_HPP
#define LILYGO_UI_MESHTASTIC_QR_CODE_HPP

#include <array>
#include <cstddef>
#include <string_view>

namespace lilygo_ui {

// Version 1-L is sufficient for a Meshtastic node id ("!" followed by eight
// hexadecimal digits), while keeping the renderer small enough for the
// embedded target.  The returned matrix contains only the 21x21 QR modules;
// callers must still draw a quiet zone around it.
constexpr std::size_t kQrCodeSize = 21;
using QrCodeMatrix = std::array<bool, kQrCodeSize * kQrCodeSize>;

// Encodes an arbitrary short UTF-8 byte string in QR byte mode.  Version 1-L
// has room for at most 17 bytes.  An empty or oversized payload returns an
// all-white matrix so the UI can safely display an unavailable value.
QrCodeMatrix qr_code_matrix(std::string_view payload);

}  // namespace lilygo_ui

#endif
