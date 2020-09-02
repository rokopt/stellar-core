// Copyright 2020 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "ledger/LedgerTxn.h"
#include "main/Application.h"
#include "main/ApplicationImpl.h"
#include "util/Timer.h"
#include "xdr/Stellar-transaction-evaluator.h"

#include <fmt/format.h>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace stellar
{

class TransactionEvaluatorApplication : public ApplicationImpl
{
  public:
    TransactionEvaluatorApplication(VirtualClock& clock, Config const& cfg);
};

class TransactionEvaluator
{
  private:
    std::shared_ptr<VirtualClock> mPrivateClock;
    std::shared_ptr<TransactionEvaluatorApplication> mPrivateApp;
    Application& mApp;

    std::shared_ptr<TransactionEvaluatorApplication>
    makePrivateApplication(std::string const& networkPassphrase);

    static std::string
    toJsonAsString(TransactionEvaluatorResponse const& response);
    static TransactionEvaluatorRequest
    fromJsonAsString(std::string const& input);

  public:
    TransactionEvaluator();
    TransactionEvaluator(std::string const& networkPassphrase);
    TransactionEvaluator(Application& app);

    TransactionEvaluatorResponse
    computeOperationEffect(TransactionEvaluatorRequest const& request);

    std::string stringCommand(std::string const& input, bool json);

    void streamCommand(std::istream& input, std::ostream& output, bool json);

    void fileCommand(std::string const& inputFile,
                     std::string const& outputFile, bool json);

    void pipeCommand(bool json);

    template <typename S, typename... Args>
    static void
    setResult(TransactionEvaluatorResponse& response,
              TransactionEvaluatorCommandResultCode resultCode,
              const S& format_str, Args&&... args)
    {
        response.responseInfo.resultCode(resultCode);
        response.readableResult.activate() =
            fmt::format(format_str, std::forward<Args>(args)...);
    }
};

}