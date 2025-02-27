#include "statusmonitor.h"
// #include <boost/predef.h> // 用于检测操作系统


/// @brief Constructor
/// @param parent 
StatusMonitor::StatusMonitor(QObject *parent) :
    QObject(parent){

    m_monitor_running = true;
}

StatusMonitor::~StatusMonitor() {

}

void StatusMonitor::monitorProcess() {
    while(m_monitor_running){
        m_mem_usage_thread = get_memory_usage();
        m_nvidia_gpu_usage_thread = get_gpu_usage();
        m_cpu_usage_thread = get_cpu_usage();
        emit resultReady(m_cpu_usage_thread, m_mem_usage_thread, m_nvidia_gpu_usage_thread);
        if(!m_monitor_running) 
            break;
    }
}

// Linux 平台实现
#if defined(UNIX)
    #include <fstream>
    #include <sstream>
    #include <unordered_map>
    #include <thread>
    #include <cstdlib>
    #include <string>
    #include <chrono>
    #include <sys/utsname.h> // Linux/Unix 头文件

    // 获取内存信息
    uint8_t StatusMonitor::get_memory_usage() {
        QProcess process;
        QStringList arguments;
        arguments << "-m";
        process.start("free", arguments); //使用free完成获取
        process.waitForFinished();
        process.readLine();
        QString str = process.readLine();
        str.replace("\n","");
        str.replace(QRegExp("( ){1,}")," ");//将连续空格替换为单个空格 用于分割
        auto lst = str.split(" ");
        if(lst.size() > 6) {
            //qDebug("mem total:%.0lfMB free:%.0lfMB",lst[1].toDouble(),lst[6].toDouble());
            uint16_t nMemTotal = lst[1].toUShort() >> 10;
            uint16_t sub_nMemFree = nMemTotal - (lst[6].toUShort() >> 10);
            m_mem_usage_thread = sub_nMemFree * 100/ nMemTotal;
            return m_mem_usage_thread ;
        }
        else {
            return false;
        }
    }

    // 获取 CPU 信息
    uint8_t StatusMonitor::get_cpu_usage() {
        // 获取 CPU 使用率
        auto get_cpu_stats = []() -> std::unordered_map<std::string, unsigned long> {
            std::ifstream stat_file("/proc/stat");
            std::string line;
            std::getline(stat_file, line);
            std::istringstream iss(line);

            std::unordered_map<std::string, unsigned long> stats;
            std::string cpu_label;
            iss >> cpu_label >> stats["user"] >> stats["nice"] >> stats["system"] >> stats["idle"]
                >> stats["iowait"] >> stats["irq"] >> stats["softirq"] >> stats["steal"] >> stats["guest"] >> stats["guest_nice"];
            return stats;
        };

        auto prev_stats = get_cpu_stats();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto curr_stats = get_cpu_stats();

        unsigned long prev_idle = prev_stats["idle"] + prev_stats["iowait"];
        unsigned long curr_idle = curr_stats["idle"] + curr_stats["iowait"];

        unsigned long prev_non_idle = prev_stats["user"] + prev_stats["nice"] + prev_stats["system"] + prev_stats["irq"] + prev_stats["softirq"] + prev_stats["steal"];
        unsigned long curr_non_idle = curr_stats["user"] + curr_stats["nice"] + curr_stats["system"] + curr_stats["irq"] + curr_stats["softirq"] + curr_stats["steal"];

        unsigned long prev_total = prev_idle + prev_non_idle;
        unsigned long curr_total = curr_idle + curr_non_idle;

        unsigned long total_diff = curr_total - prev_total;
        unsigned long idle_diff = curr_idle - prev_idle;

        double usage = (total_diff - idle_diff) * 100.0 / total_diff;

        return usage;
    }

    // 获取 GPU 信息（需要安装 NVIDIA 驱动和 nvidia-smi）
    uint8_t StatusMonitor::get_gpu_usage() {

        std::string command = "nvidia-smi --query-gpu=name,memory.total,memory.free --format=csv,noheader,nounits";
        std::string result;

        FILE* pipe = popen(command.c_str(), "r");
        if (pipe) {
            char buffer[128];
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                result += buffer;
            }
            pclose(pipe);
        }

        if (!result.empty()) {
            int locs =  0;
            int loce = 0;
            locs = result.find(',', 0);
            loce = result.find(',', locs + 1);
            long total_memory = std::stol(result.substr(locs + 1, loce - locs - 1)) >> 10;
            long free_memory = std::stol(result.substr(loce + 1)) >> 10;
            int sub = total_memory - free_memory;
            return sub * 100 / total_memory;
        }
    }

    // 获取 OS 信息
    const QString StatusMonitor::osVersion() {
        struct utsname unameData;
        std::string osName;
        if (uname(&unameData) == 0) {
            osName = unameData.sysname; // 系统名称（例如 "Linux"）
            osName += " ";
            osName += unameData.release; // 内核版本

            // 尝试读取发行版信息（例如 Ubuntu 22.04）
            std::ifstream osRelease("/etc/os-release");
            if (osRelease.is_open()) {
                std::string line;
                while (std::getline(osRelease, line)) {
                    if (line.find("PRETTY_NAME=") != std::string::npos) {
                        osName = line.substr(line.find('=') + 1);
                        osName.erase(0, osName.find_first_not_of('"')); // 去掉开头的引号
                        osName.erase(osName.find_last_not_of('"') + 1); // 去掉结尾的引号
                        break;
                    }
                }
                osRelease.close();
            }
        } else {
            osName = "Failed to get Linux/Unix version";
        }
        m_osDescribe = QString::fromStdString(osName);
        return m_osDescribe;
    } 

