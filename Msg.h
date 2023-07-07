#ifndef QF633_CODE_MSG_H
#define QF633_CODE_MSG_H

#include <cstdint>
#include <string>
#include <vector>
#include <ctime>
#include <sstream>
#include <iomanip>

struct TickData {
    std::string ContractName;
    double BestBidPrice;
    double BestBidAmount;
    double BestBidIV;
    double BestAskPrice;
    double BestAskAmount;
    double BestAskIV;
    double MarkPrice;
    double MarkIV;
    std::string UnderlyingIndex;
    double UnderlyingPrice;
    double LastPrice;
    double OpenInterest;
    uint64_t LastUpdateTimeStamp;
};

struct Msg {
    uint64_t timestamp{};
    bool isSnap;
    bool isSet = false;
    std::vector<TickData> Updates;
    
    std::string getReadableTimestamp() const {
        std::time_t rawTime = static_cast<std::time_t>(timestamp / 1000);
        std::tm* timeInfo = std::localtime(&rawTime);
        std::ostringstream oss;
        oss << std::put_time(timeInfo, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }
};

#endif //QF633_CODE_MSG_H
