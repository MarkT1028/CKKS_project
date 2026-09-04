#ifndef CKKS_RNS_VECTOR_FILE_HPP
#define CKKS_RNS_VECTOR_FILE_HPP

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ckks_vector {

// CKKS RNS vector file v1. All integers are little-endian. The fixed header is
// followed by four ring_dim-element uint64_t arrays: a, b, acc, expected.
static constexpr std::uint32_t kVersion = 1;
static constexpr std::uint32_t kHeaderBytes = 64;
static constexpr std::uint32_t kWordBits = 64;
static constexpr std::uint32_t kEvaluationDomain = 1;

struct Metadata {
    std::uint32_t ring_dim = 0;
    std::uint32_t word_bits = kWordBits;
    std::uint32_t limb_bits = 0;
    std::uint32_t domain = kEvaluationDomain;
    std::uint32_t tower_index = 0;
    std::uint32_t lhs_component = 0;
    std::uint32_t rhs_component = 0;
    std::uint32_t flags = 0;
    std::uint64_t modulus = 0;
    std::uint64_t barrett_mu = 0;
};

struct Record {
    Metadata metadata;
    std::vector<std::uint64_t> a;
    std::vector<std::uint64_t> b;
    std::vector<std::uint64_t> acc;
    std::vector<std::uint64_t> expected;
};

inline void write_u32(std::ostream& output, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        output.put(static_cast<char>((value >> shift) & 0xffU));
    }
}

inline void write_u64(std::ostream& output, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        output.put(static_cast<char>((value >> shift) & 0xffULL));
    }
}

inline std::uint32_t read_u32(std::istream& input) {
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        const int byte = input.get();
        if (byte == std::char_traits<char>::eof()) {
            throw std::runtime_error("unexpected end of vector file");
        }
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(byte))
                 << shift;
    }
    return value;
}

inline std::uint64_t read_u64(std::istream& input) {
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        const int byte = input.get();
        if (byte == std::char_traits<char>::eof()) {
            throw std::runtime_error("unexpected end of vector file");
        }
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(byte))
                 << shift;
    }
    return value;
}

inline void validate_record(const Record& record) {
    const std::size_t n = record.metadata.ring_dim;
    if (n == 0 || record.metadata.word_bits != kWordBits) {
        throw std::runtime_error("invalid ring dimension or word width");
    }
    if (record.metadata.domain != kEvaluationDomain) {
        throw std::runtime_error(
            "vector is not in OpenFHE EVALUATION/NTT domain");
    }
    if (record.metadata.modulus == 0 || record.metadata.barrett_mu == 0) {
        throw std::runtime_error("missing modulus or Barrett constant");
    }
    if (record.a.size() != n || record.b.size() != n ||
        record.acc.size() != n || record.expected.size() != n) {
        throw std::runtime_error("vector array length does not match ring_dim");
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (record.a[i] >= record.metadata.modulus ||
            record.b[i] >= record.metadata.modulus ||
            record.acc[i] >= record.metadata.modulus ||
            record.expected[i] >= record.metadata.modulus) {
            throw std::runtime_error("residue is outside [0, modulus)");
        }
    }
}

inline void write_file(const std::string& path, const Record& record) {
    validate_record(record);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create vector file: " + path);
    }

    static const char magic[8] = {'C', 'K', 'K', 'S', 'R', 'N', 'S', '1'};
    output.write(magic, sizeof(magic));
    write_u32(output, kVersion);
    write_u32(output, kHeaderBytes);
    write_u32(output, record.metadata.ring_dim);
    write_u32(output, record.metadata.word_bits);
    write_u32(output, record.metadata.limb_bits);
    write_u32(output, record.metadata.domain);
    write_u32(output, record.metadata.tower_index);
    write_u32(output, record.metadata.lhs_component);
    write_u32(output, record.metadata.rhs_component);
    write_u32(output, record.metadata.flags);
    write_u64(output, record.metadata.modulus);
    write_u64(output, record.metadata.barrett_mu);

    const std::vector<std::uint64_t>* arrays[] = {
        &record.a, &record.b, &record.acc, &record.expected};
    for (const auto* array : arrays) {
        for (const std::uint64_t value : *array) {
            write_u64(output, value);
        }
    }
    if (!output) {
        throw std::runtime_error("failed while writing vector file: " + path);
    }
}

inline Record read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open vector file: " + path);
    }

    char magic[8] = {};
    input.read(magic, sizeof(magic));
    static const char expected_magic[8] = {
        'C', 'K', 'K', 'S', 'R', 'N', 'S', '1'};
    for (unsigned i = 0; i < sizeof(magic); ++i) {
        if (magic[i] != expected_magic[i]) {
            throw std::runtime_error("invalid CKKS RNS vector magic");
        }
    }

    const std::uint32_t version = read_u32(input);
    const std::uint32_t header_bytes = read_u32(input);
    if (version != kVersion || header_bytes != kHeaderBytes) {
        throw std::runtime_error("unsupported CKKS RNS vector version");
    }

    Record record;
    record.metadata.ring_dim = read_u32(input);
    record.metadata.word_bits = read_u32(input);
    record.metadata.limb_bits = read_u32(input);
    record.metadata.domain = read_u32(input);
    record.metadata.tower_index = read_u32(input);
    record.metadata.lhs_component = read_u32(input);
    record.metadata.rhs_component = read_u32(input);
    record.metadata.flags = read_u32(input);
    record.metadata.modulus = read_u64(input);
    record.metadata.barrett_mu = read_u64(input);

    const std::uint64_t expected_bytes =
        kHeaderBytes + 4ULL * record.metadata.ring_dim *
                           sizeof(std::uint64_t);
    input.seekg(0, std::ios::end);
    const std::streamoff actual_bytes = input.tellg();
    if (actual_bytes < 0 ||
        static_cast<std::uint64_t>(actual_bytes) != expected_bytes) {
        throw std::runtime_error("vector file size does not match its header");
    }
    input.seekg(kHeaderBytes, std::ios::beg);

    std::vector<std::uint64_t>* arrays[] = {
        &record.a, &record.b, &record.acc, &record.expected};
    for (auto* array : arrays) {
        array->resize(record.metadata.ring_dim);
        for (auto& value : *array) {
            value = read_u64(input);
        }
    }
    validate_record(record);
    return record;
}

}  // namespace ckks_vector

#endif
