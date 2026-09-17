#include "openfhe.h"
#include "version.h"

#include "ckks_ciphertext_mult_vector_file.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace lbcrypto;

namespace {

constexpr std::uint32_t kRingDim = 8192;
constexpr std::uint32_t kBatchSize = kRingDim / 2;
constexpr std::uint32_t kScalingModBits = 55;
constexpr std::uint32_t kFirstModBits = 60;
constexpr std::uint32_t kMultiplicativeDepth = 2;

struct Options {
    std::filesystem::path output =
        std::filesystem::path("vectors") /
        "openfhe_ciphertext_mult.bin";
};

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--output" && i + 1 < argc) {
            options.output = argv[++i];
        } else if (argument == "--help") {
            std::cout
                << "Usage: openfhe_export_ckks_mult [--output FILE]\n"
                << "Exports every active RNS tower and all c0/c1 -> "
                   "d0/d1/d2 components.\n";
            std::exit(0);
        } else {
            throw std::runtime_error(
                "unknown or incomplete argument: " + argument);
        }
    }
    return options;
}

unsigned bit_width(std::uint64_t value) {
    unsigned bits = 0;
    while (value != 0) {
        ++bits;
        value >>= 1;
    }
    return bits;
}

unsigned __int128 reciprocal_2pow128(std::uint64_t modulus) {
    if (modulus <= 1 || (modulus & 1ULL) == 0) {
        throw std::runtime_error(
            "RNS modulus must be an odd integer greater than one");
    }
    // For odd q, q cannot divide 2^128.  Therefore this quotient is exactly
    // floor(2^128/q), while its numerator still fits in unsigned __int128.
    const unsigned __int128 maximum =
        ~static_cast<unsigned __int128>(0);
    return maximum / modulus;
}

std::uint64_t multiply_mod(std::uint64_t lhs,
                           std::uint64_t rhs,
                           std::uint64_t modulus) {
    const unsigned __int128 product =
        static_cast<unsigned __int128>(lhs) * rhs;
    return static_cast<std::uint64_t>(product % modulus);
}

std::uint64_t add_mod(std::uint64_t lhs,
                      std::uint64_t rhs,
                      std::uint64_t modulus) {
    std::uint64_t sum = lhs + rhs;
    if (sum >= modulus) {
        sum -= modulus;
    }
    return sum;
}

void validate_poly_layout(const DCRTPoly& poly,
                          std::size_t tower_count,
                          const std::string& label) {
    if (poly.GetFormat() != Format::EVALUATION) {
        throw std::runtime_error(label +
                                 " is not in EVALUATION/NTT format");
    }
    if (poly.GetNumOfElements() != tower_count) {
        throw std::runtime_error(label + " has a different tower count");
    }
    for (std::size_t tower = 0; tower < tower_count; ++tower) {
        if (poly.GetElementAtIndex(tower).GetValues().GetLength() !=
            kRingDim) {
            throw std::runtime_error(label + " has a non-8192 tower");
        }
    }
}

