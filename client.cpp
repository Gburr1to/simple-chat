#include <cstdlib>
#include <deque>
#include <iostream>
#include <thread>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

class ChatClient {
public:
    ChatClient(boost::asio::io_context& io_context, const tcp::resolver::results_type& endpoints)
        : io_context_(io_context), socket_(io_context) {
        do_connect(endpoints);
    }

    // Blokirajoča funkcija za prijavo - preprosteje za uporabniško izkušnjo
    bool login_loop() {
        while (true) {
            std::cout << "Vnesite uporabnisko ime: ";
            std::string username;
            std::getline(std::cin, username);
            
            if (username.empty()) continue;

            // Pošlji ime + novo vrstico
            std::string msg = username + "\n";
            boost::asio::write(socket_, boost::asio::buffer(msg));

            // Čakaj na odgovor (blokirajoče branje za handshake)
            boost::asio::streambuf buffer;
            boost::asio::read_until(socket_, buffer, "\n");
            std::istream is(&buffer);
            std::string response;
            std::getline(is, response);
            
             // Odstrani \r (če obstaja)
            if (!response.empty() && response.back() == '\r') response.pop_back();

            if (response == "ACCEPTED") {
                std::cout << "Prijava uspesna! Dobrodosli v klepetalnici.\n";
                return true;
            } else if (response == "REJECTED") {
                std::cout << "Napaka: Ime je ze zasedeno. Poskusite znova.\n";
            } else {
                std::cout << "Neznan odgovor streznika: " << response << "\n";
                return false;
            }
        }
    }

    void write(const std::string& msg) {
        boost::asio::post(io_context_,
            [this, msg]() {
                bool write_in_progress = !write_msgs_.empty();
                write_msgs_.push_back(msg + "\n");
                if (!write_in_progress) {
                    do_write();
                }
            });
    }

    void close() {
        boost::asio::post(io_context_, [this]() { socket_.close(); });
    }

    // Začetek asinhronega branja po uspešni prijavi
    void start_reading() {
        do_read();
    }

private:
    void do_connect(const tcp::resolver::results_type& endpoints) {
        boost::asio::connect(socket_, endpoints);
    }

    void do_read() {
        boost::asio::async_read_until(socket_, read_buffer_, "\n",
            [this](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    std::istream is(&read_buffer_);
                    std::string msg;
                    std::getline(is, msg);
                    if (!msg.empty() && msg.back() == '\r') msg.pop_back();
                    
                    std::cout << msg << "\n"; // Izpis sporočila
                    
                    do_read();
                } else {
                    socket_.close();
                }
            });
    }

    void do_write() {
        boost::asio::async_write(socket_,
            boost::asio::buffer(write_msgs_.front().data(), write_msgs_.front().length()),
            [this](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    write_msgs_.pop_front();
                    if (!write_msgs_.empty()) {
                        do_write();
                    }
                } else {
                    socket_.close();
                }
            });
    }

    boost::asio::io_context& io_context_;
    tcp::socket socket_;
    boost::asio::streambuf read_buffer_;
    std::deque<std::string> write_msgs_;
};

int main(int argc, char* argv[]) {
    try {
        if (argc != 3) {
            std::cerr << "Uporaba: client <host> <port>\n";
            return 1;
        }

        boost::asio::io_context io_context;
        tcp::resolver resolver(io_context);
        auto endpoints = resolver.resolve(argv[1], argv[2]);

        ChatClient c(io_context, endpoints);

        // 1. Sinhrona prijava
        if (!c.login_loop()) {
            return 1; 
        }

        // 2. Začni asinhrono branje
        c.start_reading();

        // 3. Zaženi omrežno nit (nit za prejemanje)
        std::thread t([&io_context]() { io_context.run(); });

        // 4. Glavna nit bere vnos s tipkovnice (nit za pošiljanje)
        std::string line;
        while (std::getline(std::cin, line)) {
            c.write(line);
        }

        c.close();
        t.join();
    } catch (std::exception& e) {
        std::cerr << "Izjema: " << e.what() << "\n";
    }

    return 0;
}