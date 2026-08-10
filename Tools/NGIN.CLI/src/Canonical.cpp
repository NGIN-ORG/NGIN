#include "Canonical.hpp"

#include <array>
#include <bit>
#include <iomanip>
#include <sstream>
#include <type_traits>

namespace NGIN::CLI
{
    namespace
    {
        auto WriteEscaped(std::ostream &out, const std::string_view value) -> void
        {
            static constexpr char hex[] = "0123456789abcdef";
            out << '"';
            for (const auto ch : value)
            {
                const auto byte = static_cast<unsigned char>(ch);
                switch (ch)
                {
                case '"': out << "\\\""; break;
                case '\\': out << "\\\\"; break;
                case '\b': out << "\\b"; break;
                case '\f': out << "\\f"; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                default:
                    if (byte < 0x20)
                    {
                        out << "\\u00" << hex[byte >> 4U] << hex[byte & 0x0fU];
                    }
                    else
                    {
                        out << ch;
                    }
                    break;
                }
            }
            out << '"';
        }

        auto WriteValue(std::ostream &out, const CanonicalValue &value) -> void
        {
            std::visit(
                [&](const auto &item) {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<T, std::nullptr_t>)
                    {
                        out << "null";
                    }
                    else if constexpr (std::is_same_v<T, bool>)
                    {
                        out << (item ? "true" : "false");
                    }
                    else if constexpr (std::is_same_v<T, std::int64_t>)
                    {
                        out << item;
                    }
                    else if constexpr (std::is_same_v<T, std::string>)
                    {
                        WriteEscaped(out, item);
                    }
                    else if constexpr (std::is_same_v<T, CanonicalValue::Array>)
                    {
                        out << '[';
                        for (std::size_t index = 0; index < item.size(); ++index)
                        {
                            if (index != 0) out << ',';
                            WriteValue(out, item[index]);
                        }
                        out << ']';
                    }
                    else
                    {
                        out << '{';
                        std::size_t index = 0;
                        for (const auto &[key, child] : item)
                        {
                            if (index++ != 0) out << ',';
                            WriteEscaped(out, key);
                            out << ':';
                            WriteValue(out, child);
                        }
                        out << '}';
                    }
                },
                value.value);
        }
    }

    auto SerializeCanonical(const CanonicalValue &value) -> std::string
    {
        std::ostringstream out;
        WriteValue(out, value);
        return out.str();
    }

    auto CanonicalDigestInput(const std::string_view kind, const CanonicalValue::Object &fields) -> std::string
    {
        CanonicalValue::Object root{{"kind", std::string(kind)}, {"value", fields}};
        return SerializeCanonical(root);
    }

    auto Sha256Fingerprint(const std::string_view value) -> std::string
    {
        static constexpr std::array<std::uint32_t, 64> constants{
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
            0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
            0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
            0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
            0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
            0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
            0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
            0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
            0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};
        std::vector<std::uint8_t> message(value.begin(), value.end());
        const auto bitLength = static_cast<std::uint64_t>(message.size()) * 8U;
        message.push_back(0x80U);
        while ((message.size() % 64U) != 56U) message.push_back(0U);
        for (int shift = 56; shift >= 0; shift -= 8)
            message.push_back(static_cast<std::uint8_t>((bitLength >> shift) & 0xffU));

        std::array<std::uint32_t, 8> state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                           0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
        for (std::size_t offset = 0; offset < message.size(); offset += 64U)
        {
            std::array<std::uint32_t, 64> words{};
            for (std::size_t index = 0; index < 16U; ++index)
            {
                const auto at = offset + index * 4U;
                words[index] = (static_cast<std::uint32_t>(message[at]) << 24U) |
                               (static_cast<std::uint32_t>(message[at + 1U]) << 16U) |
                               (static_cast<std::uint32_t>(message[at + 2U]) << 8U) |
                               static_cast<std::uint32_t>(message[at + 3U]);
            }
            for (std::size_t index = 16U; index < words.size(); ++index)
            {
                const auto s0 = std::rotr(words[index - 15U], 7) ^ std::rotr(words[index - 15U], 18) ^
                                (words[index - 15U] >> 3U);
                const auto s1 = std::rotr(words[index - 2U], 17) ^ std::rotr(words[index - 2U], 19) ^
                                (words[index - 2U] >> 10U);
                words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
            }
            auto [a, b, c, d, e, f, g, h] = state;
            for (std::size_t index = 0; index < words.size(); ++index)
            {
                const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
                const auto choice = (e & f) ^ (~e & g);
                const auto temporary1 = h + sum1 + choice + constants[index] + words[index];
                const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
                const auto majority = (a & b) ^ (a & c) ^ (b & c);
                const auto temporary2 = sum0 + majority;
                h = g; g = f; f = e; e = d + temporary1;
                d = c; c = b; b = a; a = temporary1 + temporary2;
            }
            state[0] += a; state[1] += b; state[2] += c; state[3] += d;
            state[4] += e; state[5] += f; state[6] += g; state[7] += h;
        }
        std::ostringstream out;
        out << "sha256:" << std::hex << std::setfill('0');
        for (const auto word : state) out << std::setw(8) << word;
        return out.str();
    }

    auto CanonicalFingerprint(const std::string_view kind, const CanonicalValue::Object &fields) -> std::string
    {
        return Sha256Fingerprint(CanonicalDigestInput(kind, fields));
    }
}
