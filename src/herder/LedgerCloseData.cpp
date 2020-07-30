#include "util/asio.h"
#include "LedgerCloseData.h"
#include "crypto/Hex.h"
#include "herder/Upgrades.h"
#include "main/Application.h"
#include "util/Logging.h"
#include "util/XDROperators.h"
#include <overlay/OverlayManager.h>
#include <xdrpp/marshal.h>

using namespace std;

namespace stellar
{

LedgerCloseData::LedgerCloseData(
    uint32_t ledgerSeq, std::shared_ptr<AbstractTxSetFrameForApply> txSet,
    StellarValue const& v)
    : mLedgerSeq(ledgerSeq), mTxSet(txSet), mValue(v)
{
    Value x;
    Value y(x.begin(), x.end());

    assert(txSet->getContentsHash() == mValue.txSetHash);
}

bool
opaqueStellarValueBasicPartsEqual(Value const& v1, Value const& v2)
{
    StellarValue sv1, sv2;
    try
    {
        xdr::xdr_from_opaque(v1, sv1);
        xdr::xdr_from_opaque(v2, sv2);
    }
    catch (...)
    {
        return v1 == v2;
    }
    sv1.ext.v(STELLAR_VALUE_BASIC);
    sv2.ext.v(STELLAR_VALUE_BASIC);
    return sv1 == sv2;
}

bool
containsOpaqueValueWithBasicPartsEqual(xdr::xvector<Value> const& vec,
                                       Value const& val)
{
    return std::find_if(vec.begin(), vec.end(), [&](Value const& elem) -> bool {
               return opaqueStellarValueBasicPartsEqual(val, elem);
           }) != vec.end();
}

std::string
stellarValueToString(Config const& c, StellarValue const& sv)
{
    std::stringstream res;

    res << "[";
    if (sv.ext.v() == STELLAR_VALUE_SIGNED)
    {
        res << " SIGNED@" << c.toShortString(sv.ext.lcValueSignature().nodeID);
    }
    res << " txH: " << hexAbbrev(sv.txSetHash) << ", ct: " << sv.closeTime
        << ", upgrades: [";
    for (auto const& upgrade : sv.upgrades)
    {
        if (upgrade.empty())
        {
            // should not happen as this is not valid
            res << "<empty>";
        }
        else
        {
            try
            {
                LedgerUpgrade lupgrade;
                xdr::xdr_from_opaque(upgrade, lupgrade);
                res << Upgrades::toString(lupgrade);
            }
            catch (std::exception&)
            {
                res << "<unknown>";
            }
        }
        res << ", ";
    }
    res << " ] ]";

    return res.str();
}
}
