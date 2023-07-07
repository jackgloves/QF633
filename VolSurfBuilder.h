#ifndef QF633_CODE_VOLSURFBUILDER_HPrintInfo
#define QF633_CODE_VOLSURFBUILDER_H

#include <map>
#include <iomanip>
#include "Msg.h"
#include "Date.h"

template <class Smile>
class VolSurfBuilder
{
public:
    void Process(const Msg &msg); // process message
    void PrintInfo();
    std::map<datetime_t, std::pair<Smile, double>> FitSmiles();

protected:
    // we want to keep the best level information for all instruments
    // here we use a map from contract name to BestLevelInfo, the key is contract name
    std::map<std::string, TickData> currentSurfaceRaw;
    std::map<std::string, int> months = {
            {"JAN", 1},
            {"FEB", 2},
            {"MAR", 3},
            {"APR", 4},
            {"MAY", 5},
            {"JUN", 6},
            {"JUL", 7},
            {"AUG", 8},
            {"SEP", 9},
            {"OCT", 10},
            {"NOV", 11},
            {"DEC", 12}};
    datetime_t ConvertExpiryToDate(std::string);
    double GetStrike(const std::string& cName)
    {
        std::size_t firstDashPos = cName.find('-');
        std::size_t secondDashPos = cName.find('-', firstDashPos + 1);
        std::size_t thirdDashPos = cName.find('-', secondDashPos + 1);
        std::string strikeStr = cName.substr(secondDashPos + 1, thirdDashPos - secondDashPos - 1);
        return std::stod(strikeStr);
    }
};

template <class Smile>
void VolSurfBuilder<Smile>::Process(const Msg &msg)
{
    // TODO (Step 2)
    if (msg.isSnap)
    {
        // discard currently maintained market snapshot, and construct a new copy based on the input Msg
        currentSurfaceRaw.clear();
        for (auto &ticker : msg.Updates)
        {
            currentSurfaceRaw[ticker.ContractName] = ticker;
        }
    }
    else
    {
        // update the currently maintained market snapshot
        for (auto &ticker : msg.Updates)
        {
            auto it = currentSurfaceRaw.find(ticker.ContractName);
            if (it != currentSurfaceRaw.end() && it->second.LastUpdateTimeStamp <= ticker.LastUpdateTimeStamp)
            {
                currentSurfaceRaw[ticker.ContractName] = ticker;
            }
            else
            {
                currentSurfaceRaw[ticker.ContractName] = ticker;
            }
        }
    }
}


template <class Smile>
void VolSurfBuilder<Smile>::PrintInfo() {
    // TODO (Step 2): you may print out information about VolSurfBuilder's currentSnapshot to test
    std::cout << "Number of contracts in current snapshot: " << currentSurfaceRaw.size() << std::endl;
    for (const auto& entry : currentSurfaceRaw) {
        const auto& [contractName, tickData] = entry;
        std::cout << contractName << "\t, Price= " << tickData.LastPrice << "\t, IV= " << tickData.MarkIV << std::endl;
    }
}

template <class Smile>
datetime_t VolSurfBuilder<Smile>::ConvertExpiryToDate(std::string expiry)
{
    int splitPos = expiry.find('-') + 1;
    expiry = expiry.substr(splitPos, expiry.length() - splitPos);
    splitPos = expiry.find('-');
    expiry = expiry.substr(0, splitPos);
    int y, m, d;
    if (expiry.length() == 6)
    {
        d = std::stoi(expiry.substr(0, 1));
        m = months[expiry.substr(1, 3)];
        y = 2000 + stoi(expiry.substr(4, 2));
    }
    else if (expiry.length() == 7)
    {
        d = std::stoi(expiry.substr(0, 2));
        m = months[expiry.substr(2, 3)];
        y = 2000 + stoi(expiry.substr(5, 2));
    }
    else
    {
        std::cerr << "Invalid expiry " << expiry << std::endl;
        exit(-1);
    }
    return datetime_t(y, m, d);
}

template <class Smile>
std::map<datetime_t, std::pair<Smile, double>> VolSurfBuilder<Smile>::FitSmiles()
{
    std::map<datetime_t, std::vector<TickData>> tickersByExpiry{};
    // TODO (Step 3): group the tickers in the current market snapshot by expiry date, and construct tickersByExpiry
    for (const auto& p : currentSurfaceRaw)
    {
        const datetime_t expiry_t = ConvertExpiryToDate(p.second.ContractName);
        tickersByExpiry[expiry_t].push_back(p.second);
    }

    std::map<datetime_t, std::pair<Smile, double>> res{};
    // then create Smile instance for each expiry by calling FitSmile() of the Smile
    for (auto iter = tickersByExpiry.begin(); iter != tickersByExpiry.end(); iter++)
    {
        if (iter->second.size() < 5)
            continue;
        auto sm = Smile::FitSmile(iter->second); // TODO: you need to implement FitSmile function in CubicSmile
        double fittingError = 0;
        double totalWeight = 0;
        // Calculate the fitting error with time-based weights
        for (size_t i = 0; i < iter->second.size(); ++i) {
            const auto& td = iter->second[i];
            double mIV = (td.BestBidIV + td.BestAskIV) / 200;
            double sIV = sm.Vol(GetStrike(td.ContractName));

            // Calculate the weight based on the time difference from the most recent data point
            double weight = 1.0 / (i + 1);  // Assign higher weight to more recent data points

            fittingError += weight * (mIV - sIV) * (mIV - sIV);
            totalWeight += weight;
        }
        fittingError /= totalWeight;

        res.insert(std::pair<datetime_t, std::pair<Smile, double>>(iter->first, std::pair<Smile, double>(sm, fittingError)));
    }
    return res;
}

std::string UnixMSToTime(uint64_t t)
{
    std::time_t tm = static_cast<std::time_t>(t / 1000);
    std::tm* timeInfo = std::localtime(&tm);

    char buffer[24]; // Buffer size for the formatted time string
    std::strftime(buffer, sizeof(buffer), "%FT%T", timeInfo);

    std::stringstream ss;
    ss << buffer << "." << std::setfill('0') << std::setw(3) << (t % 1000) << "Z";
    return ss.str();
}

std::string DateToTime(datetime_t d)
{
    static std::vector<std::string> month = {"", "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    std::string res = "";
    res += std::to_string(d.day) + "-" + month[d.month] + "-" + std::to_string(d.year);
    return res;
}

#endif // QF633_CODE_VOLSURFBUILDER_H
