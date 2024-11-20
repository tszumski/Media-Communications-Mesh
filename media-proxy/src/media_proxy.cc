/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <getopt.h>
#include <thread>

#include "api_server_grpc.h"
#include "api_server_tcp.h"

#include <csignal>
#include "concurrency.h"
#include "client_api.h"
#include "logger.h"
#include "manager_local.h"

#ifndef IMTL_CONFIG_PATH
#define IMTL_CONFIG_PATH "./imtl.json"
#endif

#define DEFAULT_DEV_PORT "0000:31:00.0"
#define DEFAULT_DP_IP "192.168.96.1"
#define DEFAULT_GRPC_PORT "8001"
#define DEFAULT_TCP_PORT "8002"

#include "mesh/st2110rx.h"
#include "mesh/st2110tx.h"
/* print a description of all supported options */
void usage(FILE* fp, const char* path)
{
    /* take only the last portion of the path */
    const char* basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;

    fprintf(fp, "Usage: %s [OPTION]\n", basename);
    fprintf(fp, "-h, --help\t\t"
                "Print this help and exit.\n");
    fprintf(fp, "-d, --dev=dev_port\t"
                "PCI device port (defaults: %s).\n",
        DEFAULT_DEV_PORT);
    fprintf(fp, "-i, --ip=ip_address\t"
                "IP address for media data transportation (defaults: %s).\n",
        DEFAULT_DP_IP);
    fprintf(fp, "-g, --grpc=port_number\t"
                "Port number gRPC controller (defaults: %s).\n",
        DEFAULT_GRPC_PORT);
    fprintf(fp, "-t, --tcp=port_number\t"
                "Port number for TCP socket controller (defaults: %s).\n",
        DEFAULT_TCP_PORT);
}

using namespace mesh;

// Main context with cancellation
auto ctx = context::WithCancel(context::Background());

using namespace mesh;

class EmulatedReceiver : public connection::Connection
{
  public:
    EmulatedReceiver(context::Context &ctx)
    {
        _kind = connection::Kind::receiver;
        set_state(ctx, connection::State::configured);
    }

    connection::Result on_establish(context::Context &ctx)
    {
        set_state(ctx, connection::State::active);
        return connection::Result::success;
    }

    connection::Result on_shutdown(context::Context &ctx) { return connection::Result::success; }

    connection::Result on_receive(context::Context &ctx, void *ptr, uint32_t sz, uint32_t &sent)
    {
        printf("Received %s (%u)\n", (char *)ptr, sz);
        return connection::Result::success;
    }
};

class EmulatedTransmitter : public connection::Connection
{
  public:
    EmulatedTransmitter(context::Context &ctx)
    {
        _kind = connection::Kind::transmitter;
        set_state(ctx, connection::State::configured);
    }

    connection::Result on_establish(context::Context &ctx)
    {
        set_state(ctx, connection::State::active);
        return connection::Result::success;
    }

    connection::Result on_shutdown(context::Context &ctx) { return connection::Result::success; }

    connection::Result transmit_wrapper(context::Context &ctx, void *ptr, uint32_t sz)
    {
        return transmit(ctx, ptr, sz);
    }
};

