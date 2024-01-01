#include <array>
#include <algorithm>
#include <ctime>
#include <chrono>
#include <cmath> 
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <thread>
#include <utility> 
#include <unordered_set>
#include <string>
#include <stdint.h>
#include <vector>

#include <sqlite3.h>

#include "progressbar/include/progressbar.hpp"

typedef double Activity_t; 


template<class T>
T sigmoid(T x) {
    return 1 / (1 + std::exp(-x)); 
}

class Message {
    public: 
        uint32_t messageLength; 
        uint64_t discordUserID; 
        time_t messageTimestamp; 

    public: 
        Message (char** argv) {
            this->discordUserID = std::atol(argv[0]); 
            this->messageLength = std::atoi(argv[1]); 

            struct tm tm; 
            argv[2][19] = 0;        // Null terminate at 18 characters, skip ms
            if (strptime(argv[2], "%Y-%m-%d %H:%M:%S", &tm) != NULL) 
                this->messageTimestamp = mktime(&tm); 
            else 
                std::cout << argv[2] << std::endl; 
        }

        Activity_t get_activity (time_t last_m, time_t now_ts) const {
            double interval; 
            if (last_m == 0) {
                interval = 100.0f; 
            } else {
                interval = messageTimestamp - last_m; 
            }
            if (interval < 0)   interval = -interval; 

            uint32_t message_length_adjusted = messageLength ? messageLength : 1; 
            return 10.0f * std::log10(message_length_adjusted) 
                         * sigmoid(interval / 30.0f) 
                         * std::exp((now_ts - messageTimestamp) * std::log(0.9) / 86400.0); 
        }
}; 


class UserData {
    public: 
    uint64_t discordUserID; 
    time_t lastMessage; 
    std::vector<Activity_t> activities; 
    std::vector<Activity_t> convdActivities; 

    UserData() {}

    UserData(uint64_t discordUserID, int size) : discordUserID(discordUserID), 
                                                 lastMessage(0), 
                                                 activities(size, 0.0f), 
                                                 convdActivities(size, 0.0f) {}

    void printActivity(int bucket) {
        if (bucket < 0) {
            bucket = convdActivities.size() + bucket; 
        }
        std::cout << "Bucket " << bucket << " for user " << discordUserID << ": " << convdActivities[bucket] << std::endl; 
    }
}; 


void getUniqueUsers(std::unordered_set<uint64_t>& userIDs, std::vector<Message>& messages) {
    for (auto& message: messages) {
        userIDs.emplace(message.discordUserID); 
    }
}


uint32_t getMessageCount(sqlite3* db) {
    // Get the number of messages
    // SQL query to get the count of rows in a table (replace 'your_table' with the actual table name)
    const char* countSql = "SELECT COUNT(*) FROM messages WHERE discord_channel_id != 537818427675377677 AND discord_channel_id != 566364247584669721;";
    // Callback function to handle the count result
    auto countCallback = [](void* count, int argc, char** argv, char**) -> int {
        if (argc > 0) {
            *static_cast<int*>(count) = std::stoi(argv[0]);
        }
        return 0;
    };
    int rowCount = 0;
    // Execute the count query and handle the result using the callback
    sqlite3_exec(db, countSql, countCallback, &rowCount, 0);
    
    return rowCount; 
}


int loadMessages(std::vector<Message>* messages, std::string dbfile) {
    sqlite3* db; 
    char* errMsg = 0;
    int rc;

    // Open the SQLite database
    // rc = sqlite3_open("modsdb-2021-01-07.db", &db);
    rc = sqlite3_open(dbfile.c_str(), &db);

    if (rc) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return rc;
    } else {
        std::cout << "Database opened successfully" << std::endl;
    }


    uint32_t rowCount = getMessageCount(db); 
    
    messages->reserve(rowCount); 
    progressbar bar(rowCount); 
    std::pair<std::vector<Message>*, progressbar*> data(messages, &bar); 

    // SQL query to select all rows from a table (replace 'your_table' with the actual table name)
    const char* sql = "SELECT discord_user_id, message_length, message_date FROM messages WHERE discord_channel_id != 537818427675377677 AND discord_channel_id != 566364247584669721";

    // Callback function to handle each row of the result
    auto callback = [](void* data, int argc, char** argv, char** colNames) -> int {
        std::pair<std::vector<Message>*, progressbar*>* inpData = static_cast<std::pair<std::vector<Message>*, progressbar*>*>(data); 

        inpData->first->push_back(Message(argv)); 
        inpData->second->update(); 

        return 0;
    };

    // Execute the query and handle the result using the callback
    std::cout << "Reading messages \t\t"; 
    rc = sqlite3_exec(db, sql, callback, &data, &errMsg);
    std::cout << std::endl; 

    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    } 

    std::sort(messages->begin(), messages->end(), [] (const Message& a, const Message& b) {
        return a.messageTimestamp < b.messageTimestamp; 
    }); 

    std::cout << "Loaded messages, rows processed: " << messages->size() << std::endl; 

    // Close the database
    sqlite3_close(db);
    return 0; 
}

