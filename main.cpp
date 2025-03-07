#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <array>
#endif

#include <iostream>
#include <string>
#include <cstdlib>    // 用于 getenv
#include <chrono>     // 用于计时
#include <iomanip>    // 用于格式化输出
#include <sstream>    // 用于字符串流处理
#include <vector>
#include <memory>     // 用于智能指针
#include <limits>
#include <algorithm>
#include "YOLOv12.h"

// 定义终端输出颜色
namespace Color {
    const std::string RED    = "\033[31m";
    const std::string GREEN  = "\033[32m";
    const std::string YELLOW = "\033[33m";
    const std::string BLUE   = "\033[34m";
    const std::string CYAN   = "\033[36m";  // 新增 CYAN 定义
    const std::string RESET  = "\033[0m";
}

// 用于检测是否在无头模式下运行
bool isHeadlessEnvironment() {
    char* display = getenv("DISPLAY");
    return (display == nullptr || strlen(display) == 0);
}

// =======================
// 系统状态与监控模块
// =======================

// 存放系统监控信息
struct SystemStatus {
    // CPU信息
    std::vector<float> cpu_usage;  // 每个核心的使用率（简化用，仅取第0核心）
    int cpu_freq_mhz{0};           // CPU当前频率 (MHz)
    
    // GPU信息（Jetson相关）
    float gpu_usage{0.0f};         // GPU使用率 (%)
    int gpu_freq_mhz{0};           // GPU当前频率 (MHz)
    
    // 内存信息
    float mem_used_mb{0.0f};       // 已用内存 (MB)
    float mem_total_mb{0.0f};      // 总内存 (MB)
    
    // 温度信息
    float soc_temp_c{0.0f};        // SoC 温度 (°C)
    float gpu_temp_c{0.0f};        // GPU 温度 (°C)
    
    // 功率模式（字符信息）
    std::string power_mode;
    
    // 更新时间戳
    std::chrono::system_clock::time_point last_update;

    SystemStatus() {
        last_update = std::chrono::system_clock::now();
        // 初始化 CPU 使用率向量，取系统核心数；若未获取成功默认 4 核
        unsigned int num_cores = std::thread::hardware_concurrency();
        cpu_usage.resize(num_cores > 0 ? num_cores : 4, 0.0f);
    }
};

// 系统监控类，采用 RAII 管理监控线程
class SystemMonitor {
private:
    std::atomic<bool> running{false};
    std::unique_ptr<std::thread> monitor_thread;
    std::mutex status_mutex;
    SystemStatus current_status;
    std::condition_variable cv;
    unsigned int update_interval_ms{500};  // 默认更新间隔500ms