// Windows 平台实现
#elif defined(WIN32)
    #include <windows.h>
    #include <intrin.h>
    #include <pdh.h>
    #include <wchar.h>
    #pragma comment(lib, "pdh.lib")

    // 获取内存信息
    MemoryInfo get_memory_info() {
        MEMORYSTATUSEX memory_status;
        memory_status.dwLength = sizeof(memory_status);

        if (GlobalMemoryStatusEx(&memory_status)) {
            return {
                static_cast<unsigned long>(memory_status.ullTotalPhys / (1024 * 1024)), // 总内存（MB）
                static_cast<unsigned long>(memory_status.ullAvailPhys / (1024 * 1024))  // 空闲内存（MB）
            };
        } else {
            std::cerr << "Failed to retrieve memory information." << std::endl;
            return {0, 0};
        }
    }

    // 获取 CPU 信息
    CPUInfo get_cpu_info() {
        CPUInfo info;

        // 获取 CPU 名称
        char cpu_name[0x40];
        int cpu_info[4] = { -1 };
        __cpuid(cpu_info, 0x80000002);
        memcpy(cpu_name, cpu_info, sizeof(cpu_info));
        __cpuid(cpu_info, 0x80000003);
        memcpy(cpu_name + 16, cpu_info, sizeof(cpu_info));
        __cpuid(cpu_info, 0x80000004);
        memcpy(cpu_name + 32, cpu_info, sizeof(cpu_info));
        cpu_name[48] = '\0';
        info.name = cpu_name;

        // 获取 CPU 核心数和线程数
        SYSTEM_INFO sys_info;
        GetSystemInfo(&sys_info);
        info.cores = sys_info.dwNumberOfProcessors;
        info.threads = info.cores;

        // 获取 CPU 使用率
        PDH_HQUERY query;
        PDH_HCOUNTER counter;
        PdhOpenQuery(nullptr, 0, &query);
        PdhAddCounter(query, "\\Processor(_Total)\\% Processor Time", 0, &counter);
        PdhCollectQueryData(query);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        PdhCollectQueryData(query);
        PDH_FMT_COUNTERVALUE value;
        PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value);
        PdhCloseQuery(query);
        info.usage = value.doubleValue;

        return info;
    }

    // 获取 GPU 信息（需要安装 NVIDIA 驱动和 nvidia-smi）
    GPUInfo get_gpu_info() {
        GPUInfo info;
        std::string command = "nvidia-smi --query-gpu=name,memory.total,memory.free --format=csv,noheader,nounits";
        std::string result;

        FILE* pipe = _popen(command.c_str(), "r");
        if (pipe) {
            char buffer[128];
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                result += buffer;
            }
            _pclose(pipe);
        }

        if (!result.empty()) {
            int locs =  0;
            int loce = 0;
            locs = result.find(',', 0);
            info.name = result.substr(0, locs);
            loce = result.find(',', locs + 1);
            info.total_memory = std::stol(result.substr(locs + 1, loce));
            info.free_memory = std::stol(result.substr(loce + 1, sizeof(result)));
            unsigned long sub = info.total_memory - info.free_memory;
            info.usage =  sub * 100 / info.total_memory;
        }
        MemoryInfo mem_m;
        CPUInfo cpu_m;
        GPUInfo gpu_m;
        return info;
    }

    // Windows 获取操作系统名称
    const QString StatusMonitor::osVersion() {
        std::string osName;
        OSVERSIONINFOEX osInfo;
        ZeroMemory(&osInfo, sizeof(OSVERSIONINFOEX));
        osInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

        if (GetVersionEx((OSVERSIONINFO*)&osInfo)) {
            if (osInfo.dwMajorVersion == 10 && osInfo.dwMinorVersion == 0) {
                osName = "Windows 10/11";
            } else if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 3) {
                osName = "Windows 8.1";
            } else if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 2) {
                osName = "Windows 8";
            } else if (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion == 1) {
                osName = "Windows 7";
            } else {
                osName = "Unknown Windows Version";
            }
        } else {
            osName = "Failed to get Windows version";
        }
        m_osDescribe = QString::fromStdString(osName);
        return m_osDescribe;
    }
