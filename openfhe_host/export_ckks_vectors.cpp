#include "openfhe.h"
#include "version.h"

#include "ckks_rns_vector_file.hpp"

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
constexpr std::uint32_t kLimbBits = 55;
constexpr std::uint32_t kFirstModBits = 60;
constexpr std::uint32_t kMultDepth = 2;

struct Options {
    std::filesystem::path output =
        std::filesystem::path("vectors") / "openfhe_c0xc0_t55.bin";
    int tower_index = -1;
};

unsigned bit_width(std::uint64_t value) {
    unsigned bits = 0;
    while (value != 0) {
        ++bits;
        value >>= 1;
    }
    return bits;
}

std::uint64_t compute_barrett_mu(std::uint64_t modulus) {
    const unsigned __int128 numerator =
        static_cast<unsigned __int128>(1) << (2 * kLimbBits);
    return static_cast<std::uint64_t>(numerator / modulus);
}

std::uint64_t reference_mul(std::uint64_t lhs,
                            std::uint64_t rhs,
                            std::uint64_t modulus) {
    const unsigned __int128 product =
        static_cast<unsigned __int128>(lhs) * rhs;
    return static_cast<std::uint64_t>(product % modulus);
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--output" && i + 1 < argc) {
            options.output = argv[++i];
        } else if (argument == "--tower" && i + 1 < argc) {
            options.tower_index = std::stoi(argv[++i]);
        } else if (argument == "--help") {
            std::cout
                << "Usage: openfhe_export_ckks [--output FILE] [--tower INDEX]\n"
                << "Without --tower, the first 55-bit RNS tower is selected.\n";
            std::exit(0);
        } else {
            throw std::runtime_error(
                "unknown or incomplete argument: " + argument);
        }
    }
    return options;
}

std::size_t choose_tower(const DCRTPoly& poly, int requested_index) {
    const std::size_t tower_count = poly.GetNumOfElements();
    std::cout << "Available RNS towers:\n";
    for (std::size_t i = 0; i < tower_count; ++i) {
        const std::uint64_t modulus = poly.GetElementAtIndex(i)
                                          .GetModulus()
                                          .ConvertToInt<std::uint64_t>();
        std::cout << "  tower " << i << ": q=" << modulus
                  << " (" << bit_width(modulus) << " bits)\n";
    }

    if (requested_index >= 0) {
        const std::size_t index =
            static_cast<std::size_t>(requested_index);
        if (index >= tower_count) {
            throw std::runtime_error(
                "requested tower index is out of range");
        }
        const std::uint64_t modulus = poly.GetElementAtIndex(index)
                                          .GetModulus()
                                          .ConvertToInt<std::uint64_t>();
        if (bit_width(modulus) != kLimbBits) {
            throw std::runtime_error("requested tower is not 55-bit");
        }
        return index;
    }

    for (std::size_t i = 0; i < tower_count; ++i) {
        const std::uint64_t modulus = poly.GetElementAtIndex(i)
                                          .GetModulus()
                                          .ConvertToInt<std::uint64_t>();
        if (bit_width(modulus) == kLimbBits) {
            return i;
        }
    }
    throw std::runtime_error("OpenFHE context has no 55-bit RNS tower");
}

