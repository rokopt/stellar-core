// Copyright 2020 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "lib/catch.hpp"
#include "test/TestAccount.h"
#include "test/TestUtils.h"
#include "test/TxTests.h"
#include "test/test.h"
#include "transactions/TransactionEvaluator.h"
#include "xdr/Stellar-transaction-evaluator.h"

using namespace stellar;

TEST_CASE("Stellar TransactionEvaluator", "[tx]")
{
    VirtualClock clock;
    Config cfg = getTestConfig();
    cfg.LEDGER_PROTOCOL_VERSION = Config::CURRENT_LEDGER_PROTOCOL_VERSION;
    auto app = createTestApplication(clock, cfg);
    app->start();
    auto const passphrase = app->getConfig().NETWORK_PASSPHRASE;
    REQUIRE(app->getLedgerManager()
                .getLastClosedLedgerHeader()
                .header.ledgerVersion ==
            Config::CURRENT_LEDGER_PROTOCOL_VERSION);
    auto root = TestAccount::createRoot(*app);

    TransactionEvaluatorResponse response;

    SECTION("empty command, genesis ledger")
    {
        TransactionEvaluatorRequest request;
        request.requestInfo.requestType(
            stellar::TRANSACTION_EVALUATOR_REQUEST_VALIDATE_AND_APPLY);
        response =
            TransactionEvaluator(passphrase).computeOperationEffect(request);
        LOG(INFO) << "XDR of genesis ledger: " << std::endl
                  << xdr::xdr_to_string(response);
    }

    SECTION("add data to genesis ledger")
    {
        auto const lcl = app->getLedgerManager().getLastClosedLedgerNum();

        SECTION("Create a data entry")
        {
            LedgerEntry dle;
            dle.lastModifiedLedgerSeq = lcl;
            dle.data.type(DATA);
            auto de = LedgerTestUtils::generateValidDataEntry();
            de.accountID = root.getPublicKey();
            dle.data.data() = de;

            auto dataOp = txtest::manageData(de.dataName, &de.dataValue);
            auto txFrame = root.tx({dataOp});

            LOG(INFO) << "XDR of txFrame for creating first data entry in "
                         "root account: "
                      << std::endl
                      << xdr::xdr_to_string(txFrame->getEnvelope());

            TransactionEvaluatorRequest request;
            request.requestInfo.requestType(
                stellar::TRANSACTION_EVALUATOR_REQUEST_VALIDATE_AND_APPLY);
            response = TransactionEvaluator(passphrase)
                           .computeOperationEffect(request);
            request.inputLedger.activate() =
                response.responseInfo.result().applyResult().outputLedger;
            request.txs.emplace_back(txFrame->getEnvelope());
            response = TransactionEvaluator(passphrase)
                           .computeOperationEffect(request);
            LOG(INFO) << "XDR of ledger with one data entry in root account: "
                      << std::endl
                      << xdr::xdr_to_string(response);

            SECTION("Chain a second data entry modification")
            {
                LedgerEntry dle;
                dle.lastModifiedLedgerSeq = lcl;
                dle.data.type(DATA);
                auto de = LedgerTestUtils::generateValidDataEntry();
                de.accountID = root.getPublicKey();
                dle.data.data() = de;

                auto dataOp = txtest::manageData(de.dataName, &de.dataValue);
                auto txFrame = root.tx({dataOp});

                LOG(INFO) << "XDR of txFrame for creating second data entry in "
                             "root account: "
                          << std::endl
                          << xdr::xdr_to_string(txFrame->getEnvelope());

                TransactionEvaluatorRequest request;
                request.inputLedger.activate() =
                    response.responseInfo.result().applyResult().outputLedger;
                request.requestInfo.requestType(
                    stellar::TRANSACTION_EVALUATOR_REQUEST_VALIDATE_AND_APPLY);
                request.txs.emplace_back(txFrame->getEnvelope());
                TransactionEvaluatorResponse response =
                    TransactionEvaluator(passphrase)
                        .computeOperationEffect(request);
                LOG(INFO)
                    << "XDR of ledger with two data entries in root account: "
                    << std::endl
                    << xdr::xdr_to_string(response);
            }
        }
    }
}