#include <stdio.h>
#include <fstream>
#include <iostream>
#include <chrono>
#include <vector>

#ifdef USE_GPU
#include <cuda_runtime.h>
#endif

// use opencv
#include <opencv2/opencv.hpp>

#include "tr_wrapper.h"
#include "tr_worker.h"
#include "tr_utils.h"

// third party libraries
#include "httplib.h"
#include "json.hpp"
#include "base64.h"

using json = nlohmann::json;
using namespace httplib;

void tr_log(std::string& msg, std::string& level) {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
    char buffer[100];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now_time_t));
    std::string time_str = std::string(buffer);

    printf("[%s] [%s] %s\n", time_str.c_str(), level.c_str(), msg.c_str());
}

#ifdef USE_GPU
void clear_memory() {
    cudaError_t err = cudaDeviceReset();
    if (err != cudaSuccess) {
        std::cerr << "Error resetting CUDA device: " << cudaGetErrorString(err) << std::endl;
    }
}

void resume_tr_libs(int ctpn_id, int crnn_id) {
    clear_memory();
    tr_init(0, ctpn_id, (void*)(CTPN_PATH), NULL);
    tr_init(0, crnn_id, (void*)(CRNN_PATH), NULL);
}

std::atomic<bool> isBlock(false);

void monitor_thread(TrThreadPool& tp, int ctpn_id, int crnn_id) {
    while (true) {
        size_t free_mem = 0, total_mem = 0;
        cudaMemGetInfo(&free_mem, &total_mem);
        size_t used_mem = total_mem - free_mem;
        std::string level("INFO");

        if (used_mem > total_mem * 0.7) {  // 超过90%时阻塞
            std::string start_gc("Memory usage high, blocking requests...");
            std::string end_gc("Memory cleared, resuming requests...");
            tr_log(start_gc, level);

            isBlock.store(true);  // 阻塞请求处理

            while (true) {
                if (tp.busy()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                else {
                    break;  // 不busy了就解除自旋
                }
            }

            // 重置CUDA设备并清理显存
            resume_tr_libs(ctpn_id, crnn_id);

            tr_log(end_gc, level);
            isBlock.store(false);  // 恢复请求处理
        }

        std::string regular_log_msg("Free gpu mem: ");
        regular_log_msg += std::to_string(free_mem);
        // tr_log(regular_log_msg, level);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));    // 1s监测一次
    }
}
#endif

int main() {
    printf("Initializing TR binary...\n");
    int ctpn_id = 0;
    int crnn_id = 1;
    int port = 6006;

    // initialize jobs ...
#ifdef USE_GPU
    resume_tr_libs(ctpn_id, crnn_id);
#else
    tr_init(0, ctpn_id, (void*)(CTPN_PATH), NULL);
    tr_init(0, crnn_id, (void*)(CRNN_PATH), NULL);
#endif
    TrThreadPool tr_task_pool(5);
    Server svr;
    std::vector<int> rotations = {0, 90, 270, 180};
    
#ifdef USE_GPU
    std::thread gpu_mem_monitor(monitor_thread, std::ref(tr_task_pool), ctpn_id, crnn_id);
#endif

    svr.Get("/hi", [](const Request& req, Response& res) {
        res.set_content("Hello World!", "text/plain");
    });


    svr.Post("/api/trocr", [&tr_task_pool, ctpn_id, crnn_id, &rotations](const Request& req, Response& res) {
        try {
#ifdef USE_GPU
            while (isBlock.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));  // 自旋等待block解除
            }
#endif

            int width, height, channels;
            cv::Mat img;

            if (req.has_file("file")) {
                const auto& file = req.get_file_value("file");
                // 从内存中读取图像
                std::vector<unsigned char> img_data(file.content.begin(), file.content.end());
                img = cv::imdecode(img_data, cv::IMREAD_COLOR);  // 读取为彩色图像
            } else if (req.has_param("img_base64")) {
                std::string img_base64 = req.get_param_value("img_base64");
                std::string decoded_data = base64_decode(img_base64);  // 解码Base64数据

                // 将Base64解码的数据转为vector并加载为彩色图像
                std::vector<unsigned char> img_data(decoded_data.begin(), decoded_data.end());
                img = cv::imdecode(img_data, cv::IMREAD_COLOR);  // 读取为彩色图像
            } else {
                res.set_content("Missing file or img_base64 parameter", "text/plain");
                return;
            }

            if (img.empty()) {
                res.set_content("Failed to load image", "text/plain");
                return;
            }

            // 转为灰度图像
            cv::Mat gray_img;
            cv::cvtColor(img, gray_img, cv::COLOR_BGR2GRAY);

            // 现在开始核心的推理进程，旋转图像来看看到底是不是有用的数据
            auto start = std::chrono::high_resolution_clock::now();
            std::string plain_text;

            plain_text = "";

            width = gray_img.cols;
            height = gray_img.rows;
            channels = gray_img.channels();

            unsigned char* image_data = gray_img.data;
            auto future = tr_task_pool.enqueue(image_data, height, width, channels, ctpn_id, crnn_id);
            std::vector<TrResult> results = future.get();   // inference

            // 处理 OCR 结果
            for (const auto& result : results) {
                std::string txt = std::get<1>(result);
                plain_text += txt + "|";
            }

            res.set_content(plain_text, "text/plain; charset=UTF-8");

            // log
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            std::string msg = std::string("Latency: ") + std::to_string(duration.count() / 1000.0) + " ms";
            std::string level("INFO");
            tr_log(msg, level);


        } catch (const std::exception& e) {
            res.set_content("Invalid JSON data", "text/plain");
        }
    });


    printf("Server listening on http://localhost:%d\n", port);

    svr.listen("localhost", port);

#ifdef USE_GPU
    gpu_mem_monitor.join();
#endif

    return 0;
}