void write_json(const std::filesystem::path& path,
                const ckks_ct_mult_vector::Record& record) {
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
            "cannot create metadata file: " + path.string());
    }

    output << "{\n"
           << "  \"format_version\": "
           << ckks_ct_mult_vector::kVersion << ",\n"
           << "  \"source\": \"OpenFHE CKKS ciphertexts\",\n"
           << "  \"openfhe_version\": \"" << GetOPENFHEVersion()
           << "\",\n"
           << "  \"ring_dim\": " << record.metadata.ring_dim << ",\n"
           << "  \"tower_count\": " << record.metadata.tower_count
           << ",\n"
           << "  \"word_bits\": " << record.metadata.word_bits << ",\n"
           << "  \"domain\": \"EVALUATION\",\n"
           << "  \"input_components\": 2,\n"
           << "  \"output_components\": 3,\n"
           << "  \"payload_order\": [\"lhs_c0\", \"lhs_c1\", "
              "\"rhs_c0\", \"rhs_c1\", \"expected_d0\", "
              "\"expected_d1\", \"expected_d2\"],\n"
           << "  \"layout\": \"tower-major\",\n"
           << "  \"byte_order\": \"little-endian\",\n"
           << "  \"contains_plaintext\": false,\n"
           << "  \"contains_secret_key\": false,\n"
           << "  \"towers\": [\n";

    for (std::size_t tower = 0; tower < record.towers.size(); ++tower) {
        const auto& parameter = record.towers[tower];
        output << "    {\"index\": " << tower
               << ", \"modulus\": " << parameter.modulus
               << ", \"modulus_bits\": "
               << bit_width(parameter.modulus)
               << ", \"reciprocal_low\": "
               << parameter.reciprocal_low
               << ", \"reciprocal_high\": "
               << parameter.reciprocal_high << "}";
        if (tower + 1 != record.towers.size()) {
            output << ',';
        }
        output << '\n';
    }
    output << "  ]\n}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);

        CCParams<CryptoContextCKKSRNS> parameters;
        parameters.SetMultiplicativeDepth(kMultiplicativeDepth);
        parameters.SetScalingModSize(kScalingModBits);
        parameters.SetFirstModSize(kFirstModBits);
        parameters.SetScalingTechnique(FLEXIBLEAUTO);
        parameters.SetSecurityLevel(HEStd_NotSet);
        parameters.SetRingDim(kRingDim);
        parameters.SetBatchSize(kBatchSize);

        CryptoContext<DCRTPoly> context = GenCryptoContext(parameters);
        context->Enable(PKE);
        context->Enable(KEYSWITCH);
        context->Enable(LEVELEDSHE);

        const KeyPair<DCRTPoly> key_pair = context->KeyGen();
        if (!key_pair.good()) {
            throw std::runtime_error("OpenFHE key generation failed");
        }

        const std::vector<double> lhs_slots = {
            0.25, -1.5, 2.0, 3.125, -4.0, 0.03125, 7.0, -8.5};
        const std::vector<double> rhs_slots = {
            2.0, 0.5, -3.0, 4.0, 0.25, -2.0, 1.5, 0.125};
        const Plaintext lhs_plaintext =
            context->MakeCKKSPackedPlaintext(lhs_slots);
        const Plaintext rhs_plaintext =
            context->MakeCKKSPackedPlaintext(rhs_slots);
        const Ciphertext<DCRTPoly> lhs_ciphertext =
            context->Encrypt(key_pair.publicKey, lhs_plaintext);
        const Ciphertext<DCRTPoly> rhs_ciphertext =
            context->Encrypt(key_pair.publicKey, rhs_plaintext);

        const auto& lhs_elements = lhs_ciphertext->GetElements();
        const auto& rhs_elements = rhs_ciphertext->GetElements();
        if (lhs_elements.size() != 2 || rhs_elements.size() != 2) {
            throw std::runtime_error(
                "fresh CKKS ciphertext is not two-component");
        }

        // Stop before relinearization and rescaling.  The result is the exact
        // d0/d1/d2 component expansion consumed by the accelerator.
        const Ciphertext<DCRTPoly> raw_product =
            context->EvalMultNoRelinNoCheck(lhs_ciphertext, rhs_ciphertext);
        const auto& product_elements = raw_product->GetElements();
        if (product_elements.size() != 3) {
            throw std::runtime_error(
                "OpenFHE raw product is not three-component");
        }

        const std::size_t tower_count =
            lhs_elements.at(0).GetNumOfElements();
        if (tower_count == 0 ||
            tower_count > ckks_ct_mult_vector::kMaximumTowers) {
            throw std::runtime_error(
                "OpenFHE tower count is outside accelerator limits");
        }
        validate_poly_layout(lhs_elements.at(0), tower_count, "lhs c0");
        validate_poly_layout(lhs_elements.at(1), tower_count, "lhs c1");
        validate_poly_layout(rhs_elements.at(0), tower_count, "rhs c0");
        validate_poly_layout(rhs_elements.at(1), tower_count, "rhs c1");
        validate_poly_layout(product_elements.at(0), tower_count, "d0");
        validate_poly_layout(product_elements.at(1), tower_count, "d1");
        validate_poly_layout(product_elements.at(2), tower_count, "d2");

        ckks_ct_mult_vector::Record record;
        record.metadata.ring_dim = kRingDim;
        record.metadata.tower_count =
            static_cast<std::uint32_t>(tower_count);
        record.towers.resize(tower_count);
        const std::size_t word_count = tower_count * kRingDim;
        record.lhs_c0.resize(word_count);
        record.lhs_c1.resize(word_count);
        record.rhs_c0.resize(word_count);
        record.rhs_c1.resize(word_count);
        record.expected_d0.resize(word_count);
        record.expected_d1.resize(word_count);
        record.expected_d2.resize(word_count);

        for (std::size_t tower = 0; tower < tower_count; ++tower) {
            const auto& lhs_c0_tower =
                lhs_elements.at(0).GetElementAtIndex(tower);
            const auto& lhs_c1_tower =
                lhs_elements.at(1).GetElementAtIndex(tower);
            const auto& rhs_c0_tower =
                rhs_elements.at(0).GetElementAtIndex(tower);
            const auto& rhs_c1_tower =
                rhs_elements.at(1).GetElementAtIndex(tower);
            const auto& d0_tower =
                product_elements.at(0).GetElementAtIndex(tower);
            const auto& d1_tower =
                product_elements.at(1).GetElementAtIndex(tower);
            const auto& d2_tower =
                product_elements.at(2).GetElementAtIndex(tower);

            const std::uint64_t q =
                lhs_c0_tower.GetModulus().ConvertToInt<std::uint64_t>();
            if (q < (1ULL << 48) || q >= (1ULL << 60)) {
                throw std::runtime_error(
                    "OpenFHE modulus is outside [2^48,2^60)");
            }
            const DCRTPoly* polys[] = {
                &lhs_elements.at(1), &rhs_elements.at(0),
                &rhs_elements.at(1), &product_elements.at(0),
                &product_elements.at(1), &product_elements.at(2)};
            for (const DCRTPoly* poly : polys) {
                const std::uint64_t other_q =
                    poly->GetElementAtIndex(tower)
                        .GetModulus()
                        .ConvertToInt<std::uint64_t>();
                if (other_q != q) {
                    throw std::runtime_error(
                        "component modulus mismatch at tower " +
                        std::to_string(tower));
                }
            }

            const unsigned __int128 reciprocal = reciprocal_2pow128(q);
            record.towers[tower].modulus = q;
            record.towers[tower].reciprocal_low =
                static_cast<std::uint64_t>(reciprocal);
            record.towers[tower].reciprocal_high =
                static_cast<std::uint64_t>(reciprocal >> 64);

            const auto& lhs_c0_values = lhs_c0_tower.GetValues();
            const auto& lhs_c1_values = lhs_c1_tower.GetValues();
            const auto& rhs_c0_values = rhs_c0_tower.GetValues();
            const auto& rhs_c1_values = rhs_c1_tower.GetValues();
            const auto& d0_values = d0_tower.GetValues();
            const auto& d1_values = d1_tower.GetValues();
            const auto& d2_values = d2_tower.GetValues();

            const std::size_t base = tower * kRingDim;
            for (std::size_t coefficient = 0;
                 coefficient < kRingDim; ++coefficient) {
                const std::size_t index = base + coefficient;
                const std::uint64_t a0 = lhs_c0_values[coefficient]
                    .ConvertToInt<std::uint64_t>();
                const std::uint64_t a1 = lhs_c1_values[coefficient]
                    .ConvertToInt<std::uint64_t>();
                const std::uint64_t b0 = rhs_c0_values[coefficient]
                    .ConvertToInt<std::uint64_t>();
                const std::uint64_t b1 = rhs_c1_values[coefficient]
                    .ConvertToInt<std::uint64_t>();
                const std::uint64_t d0 = d0_values[coefficient]
                    .ConvertToInt<std::uint64_t>();
                const std::uint64_t d1 = d1_values[coefficient]
                    .ConvertToInt<std::uint64_t>();
                const std::uint64_t d2 = d2_values[coefficient]
                    .ConvertToInt<std::uint64_t>();

                const std::uint64_t reference_d0 = multiply_mod(a0, b0, q);
                const std::uint64_t reference_d1 = add_mod(
                    multiply_mod(a0, b1, q),
                    multiply_mod(a1, b0, q), q);
                const std::uint64_t reference_d2 = multiply_mod(a1, b1, q);
                if (d0 != reference_d0 || d1 != reference_d1 ||
                    d2 != reference_d2) {
                    throw std::runtime_error(
                        "OpenFHE raw product mismatch at tower " +
                        std::to_string(tower) + " coefficient " +
                        std::to_string(coefficient));
                }

                record.lhs_c0[index] = a0;
                record.lhs_c1[index] = a1;
                record.rhs_c0[index] = b0;
                record.rhs_c1[index] = b1;
                record.expected_d0[index] = d0;
                record.expected_d1[index] = d1;
                record.expected_d2[index] = d2;
            }
        }

        if (!options.output.parent_path().empty()) {
            std::filesystem::create_directories(
                options.output.parent_path());
        }
        ckks_ct_mult_vector::write_file(options.output.string(), record);
        std::filesystem::path json_path = options.output;
        json_path.replace_extension(".json");
        write_json(json_path, record);

        std::cout
            << "OpenFHE " << GetOPENFHEVersion()
            << " complete CKKS ciphertext export finished\n"
            << "  output: " << options.output << '\n'
            << "  metadata: " << json_path << '\n'
            << "  ring dimension: " << kRingDim << '\n'
            << "  towers: " << tower_count << '\n'
            << "  residues per component: " << word_count << '\n'
            << "  payload: lhs(c0,c1), rhs(c0,c1), expected(d0,d1,d2)\n"
            << "  plaintext/secret key serialized: no\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