int main(int argc, char* argv[])
{
    std::string grpc_port = DEFAULT_GRPC_PORT;
    std::string tcp_port = DEFAULT_TCP_PORT;
    std::string dev_port = DEFAULT_DEV_PORT;
    std::string dp_ip = DEFAULT_DP_IP;
    int help_flag = 0;
    int opt;
    struct option longopts[] = {
        { "help", no_argument, &help_flag, 1 },
        { "dev", required_argument, NULL, 'd' },
        { "ip", required_argument, NULL, 'i' },
        { "grpc", required_argument, NULL, 'g' },
        { "tcp", required_argument, NULL, 't' },
        { 0 }
    };

    /* infinite loop, to be broken when we are done parsing options */
    while (1) {
        opt = getopt_long(argc, argv, "hd:i:g:t:", longopts, 0);
        if (opt == -1) {
            break;
        }

        switch (opt) {
        case 'h':
            help_flag = 1;
            break;
        case 'd':
            dev_port = optarg;
            break;
        case 'i':
            dp_ip = optarg;
            break;
        case 'g':
            grpc_port = optarg;
            break;
        case 't':
            tcp_port = optarg;
            break;
        case '?':
            usage(stderr, argv[0]);
            return 1;
        default:
            break;
        }
    }

    if (help_flag) {
        usage(stdout, argv[0]);
        return 0;
    }

    log::setFormatter(std::make_unique<log::StandardFormatter>());
    log::info("Media Proxy started");

    if (getenv("KAHAWAI_CFG_PATH") == NULL) {
        log::debug("Set MTL configure file path to %s", IMTL_CONFIG_PATH);
        setenv("KAHAWAI_CFG_PATH", IMTL_CONFIG_PATH, 0);
    }

    ProxyContext* proxy_ctx = new ProxyContext("0.0.0.0:" + grpc_port, "0.0.0.0:" + tcp_port);
    proxy_ctx->setDevicePort(dev_port);
    proxy_ctx->setDataPlaneAddress(dp_ip);

    // mesh::Experiment1();

    // Intercept shutdown signals to cancel the main context
    auto signal_handler = [](int sig) {
        if (sig == SIGINT || sig == SIGTERM) {
            log::info("Shutdown signal received");
            ctx.cancel();
        }
    };
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    MeshConfig_ST2110 cfg_st2110;
    {
// request.type
// request.protocol
#define IP_ADDR1 "192.168.96.1"
#define IP_ADDR2 "192.168.96.2"
        if (tcp_port == "8002") {
            memcpy(cfg_st2110.local_ip_addr, IP_ADDR1, sizeof(IP_ADDR1));
            memcpy(cfg_st2110.remote_ip_addr, IP_ADDR2, sizeof(IP_ADDR2));
        } else {
            memcpy(cfg_st2110.local_ip_addr, IP_ADDR2, sizeof(IP_ADDR2));
            memcpy(cfg_st2110.remote_ip_addr, IP_ADDR1, sizeof(IP_ADDR1));
        }
        cfg_st2110.local_port = 9001;
        cfg_st2110.remote_port = 9001;
        cfg_st2110.transport = MESH_CONN_TRANSPORT_ST2110_20;
    }

    MeshConfig_Video cfg_video;
    {
        cfg_video.fps = 30;
        cfg_video.width = 1920;
        cfg_video.height = 1080;
        cfg_video.pixel_format = MESH_VIDEO_PIXEL_FORMAT_YUV422P10LE;
    }

    if (tcp_port == "8002") {
        auto ctx = context::WithCancel(mesh::context::Background());
        connection::Result res;

        // Setup Emulated Receiver
        auto emulated_rx = new EmulatedReceiver(ctx);
        emulated_rx->establish(ctx);

        // Setup Rx connection
        auto conn_rx = new mesh::connection::ST2110_20Rx;

        res = conn_rx->configure(ctx, dev_port, cfg_st2110, cfg_video);
        if (res != connection::Result::success) {
            printf("Configure Rx failed: %s\n", mesh::connection::result2str(res));
            goto exit;
        }
        res = conn_rx->establish(ctx);
        if (res != connection::Result::success) {
            printf("Establish Rx failed: %s\n", mesh::connection::result2str(res));
            goto exit;
        }

        // Connect Rx connection to Emulated Receiver
        conn_rx->set_link(ctx, emulated_rx);

        // Sleep some sufficient time to allow receiving the data from transmitter
        mesh::thread::Sleep(ctx, std::chrono::milliseconds(5000));

    exit:
        // Shutdown Rx connection
        res = conn_rx->shutdown(ctx);
        if (res != connection::Result::success) {
            printf("Shutdown Rx failed: %s\n", mesh::connection::result2str(res));
        }

        // Destroy resources
        delete conn_rx;
        delete emulated_rx;
    } else {
        auto ctx = context::WithCancel(mesh::context::Background());
        connection::Result res;

        auto conn_tx = new mesh::connection::ST2110_20Tx;
        auto emulated_tx = new EmulatedTransmitter(ctx);

        // Setup Tx connection
        res = conn_tx->configure(ctx, dev_port, cfg_st2110, cfg_video);
        if (res != connection::Result::success) {
            printf("Configure Tx failed: %s\n", mesh::connection::result2str(res));
            goto exit2;
        }
        res = conn_tx->establish(ctx);
        if (res != connection::Result::success) {
            printf("Establish Tx failed: %s\n", mesh::connection::result2str(res));
            goto exit2;
        }

        // Setup Emulated Transmitter
        emulated_tx->establish(ctx);

        // Connect Emulated Transmitter to Tx connection
        emulated_tx->set_link(ctx, conn_tx);

        {
            // Send data
            size_t data_size = 1920 * 1080 * 2 * 2;
            void *data = calloc(1, data_size);
            for (int i = 0; i < 10; i++) {
                res = emulated_tx->transmit_wrapper(ctx, data,
                                                    data_size); // Use appropriate data here
                if (res != connection::Result::success) {
                    printf("Transmit failed: %s\n", mesh::connection::result2str(res));
                    break;
                }
                usleep(20000); // 20ms
            }
            free(data);
        }

    exit2:
        // Shutdown Tx connection
        res = conn_tx->shutdown(ctx);
        if (res != connection::Result::success) {
            printf("Shutdown Tx failed: %s\n", mesh::connection::result2str(res));
        }

        // Destroy resources
        delete emulated_tx;
        delete conn_tx;
    }

    // /* start gRPC server */
    // std::jthread rpcThread(RunRPCServer, proxy_ctx);

    // /* start TCP server */
    // std::thread tcpThread(RunTCPServer, proxy_ctx);

    // // Start ClientAPI server
    // std::thread clientApiThread([]() { RunClientAPIServer(ctx); });

    // // Wait until the main context is cancelled
    // ctx.done();

    // clientApiThread.join();

    // // Stop Local connection manager
    // log::info("Shutting down Local conn manager");
    // auto tctx = context::WithTimeout(context::Background(),
    //                                  std::chrono::milliseconds(5000));
    // connection::local_manager.shutdown(ctx);

    // log::info("Media Proxy exited");
    // exit(0);

    // rpcThread.join();
    // tcpThread.join();

    delete (proxy_ctx);

    return 0;
}
