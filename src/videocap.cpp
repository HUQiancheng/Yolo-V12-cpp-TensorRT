#include "videocap.h"

VideoCap::VideoCap(int camID, int width, int height, int fps) : running(false) {
    cap.open(camID);
    if (!cap.isOpened()) {
        throw std::runtime_error("Failed to open camera");
    }
    cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    cap.set(cv::CAP_PROP_FPS, fps);
}

VideoCap::~VideoCap() {
    stop();
    if(cap.isOpened()){
        cap.release();
    }
}

void VideoCap::start() {
    running = true;
    captureThread = std::thread(&VideoCap::captureLoop, this);
}

void VideoCap::stop() {
    running = false;
    if (captureThread.joinable()) {
        captureThread.join();
    }
}

void VideoCap::captureLoop() {
    cv::Mat temp;
    while (running) {
        if(cap.read(temp)){
            {
                std::lock_guard<std::mutex> lock(frameMutex);
                temp.copyTo(sharedFrame);
            }
            frameCV.notify_one();
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

bool VideoCap::getFrame(cv::Mat &frame) {
    std::unique_lock<std::mutex> lock(frameMutex);
    if(!frameCV.wait_for(lock, std::chrono::milliseconds(30),
                           [&]() { return !sharedFrame.empty(); })){
        return false;
    }
    sharedFrame.copyTo(frame);
    return !frame.empty();
}