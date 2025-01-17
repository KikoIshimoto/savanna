#pragma once

#include <boost/any.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/optional.hpp>
#include <boost/utility/string_view.hpp>

#include <functional>
#include <memory>
#include <thread>

#include "url.hpp"

namespace m2d
{
namespace savanna
{
	namespace beast = boost::beast;
	namespace http = beast::http;
	namespace net = boost::asio;
	namespace ssl = boost::asio::ssl;
	using tcp = boost::asio::ip::tcp;

	namespace websocket
	{
		using tls_stream = beast::websocket::stream<beast::ssl_stream<tcp::socket>>;
		using raw_stream = beast::websocket::stream<tcp::socket>;

		enum state
		{
			unknown,
			closed,
			connected
		};

		template <typename T>
		class session
		{
		private:
			tcp::resolver ws_resolver_;
			beast::flat_buffer buffer_;
			savanna::url url_;
			std::string scheme_;
			boost::optional<std::function<void(beast::flat_buffer &)>> on_message_handler_;
			T stream_;
			websocket::state current_state_ = unknown;

			void make_connection(websocket::raw_stream &stream, tcp::resolver::results_type const &results, std::string path)
			{
				net::connect(beast::get_lowest_layer(stream), results.begin(), results.end());
				stream.set_option(beast::websocket::stream_base::decorator([](beast::websocket::request_type &req) {
					req.set(http::field::user_agent, std::string(BOOST_BEAST_VERSION_STRING) + " savanna");
				}));

				stream.handshake(url_.host(), path);
			}

			void make_connection(websocket::tls_stream &stream, tcp::resolver::results_type const &results, std::string path)
			{
				net::connect(beast::get_lowest_layer(stream), results.begin(), results.end());
				stream.set_option(beast::websocket::stream_base::decorator([](beast::websocket::request_type &req) {
					req.set(http::field::user_agent, std::string(BOOST_BEAST_VERSION_STRING) + " savanna");
				}));

				stream.next_layer().handshake(ssl::stream_base::client);
				stream.handshake(url_.host(), path);
			}

			void on_read(beast::error_code ec, std::size_t bytes_transferred)
			{
				boost::ignore_unused(bytes_transferred);

				if (ec) {
					throw beast::system_error { ec };
				}

				if (on_message_handler_) {
					auto handler = *on_message_handler_;
					handler(buffer_);
				}
			}

			void on_write(beast::error_code ec, std::size_t bytes_transferred)
			{
				boost::ignore_unused(bytes_transferred);
				if (ec) {
					throw beast::system_error { ec };
				}
			}

			void set_current_state(websocket::state s)
			{
				current_state_ = s;
				state_changed(s);
			}

		public:
			std::function<void(websocket::state)> state_changed = [](websocket::state s) {};
			session(tcp::resolver resolver, T stream, savanna::url url)
			    : ws_resolver_(std::move(resolver))
			    , url_(url)
			    , scheme_(url_.scheme())
			    , stream_(std::move(stream))
			{
			}

			~session()
			{
				close();
			}

			websocket::state current_state()
			{
				return current_state_;
			}

			boost::system::error_code run(std::string path = "/")
			{
				try {
					auto control_callback = [this](beast::websocket::frame_type kind, boost::string_view payload) {
						if (kind == beast::websocket::frame_type::close) {
							this->set_current_state(closed);
						}
						boost::ignore_unused(kind, payload);
					};

					auto const results = ws_resolver_.resolve(url_.host(), url_.port_str());
					make_connection(stream_, results, path);
					stream_.control_callback(control_callback);
					set_current_state(connected);

				} catch (boost::wrapexcept<boost::system::system_error> e) {
					set_current_state(unknown);
					throw std::move(e);
				}
				boost::system::error_code ec;
				while (true) {
					beast::flat_buffer buffer;
					stream_.read(buffer, ec);
					if (ec.failed()){
						break;
					}
					if (on_message_handler_) {
						auto handler = *on_message_handler_;
						handler(buffer);
					}
				}
				return ec;
			}

			boost::system::error_code send(std::string data)
			{
				boost::system::error_code ec;
				stream_.write(net::buffer(data), ec);
				return ec;
			}

			boost::system::error_code close()
			{
				boost::system::error_code ec;
				if (stream_.is_open()) {
					stream_.close(beast::websocket::close_code::normal, ec);
				}
				return ec;
			}

			void on_message(std::function<void(beast::flat_buffer &)> handler)
			{
				on_message_handler_ = handler;
			}
		};
	}
}
}
