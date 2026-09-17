#ifndef CKKS_CIPHERTEXT_MULT_VECTOR_FILE_HPP
#define CKKS_CIPHERTEXT_MULT_VECTOR_FILE_HPP

#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace ckks_ct_mult_vector {

// Complete CKKS ciphertext-multiply vector file v2.
//
// All integers are little-endian.  The 64-byte fixed header is followed by
// one 32-byte public-parameter record per RNS tower and then seven
// tower-major uint64_t arrays:
//
//   lhs_c0, lhs_c1, rhs_c0, rhs_c1,
//   expected_d0, expected_d1, expected_d2
//
// A linear array index is tower * ring_dim + coefficient.  The file never
// contains plaintext, a secret key, or a decryption key.
static constexpr std::uint32_t kVersion = 2;
static constexpr std::uint32_t kHeaderBytes = 64;
static constexpr std::uint32_t kTowerRecordBytes = 32;
static constexpr std::uint32_t kWordBits = 64;
static constexpr std::uint32_t kEvaluationDomain = 1;
static constexpr std::uint32_t kInputComponents = 2;
static constexpr std::uint32_t kOutputComponents = 3;
static constexpr std::uint32_t kPayloadArrayCount = 7;
static constexpr std::uint32_t kMaximumTowers = 16;

struct TowerParameter {
    std::uint64_t modulus = 0;
    std::uint64_t reciprocal_low = 0;
    std::uint64_t reciprocal_high = 0;
};

struct Metadata {
    std::uint32_t ring_dim = 0;
    std::uint32_t tower_count = 0;
    std::uint32_t word_bits = kWordBits;
    std::uint32_t domain = kEvaluationDomain;
    std::uint32_t input_components = kInputComponents;
    std::uint32_t output_components = kOutputComponents;
    std::uint32_t flags = 0;
};

struct Record {
    Metadata metadata;
    std::vector<TowerParameter> towers;
    std::vector<std::uint64_t> lhs_c0;
    std::vector<std::uint64_t> lhs_c1;
    std::vector<std::uint64_t> rhs_c0;
    std::vector<std::uint64_t> rhs_c1;
    std::vector<std::uint64_t> expected_d0;
    std::vector<std::uint64_t> expected_d1;
    std::vector<std::uint64_t> expected_d2;
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
            throw std::runtime_error("unexpected end of ciphertext vector file");
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
            throw std::runtime_error("unexpected end of ciphertext vector file");
        }
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(byte))
                 << shift;
    }
    return value;
}

inline std::size_t checked_word_count(const Metadata& metadata) {
    if (metadata.ring_dim == 0 || metadata.tower_count == 0 ||
        metadata.tower_count > kMaximumTowers) {
        throw std::runtime_error("invalid ring dimension or RNS tower count");
    }
    const std::uint64_t words =
        static_cast<std::uint64_t>(metadata.ring_dim) * metadata.tower_count;
    if (words > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("ciphertext vector is too large for this host");
    }
    return static_cast<std::size_t>(words);
}

inline void validate_record(const Record& record) {
    const Metadata& metadata = record.metadata;
    const std::size_t word_count = checked_word_count(metadata);
    if (metadata.word_bits != kWordBits ||
        metadata.domain != kEvaluationDomain ||
        metadata.input_components != kInputComponents ||
        metadata.output_components != kOutputComponents) {
        throw std::runtime_error("unsupported ciphertext vector metadata");
    }
    if (record.towers.size() != metadata.tower_count) {
        throw std::runtime_error("tower parameter count does not match header");
    }

    const std::vector<std::uint64_t>* arrays[] = {
        &record.lhs_c0, &record.lhs_c1, &record.rhs_c0, &record.rhs_c1,
        &record.expected_d0, &record.expected_d1, &record.expected_d2};
    for (const auto* array : arrays) {
        if (array->size() != word_count) {
            throw std::runtime_error(
                "ciphertext payload length does not match ring_dim*tower_count");
        }
    }

    for (std::uint32_t tower = 0; tower < metadata.tower_count; ++tower) {
        const TowerParameter& parameter = record.towers[tower];
        const std::uint64_t q = parameter.modulus;
        if (q < (1ULL << 48) || q >= (1ULL << 60) ||
            parameter.reciprocal_high > 0x1FFFFULL ||
            (parameter.reciprocal_low == 0 &&
             parameter.reciprocal_high == 0)) {
            throw std::runtime_error("unsupported RNS tower parameter");
        }
        const std::size_t base =
            static_cast<std::size_t>(tower) * metadata.ring_dim;
        for (std::uint32_t coefficient = 0;
             coefficient < metadata.ring_dim; ++coefficient) {
            const std::size_t index = base + coefficient;
            for (const auto* array : arrays) {
                if ((*array)[index] >= q) {
                    throw std::runtime_error(
                        "ciphertext residue is outside [0, modulus)");
                }
            }
        }
    }
}

