#include <iostream>
#include "CsvFeeder.h"
#include "date/date.h"

uint64_t TimeToUnixMS(std::string ts) {
    std::istringstream in{ts};
    std::chrono::system_clock::time_point tp;
    in >> date::parse("%FT%T", tp);
    const auto timestamp = std::chrono::time_point_cast<std::chrono::milliseconds>(tp).time_since_epoch().count();
    return timestamp;
}

bool ReadNextMsg(std::ifstream& file, Msg& msg) {
    if (file.eof()) {
        return false;
    }
    // TODO: your implementation to read file and create the next Msg into the variable msg

    std::string line;
    std::vector<TickData> updates;
    uint64_t lastUpdateTimeStamp = 0;
    int counter = 0;
    msg.isSet = true;

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string field;

        // Parse the CSV line and store each field in the 'field' variable
        std::vector<std::string> fields;
        while (std::getline(iss, field, ',')) {
            fields.push_back(field);
        }

        // Discard first row (headers)
        if (fields[0] == "contractName") {
            continue;
        }

        // Discard any data before the first snapshot
        if (fields[2] == "update" && msg.timestamp == 0) {
            continue;
        }

        TickData update;
        update.LastUpdateTimeStamp = TimeToUnixMS(fields[1]);

        // If the current timestamp is different from the previous one, return true
        if (update.LastUpdateTimeStamp != lastUpdateTimeStamp && counter >= 1) {
            msg.timestamp = update.LastUpdateTimeStamp;
            msg.Updates = updates;
            return true;
        }

        // Set the timestamp for the message
        lastUpdateTimeStamp = update.LastUpdateTimeStamp;
        msg.timestamp = update.LastUpdateTimeStamp;

        // Determine if it's a snapshot
        msg.isSnap = (fields[2] == "snap");

        // Store data inside the TickData structure
        update.ContractName = fields[0];
        update.BestBidPrice = fields[4].empty() ? std::numeric_limits<double>::quiet_NaN() : std::stod(fields[4]);
        update.BestBidAmount = fields[5].empty() ? std::numeric_limits<double>::quiet_NaN() : std::stod(fields[5]);
        update.BestBidIV = fields[6].empty() ? std::numeric_limits<double>::quiet_NaN() : std::stod(fields[6]);
        update.BestAskPrice = fields[7].empty() ? std::numeric_limits<double>::quiet_NaN() : std::stod(fields[7]);
        update.BestAskAmount = fields[8].empty() ? std::numeric_limits<double>::quiet_NaN() : std::stod(fields[8]);
        update.BestAskIV = fields[9].empty() ? std::numeric_limits<double>::quiet_NaN() : std::stod(fields[9]);
        update.MarkPrice = fields[10].empty() ? std::numeric_limits<double>::quiet_NaN() : std::stod(fields[10]);
        update.MarkIV = fields[11].empty() ? std::numeric_limits<double>::quiet_NaN() : std::stod(fields[11]);
        update.UnderlyingIndex = fields[12];
        update.UnderlyingPrice = fields[13].empty() ? std::numeric_limits<double>::quiet_NaN() : std::stod(fields[13]);
        update.LastPrice = fields[15].empty() ? std::numeric_limits<double>::quiet_NaN() : std::stod(fields[15]);
        update.OpenInterest = fields[16].empty() ? std::numeric_limits<double>::quiet_NaN() : std::stod(fields[16]);

        updates.push_back(update);
        counter++;
    }

    msg.timestamp = lastUpdateTimeStamp;
    msg.Updates = updates;

    return true;
}


CsvFeeder::CsvFeeder(const std::string ticker_filename,
                     FeedListener feed_listener,
                     std::chrono::minutes interval,
                     TimerListener timer_listener)
        : ticker_file_(ticker_filename),
          feed_listener_(feed_listener),
          interval_(interval),
          timer_listener_(timer_listener) {
    // initialize member variables with input information, prepare for Step() processing

    ReadNextMsg(ticker_file_, msg_);
    if (msg_.isSet) {
        // initialize interval timer now_ms_
        now_ms_ = msg_.timestamp;
    } else {
        throw std::invalid_argument("empty message at initialization");
    }
}

bool CsvFeeder::Step() {
    if (msg_.isSet) {
        // call feed_listener with the loaded Msg
        feed_listener_(msg_);

        // if current message's timestamp is crossing the given interval, call time_listener, change now_ms_ to the next interval cutoff
        if (now_ms_ < msg_.timestamp) {
            timer_listener_(now_ms_);
            now_ms_ += interval_.count();
        }
        // load tick data into Msg
        // if there is no more message from the csv file, return false, otherwise true
        return ReadNextMsg(ticker_file_, msg_);
    }
    return false;
}

CsvFeeder::~CsvFeeder() {
    // release resource allocated in constructor, if any
    if (ticker_file_.is_open()) {
        ticker_file_.close();
    }
}
