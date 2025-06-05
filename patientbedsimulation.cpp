/*
MIT License

Copyright (c) 2025 Rohit Nair

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// Author: Rohit Nair

#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>
#include "mqtt/async_client.h" // Paho MQTT C++
#include <nlohmann/json.hpp> // For JSON manipulation

// --- Configuration ---
const std::string SERVER_ADDRESS("ssl://a22bv8r2s2kek2-ats.iot.eu-north-1.amazonaws.com:8883"); // Your AWS IoT Endpoint
const std::string CLIENT_ID_PREFIX("PatientBed");
const std::string TOPIC_PREFIX("PatientBed/");
const int QOS = 1;
const long TIMEOUT = 10000L; // Milliseconds

// --- Certificate Paths ---
const std::string CA_CERT_PATH("./certs/AmazonRootCA1.pem");
const std::string CLIENT_CERT_PATH_PREFIX("./certs/device_");
const std::string CLIENT_KEY_PATH_PREFIX("./certs/device_");

// --- Simulation Parameters ---
const int DATA_SEND_INTERVAL_SECONDS = 5;

// --- Inclination Parameters ---
const double MEAL_INCLINATION_DEGREES = 60.0;
const int MEAL_INCLINATION_DURATION_MINUTES = 30;
const double MINOR_INCLINATION_DEGREES = 30.0;
const int MINOR_INCLINATION_DURATION_BASE_MINUTES = 10;
const int MINOR_INCLINATION_DURATION_RAND_ADD_MINUTES = 5;
const int FLAT_STATE_BASE_DURATION_MINUTES = 45;
const int FLAT_STATE_RAND_ADD_MINUTES = 15;
const double PROBABILITY_MINOR_INCLINE = 0.20;

// Meal times (interpreted as local system time: Hour, Minute)
const std::vector<std::pair<int, int>> meal_start_times = {{8, 0}, {12, 0}, {18, 0}};

// For JSON
using json = nlohmann::json;

// Bed State
enum class BedInclinationState {
    FLAT,
    INCLINED
};

/**
 * @brief Get current timestamp in local system time in ISO 8601 format with offset.
 * @return std::string Timestamp string.
 */
std::string getCurrentTimestampLocal() {
    auto now = std::chrono::system_clock::now();
    auto itt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_local = *std::localtime(&itt); // Uses system's configured local timezone

    char buf[80];
    // Attempt to format with timezone offset (%z).
    // The output of %z can vary (e.g., +0530 on Linux, may be different on other systems/compilers).
    // ISO 8601 prefers +HH:MM, but +HHMM is also valid.
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tm_local);
    
    std::string timed_str = buf;
    // Optional: To try and force a colon in the timezone offset if %z gives +HHMM
    // This part is a bit more complex if %z format isn't consistent or if it's empty
    // For simplicity, we'll use what strftime with %z provides.
    // If more robust ISO 8601 timezone formatting is needed, a dedicated date-time library
    // like Howard Hinnant's 'tz' or C++20 chrono features would be better.

    return timed_str;
}

/**
 * @brief Telemetry class holds patient bed telemetry data and serializes it to JSON.
 */
class Telemetry {
public:
    std::string deviceId;
    std::string timestamp;
    double heartRate;
    double spo2;
    double inclination;
    std::string bedState; 

    /**
     * @brief Construct a new Telemetry object.
     * @param id Device ID.
     * @param hr Heart rate.
     * @param oxygen SpO2 value.
     * @param incl Bed inclination.
     * @param state Bed state (FLAT/INCLINED).
     */
    Telemetry(std::string id, double hr, double oxygen, double incl, BedInclinationState state)
        : deviceId(id), heartRate(hr), spo2(oxygen), inclination(incl) {
        timestamp = getCurrentTimestampLocal(); // Use local system timestamp
        bedState = (state == BedInclinationState::FLAT) ? "FLAT" : "INCLINED";
    }

    /**
     * @brief Serialize telemetry data to JSON string.
     * @return std::string JSON representation.
     */
    std::string toJson() const {
        json j;
        j["deviceId"] = deviceId;
        j["timestamp"] = timestamp;
        j["heartRate"] = heartRate;
        j["spo2"] = spo2;
        j["inclination"] = inclination;
        j["bedState"] = bedState;
        return j.dump(4);
    }
};

/**
 * @brief MQTT callback handler for connection events and message delivery.
 */