inline void write_file(const std::string& path, const Record& record) {
    validate_record(record);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
            "cannot create ciphertext vector file: " + path);
    }

    static const char magic[8] = {'C', 'K', 'K', 'S', 'C', 'T', 'M', '2'};
    output.write(magic, sizeof(magic));
    write_u32(output, kVersion);
    write_u32(output, kHeaderBytes);
    write_u32(output, record.metadata.ring_dim);
    write_u32(output, record.metadata.tower_count);
    write_u32(output, record.metadata.word_bits);
    write_u32(output, record.metadata.domain);
    write_u32(output, record.metadata.input_components);
    write_u32(output, record.metadata.output_components);
    write_u32(output, record.metadata.flags);
    write_u32(output, 0U);
    write_u32(output, kPayloadArrayCount);
    write_u32(output, kTowerRecordBytes);
    write_u64(output, 0ULL);

    for (const TowerParameter& parameter : record.towers) {
        write_u64(output, parameter.modulus);
        write_u64(output, parameter.reciprocal_low);
        write_u64(output, parameter.reciprocal_high);
        write_u64(output, 0ULL);
    }

    const std::vector<std::uint64_t>* arrays[] = {
        &record.lhs_c0, &record.lhs_c1, &record.rhs_c0, &record.rhs_c1,
        &record.expected_d0, &record.expected_d1, &record.expected_d2};
    for (const auto* array : arrays) {
        for (const std::uint64_t value : *array) {
            write_u64(output, value);
        }
    }
    if (!output) {
        throw std::runtime_error(
            "failed while writing ciphertext vector file: " + path);
    }
}

inline Record read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "cannot open ciphertext vector file: " + path);
    }

    char magic[8] = {};
    input.read(magic, sizeof(magic));
    static const char expected_magic[8] = {
        'C', 'K', 'K', 'S', 'C', 'T', 'M', '2'};
    for (unsigned i = 0; i < sizeof(magic); ++i) {
        if (magic[i] != expected_magic[i]) {
            throw std::runtime_error("invalid ciphertext vector magic");
        }
    }

    const std::uint32_t version = read_u32(input);
    const std::uint32_t header_bytes = read_u32(input);
    if (version != kVersion || header_bytes != kHeaderBytes) {
        throw std::runtime_error("unsupported ciphertext vector version");
    }

    Record record;
    record.metadata.ring_dim = read_u32(input);
    record.metadata.tower_count = read_u32(input);
    record.metadata.word_bits = read_u32(input);
    record.metadata.domain = read_u32(input);
    record.metadata.input_components = read_u32(input);
    record.metadata.output_components = read_u32(input);
    record.metadata.flags = read_u32(input);
    (void)read_u32(input);
    const std::uint32_t payload_array_count = read_u32(input);
    const std::uint32_t tower_record_bytes = read_u32(input);
    (void)read_u64(input);
    if (payload_array_count != kPayloadArrayCount ||
        tower_record_bytes != kTowerRecordBytes) {
        throw std::runtime_error("unsupported ciphertext vector layout");
    }

    const std::size_t word_count = checked_word_count(record.metadata);
    const std::uint64_t expected_bytes =
        kHeaderBytes +
        static_cast<std::uint64_t>(record.metadata.tower_count) *
            kTowerRecordBytes +
        static_cast<std::uint64_t>(kPayloadArrayCount) * word_count *
            sizeof(std::uint64_t);
    input.seekg(0, std::ios::end);
    const std::streamoff actual_bytes = input.tellg();
    if (actual_bytes < 0 ||
        static_cast<std::uint64_t>(actual_bytes) != expected_bytes) {
        throw std::runtime_error(
            "ciphertext vector file size does not match its header");
    }
    input.seekg(kHeaderBytes, std::ios::beg);

    record.towers.resize(record.metadata.tower_count);
    for (TowerParameter& parameter : record.towers) {
        parameter.modulus = read_u64(input);
        parameter.reciprocal_low = read_u64(input);
        parameter.reciprocal_high = read_u64(input);
        (void)read_u64(input);
    }

    std::vector<std::uint64_t>* arrays[] = {
        &record.lhs_c0, &record.lhs_c1, &record.rhs_c0, &record.rhs_c1,
        &record.expected_d0, &record.expected_d1, &record.expected_d2};
    for (auto* array : arrays) {
        array->resize(word_count);
        for (std::uint64_t& value : *array) {
            value = read_u64(input);
        }
    }
    validate_record(record);
    return record;
}

}  // namespace ckks_ct_mult_vector

#endif
