#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <streambuf>
#include <string> 
#include <unordered_map>
#include <utility> 
#include <vector>


#include "progressbar/include/progressbar.hpp"

struct InputData {
    time_t startTime; 
    time_t endTime; 
    uint64_t bucketSize; 
    uint64_t buckets; 
    uint64_t users; 
    std::vector<std::vector<std::pair<uint64_t, double>>> bucketData; 
}; 


struct Frame {
    time_t frameTime; 
    double totalScore; 
    std::vector<uint64_t> userIDs; 
    std::vector<double> placing; 
    std::vector<double> score; 

    void serialise(std::ofstream& ofstream) const {
        uint64_t numUsers = userIDs.size(); 
        char buf[sizeof(frameTime) 
                 + sizeof(totalScore) 
                 + sizeof(numUsers) 
                 + numUsers * sizeof(userIDs[0])
                 + numUsers * sizeof(placing[0]) 
                 + numUsers * sizeof(score[0])]; 

        char* curBuf = buf; 
        memcpy(curBuf, &frameTime, sizeof(frameTime)); 
        curBuf += sizeof(frameTime); 
        memcpy(curBuf, &totalScore, sizeof(totalScore)); 
        curBuf += sizeof(totalScore); 
        memcpy(curBuf, &numUsers, sizeof(numUsers)); 
        curBuf += sizeof(numUsers); 
        memcpy(curBuf, &userIDs[0], numUsers * sizeof(userIDs[0])); 
        curBuf += numUsers * sizeof(userIDs[0]); 
        memcpy(curBuf, &placing[0], numUsers * sizeof(placing[0])); 
        curBuf += numUsers * sizeof(placing[0]); 
        memcpy(curBuf, &score[0], numUsers * sizeof(score[0])); 

        ofstream.write(buf, sizeof(buf));       
    }
}; 


InputData loadInputData(std::string filename) {
    InputData data; 

    // Load header data
    std::ifstream ifStream = std::ifstream(filename, std::ios::binary); 
    ifStream.read(reinterpret_cast<char*>(&data.startTime), sizeof(data.startTime)); 
    ifStream.read(reinterpret_cast<char*>(&data.endTime), sizeof(data.endTime)); 
    ifStream.read(reinterpret_cast<char*>(&data.bucketSize), sizeof(data.bucketSize)); 
    ifStream.read(reinterpret_cast<char*>(&data.buckets), sizeof(data.buckets)); 
    ifStream.read(reinterpret_cast<char*>(&data.users), sizeof(data.users)); 

    std::cout << "=====   Input File Parameters   =====" << std::endl; 
    std::cout << "Start time:          " << data.startTime << std::endl; 
    std::cout << "End time:            " << data.endTime << std::endl; 
    std::cout << "Bucket Size:         " << data.bucketSize << std::endl; 
    std::cout << "# Buckets:           " << data.buckets << std::endl; 
    std::cout << "# Users:             " << data.users << std::endl; 

    // Load activity data
    progressbar dataBar(data.users); 
    std::cout << "Loading data\t\t\t"; 
    data.bucketData.reserve(data.buckets); 
    for (int i = 0; i < data.buckets; ++i)       data.bucketData.push_back(std::vector<std::pair<uint64_t, double>>()); 
    size_t bytes_per_user = 8 + 8 * data.buckets; 
    char buf[bytes_per_user]; 
    for (int i = 0; i < data.users; ++i) {
        ifStream.read(buf, sizeof(buf)); 
        uint64_t userID = *reinterpret_cast<uint64_t*>(&buf[0]); 
        double* activities = reinterpret_cast<double*>(&buf[8]); 

        for (int j = 0; j < data.buckets; ++j) {
            data.bucketData[j].push_back(std::pair(userID, activities[j])); 
        }

        dataBar.update(); 
    }
    std::cout << std::endl; 

    return data; 
}


Frame parseKeyFrame(const InputData& inputData, int usersPerFrame, int frameNum) {
    Frame frame; 
    const std::vector<std::pair<uint64_t, double>>& frameData = inputData.bucketData[frameNum]; 

    frame.frameTime = inputData.startTime + inputData.bucketSize * frameNum; 
    frame.totalScore = 0; 
    std::for_each(frameData.begin(), frameData.end(), [&] (auto x) {frame.totalScore += x.second;}); 
    for (int i = 0; i < usersPerFrame; ++i) {
        frame.userIDs.push_back(frameData[i].first); 
        frame.placing.push_back(i + 1); 
        frame.score.push_back(frameData[i].second); 
    }

    return frame; 
}


