#include "common.h"
#include <lozzaxmq/hex.h>
#include <map>
#include <set>

using namespace lozzaxmq;

TEST_CASE("failure responses - UNKNOWNCOMMAND", "[failure][UNKNOWNCOMMAND]") {
    std::string listen = "tcp://127.0.0.1:4567";
    LozzaxMQ server{
        "", "", // generate ephemeral keys
        false, // not a service node
        [](auto) { return ""; },
        get_logger("S» "),
        LogLevel::trace
    };
    server.listen_plain(listen);
    server.start();

    // Use a raw socket here because I want to see the raw commands coming on the wire
    zmq::context_t client_ctx;
    zmq::socket_t client{client_ctx, zmq::socket_type::dealer};
    client.connect(listen);
    // Handshake: we send HI, they reply HELLO.
    client.send(zmq::message_t{"HI", 2}, zmq::send_flags::none);
    {
        zmq::message_t hello;
        client.recv(hello);

        auto lock = catch_lock();
        REQUIRE( hello.to_string() == "HELLO" );
        REQUIRE_FALSE( hello.more() );
    }

    client.send(zmq::message_t{"a.a", 3}, zmq::send_flags::none);
    zmq::message_t resp;
    client.recv(resp);

    auto lock = catch_lock();
    REQUIRE( resp.to_string() == "UNKNOWNCOMMAND" );
    REQUIRE( resp.more() );
    client.recv(resp);
    REQUIRE( resp.to_string() == "a.a" );
    REQUIRE_FALSE( resp.more() );
}

TEST_CASE("failure responses - NO_REPLY_TAG", "[failure][NO_REPLY_TAG]") {
    std::string listen = "tcp://127.0.0.1:4567";
    LozzaxMQ server{
        "", "", // generate ephemeral keys
        false, // not a service node
        [](auto) { return ""; },
        get_logger("S» "),
        LogLevel::trace
    };
    server.listen_plain(listen);
    server.add_category("x", AuthLevel::none)
        .add_request_command("r", [] (auto& m) { m.send_reply("a"); });
    server.start();

    // Use a raw socket here because I want to see the raw commands coming on the wire
    zmq::context_t client_ctx;
    zmq::socket_t client{client_ctx, zmq::socket_type::dealer};
    client.connect(listen);
    // Handshake: we send HI, they reply HELLO.
    client.send(zmq::message_t{"HI", 2}, zmq::send_flags::none);
    {
        zmq::message_t hello;
        client.recv(hello);

        auto lock = catch_lock();
        REQUIRE( hello.to_string() == "HELLO" );
        REQUIRE_FALSE( hello.more() );
    }

    client.send(zmq::message_t{"x.r", 3}, zmq::send_flags::none);
    zmq::message_t resp;
    client.recv(resp);

    {
        auto lock = catch_lock();
        REQUIRE( resp.to_string() == "NO_REPLY_TAG" );
        REQUIRE( resp.more() );
        client.recv(resp);
        REQUIRE( resp.to_string() == "x.r" );
        REQUIRE_FALSE( resp.more() );
    }

    client.send(zmq::message_t{"x.r", 3}, zmq::send_flags::sndmore);
    client.send(zmq::message_t{"foo", 3}, zmq::send_flags::none);
    client.recv(resp);
    {
        auto lock = catch_lock();
        REQUIRE( resp.to_string() == "REPLY" );
        REQUIRE( resp.more() );
        client.recv(resp);
        REQUIRE( resp.to_string() == "foo" );
        REQUIRE( resp.more() );
        client.recv(resp);
        REQUIRE( resp.to_string() == "a" );
        REQUIRE_FALSE( resp.more() );
    }
}

TEST_CASE("failure responses - FORBIDDEN", "[failure][FORBIDDEN]") {
    std::string listen = "tcp://127.0.0.1:4567";
    LozzaxMQ server{
        "", "", // generate ephemeral keys
        false, // not a service node
        [](auto) { return ""; },
        get_logger("S» "),
        LogLevel::trace
    };
    server.listen_plain(listen, [](auto, auto, auto) {
            static int count = 0;
            ++count;
            return count == 1 ? AuthLevel::none : count == 2 ? AuthLevel::basic : AuthLevel::admin;
    });
    server.add_category("x", AuthLevel::basic)
        .add_command("x", [] (auto& m) { m.send_back("a"); });
    server.add_category("y", AuthLevel::admin)
        .add_command("x", [] (auto& m) { m.send_back("b"); });
    server.start();

    zmq::context_t client_ctx;
    std::array<zmq::socket_t, 3> clients;
    // Client 0 should get none auth level, client 1 should get basic, client 2 should get admin
    for (auto& client : clients) {
        client = {client_ctx, zmq::socket_type::dealer};
        client.connect(listen);
        // Handshake: we send HI, they reply HELLO.
        client.send(zmq::message_t{"HI", 2}, zmq::send_flags::none);
        {
            zmq::message_t hello;
            client.recv(hello);

            auto lock = catch_lock();
            REQUIRE( hello.to_string() == "HELLO" );
            REQUIRE_FALSE( hello.more() );
        }
    }

    for (auto& c : clients)
        c.send(zmq::message_t{"x.x", 3}, zmq::send_flags::none);

    zmq::message_t resp;
    clients[0].recv(resp);
    {
        auto lock = catch_lock();
        REQUIRE( resp.to_string() == "FORBIDDEN" );
        REQUIRE( resp.more() );
        clients[0].recv(resp);
        REQUIRE( resp.to_string() == "x.x" );
        REQUIRE_FALSE( resp.more() );
    }
    for (int i : {1, 2}) {
        clients[i].recv(resp);
        auto lock = catch_lock();
        REQUIRE( resp.to_string() == "a" );
        REQUIRE_FALSE( resp.more() );
    }

    for (auto& c : clients)
        c.send(zmq::message_t{"y.x", 3}, zmq::send_flags::none);

    for (int i : {0, 1}) {
        clients[i].recv(resp);
        auto lock = catch_lock();
        REQUIRE( resp.to_string() == "FORBIDDEN" );
        REQUIRE( resp.more() );
        clients[i].recv(resp);
        REQUIRE( resp.to_string() == "y.x" );
        REQUIRE_FALSE( resp.more() );
    }
    clients[2].recv(resp);
    {
        auto lock = catch_lock();
        REQUIRE( resp.to_string() == "b" );
        REQUIRE_FALSE( resp.more() );
    }
}

