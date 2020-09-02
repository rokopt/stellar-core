// Copyright 2020 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/TransactionEvaluator.h"
#include "database/Database.h"
#include "invariant/InvariantDoesNotHold.h"
#include "ledger/LedgerManager.h"
#include "ledger/LedgerTxn.h"
#include "transactions/TransactionUtils.h"
#include "util/Decoder.h"
#include "util/Logging.h"
#include "util/types.h"
#include "xdr/Stellar-ledger.h"
#include "xdr/Stellar-transaction-evaluator.h"
#include "xdrpp/marshal.h"
#include "xdrpp/printer.h"

#include <cereal/archives/json.hpp>

#include <fmt/format.h>

namespace stellar
{

TransactionEvaluatorApplication::TransactionEvaluatorApplication(
    VirtualClock& clock, Config const& cfg)
    : ApplicationImpl(clock, cfg)
{
}

void
TransactionEvaluatorApplication::manualClose(
    std::map<std::string, std::string> const& retMap, std::string& retStr)
{
    if (!getConfig().isStandaloneValidator())
    {
        retStr = "Manually closing ledger with parameters requires "
                 "NODE_IS_VALIDATOR and RUN_STANDALONE";
        return;
    }
    throw std::runtime_error(
        "TransactionEvaluatorApplication not implemented yet");
}

std::shared_ptr<TransactionEvaluatorApplication>
TransactionEvaluator::makePrivateApplication(
    std::string const& networkPassphrase)
{
    Config cfg;
    cfg.TRANSACTION_EVALUATOR_COMMANDS_ENABLED = true;
    cfg.MODE_ENABLES_BUCKETLIST = false;
    cfg.DISABLE_BUCKET_GC = true;
    cfg.MODE_STORES_HISTORY = false;
    cfg.RUN_STANDALONE = true;
    cfg.FORCE_SCP = true;
    cfg.NODE_IS_VALIDATOR = true;
    cfg.MANUAL_CLOSE = true;
    cfg.setNoListen();
    cfg.setNoPublish();
    cfg.QUORUM_SET.validators.push_back(cfg.NODE_SEED.getPublicKey());
    cfg.QUORUM_SET.threshold = 1;
    cfg.UNSAFE_QUORUM = true;
    cfg.NETWORK_PASSPHRASE = networkPassphrase;
    cfg.LEDGER_PROTOCOL_VERSION = Config::CURRENT_LEDGER_PROTOCOL_VERSION;
    cfg.USE_CONFIG_FOR_GENESIS = true;
    auto app = Application::create<TransactionEvaluatorApplication>(
        *mPrivateClock, cfg);
    app->start();
    return app;
}

TransactionEvaluator::TransactionEvaluator()
    : TransactionEvaluator("Stellar TransactionEvaluator")
{
}

TransactionEvaluator::TransactionEvaluator(std::string const& networkPassphrase)
    : mPrivateClock(std::make_shared<VirtualClock>())
    , mPrivateApp(makePrivateApplication(networkPassphrase))
    , mApp(*mPrivateApp)
{
}

TransactionEvaluator::TransactionEvaluator(Application& app)
    : mPrivateClock(nullptr), mPrivateApp(nullptr), mApp(app)
{
}

class TransactionEvaluatorTxSetFrame : public TxSetFrame
{
  private:
    bool const mPreserveOrder;

  public:
    TransactionEvaluatorTxSetFrame(Hash const& previousLedgerHash,
                                   bool const preserveOrder)
        : TxSetFrame(previousLedgerHash), mPreserveOrder(preserveOrder)
    {
    }

