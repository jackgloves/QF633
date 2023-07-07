#include <iostream>

#include "CsvFeeder.h"
#include "Msg.h"
#include "VolSurfBuilder.h"
#include "CubicSmile.h"

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "Usage: "
                  << argv[0] << " tick_data.csv"
                  << " outputFile.csv" << std::endl;
        return 1;
    }
    const char *ticker_filename = argv[1];

    VolSurfBuilder<CubicSmile> volBuilder;
    auto feeder_listener = [&volBuilder](const Msg &msg)
    {
        if (msg.isSet)
        {
            volBuilder.Process(msg);
        }
    };

    auto timer_listener = [&volBuilder](uint64_t now_ms)
    {
        // fit smile
        auto smiles = volBuilder.FitSmiles();
        // TODO: stream the smiles and their fitting error to outputFile.csv
        static std::ofstream fout;
        if (!fout.is_open())
        {
            fout.open("TestData/outputFile.csv", std::ios_base::app); // Open the file in append mode
            if (fout.tellp() == 0) // Check if the file is empty
            {
                fout << "TIME,EXPIRY,FUT_PRICE,ATM,BF25,RR25,BF10,RR10" << std::endl; // Write the header only if the file is empty
            }
        }

        for (const auto &sm : smiles)
        {
            fout << UnixMSToTime(now_ms) << "," << DateToTime(sm.first);
            for (const double v : sm.second.first.params)
            {
                fout << "," << v;
            }
            fout << std::endl;

            std::cout << UnixMSToTime(now_ms) << "," << DateToTime(sm.first) << ",fitting error:" << sm.second.second << std::endl;
        }
    };

    const auto interval = std::chrono::minutes(1); // we call timer_listener at 1 minute interval
    CsvFeeder csv_feeder(ticker_filename,
                         feeder_listener,
                         interval,
                         timer_listener);
    while (csv_feeder.Step())
    {
    }
    return 0;
}