TEST_CASE("failure responses - NOT_A_SERVICE_NODE", "[failure][NOT_A_SERVICE_NODE]") {
    std::string listen = "tcp://127.0.0.1:4567";
    LozzaxMQ server{
        "", "", // generate ephemeral keys
        false, // not a service node
        [](auto) { return ""; },
        get_logger("S» "),
        LogLevel::trace
    };
    server.listen_plain(listen, [](auto, auto, auto) {
            static int count = 0;
            ++count;
            return count == 1 ? AuthLevel::none : count == 2 ? AuthLevel::basic : AuthLevel::admin;
    });
    server.add_category("x", Access{AuthLevel::none, false, true})
        .add_command("x", [] (auto&) {})
        .add_request_command("r", [] (auto& m) { m.send_reply(); })
        ;
    server.start();

    zmq::context_t client_ctx;
    zmq::socket_t client{client_ctx, zmq::socket_type::dealer};
    client.connect(listen);
    // Handshake: we send HI, they reply HELLO.
    client.send(zmq::message_t{"HI", 2}, zmq::send_flags::none);
    {
        zmq::message_t hello;
        client.recv(hello);

        auto lock = catch_lock();
        REQUIRE( hello.to_string() == "HELLO" );
        REQUIRE_FALSE( hello.more() );
    }

    client.send(zmq::message_t{"x.x", 3}, zmq::send_flags::none);

    zmq::message_t resp;
    client.recv(resp);
    {
        auto lock = catch_lock();
        REQUIRE( resp.to_string() == "NOT_A_SERVICE_NODE" );
        REQUIRE( resp.more() );
        client.recv(resp);
        REQUIRE( resp.to_string() == "x.x" );
        REQUIRE_FALSE( resp.more() );
    }

    client.send(zmq::message_t{"x.r", 3}, zmq::send_flags::sndmore);
    client.send(zmq::message_t{"xyz123", 6}, zmq::send_flags::none); // reply tag

    client.recv(resp);
    {
        auto lock = catch_lock();
        REQUIRE( resp.to_string() == "NOT_A_SERVICE_NODE" );
        REQUIRE( resp.more() );
        client.recv(resp);
        REQUIRE( resp.to_string() == "REPLY" );
        REQUIRE( resp.more() );
        client.recv(resp);
        REQUIRE( resp.to_string() == "xyz123" );
        REQUIRE_FALSE( resp.more() );
    }
}

TEST_CASE("failure responses - FORBIDDEN_SN", "[failure][FORBIDDEN_SN]") {
    std::string listen = "tcp://127.0.0.1:4567";
    LozzaxMQ server{
        "", "", // generate ephemeral keys
        false, // not a service node
        [](auto) { return ""; },
        get_logger("S» "),
        LogLevel::trace
    };
    server.listen_plain(listen, [](auto, auto, auto) {
            static int count = 0;
            ++count;
            return count == 1 ? AuthLevel::none : count == 2 ? AuthLevel::basic : AuthLevel::admin;
    });
    server.add_category("x", Access{AuthLevel::none, true, false})
        .add_command("x", [] (auto&) {})
        .add_request_command("r", [] (auto& m) { m.send_reply(); })
        ;
    server.start();

    zmq::context_t client_ctx;
    zmq::socket_t client{client_ctx, zmq::socket_type::dealer};
    client.connect(listen);
    // Handshake: we send HI, they reply HELLO.
    client.send(zmq::message_t{"HI", 2}, zmq::send_flags::none);
    {
        zmq::message_t hello;
        client.recv(hello);

        auto lock = catch_lock();
        REQUIRE( hello.to_string() == "HELLO" );
        REQUIRE_FALSE( hello.more() );
    }

    client.send(zmq::message_t{"x.x", 3}, zmq::send_flags::none);

    zmq::message_t resp;
    client.recv(resp);
    {
        auto lock = catch_lock();
        REQUIRE( resp.to_string() == "FORBIDDEN_SN" );
        REQUIRE( resp.more() );
        client.recv(resp);
        REQUIRE( resp.to_string() == "x.x" );
        REQUIRE_FALSE( resp.more() );
    }

    client.send(zmq::message_t{"x.r", 3}, zmq::send_flags::sndmore);
    client.send(zmq::message_t{"xyz123", 6}, zmq::send_flags::none); // reply tag

    client.recv(resp);
    {
        auto lock = catch_lock();
        REQUIRE( resp.to_string() == "FORBIDDEN_SN" );
        REQUIRE( resp.more() );
        client.recv(resp);
        REQUIRE( resp.to_string() == "REPLY" );
        REQUIRE( resp.more() );
        client.recv(resp);
        REQUIRE( resp.to_string() == "xyz123" );
        REQUIRE_FALSE( resp.more() );
    }
}