    std::vector<TransactionFrameBasePtr> sortForApply() override;
    void sortForHash() override;
};

std::vector<TransactionFrameBasePtr>
TransactionEvaluatorTxSetFrame::sortForApply()
{
    return mPreserveOrder ? mTransactions : TxSetFrame::sortForApply();
}

void
TransactionEvaluatorTxSetFrame::sortForHash()
{
    if (!mPreserveOrder)
    {
        TxSetFrame::sortForHash();
    }
}

TransactionEvaluatorResponse
TransactionEvaluator::computeOperationEffect(
    TransactionEvaluatorRequest const& request)
{
    TransactionEvaluatorResponse response;
    response.responseInfo.resultCode(TRANSACTION_EVALUATOR_COMMAND_SUCCESS);
    response.responseInfo.result().requestType(
        request.requestInfo.requestType());

    auto const& inputLedger = request.inputLedger;
    LedgerTxn neverCommittingLtx(mApp.getLedgerTxnRoot());

    auto const baseLedgerSeq = neverCommittingLtx.getHeader().ledgerSeq;
    auto const lastCloseTime =
        neverCommittingLtx.getHeader().scpValue.closeTime;
    auto const now = VirtualClock::to_time_t(mApp.getClock().system_now());
    auto const minCloseTime = std::max<TimePoint>(lastCloseTime + 1, now);

    if (inputLedger)
    {
        // (TODO:  Here we will check the input ledger's signature).

        if (inputLedger->ledgerState.baseLedger != baseLedgerSeq)
        {
            setResult(response,
                      TRANSACTION_EVALUATOR_COMMAND_INPUT_LEDGER_INVALID,
                      FMT_STRING("Input base ledger ({}) stale (current={})"),
                      inputLedger->ledgerState.baseLedger, baseLedgerSeq);
            response.responseInfo.inputLedgerStatus() =
                TRANSACTION_EVALUATOR_INPUT_LEDGER_STATUS_BASE_LEDGER_CHANGED;
            return response;
        }

        neverCommittingLtx.loadHeader().current() =
            inputLedger->ledgerState.header;

        {
            LedgerTxn createInputLedgerEntriesTxn(neverCommittingLtx);
            for (auto const& entry : inputLedger->ledgerState.entries)
            {
                switch (entry.type())
                {
                case INITENTRY:
                {
                    createInputLedgerEntriesTxn.create(entry.liveEntry());
                    break;
                }
                case LIVEENTRY:
                {
                    auto existingEntry = createInputLedgerEntriesTxn.load(
                        LedgerEntryKey(entry.liveEntry()));
                    if (!existingEntry)
                    {
                        throw std::runtime_error(
                            "Live TransactionEvaluator input ledger entry "
                            "not found");
                    }
                    existingEntry.current().data = entry.liveEntry().data;
                    break;
                }
                case DEADENTRY:
                {
                    createInputLedgerEntriesTxn.erase(entry.deadEntry());
                    break;
                }
                case METAENTRY:
                {
                    throw std::runtime_error(
                        "TransactionEvaluator input ledger entry contains "
                        "bucket metadata");
                    break;
                }
                }
            }
            createInputLedgerEntriesTxn.commit();
        }
    }

    auto const newCloseTime =
        (request.requestInfo.requestType() ==
             TRANSACTION_EVALUATOR_REQUEST_VALIDATE_AND_APPLY &&
         request.requestInfo.applyRequest().headerData.closeTime)
            ? *request.requestInfo.applyRequest().headerData.closeTime
            : minCloseTime;

    if (newCloseTime < minCloseTime)
    {
        setResult(response, TRANSACTION_EVALUATOR_COMMAND_INVALID_CLOSE_TIME,
                  FMT_STRING("Invalid closeTime ({}) submitted -- last={}, "
                             "now={}, min={}"),
                  *request.requestInfo.applyRequest().headerData.closeTime,
                  lastCloseTime, now, minCloseTime);
        return response;
    }

    bool preserveOrder = false;
    if (request.requestInfo.requestType() ==
        TRANSACTION_EVALUATOR_REQUEST_VALIDATE_AND_APPLY)
    {
        if (request.requestInfo.applyRequest().applyType ==
            TRANSACTION_EVALUATOR_APPLY_TYPE_SETUP)
        {
            preserveOrder = true;
        }
        else
        {
            if (request.requestInfo.applyRequest().applyType !=
                TRANSACTION_EVALUATOR_APPLY_TYPE_TEST)
            {
                setResult(response,
                          TRANSACTION_EVALUATOR_COMMAND_INVALID_APPLY_TYPE,
                          FMT_STRING("Invalid applyType ({}) submitted"),
                          request.requestInfo.applyRequest().applyType);
                return response;
            }
        }
    }

    auto txSet = std::make_shared<TransactionEvaluatorTxSetFrame>(
        mApp.getLedgerManager().getLastClosedLedgerHeader().hash,
        preserveOrder);

    for (auto const& txEnvelope : request.txs)
    {
        txSet->add(TransactionFrameBase::makeTransactionFromWire(
            mApp.getNetworkID(), txEnvelope));
    }

    if (request.requestInfo.requestType() ==
        TRANSACTION_EVALUATOR_REQUEST_VALIDATE_ONLY)
    {
        auto& responseResultPairs =
            response.responseInfo.result().validateResult().resultPairs;
        LedgerTxn validityCheckLtx(neverCommittingLtx);
        for (auto const& tx : txSet->mTransactions)
        {
            tx->checkValid(validityCheckLtx, 0, 0,
                           getUpperBoundCloseTimeOffset(mApp, lastCloseTime));
            TransactionResultPair resultPair;
            resultPair.transactionHash = tx->getContentsHash();
            resultPair.result = tx->getResult();
            responseResultPairs.push_back(resultPair);
        }
        validityCheckLtx.commit();
        return response;
    }

    if (request.requestInfo.requestType() !=
        TRANSACTION_EVALUATOR_REQUEST_VALIDATE_AND_APPLY)
    {
        setResult(response, TRANSACTION_EVALUATOR_COMMAND_INVALID_REQUEST_TYPE,
                  FMT_STRING("Invalid requestType ({}) submitted"),
                  request.requestInfo.requestType());
        return response;
    }

    {
        LedgerTxn trimInvalidTxn(neverCommittingLtx);
        auto const closeTimeOffset = newCloseTime - lastCloseTime;
        auto removed = txSet->trimInvalid(trimInvalidTxn, closeTimeOffset,
                                          closeTimeOffset);
        for (auto const& tx : removed)
        {
            TransactionResultPair resultPair;
            resultPair.result = tx->getResult();
            resultPair.transactionHash = tx->getContentsHash();
            response.responseInfo.result().applyResult().trimmed.push_back(
                resultPair);
        }
    }

    StellarValue sv(txSet->getContentsHash(), newCloseTime, emptyUpgradeSteps,
                    STELLAR_VALUE_BASIC);

    LedgerCloseData ledgerCloseData(
        mApp.getLedgerManager().getLastClosedLedgerNum() + 1, txSet, sv);

    auto ledgerCloseMeta = std::make_unique<LedgerCloseMeta>();

    {
        LedgerTxn closeLedgerTxn(neverCommittingLtx);
        mApp.getLedgerManager().closeLedger(ledgerCloseData, closeLedgerTxn,
                                            ledgerCloseMeta);
        closeLedgerTxn.commit();
    }

    auto& outputLedger =
        response.responseInfo.result().applyResult().outputLedger;

    outputLedger.ledgerState.baseLedger = baseLedgerSeq;
    outputLedger.ledgerState.header = neverCommittingLtx.loadHeader().current();

    std::vector<LedgerEntry> liveEntries, initEntries;
    std::vector<LedgerKey> deadEntries;
    neverCommittingLtx.getAllEntries(initEntries, liveEntries, deadEntries);
    auto& outputLedgerEntries = outputLedger.ledgerState.entries;
    for (auto const& ledgerEntry : initEntries)
    {
        BucketEntry bucketEntry;
        bucketEntry.type(INITENTRY);
        bucketEntry.liveEntry() = ledgerEntry;
        outputLedgerEntries.push_back(bucketEntry);
    }
    for (auto const& ledgerEntry : liveEntries)
    {
        BucketEntry bucketEntry;
        bucketEntry.type(LIVEENTRY);
        bucketEntry.liveEntry() = ledgerEntry;
        outputLedgerEntries.push_back(bucketEntry);
    }
    for (auto const& ledgerKey : deadEntries)
    {
        BucketEntry bucketEntry;
        bucketEntry.type(DEADENTRY);
        bucketEntry.deadEntry() = ledgerKey;
        outputLedgerEntries.push_back(bucketEntry);
    }

    response.responseInfo.result().applyResult().metadata = *ledgerCloseMeta;

    return response;
}

TransactionEvaluatorRequest
TransactionEvaluator::fromJsonAsString(std::string const& input)
{
    throw std::runtime_error(
        "TransactionEvaluator JSON input not implemented yet");
}

std::string
TransactionEvaluator::toJsonAsString(
    TransactionEvaluatorResponse const& response)
{

    throw std::runtime_error(
        "TransactionEvaluator JSON output not implemented yet");
}

std::string
TransactionEvaluator::stringCommand(std::string const& input, bool json)
{
    TransactionEvaluatorRequest request;
    TransactionEvaluatorResponse response;
    if (json)
    {
        try
        {
            request = fromJsonAsString(input);
        }
        catch (std::exception const& e)
        {
            throw std::runtime_error(
                fmt::format(FMT_STRING("Formatting JSON parsing error ('{}') "
                                       "as output JSON not implemented yet"),
                            e.what()));
        }
    }
    else
    {
        try
        {
            decodeOpaqueXDR(input, request);
        }
        catch (std::exception const& e)
        {
            setResult(
                response, TRANSACTION_EVALUATOR_COMMAND_MALFORMED_XDR,
                FMT_STRING("Malformed TransactionEvaluator command XDR: '{}'"),
                e.what());
            return decoder::encode_b64(xdr::xdr_to_opaque(response));
        }
    }
    response = computeOperationEffect(request);
    return json ? toJsonAsString(response)
                : decoder::encode_b64(xdr::xdr_to_opaque(response));
}

void
TransactionEvaluator::streamCommand(std::istream& input, std::ostream& output,
                                    bool json)
{
    std::stringstream buffer;
    buffer << input.rdbuf();
    output << stringCommand(buffer.str(), json);
}

void
TransactionEvaluator::fileCommand(std::string const& inputFile,
                                  std::string const& outputFile, bool json)
{
    std::ifstream inputStream(inputFile);
    std::ofstream outputStream(outputFile);
    streamCommand(inputStream, outputStream, json);
}

void
TransactionEvaluator::pipeCommand(bool json)
{
    streamCommand(std::cin, std::cout, json);
}

}