// Copyright (c) 2022 Papa Libasse Sow.
// https://github.com/Nandite/R2000-Visualizer
// Distributed under the MIT Software License (X11 license).
//
// SPDX-License-Identifier: MIT
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "Control/Parameters.hpp"
#include "DataLink/DataLink.hpp"
#include "DataLink/DataLinkBuilder.hpp"
#include "R2000.hpp"
#include "ScanToPointCloud.hpp"
#include <boost/asio.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <chrono>
#include <iostream>
#include <pcl/common/transforms.h>
#include <pcl/visualization/pcl_visualizer.h>
#include "StatusWatcher.hpp"
#include <experimental/memory>
#include <utility>

#define FREQUENCY 35
#define SAMPLES_PER_SCAN 7200
#define START_ANGLE (-1800000)
#define WATCHDOG_TIMEOUT 5000
#define PACKET_TYPE Device::Parameters::PACKET_TYPE::A

using namespace std::chrono_literals;

/**
 * @param address The address to test.
 * @return True if the address has an ipv4 valid form, False otherwise.
 */
bool isValidIpv4(const std::string &address) {
    boost::system::error_code errorCode{};
    const auto ipv4Address{boost::asio::ip::address::from_string(address, errorCode)};
    return !errorCode && ipv4Address.is_v4();
}

bool interruptProgram{false};

void interrupt(int) {
    interruptProgram = true;
}

