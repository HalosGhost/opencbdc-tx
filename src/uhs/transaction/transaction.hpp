// Copyright (c) 2021 MIT Digital Currency Initiative,
//                    Federal Reserve Bank of Boston
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENCBDC_TX_SRC_TRANSACTION_TRANSACTION_H_
#define OPENCBDC_TX_SRC_TRANSACTION_TRANSACTION_H_

#include "util/common/random_source.hpp"
#include "util/common/commitment.hpp"
#include "crypto/sha256.h"
#include "util/common/hash.hpp"
#include "util/common/keys.hpp"
#include "util/serialization/format.hpp"
#include "util/serialization/util.hpp"

#include <secp256k1_schnorrsig.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace cbdc::transaction {
    /// \brief The unique identifier of a specific \ref output from
    ///        a transaction
    ///
    /// Uniquely identifies an \ref output from a previous transaction.
    /// Output owners construct \ref input s in new transactions out of outputs
    /// and their corresponding \ref out_point s.
    ///
    /// \see \ref cbdc::operator<<(serializer&, const transaction::out_point&)
    struct out_point {
        /// The hash of the transaction which created the out_point
        hash_t m_tx_id{};

        /// The index of the output in the transaction's output list
        /// \see transaction::full_tx::m_outputs
        uint64_t m_index{0};

        auto operator==(const out_point& rhs) const -> bool;
        auto operator<(const out_point& rhs) const -> bool;

        out_point(const hash_t& hash, uint64_t index);

        out_point() = default;
    };

    /// \brief An output of a transaction
    ///
    /// An output created by a transaction.
    /// Its owner can spend it as an \ref input in a transaction.
    ///
    /// \see \ref cbdc::operator<<(serializer&, const transaction::output&)
    struct output {
        /// Hash of the witness program
        hash_t m_witness_program_commitment{};

        /// The integral value of the output, in atomic units of currency
        uint64_t m_value{0};

        auto operator==(const output& rhs) const -> bool;
        auto operator!=(const output& rhs) const -> bool;

        output() = default;
    };

    /// \brief Additional information a spender needs to spend an input
    struct spend_data {
        /// The blinding factor for the auxiliary commitment
        hash_t m_blind{};
        /// The value of the associated output
        uint64_t m_value{0};
    };

    /// \brief An input for a new transaction
    ///
    /// An \ref out_point and associated \ref output which a client intends to
    /// spend in a new transaction.
    ///
    /// \see \ref cbdc::operator<<(serializer&, const transaction::input&)
    struct input {
        /// The unique identifier of the output
        out_point m_prevout;

        /// The output's data
        output m_prevout_data;

        auto operator==(const input& rhs) const -> bool;
        auto operator!=(const input& rhs) const -> bool;

        [[nodiscard]] auto hash() const -> hash_t;

        input() = default;
    };

    struct transaction_proof {
        /// The signatures (one per input, in-order) on the nonces used for
        /// compressing UHS IDs
        std::vector<signature_t> m_noncesigs;
    };

    /// \brief A complete transaction
    ///
    /// Complete set of transaction data:
    ///   - the set of specific outputs the client wishes to spend (inputs)
    ///   - the set of new outputs the client wishes to produce
    ///   - the set of witness programs matching the declared commitments
    ///     of each associated output being spent
    ///
    /// \see \ref cbdc::operator<<(serializer&, const transaction::full_tx&)
    struct full_tx {
        /// The set of inputs for the transaction
        std::vector<input> m_inputs{};

        /// The set of new outputs created by the transaction
        std::vector<output> m_outputs{};

        /// The set of witnesses
        std::vector<witness_t> m_witness{};

        auto operator==(const full_tx& rhs) const -> bool;

        full_tx() = default;
    };

    /// \brief A compacted output of a transaction
    ///
    /// Contains all (and only) the information necessecary for the UHS
    /// to be updated and for the system to perform audits.
    ///
    /// \see \ref cbdc::operator<<(serializer&, const transaction::compact_output&)
    struct compact_output {
        /// The UHS ID for the output
        hash_t m_id{};
        /// The nonce used to compress the Pedersen Commitment to 32 bytes
        commitment_t m_auxiliary{};
        /// The rangeproof guaranteeing that the output is greater than 0
        rangeproof_t<> m_range{};
        /// The signature proving consistency
        signature_t m_consistency{};

        compact_output(const hash_t& id, const commitment_t& aux,
            const rangeproof_t<>& range, const signature_t& consist);
        compact_output() = default;

        auto operator==(const compact_output& rhs) const -> bool;
        auto operator!=(const compact_output& rhs) const -> bool;
    };

    /// \brief A condensed, hash-only transaction representation
    ///
    /// The minimum amount of data necessary for the transaction processor to
    /// update the UHS with the changes from a \ref full_tx.
    ///
    /// \see \ref cbdc::operator<<(serializer&, const transaction::compact_tx&)
    struct compact_tx {
        /// The hash of the full transaction returned by \ref tx_id
        hash_t m_id{};

        /// The set of hashes of the transaction's inputs
        std::vector<hash_t> m_inputs;

        /// The set of hashes of the new outputs created in the transaction
        std::vector<hash_t> m_uhs_outputs;

        auto operator==(const compact_tx& tx) const noexcept -> bool;

        compact_tx() = default;

        explicit compact_tx(const full_tx& tx);
    };

    struct compact_tx_hasher {
        auto operator()(compact_tx const& tx) const noexcept -> size_t;
    };

    /// \brief Calculates the unique hash of a full transaction
    ///
    /// Returns a cryptographic hash of the inputs concatenated with the
    /// outputs (which are first transformed into inputs). Because output
    /// owners cannot reuse outputs across different transactions, this method
    /// will always generate a unique identifier for valid transactions.
    ///
    /// \param tx the \ref full_tx to hash
    /// \return the resultant hash of the transaction
    [[nodiscard]] auto tx_id(const full_tx& tx) noexcept -> hash_t;

    /// Converts the output at the specified index to an input
    /// \param tx the transaction from which to read outputs
    /// \param i index of the target output
    /// \param txid the txid of the transaction
    /// \return resultant input, or std::nullopt if i is invalid.
    auto input_from_output(const full_tx& tx, size_t i, const hash_t& txid)
        -> std::optional<input>;

    /// Calls input_from_output after calculating the TXID
    /// \param tx the transaction from which to read outputs
    /// \param i index of the target output
    /// \return result of input_from_output(tx, i, tx_id(tx))
    auto input_from_output(const full_tx& tx, size_t i)
        -> std::optional<input>;

    auto uhs_id_from_output(const hash_t& entropy,
                            uint64_t i,
                            const output& output) -> hash_t;
}

#endif // OPENCBDC_TX_SRC_TRANSACTION_TRANSACTION_H_
