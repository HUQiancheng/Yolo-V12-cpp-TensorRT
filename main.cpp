#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <fstream>
#include <cuda_runtime.h>
#include "include/YOLOv12.h"
#include "include/common.h" // Contains CLASS_NAMES and COLORS
#include "videocap.h"

using namespace std;
using namespace cv;

// Simple logger class for TensorRT
class Logger : public nvinfer1::ILogger
{
    void log(Severity severity, const char *msg) noexcept override
    {
        if (severity <= Severity::kWARNING)
            std::cout << msg << std::endl;
    }
} logger;

int main(int argc, char *argv[])
{
    // Console text colors
    const std::string RED_COLOR = "\033[31m";
    const std::string GREEN_COLOR = "\033[32m";
    const std::string YELLOW_COLOR = "\033[33m";
    const std::string RESET_COLOR = "\033[0m";

    if (argc != 3)
    {
        std::cerr << RED_COLOR << "Usage: " << argv[0] << " <engine_path> <camera_id>" 
                  << RESET_COLOR << std::endl;
        return 1;
    }

    std::string enginePath = argv[1];
    int cameraId = std::stoi(argv[2]);

    // Check for available CUDA devices
    int deviceCount = 0;
    cudaGetDeviceCount(&deviceCount);
    if (deviceCount == 0)
    {
        std::cerr << RED_COLOR << "No CUDA devices found" << RESET_COLOR << std::endl;
        return 1;
    }

    // Set the first CUDA device
    if (cudaSetDevice(0) != cudaSuccess)
    {
        std::cerr << RED_COLOR << "Failed to set CUDA device" << RESET_COLOR << std::endl;
        return 1;
    }

    try
    {
        // Verify engine file existence
        std::ifstream engineFile(enginePath, std::ios::binary);
        if (!engineFile.good())
        {
            std::cerr << RED_COLOR << "Engine file not found: " << enginePath 
                      << RESET_COLOR << std::endl;
            return 1;
        }
        engineFile.close();

        // Initialize YOLOv12 model
        std::unique_ptr<YOLOv12> yolov12;
        try
        {
            yolov12 = std::make_unique<YOLOv12>(enginePath, logger);
        }
        catch (const std::exception &e)
        {
            std::cerr << RED_COLOR << "YOLOv12 initialization failed: " << e.what() 
                      << RESET_COLOR << std::endl;
            return 1;
        }
        std::cout << GREEN_COLOR << "Model loaded successfully" << RESET_COLOR << std::endl;

        // 创建可变尺寸的显示窗口
        namedWindow("YOLOv12 Real-time Detection", WINDOW_NORMAL);
        resizeWindow("YOLOv12 Real-time Detection", 1280, 720);

        // 采用 VideoCap 类封装摄像头采集（640x480，30FPS）
        VideoCap videoCap(cameraId, 640, 480, 30);
        videoCap.start();

        // 主处理循环
        std::atomic<bool> running{ true };
        while (running)
        {
            cv::Mat frame;
            if (!videoCap.getFrame(frame))
                continue;
            if (frame.empty())
                continue;

            try
            {
                auto start = std::chrono::steady_clock::now();
                
                auto start_pre = std::chrono::steady_clock::now();
                yolov12->preprocess(frame);
                auto end_pre = std::chrono::steady_clock::now();
                
                auto start_inf = std::chrono::steady_clock::now();
                yolov12->infer();
                auto end_inf = std::chrono::steady_clock::now();
                
                std::vector<Detection> detections;
                auto start_post = std::chrono::steady_clock::now();
                yolov12->postprocess(detections);
                auto end_post = std::chrono::steady_clock::now();
                
                double t_pre  = std::chrono::duration<double, std::milli>(end_pre - start_pre).count();
                double t_inf  = std::chrono::duration<double, std::milli>(end_inf - start_inf).count();
                double t_post = std::chrono::duration<double, std::milli>(end_post - start_post).count();
                double t_total = t_pre + t_inf + t_post;
                
                // 在终端更新处理时间信息（覆盖上一行输出）
                std::cout << "\rPre: " << static_cast<int>(t_pre) << "ms | "
                          << "Inf: " << static_cast<int>(t_inf) << "ms | "
                          << "Post: " << static_cast<int>(t_post) << "ms | "
                          << "Total: " << static_cast<int>(t_total) << "ms" << std::flush;
                
                // 如果没有检测到目标，则提示 "No detections"
                if (detections.empty())
                {
                    cv::putText(frame, "No detections", cv::Point(10, 50),
                                cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);
                }
                
                // 绘制检测框与标签
                for (const auto &det : detections)
                {
                    int clsID = det.class_id;
                    if (clsID < 0 || clsID >= (int)CLASS_NAMES.size())
                        continue;
                    const auto &color = COLORS[clsID % COLORS.size()];
                    cv::rectangle(frame, det.bbox, cv::Scalar(color[2], color[1], color[0]), 2);
                    
                    std::string label = CLASS_NAMES[clsID] + " " +
                                        std::to_string(static_cast<int>(det.conf * 100)) + "%";
                    int baseline = 0;
                    cv::Size labelSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);
                    cv::Point labelOrigin(det.bbox.x, det.bbox.y - 5);
                    
                    cv::rectangle(frame,
                                  cv::Point(labelOrigin.x, labelOrigin.y - labelSize.height),
                                  cv::Point(labelOrigin.x + labelSize.width, labelOrigin.y + baseline),
                                  cv::Scalar(color[2], color[1], color[0]), cv::FILLED);
                    cv::putText(frame, label, labelOrigin, cv::FONT_HERSHEY_SIMPLEX, 0.5,
                                cv::Scalar(255, 255, 255), 1);
                }
                
                auto end = std::chrono::steady_clock::now();
                float fps = 1000.0f / std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                cv::putText(frame, "FPS: " + std::to_string(static_cast<int>(fps)),
                            cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);
                
                cv::imshow("YOLOv12 Real-time Detection", frame);
            }
            catch (const std::exception &e)
            {
                std::cerr << RED_COLOR << "Inference error: " << e.what() 
                          << RESET_COLOR << std::endl;
                running = false;
            }
            
            if (cv::waitKey(1) == 'q')
                running = false;
        }
        
        // Cleanup
        videoCap.stop();
        cv::destroyAllWindows();
    }
    catch (const std::exception &e)
    {
        std::cerr << RED_COLOR << "Error: " << e.what() 
                  << RESET_COLOR << std::endl;
        return 1;
    }
    
    // CUDA cleanup
    cudaDeviceReset();
    return 0;
}