#pragma once
#include <opencv2/opencv.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

class VideoCap {
public:
    VideoCap(int camID, int width, int height, int fps);
    ~VideoCap();

    // 获取最新的一帧（线程安全）
    bool getFrame(cv::Mat &frame);

    // 启动和停止采集
    void start();
    void stop();

private:
    void captureLoop();

    cv::VideoCapture cap;
    cv::Mat sharedFrame;
    std::mutex frameMutex;
    std::condition_variable frameCV;
    std::atomic<bool> running;
    std::thread captureThread;
};