void write_json(const std::filesystem::path& path,
                const ckks_vector::Record& record,
                std::size_t ciphertext_components,
                std::size_t tower_count) {
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
            "cannot create metadata file: " + path.string());
    }
    const auto& metadata = record.metadata;
    output << "{\n"
           << "  \"format_version\": " << ckks_vector::kVersion << ",\n"
           << "  \"source\": \"OpenFHE CKKS ciphertext\",\n"
           << "  \"openfhe_version\": \"" << GetOPENFHEVersion()
           << "\",\n"
           << "  \"ring_dim\": " << metadata.ring_dim << ",\n"
           << "  \"word_bits\": " << metadata.word_bits << ",\n"
           << "  \"limb_bits\": " << metadata.limb_bits << ",\n"
           << "  \"domain\": \"EVALUATION\",\n"
           << "  \"tower_index\": " << metadata.tower_index << ",\n"
           << "  \"tower_count\": " << tower_count << ",\n"
           << "  \"ciphertext_components\": "
           << ciphertext_components << ",\n"
           << "  \"lhs_component\": " << metadata.lhs_component << ",\n"
           << "  \"rhs_component\": " << metadata.rhs_component << ",\n"
           << "  \"modulus\": " << metadata.modulus << ",\n"
           << "  \"barrett_mu_floor_2pow110_div_q\": "
           << metadata.barrett_mu << ",\n"
           << "  \"payload_order\": "
              "[\"a\", \"b\", \"acc\", \"expected\"],\n"
           << "  \"elements_per_array\": "
           << metadata.ring_dim << ",\n"
           << "  \"byte_order\": \"little-endian\"\n"
           << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);

        CCParams<CryptoContextCKKSRNS> parameters;
        parameters.SetMultiplicativeDepth(kMultDepth);
        parameters.SetScalingModSize(kLimbBits);
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

        if (lhs_ciphertext->GetElements().size() != 2 ||
            rhs_ciphertext->GetElements().size() != 2) {
            throw std::runtime_error(
                "fresh CKKS ciphertext is not two-component");
        }
        const DCRTPoly& lhs_poly =
            lhs_ciphertext->GetElements().at(0);
        const DCRTPoly& rhs_poly =
            rhs_ciphertext->GetElements().at(0);
        if (lhs_poly.GetFormat() != Format::EVALUATION ||
            rhs_poly.GetFormat() != Format::EVALUATION) {
            throw std::runtime_error(
                "ciphertext is not in EVALUATION/NTT format");
        }

        const std::size_t tower_index =
            choose_tower(lhs_poly, options.tower_index);
        if (rhs_poly.GetNumOfElements() !=
            lhs_poly.GetNumOfElements()) {
            throw std::runtime_error(
                "ciphertexts have different tower counts");
        }

        // This OpenFHE API performs the raw component convolution without
        // relinearization or rescaling. Its e0 component is exactly c0*c0.
        const Ciphertext<DCRTPoly> raw_product =
            context->EvalMultNoRelinNoCheck(
                lhs_ciphertext, rhs_ciphertext);
        const DCRTPoly& product_poly =
            raw_product->GetElements().at(0);

        const auto& lhs_tower =
            lhs_poly.GetElementAtIndex(tower_index);
        const auto& rhs_tower =
            rhs_poly.GetElementAtIndex(tower_index);
        const auto& product_tower =
            product_poly.GetElementAtIndex(tower_index);
        const std::uint64_t modulus =
            lhs_tower.GetModulus().ConvertToInt<std::uint64_t>();
        if (rhs_tower.GetModulus().ConvertToInt<std::uint64_t>() !=
                modulus ||
            product_tower.GetModulus().ConvertToInt<std::uint64_t>() !=
                modulus) {
            throw std::runtime_error(
                "selected tower moduli do not match");
        }

        const auto& lhs_values = lhs_tower.GetValues();
        const auto& rhs_values = rhs_tower.GetValues();
        const auto& product_values = product_tower.GetValues();
        if (lhs_values.GetLength() != kRingDim ||
            rhs_values.GetLength() != kRingDim ||
            product_values.GetLength() != kRingDim) {
            throw std::runtime_error(
                "OpenFHE tower length is not 8192");
        }

        ckks_vector::Record record;
        record.metadata.ring_dim = kRingDim;
        record.metadata.limb_bits = kLimbBits;
        record.metadata.domain =
            ckks_vector::kEvaluationDomain;
        record.metadata.tower_index =
            static_cast<std::uint32_t>(tower_index);
        record.metadata.lhs_component = 0;
        record.metadata.rhs_component = 0;
        record.metadata.modulus = modulus;
        record.metadata.barrett_mu =
            compute_barrett_mu(modulus);
        record.a.resize(kRingDim);
        record.b.resize(kRingDim);
        record.acc.assign(kRingDim, 0);
        record.expected.resize(kRingDim);

        for (std::size_t i = 0; i < kRingDim; ++i) {
            record.a[i] =
                lhs_values[i].ConvertToInt<std::uint64_t>();
            record.b[i] =
                rhs_values[i].ConvertToInt<std::uint64_t>();
            record.expected[i] =
                product_values[i].ConvertToInt<std::uint64_t>();
            const std::uint64_t reference =
                reference_mul(record.a[i], record.b[i], modulus);
            if (reference != record.expected[i]) {
                throw std::runtime_error(
                    "OpenFHE raw e0 does not match modular "
                    "multiplication at index " + std::to_string(i));
            }
        }

        if (!options.output.parent_path().empty()) {
            std::filesystem::create_directories(
                options.output.parent_path());
        }
        ckks_vector::write_file(options.output.string(), record);
        std::filesystem::path json_path = options.output;
        json_path.replace_extension(".json");
        write_json(json_path, record,
                   lhs_ciphertext->GetElements().size(),
                   lhs_poly.GetNumOfElements());

        std::cout
            << "OpenFHE " << GetOPENFHEVersion()
            << " CKKS export complete\n"
            << "  output: " << options.output << '\n'
            << "  metadata: " << json_path << '\n'
            << "  tower: " << tower_index << '\n'
            << "  modulus: " << modulus << '\n'
            << "  Barrett mu: " << record.metadata.barrett_mu << '\n'
            << "  residues: " << kRingDim << " per array\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