int main(int argc, const char** argv) {
    // Default parameters
    std::string dbfile = "modsdb-2021-01-07.db"; 
    time_t startTime = 1546254000; 
    time_t endTime = 1704020400; 
    time_t bucketSize = 86400; 
    size_t fusionHorizon = 30; 
    size_t numThreads = 1; 
    std::string outfile = "out.csv"; 
    
    // Parse shit
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0) {
            // TODO 
            exit(EXIT_SUCCESS); 
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dbpath") == 0) {
            if (i + 1 < argc) {
                dbfile = argv[i + 1];
                ++i;  
            } else {
                std::cerr << "-d|--dbpath requires an argument!" << std::endl; 
                exit(EXIT_FAILURE); 
            }
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--starttime") == 0) {
            if (i + 1 < argc) {
                startTime = atoll(argv[i + 1]); 
                ++i; 
            } else {
                std::cerr << "-s|--starttime requires an argument!" << std::endl; 
                exit(EXIT_FAILURE); 
            }
        } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--endtime") == 0) {
            if (i + 1 < argc) {
                endTime = atoll(argv[i + 1]); 
                ++i; 
            } else {
                std::cerr << "-e|--endtime requres an argument!" << std::endl; 
                exit(EXIT_FAILURE); 
            }
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bucketsize") == 0) {
            if (i + 1 < argc) {
                bucketSize = atoll(argv[i + 1]); 
                ++i; 
            } else {
                std::cerr << "-b|--bucketsize requres an argument!" << std::endl; 
                exit(EXIT_FAILURE); 
            }
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--outfile") == 0) {
            if (i + 1 < argc) {
                outfile = argv[i + 1]; 
                ++i; 
            } else {
                std::cerr << "-o|--outfile requres an argument!" << std::endl; 
                exit(EXIT_FAILURE); 
            }
        } else if (strcmp(argv[i], "-fh") == 0 || strcmp(argv[i], "--fusion-horizon") == 0) {
            if (i + 1 < argc) {
                fusionHorizon = atoi(argv[i + 1]);  
                ++i; 
            } else {
                std::cerr << "-fh|--fusion-horizon requres an argument!" << std::endl; 
                exit(EXIT_FAILURE); 
            }
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--threads") == 0) {
            if (i + 1 < argc) {
                numThreads = atoi(argv[i + 1]);  
                ++i; 
            } else {
                std::cerr << "-t|--threads requres an argument!" << std::endl; 
                exit(EXIT_FAILURE); 
            }
        } else {
            std::cerr << argv[i] << " is not recognised!" << std::endl; 
            exit(EXIT_FAILURE); 
        }
    }

    time_t tdiff = endTime - startTime; 
    size_t buckets = tdiff / bucketSize + (tdiff % bucketSize != 0); 
    std::cout << "DB File:        " << dbfile << std::endl; 
    std::cout << "Start time:     " << startTime << "\t" << std::put_time(std::gmtime(&startTime), "%Y-%m-%d %H:%M:%S") << std::endl; 
    std::cout << "End time:       " << endTime << "\t" << std::put_time(std::gmtime(&endTime), "%Y-%m-%d %H:%M:%S") << std::endl; 
    std::cout << "Bucket size:    " << bucketSize << std::endl; 
    std::cout << "Output file:    " << outfile << std::endl; 
    std::cout << "# Buckets:      " << buckets << std::endl; 
    std::cout << "Fusion horizon: " << fusionHorizon << std::endl; 

    // Load messages and create activity
    std::vector<Message> messages; 
    loadMessages(&messages, dbfile); 

    std::unordered_set<uint64_t> userIDs; 
    getUniqueUsers(userIDs, messages); 
    std::cout << "Loaded users, total number: " << userIDs.size() << std::endl; 

    progressbar usersBar(userIDs.size()); 
    std::unordered_map<uint64_t, UserData> userData; 

    std::cout << "Creating users \t\t\t"; 
    for (const auto& i : userIDs) {
        userData[i] = UserData(i, buckets); 
        usersBar.update(); 
    }
    std::cout << std::endl; 
    
    // Process messages
    std::cout << "Processing messages\t\t"; 
    progressbar msgBar(messages.size()); 
    for (const auto& message: messages) {
        size_t bucket = (message.messageTimestamp - startTime) / bucketSize;
        time_t nowTimestamp = startTime + bucketSize * (bucket + 1); 

        userData[message.discordUserID].activities[bucket] += message.get_activity(
            userData[message.discordUserID].lastMessage, nowTimestamp
        ); 
        userData[message.discordUserID].lastMessage = message.messageTimestamp; 

        // std::cout << message.discordUserID << "\t" << bucket << "\t" 
        // << userData[message.discordUserID].activities[bucket] << std::endl; 
        msgBar.update(); 
    }
    std::cout << std::endl; 

    // Convolve
    std::vector<Activity_t> kernel(fusionHorizon); 
    for (int i = 0; i < fusionHorizon; ++i) {
        kernel[i] = std::exp(i * (float) bucketSize * std::log(0.9) / 86400.0); 
    }

    if (numThreads == 1) {      // SINGLE THREAD
        std::cout << "Convolving activity\t\t"; 
        progressbar convBar(userIDs.size()); 
        for (const auto& user: userIDs) {
            for (int i = 0; i < std::min(buckets, fusionHorizon); ++i) {
                for (int j = 0; j <= i; ++j) {
                    userData[user].convdActivities[i] += userData[user].activities[i - j] * kernel[j]; 
                }
            }

            for (int i = fusionHorizon; i < buckets; ++i) {
                for (int j = 0; j < fusionHorizon; ++j) {
                    userData[user].convdActivities[i] += userData[user].activities[i - j] * kernel[j]; 
                }
            }

            convBar.update(); 
        }
        std::cout << std::endl; 
    } else {                    // MULTI THREAD
        std::cout << "Convolving activity\t\t"; 
        progressbar convBar(userIDs.size()); 
        auto it = userIDs.begin(); 
        std::mutex itMutex; 
        bool done = false; 
        
        std::vector<std::thread> threads(numThreads); 
        for (auto& thread : threads) {
            thread = std::thread([&]() {
                while (true) {
                    itMutex.lock(); 
                    uint64_t user = 0; 
                    if (std::next(it) == userIDs.end()) {
                        if (!done) {
                            convBar.update(); 
                            done = true; 
                        }
                        itMutex.unlock(); 
                        break; 
                    } else {
                        user = *it; 
                        ++it; 
                    }
                    convBar.update(); 
                    itMutex.unlock(); 

                    for (int i = 0; i < std::min(buckets, fusionHorizon); ++i) {
                        for (int j = 0; j <= i; ++j) {
                            userData[user].convdActivities[i] += userData[user].activities[i - j] * kernel[j]; 
                        }
                    }

                    for (int i = fusionHorizon; i < buckets; ++i) {
                        for (int j = 0; j < fusionHorizon; ++j) {
                            userData[user].convdActivities[i] += userData[user].activities[i - j] * kernel[j]; 
                        }
                    }

                }
            }); 
        }

        for (auto& thread : threads) {
            thread.join(); 
        }


        std::cout << std::endl; 
    }


    // Write out data
    auto binfile = std::fstream(outfile, std::ios::out | std::ios::binary); 
    binfile.write((char*) &startTime, sizeof(startTime)); 
    binfile.write((char*) &endTime, sizeof(endTime)); 
    binfile.write((char*) &bucketSize, sizeof(bucketSize)); 
    binfile.write((char*) &buckets, sizeof(buckets)); 
    uint64_t numUsers = userIDs.size(); 
    binfile.write((char*) &numUsers, sizeof(numUsers)); 

    std::cout << "Writing data\t\t\t"; 
    progressbar outBar(userIDs.size()); 
    for (const auto& user: userIDs) {
        auto data = userData[user]; 
        binfile.write((char*) &user, sizeof(user)); 
        binfile.write((char*) &data.convdActivities[0], sizeof(data.convdActivities[0]) * buckets); 
        outBar.update(); 
    }
    std::cout << std::endl; 
    binfile.close(); 
    
    return 0;
}