    // 读取文本文件内容
    std::string readFile(const std::string &path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return "";
        }
        std::string content;
        std::getline(file, content);
        return content;
    }

    // 执行命令并返回输出内容
    std::string execCommand(const std::string &cmd) {
        std::array<char, 128> buffer;
        std::string result;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
        if (!pipe) return "";
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
        return result;
    }

    // 更新 CPU 信息：频率与使用率（此处仅对第0个核心作简化处理）
    void updateCpuInfo() {
        std::string freq_str = readFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
        if (!freq_str.empty()) {
            try {
                current_status.cpu_freq_mhz = std::stoi(freq_str) / 1000; // 转换为 MHz
            } catch (...) { }
        }
        std::string cpu_info = execCommand("top -bn1 | grep '%Cpu' | head -1");
        if (!cpu_info.empty()) {
            size_t pos = cpu_info.find(":");
            if (pos != std::string::npos) {
                size_t user_pos = cpu_info.find("us", pos);
                if (user_pos != std::string::npos) {
                    try {
                        std::string substr = cpu_info.substr(pos + 1, user_pos - pos - 1);
                        float usage = std::stof(substr);
                        if (!current_status.cpu_usage.empty()) {
                            current_status.cpu_usage[0] = usage;
                        }
                    } catch (...) { }
                }
            }
        }
    }

    // 更新 GPU 信息
    void updateGpuInfo() {
        std::string gpu_freq_path = "/sys/devices/17000000.gv11b/devfreq/17000000.gv11b/cur_freq";
        if (access(gpu_freq_path.c_str(), F_OK) != -1) {
            std::string freq_str = readFile(gpu_freq_path);
            if (!freq_str.empty()) {
                try {
                    current_status.gpu_freq_mhz = std::stoi(freq_str) / 1000000; // 转换为MHz
                } catch (...) { }
            }
        }
        // 通过 tegrastats（简化方式）获取 GPU 使用率
        std::string gpu_info = execCommand("tegrastats --interval 100 --count 1 | grep GR3D");
        if (!gpu_info.empty()) {
            size_t gr3d_pos = gpu_info.find("GR3D_FREQ");
            if (gr3d_pos != std::string::npos) {
                size_t percent_pos = gpu_info.find("%", gr3d_pos);
                if (percent_pos != std::string::npos) {
                    size_t value_start = gpu_info.rfind(" ", percent_pos) + 1;
                    if (value_start < percent_pos) {
                        try {
                            std::string value = gpu_info.substr(value_start, percent_pos - value_start);
                            current_status.gpu_usage = std::stof(value);
                        } catch (...) { }
                    }
                }
            }
        }
    }

    // 更新温度信息
    void updateTempInfo() {
        std::string thermal_path = "/sys/devices/virtual/thermal/thermal_zone0/temp";
        if (access(thermal_path.c_str(), F_OK) != -1) {
            std::string temp_str = readFile(thermal_path);
            if (!temp_str.empty()) {
                try {
                    current_status.soc_temp_c = std::stoi(temp_str) / 1000.0f;
                } catch (...) { }
            }
        }
        thermal_path = "/sys/devices/virtual/thermal/thermal_zone1/temp";
        if (access(thermal_path.c_str(), F_OK) != -1) {
            std::string temp_str = readFile(thermal_path);
            if (!temp_str.empty()) {
                try {
                    current_status.gpu_temp_c = std::stoi(temp_str) / 1000.0f;
                } catch (...) { }
            }
        }
    }

    // 更新内存信息
    void updateMemoryInfo() {
        std::string mem_info = execCommand("free -m | grep 'Mem:'");
        if (!mem_info.empty()) {
            std::istringstream iss(mem_info);
            std::string label;
            iss >> label; // 跳过 "Mem:"
            iss >> current_status.mem_total_mb >> current_status.mem_used_mb;
        }
    }

    // 更新功率模式信息
    void updatePowerMode() {
        std::string power_info = execCommand("nvpmodel -q | grep 'Power Mode'");
        if (!power_info.empty()) {
            current_status.power_mode = power_info;
            if (!current_status.power_mode.empty() &&
                current_status.power_mode.back() == '\n') {
                current_status.power_mode.pop_back();
            }
        }
    }

    // 监控线程主循环函数
    void monitorFunction() {
        while (running) {
            {
                std::lock_guard<std::mutex> lock(status_mutex);
                updateCpuInfo();
                updateGpuInfo();
                updateTempInfo();
                updateMemoryInfo();
                updatePowerMode();
                current_status.last_update = std::chrono::system_clock::now();
                cv.notify_all();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(update_interval_ms));
        }
    }

public:
    SystemMonitor(unsigned int interval_ms = 500) : update_interval_ms(interval_ms) {}
    
    ~SystemMonitor() {
        stop();
    }
    
    // 启动监控线程
    void start() {
        if (!running.exchange(true)) {
            monitor_thread = std::make_unique<std::thread>([this]() { monitorFunction(); });
        }
    }
    
    // 停止监控线程
    void stop() {
        if (running.exchange(false)) {
            if (monitor_thread && monitor_thread->joinable()) {
                monitor_thread->join();
                monitor_thread.reset();
            }
        }
    }
    
    // 获取当前最新系统状态（注意：仅返回某个时刻的值）
    SystemStatus getStatus() {
        std::lock_guard<std::mutex> lock(status_mutex);
        return current_status;
    }
    
    // 可选：等待下一次状态更新
    bool waitForUpdate(unsigned int timeout_ms = 1000) {
        std::unique_lock<std::mutex> lock(status_mutex);
        auto now = std::chrono::system_clock::now();
        return cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                           [this, now]() { return current_status.last_update > now; });
    }
};