int main(int argc, char **argv) {

    std::signal(SIGTERM, interrupt);
    std::signal(SIGKILL, interrupt);
    std::signal(SIGINT, interrupt);

    using Point = pcl::PointXYZ;
    using PointCloudColorHandler = pcl::visualization::PointCloudColorHandlerCustom<Point>;
    const auto VIEWER_POINT_SIZE_ID{pcl::visualization::PCL_VISUALIZER_POINT_SIZE};
    const auto VIEWER_SCAN_CLOUD_ID{"scan_cloud"};

    boost::program_options::variables_map programOptions{};
    boost::program_options::options_description programOptionsDescriptions{
            "Perform a continuous TCP sharedScan data acquisition from a R2000 sensor and display it", 1024, 512};
    programOptionsDescriptions.add_options()("help,h", "Print out how to use the program")
            ("address,a", boost::program_options::value<std::string>()->required(), "Address of the device.");
    try {
        boost::program_options::parsed_options parsedProgramOptions{
                boost::program_options::command_line_parser(argc, argv).options(programOptionsDescriptions).run()};
        boost::program_options::store(parsedProgramOptions, programOptions);
    }
    catch (const boost::program_options::unknown_option &error) {
        std::clog << "Unknown option (" << error.get_option_name() << ")." << std::endl;
        return EXIT_FAILURE;
    }
    catch (const boost::program_options::invalid_command_line_syntax &error) {
        std::clog << "The command syntax is invalid for the argument " << error.get_option_name() << " ("
                  << error.what() << ")" << std::endl;
        return EXIT_FAILURE;
    }
    const auto isHelpRequested{programOptions.count("help") > 0};
    if (isHelpRequested) {
        std::cout << programOptionsDescriptions << std::endl;
        return EXIT_SUCCESS;
    }
    try {
        boost::program_options::notify(programOptions);
    }
    catch (const boost::program_options::required_option &error) {
        std::clog << "Missing mandatory argument (" << error.get_option_name() << ")." << std::endl;
        std::clog << programOptionsDescriptions << std::endl;
        return EXIT_FAILURE;
    }

    const auto deviceAddressAsString{programOptions["address"].as<std::string>()};
    if (deviceAddressAsString.empty()) {
        std::clog << "You must specify the device address." << std::endl;
        return EXIT_FAILURE;
    }
    if (!isValidIpv4(deviceAddressAsString)) {
        std::clog << "You must specify a valid IPV4 device address (" << deviceAddressAsString << ")." << std::endl;
        return EXIT_FAILURE;
    }

    const auto device{Device::R2000::makeShared({"R2000", deviceAddressAsString})};

    Device::Commands::SetParametersCommand setParametersCommand{*device};
    const auto hmiBuilder{Device::Parameters::ReadWriteParameters::HmiDisplay()
                                  .unlockHmiButton()
                                  .unlockHmiParameters()
                                  .withHmiLanguage(Device::Parameters::Language::ENGLISH)
                                  .withHmiDisplayMode(Device::Parameters::HMI_DISPLAY_MODE::APPLICATION_TEXT)
                                  .withHmiApplicationText1("TCP Scan")
                                  .withHmiApplicationText2("Acquisition")};
    const auto measureBuilder{Device::Parameters::ReadWriteParameters::Measure()
                                      .withOperatingMode(Device::Parameters::OPERATING_MODE::MEASURE)
                                      .withScanFrequency(FREQUENCY)
                                      .withSamplesPerScan(SAMPLES_PER_SCAN)
                                      .withScanDirection(Device::Parameters::SCAN_DIRECTION::CCW)};

    auto configureFuture{setParametersCommand.asyncExecute(1s, hmiBuilder, measureBuilder)};
    if (!configureFuture) {
        std::clog << "Could not configure the device (Busy)." << std::endl;
        return EXIT_FAILURE;
    }
    configureFuture->wait();
    const auto configureResult{configureFuture->get()};
    if (configureResult != Device::RequestResult::SUCCESS) {
        std::clog << "Could not configure the sensor (" << Device::requestResultToString(configureResult) << ")."
                  << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "Device configured. Setting up data link..." << std::endl;
    auto handleParameters{Device::Parameters::ReadWriteParameters::TcpHandle{}
                                  .withPacketType(PACKET_TYPE)
                                  .withStartAngle(START_ANGLE)
                                  .withWatchdog()
                                  .withWatchdogTimeout(WATCHDOG_TIMEOUT)};
    auto future{Device::DataLinkBuilder(handleParameters).build(device, 1s)};
    future.wait();
    auto buildResult{future.get()};
    const auto asyncRequestResult{buildResult.first};
    if (asyncRequestResult != Device::RequestResult::SUCCESS) {
        std::clog << "Could not establish a data link with sensor at " << device->getHostname() << " ("
                  << Device::requestResultToString(asyncRequestResult) << ")." << std::endl;
        return EXIT_FAILURE;
    }

    const auto port{0};
    pcl::visualization::PCLVisualizer viewer{"Scan viewer"};
    viewer.setBackgroundColor(0.35, 0.35, 0.35, port);
    viewer.setSize(1280, 1024);
    viewer.addCoordinateSystem(150.0f, 0.0f, 0.0f, 0.0f, "Zero");
    PointCloud::ScanToPointCloud<Point> converter{SAMPLES_PER_SCAN, -M_PI};

    std::atomic_bool deviceHasDisconnected{false};
    Device::DataLink::SharedScan sharedScan{{}};
    std::shared_ptr<Device::DataLink> dataLink{buildResult.second};
    dataLink->addOnDataLinkConnectionLostCallback([&deviceHasDisconnected]() {
        std::clog << "A disconnection of the sensor, or a network error has occurred." << std::endl;
        deviceHasDisconnected.store(true, std::memory_order_release);
    });
    dataLink->addOnNewScanAvailableCallback([&sharedScan](Device::DataLink::SharedScan newScan) {
        std::atomic_store_explicit(&sharedScan, std::move(newScan), std::memory_order_release);
    });

    auto timestamp{std::chrono::steady_clock::now()};
    while (!viewer.wasStopped() && !interruptProgram && !deviceHasDisconnected.load(std::memory_order_acquire)) {
        viewer.spinOnce(50);
        const auto scan{std::atomic_load_explicit(&sharedScan, std::memory_order_acquire)};
        if (!scan || (scan->getTimestamp() == timestamp)) {
            continue;
        }
        timestamp = scan->getTimestamp();
        pcl::PointCloud<Point>::Ptr cloud{new pcl::PointCloud<Point>};
        converter.convert(*scan, *cloud);
        PointCloudColorHandler scannedCloudColor{cloud, 0, 240, 0};
        viewer.removePointCloud(VIEWER_SCAN_CLOUD_ID, port);
        viewer.addPointCloud(cloud, scannedCloudColor, VIEWER_SCAN_CLOUD_ID, port);
        viewer.setPointCloudRenderingProperties(VIEWER_POINT_SIZE_ID, 2, VIEWER_SCAN_CLOUD_ID, port);
    }

    viewer.close();
    return EXIT_SUCCESS;
}
