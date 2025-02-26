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
#include "include/common.h"  // Contains CLASS_NAMES and COLORS

class Logger : public nvinfer1::ILogger {
    void log(Severity severity, const char* msg) noexcept override {
        if (severity <= Severity::kWARNING)
            std::cout << msg << std::endl;
    }
}logger;

int main(int argc, char* argv[]) {
    // Console text colors
    const std::string RED_COLOR = "\033[31m";
    const std::string GREEN_COLOR = "\033[32m";
    const std::string YELLOW_COLOR = "\033[33m";
    const std::string RESET_COLOR = "\033[0m";

    if (argc != 3) {
        std::cerr << RED_COLOR << "Usage: " << argv[0] << " <engine_path> <camera_id>" 
                  << RESET_COLOR << std::endl;
        return 1;
    }

    std::string enginePath = argv[1];
    int cameraId = std::stoi(argv[2]);

    // Check for available CUDA devices
    int deviceCount = 0;
    cudaGetDeviceCount(&deviceCount);
    if (deviceCount == 0) {
        std::cerr << RED_COLOR << "No CUDA devices found" << RESET_COLOR << std::endl;
        return 1;
    }

    // Set the first CUDA device
    if (cudaSetDevice(0) != cudaSuccess) {
        std::cerr << RED_COLOR << "Failed to set CUDA device" << RESET_COLOR << std::endl;
        return 1;
    }

    try {
        // Verify that the engine file exists
        std::ifstream engineFile(enginePath, std::ios::binary);
        if (!engineFile.good()) {
            std::cerr << RED_COLOR << "Engine file not found: " << enginePath 
                      << RESET_COLOR << std::endl;
            return 1;
        }
        engineFile.close();

        // Initialize YOLOv12 model via smart pointer
        std::unique_ptr<YOLOv12> yolov12;
        try {
            yolov12 = std::make_unique<YOLOv12>(enginePath, logger);
        } catch (const std::exception& e) {
            std::cerr << RED_COLOR << "YOLOv12 initialization failed: " << e.what() 
                      << RESET_COLOR << std::endl;
            return 1;
        }
        std::cout << GREEN_COLOR << "Model loaded successfully" << RESET_COLOR << std::endl;

        // Configure the camera
        cv::VideoCapture cap;
        cap.set(cv::CAP_PROP_BUFFERSIZE, 3);
        if (!cap.open(cameraId)) {
            std::cerr << RED_COLOR << "Failed to open camera " << cameraId 
                      << RESET_COLOR << std::endl;
            return 1;
        }

        // Set camera properties
        cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        cap.set(cv::CAP_PROP_FPS, 30);

        // Test the camera by reading an initial frame
        cv::Mat testFrame;
        if (!cap.read(testFrame) || testFrame.empty()) {
            std::cerr << RED_COLOR << "Failed to read initial frame" << RESET_COLOR << std::endl;
            return 1;
        }
        std::cout << GREEN_COLOR << "Camera initialized successfully" << RESET_COLOR << std::endl;

        // Prepare threading resources for frame capture
        std::mutex frameMutex;
        std::condition_variable frameCV;
        cv::Mat sharedFrame;
        std::atomic<bool> running{true};

        // Start the capture thread to continuously read frames
        std::thread captureThread([&]() {
            cv::Mat temp;
            while (running) {
                if (cap.read(temp)) {
                    std::lock_guard<std::mutex> lock(frameMutex);
                    temp.copyTo(sharedFrame);
                    frameCV.notify_one();
                } else {
                    std::cout << YELLOW_COLOR << "Camera read failure" 
                              << RESET_COLOR << std::endl;
                    running = false;
                }
            }
        });

        // Main processing loop
        while (running) {
            cv::Mat frame;
            {
                std::unique_lock<std::mutex> lock(frameMutex);
                if (!frameCV.wait_for(lock, std::chrono::milliseconds(30),
                    [&]() { return !sharedFrame.empty(); })) {
                    continue;
                }
                sharedFrame.copyTo(frame);
            }
            if (frame.empty()) continue;

            try {
                auto start = std::chrono::steady_clock::now();

                // Run the inference pipeline
                yolov12->preprocess(frame);
                yolov12->infer();
                std::vector<Detection> detections;
                yolov12->postprocess(detections);

                // Debug log: print number of detections
                std::cout << "[DEBUG] Number of detections: " << detections.size() << std::endl;

                // If no detections, add a text indication
                if (detections.empty()) {
                    cv::putText(frame, "No detections", cv::Point(10,50),
                        cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0,0,255), 2);
                }

                // Draw bounding boxes with labels using CLASS_NAMES and COLORS from common.h
                for (const auto& det : detections) {
                    int clsID = det.class_id;
                    if (clsID < 0 || clsID >= (int)CLASS_NAMES.size())
                        continue;
                    // Use modulo in case there are more classes than colors
                    const auto& color = COLORS[clsID % COLORS.size()];
                    // OpenCV expects colors in BGR order (we stored them in RGB)
                    cv::rectangle(frame, det.bbox, 
                        cv::Scalar(color[2], color[1], color[0]), 2);
                    
                    std::string label = CLASS_NAMES[clsID] + " " + 
                        std::to_string(static_cast<int>(det.conf * 100)) + "%";
                    int baseline = 0;
                    cv::Size labelSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);
                    cv::Point labelOrigin(det.bbox.x, det.bbox.y - 5);

                    // Draw filled rectangle as label background
                    cv::rectangle(frame, 
                        cv::Point(labelOrigin.x, labelOrigin.y - labelSize.height),
                        cv::Point(labelOrigin.x + labelSize.width, labelOrigin.y + baseline),
                        cv::Scalar(color[2], color[1], color[0]), cv::FILLED);
                    // Draw the label text in white
                    cv::putText(frame, label, labelOrigin, cv::FONT_HERSHEY_SIMPLEX, 0.5, 
                        cv::Scalar(255, 255, 255), 1);
                }
                
                auto end = std::chrono::steady_clock::now();
                float fps = 1000.0f / std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                cv::putText(frame, "FPS: " + std::to_string(static_cast<int>(fps)),
                    cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);
                
                cv::imshow("YOLOv12 Real-time Detection", frame);
            } catch (const std::exception& e) {
                std::cerr << RED_COLOR << "Inference error: " << e.what() 
                          << RESET_COLOR << std::endl;
                running = false;
                continue;
            }

            if (cv::waitKey(1) == 'q')
                running = false;
        }

        // Cleanup
        running = false;
        captureThread.join();
        cap.release();
        cv::destroyAllWindows();

    } catch (const std::exception& e) {
        std::cerr << RED_COLOR << "Error: " << e.what() 
                  << RESET_COLOR << std::endl;
        return 1;
    }

    // CUDA cleanup
    cudaDeviceReset();
    return 0;
}