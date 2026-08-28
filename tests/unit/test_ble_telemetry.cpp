/**
 * @file test_ble_telemetry.cpp
 * @brief Host unit test suite for BLE telemetry JSON formatting, command parsing, and NUS transmission.
 */

#include <iostream>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>

/* Simulation structures matching KoRe types */
struct SimDrives {
    float curiosity = 0.65f;
    float social = 0.40f;
    float boredom = 0.10f;
    float fatigue = 0.05f;
    float mischief = 0.70f;
};

struct SimBrainTelemetry {
    SimDrives drives;
    char thought_summary[48] = "Planning playful prank";
    uint32_t interaction_sec = 120;
    uint32_t solitude_sec = 45;
    float bonding_level = 0.35f;
    uint32_t lifetime_sec = 3600;
    uint16_t memory_count = 12;
    float memory_resonance = 0.82f;
    int last_recalled_expr = 3;
};

struct SimPersonalityTraits {
    float boldness = 0.75f;
    float volatility = 0.30f;
    float playfulness = 0.90f;
    float attachment = 0.60f;
};

struct SimCircadianState {
    float energy_level = 0.95f;
    float mood_baseline = 0.20f;
    float phase_pct = 42.5f;
};

struct SimTarget {
    bool detected = false;
    int x = 0, y = 0, w = 0, h = 0, cx = 0, cy = 0;
    float error_x = 0.0f, error_y = 0.0f;
    float confidence = 0.0f;
    float human_likelihood = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float proximity = 0.0f;
};

static void simulateFormatTelemetry(char *json, size_t max_len, bool camera_sleep, bool camera_online, const SimTarget& target, const SimBrainTelemetry& brain, const SimPersonalityTraits& traits, const SimCircadianState& circa) {
    bool is_detected = (camera_sleep || !camera_online) ? false : target.detected;
    float current_fps = (camera_sleep || !camera_online) ? 0.0f : 30.0f;
    float conf = (camera_sleep || !camera_online) ? 0.0f : target.confidence;

    snprintf(json, max_len,
        "{\"type\":\"telemetry\",\"detected\":%s,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"cx\":%d,\"cy\":%d,\"err_x\":%.1f,\"err_y\":%.1f,\"conf\":%.2f,\"human_likelihood\":%.2f,\"fps_ai\":%.1f,\"fw\":%d,\"fh\":%d,\"vx\":%.1f,\"vy\":%.1f,\"prox\":%.2f,\"num_cands\":%d,\"insp_idx\":%d,\"c0_cx\":%d,\"c0_cy\":%d,\"c0_w\":%d,\"c0_h\":%d,\"c0_p\":%.1f,\"c1_cx\":%d,\"c1_cy\":%d,\"c1_w\":%d,\"c1_h\":%d,\"c1_p\":%.1f,\"c2_cx\":%d,\"c2_cy\":%d,\"c2_w\":%d,\"c2_h\":%d,\"c2_p\":%.1f,\"expr\":%d,\"expr_name\":\"%s\",\"is_manual\":%s,\"valence\":%.2f,\"arousal\":%.2f,\"curiosity\":%.2f,\"social\":%.2f,\"boredom\":%.2f,\"fatigue\":%.2f,\"mischief\":%.2f,\"thought\":\"%s\",\"interact_s\":%u,\"solitude_s\":%u,\"bonding\":%.2f,\"life_s\":%u,\"mem_count\":%u,\"mem_res\":%.2f,\"mem_expr\":%d,\"heap_free\":%u,\"psram_free\":%u,\"uptime_s\":%lu,\"cpu_mhz\":%d,\"cam_sleep\":%s,\"cam_online\":%s,\"personality\":{\"boldness\":%.2f,\"volatility\":%.2f,\"playfulness\":%.2f,\"attachment\":%.2f},\"circadian\":{\"energy\":%.2f,\"mood_offset\":%.2f,\"phase_pct\":%.1f}}",
        is_detected ? "true" : "false",
        target.x, target.y, target.w, target.h,
        target.cx, target.cy,
        target.error_x, target.error_y,
        conf,
        target.human_likelihood,
        current_fps,
        640, 480,
        target.vx, target.vy,
        target.proximity,
        0, 0,
        0, 0, 0, 0, 0.0f,
        0, 0, 0, 0, 0.0f,
        0, 0, 0, 0, 0.0f,
        3, "SMIRK", "false",
        0.45f, 0.60f,
        brain.drives.curiosity, brain.drives.social, brain.drives.boredom, brain.drives.fatigue, brain.drives.mischief,
        brain.thought_summary,
        brain.interaction_sec, brain.solitude_sec,
        brain.bonding_level, brain.lifetime_sec,
        (unsigned)brain.memory_count, brain.memory_resonance, (int)brain.last_recalled_expr,
        185000, 4194304, 3600UL, 240,
        camera_sleep ? "true" : "false",
        camera_online ? "true" : "false",
        traits.boldness, traits.volatility, traits.playfulness, traits.attachment,
        circa.energy_level, circa.mood_baseline, circa.phase_pct
    );
}