#endif

void StatusMonitor::endMonitorProcess(){
    QMutexLocker locker(&mutex);
    m_monitor_running = !m_monitor_running;
}

const QString StatusMonitor::localMachineName(){
    m_localMachineName = QHostInfo::localHostName();
    return m_localMachineName;
}

const QString StatusMonitor::cpuType() {
    // 获取 CPU 名称
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    std::string cpu_name;
    while (std::getline(cpuinfo, line)) {
        if (line.find("model name") != std::string::npos) {
            size_t colon_pos = line.find(":");
            if (colon_pos != std::string::npos) {
                cpu_name = line.substr(colon_pos + 2); // 去掉冒号和空格
                break;
            }
        }
    }
    m_cpuDescribe = QString::fromStdString(cpu_name);
    return m_cpuDescribe;
}


const QString StatusMonitor::nvidiaGPU() {
    std::string command = "nvidia-smi --query-gpu=name,memory.total,memory.free --format=csv,noheader,nounits";
    std::string result;

    FILE* pipe = popen(command.c_str(), "r");
    if (pipe) {
        char buffer[128];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        pclose(pipe);
    }

    //if (!result.empty()) {
        int locs =  0;
        int loce = 0;
        locs = result.find(',', 0);
        m_gpuDescribe = QString::fromStdString(result.substr(0, locs));
        return m_gpuDescribe;
    //}

}

const QString StatusMonitor::memory() {
    QProcess process;
    QStringList arguments;
    arguments << "-m";
    process.start("free", arguments); //使用free完成获取
    process.waitForFinished();
    process.readLine();
    QString str = process.readLine();
    str.replace("\n","");
    str.replace(QRegExp("( ){1,}")," ");//将连续空格替换为单个空格 用于分割
    auto lst = str.split(" ");
    //if(lst.size() > 6) {
        //qDebug("mem total:%.0lfMB free:%.0lfMB",lst[1].toDouble(),lst[6].toDouble());
        int med = lst[1].toUShort() >> 10;
        m_memDescribe = QString::number(med);
        return m_memDescribe;
    //}
}