// ==========================
// 性能统计模块
// ==========================
class PerformanceTracker {
private:
    struct FrameMetrics {
        double preprocess_time{0.0};
        double inference_time{0.0};
        double postprocess_time{0.0};
        double total_time{0.0};
    };
    
    std::vector<FrameMetrics> metrics;
    std::mutex metrics_mutex;
    
public:
    PerformanceTracker(size_t reserve_size = 1000) {
        metrics.reserve(reserve_size);
    }
    
    // 记录单帧性能数据
    void recordFrame(double preprocess_ms, double inference_ms, double postprocess_ms) {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        FrameMetrics fm;
        fm.preprocess_time = preprocess_ms;
        fm.inference_time = inference_ms;
        fm.postprocess_time = postprocess_ms;
        fm.total_time = preprocess_ms + inference_ms + postprocess_ms;
        metrics.push_back(fm);
    }
    
    // 获取最后一帧度量数据
    FrameMetrics getLastFrameMetrics() {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        return metrics.empty() ? FrameMetrics() : metrics.back();
    }
    
    struct AggregateMetrics {
        double avg_preprocess{0.0};
        double avg_inference{0.0};
        double avg_postprocess{0.0};
        double avg_total{0.0};
        double min_total{std::numeric_limits<double>::max()};
        double max_total{0.0};
        double fps{0.0};
    };
    
    AggregateMetrics getAggregateMetrics() {
        std::lock_guard<std::mutex> lock(metrics_mutex);
        AggregateMetrics agg;
        if (metrics.empty())
            return agg;
        
        double total_pre = 0.0, total_inf = 0.0, total_post = 0.0, total_all = 0.0;
        for (const auto &m : metrics) {
            total_pre += m.preprocess_time;
            total_inf += m.inference_time;
            total_post += m.postprocess_time;
            total_all += m.total_time;
            agg.min_total = std::min(agg.min_total, m.total_time);
            agg.max_total = std::max(agg.max_total, m.total_time);
        }
        size_t n = metrics.size();
        agg.avg_preprocess = total_pre / n;
        agg.avg_inference = total_inf / n;
        agg.avg_postprocess = total_post / n;
        agg.avg_total = total_all / n;
        agg.fps = (agg.avg_total > 0.0) ? (1000.0 / agg.avg_total) : 0.0;
        return agg;
    }
};

// ==========================
// TensorRT Logger 实现
// ==========================
class TRTLogger : public nvinfer1::ILogger {
    void log(Severity severity, const char *msg) noexcept override {
        if (severity <= Severity::kWARNING)
            std::cout << msg << std::endl;
    }
} trtLogger;

