#include "rpcserver.h"

#include "amount.h"
#include "core_io.h"
#include "lux/storagecontroller.h"
#include "main.h"
//#include "script/script_error.h"
//#include "script/sign.h"
//#include "version.h"
#ifdef ENABLE_WALLET
#include "wallet.h"
#endif

#include <boost/assign/list_of.hpp>

extern UniValue createrawtransaction(UniValue const & params, bool fHelp);
extern UniValue fundrawtransaction(UniValue const & params, bool fHelp);
extern UniValue signrawtransaction(UniValue const & params, bool fHelp);
extern UniValue sendrawtransaction(UniValue const & params, bool fHelp);
extern std::string EncodeHexTx(const CTransaction& tx);

UniValue SignAndSentNewTx(UniValue params, CKeyID const & changeAddress = CKeyID())
{
    UniValue signparams(UniValue::VARR);
    signparams.push_back(params["hex"]);
    UniValue signedTxObj = signrawtransaction(signparams, false);

    UniValue sendparams(UniValue::VARR);
    sendparams.push_back(signedTxObj["hex"]);
    UniValue sent = sendrawtransaction(sendparams, false);
    return sent;
}

UniValue dfscreaterawordertx(UniValue const & params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw std::runtime_error(
                "dfscreaterawordertx\n"
                        "\nArguments:\n"
                        "1. \"orders hash\":hash    (string, required) The hash of announces order\n"
                        "2. \"proposal hash\":hash  (string, required) The hash of proposal\n"
                        "\nResult:\n"
                        "\"hex\"             (string) The transaction hash in hex\n"
        );

#ifdef ENABLE_WALLET
//    LOCK2(cs_main, wallet->cs_wallet);
#else
    LOCK(cs_main);
#endif

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VSTR), true);

    uint256 orderHash = uint256{params[0].get_str()};

    const StorageOrder *order = storageController->GetAnnounce(orderHash);

    if (!order) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, order not found");
    }

    uint256 proposalHash = uint256{params[1].get_str()};

    auto metadata = storageController->GetMetadata(orderHash, proposalHash);
    const StorageProposal proposal = storageController->GetProposal(orderHash, proposalHash);

    DecryptionKeys keys = metadata.second;
    uint256 merkleRootHash = metadata.first;
    CDataStream ss(STORAGE_TX_PREFIX, SER_NETWORK, PROTOCOL_VERSION);
    ss << static_cast<unsigned char>(StorageTxTypes::Announce)
       << order->fileURI
       << order->filename
       << order->fileSize
       << keys
       << merkleRootHash
       << order->maxGap
       << proposal.rate
       << order->storageUntil;

    CMutableTransaction mTx;
    CScript scriptMarkerOut = CScript() << OP_RETURN << ToByteVector(ss);
    CScript scriptMerkleProofOut = CScript() << ToByteVector(merkleRootHash) << OP_MERKLE_PATH;

    CAmount amount = std::difftime(order->storageUntil, std::time(nullptr)) * proposal.rate * (uint64_t)(order->fileSize / 1000); // rate for 1 Kb

    mTx.vout.emplace_back(0, scriptMarkerOut);
    mTx.vout.emplace_back(amount, scriptMerkleProofOut);

    UniValue obj = EncodeHexTx(mTx);
    UniValue created(UniValue::VARR);
    created.push_back(obj);

    UniValue fundsparams = fundrawtransaction(created, false);

    return SignAndSentNewTx(fundsparams);
}

UniValue dfschecktx(UniValue const & params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
                "dfschecktx\n"
                        "\nArguments:\n"
                        "1. \"tx hash\":hash (string, required) The transaction hash\n"
                        "\nResult:\n"
                        "\"type\"             (string) order/proof/none type\n"
        );

    CTransaction tx;
    uint256 hashBlock;
    GetTransaction(uint256{params[0].get_str()}, tx, Params().GetConsensus(), hashBlock, true);

    std::vector<unsigned char> data;
    StorageTxTypes type = tx.IsStorageOrder(data);

    UniValue obj(UniValue::VSTR);
    switch (type) {
        case StorageTxTypes::None:
            obj = "None";
            break;
        case StorageTxTypes::Announce:
            obj = "Announce";
            break;
        case StorageTxTypes::Proof:
            obj = "Proof";
            break;
    }
    return obj;
}

