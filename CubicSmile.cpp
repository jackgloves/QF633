#include "CubicSmile.h"
#include "BSAnalytics.h"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <map>
#include <time.h>
#include <errno.h>
#include <cstring>

double GetStrike(const std::string &cName)
{
    std::size_t pos = 0;
    pos = cName.find('-', pos);
    pos = cName.find('-', pos + 1) + 1;
    std::size_t pos_end = cName.find('-', pos);
    return std::stod(cName.substr(pos, pos_end - pos));
}

time_t GetExpiryTime(std::string expiry)
{
    static std::map<std::string, int> months = {
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

    int splitPos = expiry.find('-') + 1;
    expiry = expiry.substr(splitPos, expiry.length() - splitPos);
    splitPos = expiry.find('-');
    expiry = expiry.substr(0, splitPos);
    int y, m, d;
    if (expiry.length() == 6)
    {
        d = std::stoi(expiry.substr(0, 1));
        m = months[expiry.substr(1, 3)];
        y = stoi(expiry.substr(4, 2));
    }
    else if (expiry.length() == 7)
    {
        d = std::stoi(expiry.substr(0, 2));
        m = months[expiry.substr(2, 3)];
        y = stoi(expiry.substr(5, 2));
    }
    else
    {
        std::cerr << "Invalid expiry " << expiry << std::endl;
        exit(-1);
    }

    struct tm time;
    time.tm_year = y + 100;
    time.tm_mon = m - 1;
    time.tm_mday = d;
    time.tm_hour = time.tm_min = time.tm_sec = 0;
    time.tm_isdst = 0;

    auto res = mktime(&time);
    if (res == time_t(-1))
    {
        std::cout << std::strerror(errno) << std::endl;
        exit(0);
    }
    return res;
}

CubicSmile CubicSmile::FitSmile(const std::vector<TickData> &volTickerSnap)
{
    double fwd, T, atmvol, bf25, rr25, bf10, rr10;
    // TODO (step 3): fit a CubicSmile that is close to the raw tickers

    // - make sure all tickData are on the same expiry and same underlying
    uint64_t lastTime = 0;
    int index = 0;

    for (const auto& volSnap : volTickerSnap)
    {
        if (volSnap.LastUpdateTimeStamp > lastTime)
        {
            lastTime = volSnap.LastUpdateTimeStamp;
            index = &volSnap - &volTickerSnap[0];
        }
    }

    // - get latest underlying price from all tickers based on LastUpdateTimeStamp
    const auto& volSnap = volTickerSnap[index];
    fwd = volSnap.UnderlyingPrice;
    double expiryTime = GetExpiryTime(volSnap.ContractName) * 1000;
    double curTime = volSnap.LastUpdateTimeStamp;


    // - get time to expiry T
    T = std::max(1e-6, (expiryTime - curTime) / 3.1536e10);
    //T = (((expiryTime - curTime) + (2.88e7)) / 3.1536e10);
    //T = ((expiryTime - curTime) + (23 * 60 * 60 * 1000) + (59 * 60 * 1000))/ 3.1536e10;

    // - fit the 5 parameters of the smile, atmvol, bf25, rr25, bf10, and rr10 using L-BFGS-B solver, to the ticker data
    double iv = impliedVol(Call, GetStrike(volTickerSnap[index].ContractName), fwd, T, volTickerSnap[index].BestBidPrice * fwd);
    double undiscPrice = bsUndisc(Call, fwd, fwd, T, iv);
    atmvol = impliedVol(Call, fwd, fwd, T, undiscPrice);
    double mIV = volTickerSnap[index].MarkIV / 200;
    if (atmvol == 0.0001) {
        atmvol = mIV;
    }
    double stdev = atmvol * sqrt(T);

    double k_qd90 = quickDeltaToStrike(0.9, fwd, stdev);
    double k_qd75 = quickDeltaToStrike(0.75, fwd, stdev);
    double k_qd25 = quickDeltaToStrike(0.25, fwd, stdev);
    double k_qd10 = quickDeltaToStrike(0.1, fwd, stdev);

    double v_qd90 = impliedVol(Call, k_qd90, fwd, T, undiscPrice + fwd - k_qd90);
    double v_qd75 = impliedVol(Call, k_qd75, fwd, T, undiscPrice + fwd - k_qd75);
    double v_qd25 = impliedVol(Put, k_qd25, fwd, T, undiscPrice + k_qd25 - fwd);
    double v_qd10 = impliedVol(Put, k_qd10, fwd, T, undiscPrice + k_qd10 - fwd);
    bf25 = (v_qd25 + v_qd75) / 2 - atmvol;
    rr25 = v_qd25 - v_qd75;
    bf10 = (v_qd10 + v_qd90) / 2 - atmvol;
    rr10 = v_qd10 - v_qd90;

    // after the fitting, we can return the resulting smile
    return CubicSmile(fwd, T, atmvol, bf25, rr25, bf10, rr10);
}

CubicSmile::CubicSmile(double underlyingPrice, double T, double atmvol, double bf25, double rr25, double bf10, double rr10)
{
    // save parameters
    params.push_back(underlyingPrice);
    params.push_back(atmvol);
    params.push_back(bf25);
    params.push_back(rr25);
    params.push_back(bf10);
    params.push_back(rr10);

    // convert delta marks to strike vol marks, setup strikeMarks, then call BUildInterp
    double v_qd90 = atmvol + bf10 - rr10 / 2.0;
    double v_qd75 = atmvol + bf25 - rr25 / 2.0;
    double v_qd25 = atmvol + bf25 + rr25 / 2.0;
    double v_qd10 = atmvol + bf10 + rr10 / 2.0;

    // we use quick delta: qd = N(log(F/K / (atmvol) / sqrt(T))
    double stdev = atmvol * sqrt(T);
    double k_qd90 = quickDeltaToStrike(0.9, underlyingPrice, stdev);
    double k_qd75 = quickDeltaToStrike(0.75, underlyingPrice, stdev);
    double k_qd25 = quickDeltaToStrike(0.25, underlyingPrice, stdev);
    double k_qd10 = quickDeltaToStrike(0.1, underlyingPrice, stdev);

    strikeMarks.push_back(std::pair<double, double>(k_qd90, v_qd90));
    strikeMarks.push_back(std::pair<double, double>(k_qd75, v_qd75));
    strikeMarks.push_back(std::pair<double, double>(underlyingPrice, atmvol));
    strikeMarks.push_back(std::pair<double, double>(k_qd25, v_qd25));
    strikeMarks.push_back(std::pair<double, double>(k_qd10, v_qd10));
    BuildInterp();
}

void CubicSmile::BuildInterp()
{
    int n = strikeMarks.size();
    // end y' are zero, flat extrapolation
    double yp1 = 0;
    double ypn = 0;
    y2.resize(n);
    vector<double> u(n - 1);

    y2[0] = -0.5;
    u[0] = (3.0 / (strikeMarks[1].first - strikeMarks[0].first)) *
           ((strikeMarks[1].second - strikeMarks[0].second) / (strikeMarks[1].first - strikeMarks[0].first) - yp1);

    for (int i = 1; i < n - 1; i++)
    {
        double sig = (strikeMarks[i].first - strikeMarks[i - 1].first) / (strikeMarks[i + 1].first - strikeMarks[i - 1].first);
        double p = sig * y2[i - 1] + 2.0;
        y2[i] = (sig - 1.0) / p;
        u[i] = (strikeMarks[i + 1].second - strikeMarks[i].second) / (strikeMarks[i + 1].first - strikeMarks[i].first) - (strikeMarks[i].second - strikeMarks[i - 1].second) / (strikeMarks[i].first - strikeMarks[i - 1].first);
        u[i] = (6.0 * u[i] / (strikeMarks[i + 1].first - strikeMarks[i - 1].first) - sig * u[i - 1]) / p;
    }

    double qn = 0.5;
    double un = (3.0 / (strikeMarks[n - 1].first - strikeMarks[n - 2].first)) *
                (ypn - (strikeMarks[n - 1].second - strikeMarks[n - 2].second) / (strikeMarks[n - 1].first - strikeMarks[n - 2].first));

    y2[n - 1] = (un - qn * u[n - 2]) / (qn * y2[n - 2] + 1.0);

    for (int i = n - 2; i >= 0; i--)
    {
        y2[i] = y2[i] * y2[i + 1] + u[i];
    }
}

double CubicSmile::Vol(double strike)
{
    unsigned i;
    // we use trivial search, but can consider binary search for better performance
    for (i = 0; i < strikeMarks.size(); i++)
        if (strike < strikeMarks[i].first)
            break; // i stores the index of the right end of the bracket

    // extrapolation
    if (i == 0)
        return strikeMarks[i].second;
    if (i == strikeMarks.size())
        return strikeMarks[i - 1].second;

    // interpolate
    double h = strikeMarks[i].first - strikeMarks[i - 1].first;
    double a = (strikeMarks[i].first - strike) / h;
    double b = 1 - a;
    double c = (a * a * a - a) * h * h / 6.0;
    double d = (b * b * b - b) * h * h / 6.0;
    return a * strikeMarks[i - 1].second + b * strikeMarks[i].second + c * y2[i - 1] + d * y2[i];
}