// ==========================
// 主函数
// ==========================
int main(int argc, char *argv[])
{
    // 检测是否无头模式
    bool headless = isHeadlessEnvironment();
    if (headless) {
        std::cout << Color::YELLOW << "Running in headless mode. GUI display disabled." 
                  << Color::RESET << std::endl;
        putenv((char*)"OPENCV_VIDEOIO_PRIORITY_INTEL_MFX=0");
        putenv((char*)"OPENCV_VIDEOIO_DEBUG=0");
    }
    
    // 参数检查
    if (argc < 4 || argc > 5) {
        std::cerr << Color::RED << "Usage: " << Color::RESET << argv[0]
                  << " <mode> <input_path> <engine_path> [onnx_path]" << std::endl;
        std::cerr << Color::YELLOW << "  <mode> - 'convert', 'infer_video', or 'infer_image'" 
                  << Color::RESET << std::endl;
        std::cerr << Color::YELLOW << "  <input_path> - Path to input video/image or ONNX model" 
                  << Color::RESET << std::endl;
        std::cerr << Color::YELLOW << "  <engine_path> - Path to TensorRT engine file" 
                  << Color::RESET << std::endl;
        std::cerr << Color::YELLOW << "  [onnx_path] - Path to ONNX model (only for 'convert' mode)" 
                  << Color::RESET << std::endl;
        return 1;
    }
    
    std::string mode = argv[1];
    std::string inputPath = argv[2];
    std::string enginePath = argv[3];
    std::string onnxPath;
    
    // 防止参数传反（例如：infer_video 模式下）
    if (mode == "infer_video" && inputPath.find(".engine") != std::string::npos &&
        enginePath.find(".mp4") != std::string::npos) {
        std::cerr << Color::RED << "Warning: Parameters seem to be in wrong order!" 
                  << Color::RESET << std::endl;
        std::cerr << Color::YELLOW << "Did you mean: " << argv[0] << " infer_video " 
                  << enginePath << " " << inputPath << " ?" << Color::RESET << std::endl;
        return 1;
    }
    
    if (mode == "convert") {
        if (argc != 5) {
            std::cerr << Color::RED << "Usage for conversion: " << Color::RESET 
                      << argv[0] << " convert <onnx_path> <engine_path>" << std::endl;
            return 1;
        }
        onnxPath = inputPath; // 在 convert 模式下，第一个参数为 ONNX 模型路径
    } else if (mode == "infer_video" || mode == "infer_image") {
        if (argc != 4) {
            std::cerr << Color::RED << "Usage for " << mode << ": " << Color::RESET 
                      << argv[0] << " " << mode << " <input_path> <engine_path>" << std::endl;
            return 1;
        }
    } else {
        std::cerr << Color::RED << "Invalid mode. Use 'convert', 'infer_video', or 'infer_image'." 
                  << Color::RESET << std::endl;
        return 1;
    }
    
    // 确保输出目录存在
#ifdef _WIN32
    CreateDirectory("./output", NULL);
    CreateDirectory("./output/video", NULL);
    CreateDirectory("./output/image", NULL);
#else
    mkdir("./output", 0777);
    mkdir("./output/video", 0777);
    mkdir("./output/image", 0777);
#endif

    // 处理 convert 模式
    if (mode == "convert") {
        try {
            YOLOv12 yolov12(onnxPath, trtLogger);
            std::cout << Color::GREEN << "Model conversion successful. Engine saved." 
                      << Color::RESET << std::endl;
        } catch (const std::exception &e) {
            std::cerr << Color::RED << "Error during model conversion: " 
                      << e.what() << Color::RESET << std::endl;
            return 1;
        }
        return 0;
    }
    // 处理 infer_video 和 infer_image 模式
    else if (mode == "infer_video" || mode == "infer_image") {
        try {
            YOLOv12 yolov12(enginePath, trtLogger);
            
            if (mode == "infer_video") {
                // 启动系统监控（单独线程）
                SystemMonitor sysMonitor;
                sysMonitor.start();
                // 初始化性能统计器
                PerformanceTracker perfTracker(1000);
                
                // 定义系统状态聚合器，每 30 帧采样一次
                struct AggregatedSystemInfo {
                    double sum_cpu_freq = 0.0;
                    double sum_gpu_freq = 0.0;
                    double sum_soc_temp = 0.0;
                    double sum_gpu_temp = 0.0;
                    double sum_mem_used = 0.0;
                    double sum_mem_total = 0.0;
                    double sum_gpu_usage = 0.0;
                    int count = 0;
                    
                    void add(const SystemStatus &s) {
                        sum_cpu_freq += s.cpu_freq_mhz;
                        sum_gpu_freq += s.gpu_freq_mhz;
                        sum_soc_temp += s.soc_temp_c;
                        sum_gpu_temp += s.gpu_temp_c;
                        sum_mem_used += s.mem_used_mb;
                        sum_mem_total += s.mem_total_mb;
                        sum_gpu_usage += s.gpu_usage;
                        count++;
                    }
                    
                    double avg_cpu_freq() const { return count > 0 ? sum_cpu_freq / count : 0.0; }
                    double avg_gpu_freq() const { return count > 0 ? sum_gpu_freq / count : 0.0; }
                    double avg_soc_temp() const { return count > 0 ? sum_soc_temp / count : 0.0; }
                    double avg_gpu_temp() const { return count > 0 ? sum_gpu_temp / count : 0.0; }
                    double avg_mem_used() const { return count > 0 ? sum_mem_used / count : 0.0; }
                    double avg_mem_total() const { return count > 0 ? sum_mem_total / count : 0.0; }
                    double avg_gpu_usage() const { return count > 0 ? sum_gpu_usage / count : 0.0; }
                };
                AggregatedSystemInfo aggSys;
                
                // 打开视频文件
                cv::VideoCapture cap(inputPath);
                if (!cap.isOpened()) {
                    std::cerr << Color::RED << "Failed to open video file: " 
                              << inputPath << Color::RESET << std::endl;
                    return 1;
                }
                
                // 获取视频参数
                int frame_width  = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
                int frame_height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
                // 使用 MP4 输出（mp4v编码）
                std::string outputVideoPath = "./output/video/output_video.mp4";
                cv::VideoWriter video(outputVideoPath, cv::VideoWriter::fourcc('m','p','4','v'), 
                                      30, cv::Size(frame_width, frame_height));
                if (!video.isOpened()) {
                    std::cerr << Color::RED << "Failed to open video writer." 
                              << Color::RESET << std::endl;
                    return 1;
                }
                
                int total_frames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
                int processed_frames = 0;
                
                cv::Mat frame;
                while (cap.read(frame)) {
                    // 计时：预处理
                    auto t_pre_start = std::chrono::high_resolution_clock::now();
                    yolov12.preprocess(frame);
                    auto t_pre_end = std::chrono::high_resolution_clock::now();
                    double preprocess_time = std::chrono::duration<double, std::milli>(t_pre_end - t_pre_start).count();
                    
                    // 计时：推理
                    auto t_inf_start = std::chrono::high_resolution_clock::now();
                    yolov12.infer();
                    auto t_inf_end = std::chrono::high_resolution_clock::now();
                    double inference_time = std::chrono::duration<double, std::milli>(t_inf_end - t_inf_start).count();
                    
                    // 计时：后处理
                    auto t_post_start = std::chrono::high_resolution_clock::now();
                    std::vector<Detection> detections;
                    yolov12.postprocess(detections);
                    auto t_post_end = std::chrono::high_resolution_clock::now();
                    double postprocess_time = std::chrono::duration<double, std::milli>(t_post_end - t_post_start).count();
                    
                    // 绘制检测结果
                    yolov12.draw(frame, detections);
                    
                    // 显示图像（仅非无头模式下）
                    if (!headless) {
                        cv::imshow("Inference", frame);
                        if (cv::waitKey(1) == 27) { // 按 ESC 键退出
                            break;
                        }
                    }
                    
                    // 写入输出视频
                    video.write(frame);
                    
                    processed_frames++;
                    // 记录当前帧性能
                    perfTracker.recordFrame(preprocess_time, inference_time, postprocess_time);
                    
                    // 每30帧采样一次系统状态并更新聚合器
                    if (processed_frames % 30 == 0 || processed_frames == total_frames) {
                        SystemStatus sample = sysMonitor.getStatus();
                        aggSys.add(sample);
                        std::cout << Color::CYAN << "System Information (averaged so far):" << Color::RESET << std::endl;
                        std::cout << "  CPU Frequency: " << std::fixed << std::setprecision(0) << aggSys.avg_cpu_freq() << " MHz" << std::endl;
                        std::cout << "  GPU Frequency: " << std::fixed << std::setprecision(0) << aggSys.avg_gpu_freq() << " MHz" << std::endl;
                        std::cout << "  SoC Temperature: " << std::fixed << std::setprecision(1) << aggSys.avg_soc_temp() << " °C" << std::endl;
                        std::cout << "  GPU Temperature: " << std::fixed << std::setprecision(1) << aggSys.avg_gpu_temp() << " °C" << std::endl;
                        std::cout << "  Memory Usage: " << std::fixed << std::setprecision(1) 
                                  << aggSys.avg_mem_used() << " / " << aggSys.avg_mem_total() << " MB" << std::endl;
                        std::cout << "  GPU Usage: " << std::fixed << std::setprecision(1) << aggSys.avg_gpu_usage() << " %" << std::endl;
                        std::cout << "  Power Mode: " << sample.power_mode << std::endl;
                    }
                }
                
                cap.release();
                video.release();
                if (!headless)
                    cv::destroyAllWindows();
                
                // 获取聚合性能统计并打印
                auto aggMetrics = perfTracker.getAggregateMetrics();
                std::cout << Color::GREEN << "Video inference completed. Output saved to " 
                          << outputVideoPath << Color::RESET << std::endl;
                std::cout << Color::BLUE << "Performance Statistics:" << Color::RESET << std::endl;
                std::cout << "  Processed " << processed_frames << " frames" << std::endl;
                std::cout << "  Average Preprocess Time: " << std::fixed << std::setprecision(2) 
                          << aggMetrics.avg_preprocess << " ms" << std::endl;
                std::cout << "  Average Inference Time: " << aggMetrics.avg_inference << " ms" << std::endl;
                std::cout << "  Average Postprocess Time: " << aggMetrics.avg_postprocess << " ms" << std::endl;
                std::cout << "  Average Total Time per Frame: " << aggMetrics.avg_total << " ms" << std::endl;
                std::cout << "  Effective FPS: " << std::fixed << std::setprecision(1) << aggMetrics.fps << std::endl;
                
                // 停止系统监控，并打印最终聚合系统信息
                sysMonitor.stop();
                std::cout << Color::CYAN << "Final Aggregated System Information:" << Color::RESET << std::endl;
                std::cout << "  CPU Frequency: " << std::fixed << std::setprecision(0) << aggSys.avg_cpu_freq() << " MHz" << std::endl;
                std::cout << "  GPU Frequency: " << std::fixed << std::setprecision(0) << aggSys.avg_gpu_freq() << " MHz" << std::endl;
                std::cout << "  SoC Temperature: " << std::fixed << std::setprecision(1) << aggSys.avg_soc_temp() << " °C" << std::endl;
                std::cout << "  GPU Temperature: " << std::fixed << std::setprecision(1) << aggSys.avg_gpu_temp() << " °C" << std::endl;
                std::cout << "  Memory Usage: " << std::fixed << std::setprecision(1) 
                          << aggSys.avg_mem_used() << " / " << aggSys.avg_mem_total() << " MB" << std::endl;
                std::cout << "  GPU Usage: " << std::fixed << std::setprecision(1) << aggSys.avg_gpu_usage() << " %" << std::endl;
            }
            else if (mode == "infer_image") {
                cv::Mat image = cv::imread(inputPath);
                if (image.empty()) {
                    std::cerr << Color::RED << "Failed to read image: " 
                              << inputPath << Color::RESET << std::endl;
                    return 1;
                }
                
                // 计时：预处理
                auto t_pre_start = std::chrono::high_resolution_clock::now();
                yolov12.preprocess(image);
                auto t_pre_end = std::chrono::high_resolution_clock::now();
                double preprocess_time = std::chrono::duration<double, std::milli>(t_pre_end - t_pre_start).count();
                
                // 计时：推理
                auto t_inf_start = std::chrono::high_resolution_clock::now();
                yolov12.infer();
                auto t_inf_end = std::chrono::high_resolution_clock::now();
                double inference_time = std::chrono::duration<double, std::milli>(t_inf_end - t_inf_start).count();
                
                // 计时：后处理
                auto t_post_start = std::chrono::high_resolution_clock::now();
                std::vector<Detection> detections;
                yolov12.postprocess(detections);
                auto t_post_end = std::chrono::high_resolution_clock::now();
                double postprocess_time = std::chrono::duration<double, std::milli>(t_post_end - t_post_start).count();
                
                yolov12.draw(image, detections);
                if (!headless) {
                    cv::imshow("Inference", image);
                    cv::waitKey(0);
                }
                
                // 保存输出图像
                std::string outputImagePath = "./output/image/output_image.jpg";
                cv::imwrite(outputImagePath, image);
                
                std::cout << Color::GREEN << "Image inference completed. Output saved to " 
                          << outputImagePath << Color::RESET << std::endl;
                std::cout << Color::BLUE << "Performance Statistics:" << Color::RESET << std::endl;
                std::cout << "  Preprocess Time: " << std::fixed << std::setprecision(2) 
                          << preprocess_time << " ms" << std::endl;
                std::cout << "  Inference Time: " << inference_time << " ms" << std::endl;
                std::cout << "  Postprocess Time: " << postprocess_time << " ms" << std::endl;
                std::cout << "  Total Time: " << preprocess_time + inference_time + postprocess_time 
                          << " ms" << std::endl;
            }
        }
        catch (const std::exception &e) {
            std::cerr << Color::RED << "Error during inference: " << e.what() 
                      << Color::RESET << std::endl;
            return 1;
        }
    }
    
    return 0;
}