std::vector<Frame> interpolateFrames(const Frame& lastFrame, 
                                     const Frame& nextFrame, 
                                     const InputData& inputData, 
                                     int frameIdx, 
                                     int usersPerFrame, 
                                     int framesPerData) {
    // Find the scores in both the first frame and the next
    const std::vector<std::pair<uint64_t, double>>& curScores = inputData.bucketData[frameIdx]; 
    const std::vector<std::pair<uint64_t, double>>& nxtScores = inputData.bucketData[frameIdx + 1]; 
    std::map<uint64_t, std::tuple<uint64_t, double, uint64_t, double>> scores;     // userID -> (current placing, current score, next placing, next score)

    for (int i = 0; i < usersPerFrame; ++i) {
        if (scores.find(curScores[i].first) != scores.end()) {
            scores[curScores[i].first] = {100, -1.0, 100, -1.0}; 
        }
        if (scores.find(nxtScores[i].first) != scores.end()) {
            scores[nxtScores[i].first] = {100, -1.0, 100, -1.0}; 
        }
    }
    for (int i = 0; i < usersPerFrame; ++i) {
        std::get<0>(scores[curScores[i].first]) = i + 1; 
        std::get<1>(scores[curScores[i].first]) = curScores[i].second; 
        std::get<2>(scores[nxtScores[i].first]) = i + 1; 
        std::get<3>(scores[nxtScores[i].first]) = nxtScores[i].second; 
    }
    for (int i = 0; i < usersPerFrame; ++i) {       // Find all first framers in the second frame
        uint64_t userID = curScores[i].first; 
        for (int i = 0; i < nxtScores.size(); ++i) {
            if (nxtScores[i].first == userID) {
                std::get<2>(scores[userID]) = i + 1; 
                std::get<3>(scores[userID]) = nxtScores[i].second; 
            }
        }
    }
    for (int i = 0; i < usersPerFrame; ++i) {       // Find all second framers in the first frame
        uint64_t userID = nxtScores[i].first; 
        for (int i = 0; i < curScores.size(); ++i) {
            if (curScores[i].first == userID) {
                std::get<0>(scores[userID]) = i + 1; 
                std::get<1>(scores[userID]) = curScores[i].second; 
            }
        }    
    }

    // for (const auto& [x, y] : scores) {
    //     std::cout << x << "\t" << std::get<0>(y) << "\t" << std::get<1>(y) 
    //               << "\t" << std::get<2>(y) << "\t" << std::get<3>(y) << std::endl; 
    // }   std::cout << std::endl; 

    // Create the interpolated frames
    std::vector<Frame> frames(framesPerData - 1); 
    for (int i = 0; i < framesPerData - 1; ++i) {
        double coeff = 1 - ((double) i + 1.0) / framesPerData; 

        frames[i].frameTime = inputData.startTime + inputData.bucketSize * (frameIdx + 1 - coeff); 
        frames[i].totalScore = coeff * lastFrame.totalScore + (1 - coeff) * nextFrame.totalScore; 
        
        std::vector<std::tuple<uint64_t, double, double>> interpScores; 
        for (const auto& [userID, data] : scores) {
            interpScores.emplace_back(
                userID, 
                coeff * static_cast<double>(std::get<0>(data)) + (1 - coeff) * static_cast<double>(std::get<2>(data)), 
                coeff * static_cast<double>(std::get<1>(data)) + (1 - coeff) * static_cast<double>(std::get<3>(data))
            ); 
        }

        std::sort(interpScores.begin(), interpScores.end(), [] (const auto& x1, const auto& x2) {
            return std::get<2>(x1) > std::get<2>(x2); 
        }); 
        for (int i = 0; i < interpScores.size(); ++i) {
            if (std::get<1>(interpScores[i]) > usersPerFrame + 5) {
                interpScores.erase(interpScores.begin() + i); 
                --i; 
            }
        }

        // for (const auto& x : interpScores) {
        //     std::cout << std::get<0>(x) << "\t" << std::get<1>(x) << "\t" << std::get<2>(x) << std::endl; 
        // }   std::cout << std::endl; 

        for (const auto& x : interpScores) {
            frames[i].userIDs.push_back(std::get<0>(x)); 
            frames[i].placing.push_back(std::get<1>(x)); 
            frames[i].score.push_back(std::get<2>(x)); 
        }
    }

    return frames; 
}

