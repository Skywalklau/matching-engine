#pragma once

#include "order.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

// ---------------------------------------------------------------------------
// Nasdaq ITCH 5.0 Binary Protocol Parser
//
// Wire format: each message is preceded by a 2-byte big-endian length field.
// The length counts the message bytes (including type byte) but NOT itself.
// All multi-byte fields are big-endian — converted to host byte order here.
//
// Supported message types:
//   'A' (0x41) Add Order
//   'F' (0x46) Add Order with MPID  (same fields; MPID is ignored)
//   'D' (0x44) Delete Order
//   'X' (0x58) Order Cancel (partial cancel)
//   'E' (0x45) Order Executed
//   'U' (0x55) Order Replace
//
// All other message types are silently skipped.
// ---------------------------------------------------------------------------

struct AddOrderMessage {
    uint64_t order_ref;
    uint64_t timestamp_ns;
    uint32_t shares;
    uint32_t price;
    char     stock[9];
    Side     side;
};

struct DeleteOrderMessage {
    uint64_t order_ref;
    uint64_t timestamp_ns;
};

struct CancelOrderMessage {
    uint64_t order_ref;
    uint64_t timestamp_ns;
    uint32_t cancelled_shares;
};

struct ExecutedOrderMessage {
    uint64_t order_ref;
    uint64_t timestamp_ns;
    uint64_t match_number;
    uint32_t executed_shares;
};

struct ReplaceOrderMessage {
    uint64_t original_order_ref;
    uint64_t new_order_ref;
    uint64_t timestamp_ns;
    uint32_t shares;
    uint32_t price;
};

using ITCHMessage = std::variant<
    AddOrderMessage,
    DeleteOrderMessage,
    CancelOrderMessage,
    ExecutedOrderMessage,
    ReplaceOrderMessage
>;

// ---------------------------------------------------------------------------
// ITCHParser
// ---------------------------------------------------------------------------

class ITCHParser {
public:
    // ---------------------------------------------------------------------------
    // parse() — in-memory parse.
    // Parses all messages from a raw binary buffer already in memory.
    // Only suitable for small buffers. For large files use parseFile().
    // ---------------------------------------------------------------------------
    std::vector<ITCHMessage> parse(const uint8_t* data, size_t len) const;

    // ---------------------------------------------------------------------------
    // parseFile() — streaming parse. Reads one message at a time.
    // Memory usage is O(1) regardless of file size.
    // Calls callback(msg) for each successfully parsed message.
    // Unknown message types are silently skipped.
    //
    // Template parameter avoids std::function overhead on the hot path.
    // ---------------------------------------------------------------------------
    template <typename Callback>
    void parseFile(const std::string& path, Callback&& callback) const {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + path);
        }

        // Stack buffer — large enough for any ITCH 5.0 message.
        // The largest defined message type is ~80 bytes; 512 is generous.
        uint8_t buf[512];
        uint8_t len_bytes[2];

        while (file.read(reinterpret_cast<char*>(len_bytes), 2)) {
            const uint16_t msg_len =
                static_cast<uint16_t>(
                    (static_cast<uint32_t>(len_bytes[0]) << 8u) |
                     static_cast<uint32_t>(len_bytes[1]));

            if (msg_len == 0u) continue;

            // Skip messages larger than our buffer (future spec extensions).
            if (static_cast<size_t>(msg_len) > sizeof(buf)) {
                file.seekg(static_cast<std::streamoff>(msg_len),
                           std::ios::cur);
                continue;
            }

            if (!file.read(reinterpret_cast<char*>(buf),
                           static_cast<std::streamsize>(msg_len))) break;

            const uint8_t  type     = buf[0];
            const uint8_t* body     = buf + 1u;
            const size_t   body_len = static_cast<size_t>(msg_len) - 1u;

            auto result = parseOne(type, body, body_len);
            if (result.has_value()) {
                callback(std::move(*result));
            }
        }
    }

    // Convert an AddOrderMessage to an Order for the matching engine.
    static Order toOrder(const AddOrderMessage& msg) noexcept;

    // Parse a single message given its type byte and body.
    // Public so callers can build custom streaming pipelines.
    static std::optional<ITCHMessage> parseOne(uint8_t        type,
                                               const uint8_t* body,
                                               size_t         body_len);

private:
    static uint16_t readU16(const uint8_t* p) noexcept {
        return static_cast<uint16_t>(
            (static_cast<uint32_t>(p[0]) << 8u) |
             static_cast<uint32_t>(p[1]));
    }

    static uint32_t readU32(const uint8_t* p) noexcept {
        return (static_cast<uint32_t>(p[0]) << 24u) |
               (static_cast<uint32_t>(p[1]) << 16u) |
               (static_cast<uint32_t>(p[2]) <<  8u) |
                static_cast<uint32_t>(p[3]);
    }

    static uint64_t readU48(const uint8_t* p) noexcept {
        return (static_cast<uint64_t>(p[0]) << 40u) |
               (static_cast<uint64_t>(p[1]) << 32u) |
               (static_cast<uint64_t>(p[2]) << 24u) |
               (static_cast<uint64_t>(p[3]) << 16u) |
               (static_cast<uint64_t>(p[4]) <<  8u) |
                static_cast<uint64_t>(p[5]);
    }

    static uint64_t readU64(const uint8_t* p) noexcept {
        return (static_cast<uint64_t>(p[0]) << 56u) |
               (static_cast<uint64_t>(p[1]) << 48u) |
               (static_cast<uint64_t>(p[2]) << 40u) |
               (static_cast<uint64_t>(p[3]) << 32u) |
               (static_cast<uint64_t>(p[4]) << 24u) |
               (static_cast<uint64_t>(p[5]) << 16u) |
               (static_cast<uint64_t>(p[6]) <<  8u) |
                static_cast<uint64_t>(p[7]);
    }
};