UniValue dfscreaterawprooftx(UniValue const & params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw std::runtime_error(
                "dfscreaterawprooftx\n"
                        "\nArguments:\n"
                        "1. \"merkle root hash\":hash (string, required) The merkle root hash of file replica\n"
                        "2. \"address\": P2PKH      (string, required)\n"
                        "\nResult:\n"
                        "\"hex\"             (string) The transaction hash in hex\n"
        );

#ifdef ENABLE_WALLET
    //    LOCK2(cs_main, wallet->cs_wallet);
#else
    LOCK(cs_main);
#endif

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VSTR), true);

    uint256 merkleRootHash = uint256{params[0].get_str()};

    const StorageOrderDB *orderDB = storageController->GetOrderDB(merkleRootHash);

    if (!orderDB) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, order not found");
    }

    CTransaction orderTx;
    CTransaction *lastProofTx;
    uint256 hashBlockOrderTx;
    if (!GetTransaction(orderDB->hashTx, orderTx, Params().GetConsensus(), hashBlockOrderTx, true)) {
        throw JSONRPCError(RPC_VERIFY_REJECTED, "transaction not found not found");
    }

    const std::vector<StorageProofDB> proofs = storageController->GetProofs(merkleRootHash);
    if (proofs.size()) {
        std::vector<StorageProofDB> sortedProofs(proofs.begin(), proofs.end());
        std::sort(sortedProofs.begin(), sortedProofs.end(), [](const StorageProofDB &proofA, const StorageProofDB &proofB) {
            return std::difftime(proofA.time, proofB.time) > 0;
        });
        CTransaction proofTx;
        uint256 hashBlockProofTx;
        if (!GetTransaction(sortedProofs[0].hashTx, proofTx, Params().GetConsensus(), hashBlockProofTx, true)) {
            throw JSONRPCError(RPC_VERIFY_REJECTED, "transaction not found not found");
        }
        lastProofTx = &proofTx;
    } else {
        lastProofTx = &orderTx;
    }

    std::string destBase58 = params[1].get_str();
    CTxDestination dest = DecodeDestination(destBase58);

    if (!IsValidDestination(dest))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid address: ") + destBase58);

    StorageProofDB proof;
    proof.time = std::time(nullptr);
    proof.fileURI = orderDB->fileURI;
    proof.merkleRootHash = merkleRootHash;
    proof.rate = orderDB->rate;

    CDataStream ss(STORAGE_TX_PREFIX, SER_NETWORK, PROTOCOL_VERSION);
    ss << static_cast<unsigned char>(StorageTxTypes::Proof)
       << proof.time
       << proof.fileURI
       << proof.merkleRootHash
       << proof.rate;

    CScript scriptMarkerOut = CScript() << OP_RETURN << ToByteVector(ss);
    CScript scriptMerkleProofOut = CScript() << ToByteVector(merkleRootHash) << OP_MERKLE_PATH;

    uint64_t nProofOut = 0;

    for (const CTxOut out : lastProofTx->vout) {
        if (out.scriptPubKey.HasOpDFSProof()) {
            break;
        }
        nProofOut++;
    }
    CAmount fullAmount = lastProofTx->vout[nProofOut].nValue;
    CAmount fee = MIN_TX_FEE;
    CAmount amount = std::difftime(std::time(nullptr), lastProofTx->nTime) * orderDB->rate * orderDB->fileSize;

    UniValue vouts(UniValue::VOBJ);
    vouts.push_back(Pair(EncodeDestination(scriptMarkerOut), ValueFromAmount(10000))); // min tx fee
    vouts.push_back(Pair(EncodeDestination(CTxDestination(GetScriptForDestination(dest))), ValueFromAmount(amount - fee)));
    vouts.push_back(Pair(EncodeDestination(scriptMerkleProofOut), ValueFromAmount(fullAmount - amount - fee)));

    UniValue inputs(UniValue::VARR);
    UniValue entry(UniValue::VOBJ);
    entry.push_back(Pair("txid", lastProofTx->GetHash().GetHex()));
    entry.push_back(Pair("vout", nProofOut));
    inputs.push_back(entry);

    UniValue newparams(UniValue::VARR);
    newparams.push_back(inputs);
    newparams.push_back(vouts);

    UniValue created = createrawtransaction(newparams, false);

    return SignAndSentNewTx(created);
}