class callback : public virtual mqtt::callback {
    mqtt::async_client& cli_;
    void connected(const std::string& cause) override {
        std::cout << "\n[" << getCurrentTimestampLocal() << "] Connection success" << std::endl;
    }
    void connection_lost(const std::string& cause) override {
        std::cerr << "\n[" << getCurrentTimestampLocal() << "] Connection lost: " << cause << std::endl;
    }
    void message_arrived(mqtt::const_message_ptr msg) override {
        std::cout << "Message arrived on topic: " << msg->get_topic() << std::endl;
        std::cout << "\tPayload: " << msg->to_string() << std::endl;
    }
    void delivery_complete(mqtt::delivery_token_ptr tok) override { /* Optional */ }
public:
    /**
     * @brief Construct a new callback object.
     * @param client Reference to MQTT async client.
     */
    callback(mqtt::async_client& client) : cli_(client) {}
};

/**
 * @brief Main function for Patient Bed Simulator.
 * Connects to MQTT, simulates telemetry, and publishes data at intervals.
 */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <device_instance_number (e.g., 1 or 2)>" << std::endl;
        return 1;
    }

    std::string deviceInstanceNumStr = argv[1];
    std::string clientId = CLIENT_ID_PREFIX + deviceInstanceNumStr;
    std::string topic = TOPIC_PREFIX + deviceInstanceNumStr + "/data";
    std::string clientCertPath = CLIENT_CERT_PATH_PREFIX + deviceInstanceNumStr + ".pem.crt";
    std::string clientKeyPath = CLIENT_KEY_PATH_PREFIX + deviceInstanceNumStr + ".private.key";

    std::cout << "[" << getCurrentTimestampLocal() << "] Starting Patient Bed Simulator: " << clientId << std::endl;
    std::cout << "[" << getCurrentTimestampLocal() << "] Publishing to topic: " << topic << std::endl;
    
    mqtt::async_client client(SERVER_ADDRESS, clientId);
    callback cb(client);
    client.set_callback(cb);
    mqtt::ssl_options ssl_opts;
    ssl_opts.set_trust_store(CA_CERT_PATH);
    ssl_opts.set_key_store(clientCertPath);
    ssl_opts.set_private_key(clientKeyPath);
    mqtt::connect_options conn_opts;
    conn_opts.set_keep_alive_interval(60);
    conn_opts.set_clean_session(true);
    conn_opts.set_ssl(ssl_opts);
    conn_opts.set_automatic_reconnect(true);

    std::cout << "[" << getCurrentTimestampLocal() << "] Connecting to MQTT broker at " << SERVER_ADDRESS << "..." << std::endl;
    try {
        client.connect(conn_opts)->wait();
    } catch (const mqtt::exception& exc) {
        std::cerr << "[" << getCurrentTimestampLocal() << "] Error connecting: " << exc.what() << std::endl;
        return 1;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> heart_rate_dist(55.0, 85.0);
    std::uniform_real_distribution<> spo2_dist(95.0, 99.5);
    std::uniform_real_distribution<> probability_dist(0.0, 1.0);
    std::uniform_int_distribution<> minor_incline_duration_rand_addon(0, MINOR_INCLINATION_DURATION_RAND_ADD_MINUTES -1);
    std::uniform_int_distribution<> flat_duration_rand_addon(0, FLAT_STATE_RAND_ADD_MINUTES -1);

    BedInclinationState currentInclinationState = BedInclinationState::FLAT;
    double currentInclination = 0.0;
    auto lastNonMealStateChangeTime = std::chrono::steady_clock::now();
    int currentNonMealStateDurationSeconds = (FLAT_STATE_BASE_DURATION_MINUTES + flat_duration_rand_addon(gen)) * 60;
    bool inMealInclineOverride = false;

    while (true) {
        double hr = heart_rate_dist(gen);
        double spo2 = spo2_dist(gen);

        // --- Inclination Logic using Local System Time ---
        auto now_for_time_check = std::chrono::system_clock::now();
        time_t itt_for_check = std::chrono::system_clock::to_time_t(now_for_time_check);
        std::tm current_local_tm_struct = *std::localtime(&itt_for_check); // Uses system's local timezone

        int current_hour_local = current_local_tm_struct.tm_hour;
        int current_minute_local = current_local_tm_struct.tm_min;

        bool is_currently_meal_time_slot = false;
        for (const auto& meal_time : meal_start_times) { // Uses local meal times
            int meal_start_hour = meal_time.first;
            int meal_start_minute = meal_time.second;
            
            int current_total_minutes_from_midnight = current_hour_local * 60 + current_minute_local;
            int meal_start_total_minutes_from_midnight = meal_start_hour * 60 + meal_start_minute;
            int meal_end_total_minutes_from_midnight = meal_start_total_minutes_from_midnight + MEAL_INCLINATION_DURATION_MINUTES;

            if (current_total_minutes_from_midnight >= meal_start_total_minutes_from_midnight &&
                current_total_minutes_from_midnight < meal_end_total_minutes_from_midnight) {
                is_currently_meal_time_slot = true;
                break;
            }
        }

        auto now_steady = std::chrono::steady_clock::now();

        if (is_currently_meal_time_slot) {
            if (!inMealInclineOverride) {
                 std::cout << "[" << getCurrentTimestampLocal() << "] Bed " << deviceInstanceNumStr << " INCLINED for meal to " << MEAL_INCLINATION_DEGREES << " degrees." << std::endl;
            }
            currentInclination = MEAL_INCLINATION_DEGREES;
            currentInclinationState = BedInclinationState::INCLINED;
            inMealInclineOverride = true;
        } else { 
            if (inMealInclineOverride) { 
                currentInclination = 0.0;
                currentInclinationState = BedInclinationState::FLAT;
                currentNonMealStateDurationSeconds = (FLAT_STATE_BASE_DURATION_MINUTES + flat_duration_rand_addon(gen)) * 60;
                lastNonMealStateChangeTime = now_steady;
                inMealInclineOverride = false;
                std::cout << "[" << getCurrentTimestampLocal() << "] Bed " << deviceInstanceNumStr << " set to FLAT after meal." << std::endl;
            } else { 
                auto elapsedSinceLastNonMealChange_seconds = std::chrono::duration_cast<std::chrono::seconds>(now_steady - lastNonMealStateChangeTime).count();

                if (elapsedSinceLastNonMealChange_seconds >= currentNonMealStateDurationSeconds) {
                    if (currentInclinationState == BedInclinationState::FLAT) {
                        if (probability_dist(gen) < PROBABILITY_MINOR_INCLINE) {
                            currentInclination = MINOR_INCLINATION_DEGREES;
                            currentInclinationState = BedInclinationState::INCLINED;
                            currentNonMealStateDurationSeconds = (MINOR_INCLINATION_DURATION_BASE_MINUTES + minor_incline_duration_rand_addon(gen)) * 60;
                            std::cout << "[" << getCurrentTimestampLocal() << "] Bed " << deviceInstanceNumStr << " INCLINED (minor) to " << currentInclination << " degrees." << std::endl;
                        } else {
                            currentInclination = 0.0; 
                            currentInclinationState = BedInclinationState::FLAT;
                            currentNonMealStateDurationSeconds = (FLAT_STATE_BASE_DURATION_MINUTES + flat_duration_rand_addon(gen)) * 60;
                        }
                    } else { 
                        currentInclination = 0.0;
                        currentInclinationState = BedInclinationState::FLAT;
                        currentNonMealStateDurationSeconds = (FLAT_STATE_BASE_DURATION_MINUTES + flat_duration_rand_addon(gen)) * 60;
                        std::cout << "[" << getCurrentTimestampLocal() << "] Bed " << deviceInstanceNumStr << " set to FLAT after minor incline." << std::endl;
                    }
                    lastNonMealStateChangeTime = now_steady;
                }
            }
        }
        // --- End of Inclination Logic ---

        Telemetry telemetryData(clientId, hr, spo2, currentInclination, currentInclinationState);
        std::string payload = telemetryData.toJson();

        mqtt::message_ptr pubmsg = mqtt::make_message(topic, payload);
        pubmsg->set_qos(QOS);

        try {
            if (!client.is_connected()) {
                 std::cerr << "[" << getCurrentTimestampLocal() << "] Client not connected. Retrying connection by Paho..." << std::endl;
            }
            client.publish(pubmsg)->wait();
        } catch (const mqtt::exception& exc) {
            std::cerr << "[" << getCurrentTimestampLocal() << "] Error publishing: " << exc.what() << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(DATA_SEND_INTERVAL_SECONDS));
    }

    try {
        std::cout << "\n[" << getCurrentTimestampLocal() << "] Disconnecting..." << std::endl;
        client.disconnect()->wait();
        std::cout << "[" << getCurrentTimestampLocal() << "] Disconnected." << std::endl;
    } catch (const mqtt::exception& exc) {
        std::cerr << "[" << getCurrentTimestampLocal() << "] Error disconnecting: " << exc.what() << std::endl;
    }

    return 0;
}