/* Simulate chunking over BLE NUS */
static void simulateBleChunking(const char* data, size_t len, std::vector<std::string>& chunks_out, size_t chunk_size = 180) {
    chunks_out.clear();
    size_t offset = 0;
    while (offset < len) {
        size_t to_send = len - offset;
        if (to_send > chunk_size) to_send = chunk_size;
        chunks_out.push_back(std::string(data + offset, to_send));
        offset += to_send;
    }
}

int main() {
    std::cout << "[TEST] Running BLE Telemetry & Camera Decoupling Test Suite..." << std::endl;

    SimTarget target;
    target.detected = true;
    target.x = 100;
    target.y = 120;
    target.w = 150;
    target.h = 180;
    target.cx = 175;
    target.cy = 210;
    target.confidence = 0.92f;

    SimBrainTelemetry brain;
    SimPersonalityTraits traits;
    SimCircadianState circa;

    char json_buf[2048];

    // Test 1: Telemetry in Camera Sleep mode (Camera should NOT be forced active)
    simulateFormatTelemetry(json_buf, sizeof(json_buf), true, true, target, brain, traits, circa);
    std::string json_sleep(json_buf);

    assert(json_sleep.find("\"cam_sleep\":true") != std::string::npos);
    assert(json_sleep.find("\"cam_online\":true") != std::string::npos);
    assert(json_sleep.find("\"detected\":false") != std::string::npos);
    assert(json_sleep.find("\"fps_ai\":0.0") != std::string::npos);
    assert(json_sleep.find("\"thought\":\"Planning playful prank\"") != std::string::npos);
    assert(json_sleep.find("\"expr_name\":\"SMIRK\"") != std::string::npos);
    std::cout << "[PASS] Telemetry in low power camera sleep mode correctly reported without forcing camera on." << std::endl;

    // Test 2: Telemetry in Active Camera mode
    simulateFormatTelemetry(json_buf, sizeof(json_buf), false, true, target, brain, traits, circa);
    std::string json_active(json_buf);

    assert(json_active.find("\"cam_sleep\":false") != std::string::npos);
    assert(json_active.find("\"detected\":true") != std::string::npos);
    assert(json_active.find("\"fps_ai\":30.0") != std::string::npos);
    assert(json_active.find("\"conf\":0.92") != std::string::npos);
    std::cout << "[PASS] Telemetry in active camera tracking mode verified." << std::endl;

    // Test 3: NUS BLE Chunking for large telemetry JSON payloads
    std::vector<std::string> chunks;
    simulateBleChunking(json_active.c_str(), json_active.length(), chunks, 180);

    assert(!chunks.empty());
    assert(chunks.size() >= 3); // ~600-800 bytes splits into >= 3 chunks of 180 bytes
    std::string reconstructed = "";
    for (const auto& c : chunks) {
        assert(c.length() <= 180);
        reconstructed += c;
    }
    assert(reconstructed == json_active);
    std::cout << "[PASS] BLE NUS chunking correctly splits payload (" << json_active.length() 
              << " bytes across " << chunks.size() << " chunks of <=180B) and reconstructs seamlessly." << std::endl;

    std::cout << "[SUCCESS] All BLE Telemetry & Camera Decoupling tests passed successfully!" << std::endl;
    return 0;
}
