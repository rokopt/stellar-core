// Copyright 2020 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

%#include "xdr/Stellar-ledger.h"

namespace stellar
{

typedef string stringVarLen<>;
typedef uint32 LedgerSequenceNumber;

struct TransactionEvaluatorLedgerState
{
    LedgerSequenceNumber baseLedger;
    LedgerHeader header;
    BucketEntry entries<>;

    // reserved for future use
    union switch (int v)
    {
    case 0:
        void;
    }
    ext;
};

struct TransactionEvaluatorSignedLedgerState
{
    TransactionEvaluatorLedgerState ledgerState;
    Signature signature;
};

struct TransactionEvaluatorClientModifiableHeaderData
{
    LedgerSequenceNumber* newSequenceNumber;
    TimePoint* closeTime;

    // reserved for future use
    union switch (int v)
    {
    case 0:
        void;
    }
    ext;
};

enum TransactionEvaluatorRequestType {
    TRANSACTION_EVALUATOR_REQUEST_VALIDATE_ONLY = 0,
    TRANSACTION_EVALUATOR_REQUEST_VALIDATE_AND_APPLY = 1
};

enum TransactionEvaluatorApplyType {
    TRANSACTION_EVALUATOR_APPLY_TYPE_SETUP = 0,
    TRANSACTION_EVALUATOR_APPLY_TYPE_TEST = 1
};

struct TransactionEvaluatorRequest
{
    TransactionEvaluatorSignedLedgerState* inputLedger;
    TransactionEnvelope txs<>;

    union switch (TransactionEvaluatorRequestType requestType)
    {
    case TRANSACTION_EVALUATOR_REQUEST_VALIDATE_ONLY:
        void;
    case TRANSACTION_EVALUATOR_REQUEST_VALIDATE_AND_APPLY:
        struct
        {
            TransactionEvaluatorClientModifiableHeaderData headerData;
            TransactionEvaluatorApplyType applyType;
            UpgradeType upgrades;
        } applyRequest;
    } requestInfo;

    // reserved for future use
    union switch (int v)
    {
    case 0:
        void;
    }
    ext;
};

enum TransactionEvaluatorCommandResultCode
{
    TRANSACTION_EVALUATOR_COMMAND_SUCCESS = 0,
    TRANSACTION_EVALUATOR_COMMAND_TRANSACTION_EVALUATOR_NOT_ENABLED = -1,
    TRANSACTION_EVALUATOR_COMMAND_MISSING_COMMAND_ARGUMENT = -2,
    TRANSACTION_EVALUATOR_COMMAND_BOTH_XDR_AND_JSON_ARGUMENTS = -3,
    TRANSACTION_EVALUATOR_COMMAND_MISSING_ARGUMENT = -4,
    TRANSACTION_EVALUATOR_COMMAND_INVALID_COMMAND_NAME = -5,
    TRANSACTION_EVALUATOR_COMMAND_MALFORMED_XDR = -6,
    TRANSACTION_EVALUATOR_COMMAND_INVALID_REQUEST_TYPE = -7,
    TRANSACTION_EVALUATOR_COMMAND_INVALID_CLOSE_TIME = -8,
    TRANSACTION_EVALUATOR_COMMAND_INVALID_APPLY_TYPE = -9,
    TRANSACTION_EVALUATOR_COMMAND_INPUT_LEDGER_INVALID = -10
};

typedef TransactionResultPair TransactionResultPairs<>;

enum TransactionEvaluatorInputLedgerStatus {
    TRANSACTION_EVALUATOR_INPUT_LEDGER_STATUS_VALID = 0,
    TRANSACTION_EVALUATOR_INPUT_LEDGER_STATUS_SIGNATURE_CHECK_FAILED = 1,
    TRANSACTION_EVALUATOR_INPUT_LEDGER_STATUS_BASE_LEDGER_CHANGED = 2
};

struct TransactionEvaluatorResponse
{
    union switch (TransactionEvaluatorCommandResultCode resultCode)
    {
    case TRANSACTION_EVALUATOR_COMMAND_SUCCESS:
        union switch (TransactionEvaluatorRequestType requestType)
        {
        case TRANSACTION_EVALUATOR_REQUEST_VALIDATE_ONLY:
            struct
            {
                TransactionResultPairs resultPairs;
            } validateResult;
        case TRANSACTION_EVALUATOR_REQUEST_VALIDATE_AND_APPLY:
            struct
            {
                TransactionResultPairs trimmed;
                LedgerCloseMeta metadata;
                TransactionEvaluatorSignedLedgerState outputLedger;
            } applyResult;
        } result;
    case TRANSACTION_EVALUATOR_COMMAND_INPUT_LEDGER_INVALID:
        TransactionEvaluatorInputLedgerStatus inputLedgerStatus;
    default:
        void;
    } responseInfo;

    stringVarLen* readableResult;

    // reserved for future use
    union switch (int v)
    {
    case 0:
        void;
    }
    ext;
};
}