int main(int argc, const char** argv) {
    // Default parameters
    std::string inputFile = "tbl2021.bin"; 
    std::string outputFile = "frameinfo.bin"; 
    int framesPerData = 4; 
    int usersPerFrame = 15; 

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0) {
            // TODO 
            exit(EXIT_SUCCESS); 
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) {
            if (i + 1 < argc) {
                inputFile = argv[i + 1];
                ++i;  
            } else {
                std::cerr << "-i|--input requires an argument!" << std::endl; 
                exit(EXIT_FAILURE); 
            }
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                outputFile = argv[i + 1];
                ++i;  
            } else {
                std::cerr << "-o|--output requires an argument!" << std::endl; 
                exit(EXIT_FAILURE); 
            }
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--framesperdata") == 0) {
            if (i + 1 < argc) {
                framesPerData = atoi(argv[i + 1]);
                ++i;  
            } else {
                std::cerr << "-f|--framesperdata requires an argument!" << std::endl; 
                exit(EXIT_FAILURE); 
            }
        } else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--usersperframe") == 0) {
            if (i + 1 < argc) {
                usersPerFrame = atoi(argv[i + 1]);
                ++i;  
            } else {
                std::cerr << "-u|--usersperframe requires an argument!" << std::endl; 
                exit(EXIT_FAILURE); 
            }
        } else {
            std::cerr << argv[i] << " is not recognised!" << std::endl; 
            exit(EXIT_FAILURE); 
        }
    }

    std::cout << "=====    Frame Gen Parameters   =====" << std::endl; 
    std::cout << "Input file:          " << inputFile << std::endl; 
    std::cout << "Output file:         " << outputFile << std::endl; 
    std::cout << "Frames Per Point:    " << framesPerData << std::endl; 
    std::cout << "Users Per Frame:     " << usersPerFrame << std::endl; 

    // Load and sort input data
    InputData inputData = loadInputData(inputFile); 
    std::cout << "Sorting data\t\t\t"; 
    progressbar sortBar(inputData.buckets); 
    for (auto& bucketData: inputData.bucketData) {
        std::sort(bucketData.begin(), bucketData.end(), 
            [] (const auto& a1, const auto& a2) {
                if (a2.second == a1.second)     return a1.first < a2.first; 
                return a2.second < a1.second; 
            }
        ); 
        sortBar.update(); 
    }
    std::cout << std::endl; 

    std::cout << "Generating frames\t\t"; 
    progressbar framesBar(inputData.buckets); 

    // Generate frames
    std::ofstream framesFile(outputFile, std::ios::out | std::ios::binary); 
    uint64_t numFrames = framesPerData * (inputData.buckets - 1) + 1; 
    framesFile.write(reinterpret_cast<char*>(&numFrames), sizeof(numFrames)); 
    Frame lastFrame = parseKeyFrame(inputData, usersPerFrame, 0); 
    for (int i = 0; i < inputData.buckets - 1; ++i) {
        lastFrame.serialise(framesFile); 
        Frame nextFrame = parseKeyFrame(inputData, usersPerFrame, i + 1); 

        for (const auto& x : interpolateFrames(lastFrame, nextFrame, inputData, i, usersPerFrame, framesPerData)) {
            x.serialise(framesFile); 
        }

        lastFrame = nextFrame; 
        framesBar.update(); 
    }
    lastFrame.serialise(framesFile); 
    framesBar.update(); 

    std::cout << std::endl; 

    // int frameInspect = 250; 
    // Frame lf = parseKeyFrame(inputData, usersPerFrame, frameInspect); 
    // Frame nf = parseKeyFrame(inputData, usersPerFrame, frameInspect + 1); 
    // interpolateFrames(lf, nf, inputData, frameInspect, usersPerFrame, framesPerData); 
}