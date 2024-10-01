/*
 * ApiServer.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>


class http_connection : public std::enable_shared_from_this<http_connection>
{
public:
    http_connection(boost::asio::ip::tcp::socket socket);
    void start();

private:
    boost::asio::ip::tcp::socket socket_;
    boost::beast::flat_buffer buffer_{8192};
    boost::beast::http::request<boost::beast::http::dynamic_body> request_;
    boost::beast::http::response<boost::beast::http::dynamic_body> response_;
    boost::asio::steady_timer deadline_;

    void read_request();
    void process_request();
    void create_response();
    void write_response();
    void check_deadline();
};

class ApiServer
{
public:
    ApiServer(std::string ip, int port);
    void http_server_(boost::asio::ip::tcp::acceptor& acceptor, boost::asio::ip::tcp::socket& socket);
    void startServer();
private:
    boost::asio::io_context ioc;
    boost::asio::ip::tcp::acceptor acceptor;
    boost::asio::ip::tcp::socket socket;
};
