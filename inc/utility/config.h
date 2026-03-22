#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>


struct Config {

    uint16_t port = 8080;
    std::string bind = "0.0.0.0";
	int shardCount = std::thread::hardware_concurrency();
    int maintenanceIntervalMs = 100;
    uint64_t maxMemoryUsage = 8ULL * 1024 * 1024 * 1024; // 8 GB

    static Config loadFromFile(const std::string& path)
    {
        try
        {
            if (!std::filesystem::exists(path))
            {
                std::cout << "Config file not found, using default configuration." << std::endl;
                return Config();
            }

            std::ifstream file(path);

            if (!file.is_open())
            {
                std::cerr << "Failed to open config file: " << path << ", using default configuration." << std::endl;
                return Config();
            }

            Config config;
            std::string line;
            int lineNumber = 0;

            while (std::getline(file, line))
            {
                ++lineNumber;

                // Skip leading whitespace
                size_t pos = line.find_first_not_of(" \t\r");
                if (pos == std::string::npos) continue;  // Empty line
                
                // Skip comment lines
                if (line[pos] == '#') continue;
                
                line = line.substr(pos);
                
                // Extract key
                pos = line.find_first_of(" \t");
                if (pos == std::string::npos) continue;  // No value
                std::string key = line.substr(0, pos);
                
                // Extract value
                size_t valueStart = line.find_first_not_of(" \t", pos);
                if (valueStart == std::string::npos) continue;  // No value
                
                size_t valueEnd = line.find_first_of(" \t#", valueStart);  // Also handle comments
                std::string value = line.substr(valueStart, valueEnd - valueStart);
                
                if (value.empty()) continue;

                try
                {
                    if (key == "port")
                    {
                        int portVal = std::stoi(value);
                        if (portVal < 1 || portVal > 65535)
                        {
                            std::cerr << "Warning: Invalid port value " << portVal 
                                      << " on line " << lineNumber << ", using default." << std::endl;
                        }
                        else
                        {
                            config.port = static_cast<uint16_t>(portVal);
                        }
                    }
                    else if (key == "bind")
                    {
                        config.bind = value;
                    }
                    else if (key == "maintenance_interval_ms")
                    {
                        int intervalVal = std::stoi(value);
                        if (intervalVal < 0)
                        {
                            std::cerr << "Warning: Invalid maintenance_interval_ms value " << intervalVal 
                                      << " on line " << lineNumber << ", using default." << std::endl;
                        }
                        else
                        {
                            config.maintenanceIntervalMs = intervalVal;
                        }
                    }
                    else if (key == "max_memory_usage")
                    {
                        config.maxMemoryUsage = std::stoull(value);
                    }
                    else if (key == "shards")
                    {
                        int shardVal = std::stoi(value);
                        if (shardVal < 1 || shardVal > std::thread::hardware_concurrency())
                            std::cerr << "Warning: invalid shard count, can be between 1 and " << std::thread::hardware_concurrency() << std::endl;
                        else
                            config.shardCount = static_cast<uint32_t>(shardVal);
                    }
                    else
                    {
                        std::cerr << "Warning: Unknown configuration key '" << key 
                                  << "' on line " << lineNumber << std::endl;
                    }
                }
                catch (const std::invalid_argument& e)
                {
                    std::cerr << "Warning: Invalid value '" << value << "' for key '" << key 
                              << "' on line " << lineNumber << ": " << e.what() << std::endl;
                }
                catch (const std::out_of_range& e)
                {
                    std::cerr << "Warning: Value out of range '" << value << "' for key '" << key 
                              << "' on line " << lineNumber << ": " << e.what() << std::endl;
                }
            }
            return config;
        } 
        catch (const std::exception& e) 
        {
            std::cerr << "Error loading config file: " << e.what() << ", using default configuration." << std::endl;
            return Config();
        }
    }
};