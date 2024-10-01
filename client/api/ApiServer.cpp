/*
 * ApiServer.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include "ApiServer.h"

http_connection::http_connection(boost::asio::ip::tcp::socket socket)
    : socket_(std::move(socket)), deadline_(socket_.get_executor(), std::chrono::seconds(60))
{
}

void http_connection::start()
{
    read_request();
    check_deadline();
}

void http_connection::read_request()
{
    auto self = shared_from_this();

    boost::beast::http::async_read(
        socket_,
        buffer_,
        request_,
        [self](boost::beast::error_code ec,
            std::size_t bytes_transferred)
        {
            boost::ignore_unused(bytes_transferred);
            if(!ec)
                self->process_request();
        });
}

void http_connection::process_request()
{
    response_.version(request_.version());
    response_.keep_alive(false);

    switch(request_.method())
    {
    case boost::beast::http::verb::get:
        response_.result(boost::beast::http::status::ok);
        response_.set(boost::beast::http::field::server, "Beast");
        create_response();
        break;

    default:
        // We return responses indicating an error if
        // we do not recognize the request method.
        response_.result(boost::beast::http::status::bad_request);
        response_.set(boost::beast::http::field::content_type, "text/plain");
        boost::beast::ostream(response_.body())
            << "Invalid request-method '"
            << std::string(request_.method_string())
            << "'";
        break;
    }

    write_response();
}

void http_connection::create_response()
{
    if(request_.target() == "/count")
    {
        response_.set(boost::beast::http::field::content_type, "text/html");
        boost::beast::ostream(response_.body())
            << "<html>\n"
            <<  "<head><title>Request count</title></head>\n"
            <<  "<body>\n"
            <<  "<h1>Request count</h1>\n"
            <<  "<p>There have been "
            <<  " requests so far.</p>\n"
            <<  "</body>\n"
            <<  "</html>\n";
    }
    else if(request_.target() == "/time")
    {
        response_.set(boost::beast::http::field::content_type, "text/html");
        boost::beast::ostream(response_.body())
            <<  "<html>\n"
            <<  "<head><title>Current time</title></head>\n"
            <<  "<body>\n"
            <<  "<h1>Current time</h1>\n"
            <<  "<p>The current time is "
            <<  " seconds since the epoch.</p>\n"
            <<  "</body>\n"
            <<  "</html>\n";
    }
    else
    {
        response_.result(boost::beast::http::status::not_found);
        response_.set(boost::beast::http::field::content_type, "text/plain");
        boost::beast::ostream(response_.body()) << "File not found\r\n";
    }
}

void http_connection::write_response()
{
    auto self = shared_from_this();

    response_.content_length(response_.body().size());

    boost::beast::http::async_write(
        socket_,
        response_,
        [self](boost::beast::error_code ec, std::size_t)
        {
            self->socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
            self->deadline_.cancel();
        });
}

void http_connection::check_deadline()
{
    auto self = shared_from_this();

    deadline_.async_wait(
        [self](boost::beast::error_code ec)
        {
            if(!ec)
            {
                // Close socket to cancel any outstanding operation.
                self->socket_.close(ec);
            }
        });
}

ApiServer::ApiServer(std::string ip, int port)
: ioc({1}),
  acceptor(boost::asio::ip::tcp::acceptor(ioc, {boost::asio::ip::make_address(ip), port})),
  socket(boost::asio::ip::tcp::socket(ioc))
{
}

void ApiServer::http_server_(boost::asio::ip::tcp::acceptor& acceptor, boost::asio::ip::tcp::socket& socket)
{
  acceptor.async_accept(socket,
      [&](boost::beast::error_code ec)
      {
          if(!ec)
              std::make_shared<http_connection>(std::move(socket))->start();
          http_server_(acceptor, socket);
      });
}

void ApiServer::startServer()
{
    try
    {
        http_server_(acceptor, socket);

        ioc.run();
    }
    catch(std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}
