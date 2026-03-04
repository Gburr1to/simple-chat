#include <cstdlib>
#include <deque>
#include <iostream>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <mutex>
#include <algorithm>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

// Vmesnik za udeleženca v klepetu
class ChatParticipant {
public:
    virtual ~ChatParticipant() {}
    virtual void deliver(const std::string& msg) = 0;
    virtual std::string get_username() const = 0;
};

typedef std::shared_ptr<ChatParticipant> ChatParticipantPtr;

// Razred, ki upravlja sobo za klepet (seznam uporabnikov)
class ChatRoom {
public:
    // Pridružitev uporabnika - vrne false, če ime že obstaja
    bool join(ChatParticipantPtr participant, const std::string& username) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (usernames_.find(username) != usernames_.end()) {
            return false; // Ime zasedeno
        }
        participants_.insert(participant);
        usernames_.insert(username);

        // Obvestilo ostalim
        std::string sys_msg = "[SERVER]: Uporabnik " + username + " se je pridruzil.\n";
        broadcast(sys_msg, participant);
        return true;
    }

    void leave(ChatParticipantPtr participant) {
        std::lock_guard<std::mutex> lock(mutex_);   //lock_guard je objekt, ki se kljub napakam pravilno izniči in se mutex odklene
        if (participants_.find(participant) != participants_.end()) {
            std::string username = participant->get_username();
            participants_.erase(participant);
            usernames_.erase(username);

            std::string sys_msg = "[SERVER]: Uporabnik " + username + " je odsel.\n";
            broadcast(sys_msg, participant);
        }
    }

    // Pošlje sporočilo vsem RAZEN pošiljatelju
    void broadcast(const std::string& msg, ChatParticipantPtr sender) {
        // Opomba: Tukaj ne zaklepamo znotraj zanke, da preprečimo deadlock,
        // če deliver() kliče kaj drugega. (V preprosti implementaciji je OK).
        for (auto participant : participants_) {
            if (participant != sender) {
                participant->deliver(msg);
            }
        }
    }

private:
    std::set<ChatParticipantPtr> participants_;  //set se uporablja, ker omogoča zelo hitro iskanje podatkov, logaritmično
    std::set<std::string> usernames_;
    std::mutex mutex_; // Zaščita za večnitni dostop
};

// Sejo (povezavo) z enim odjemalcem
class ChatSession
    : public ChatParticipant,
      public std::enable_shared_from_this<ChatSession> { //da se razred ChatSession ne uniči dokler so v
                                                        //teku omrežni klici, ki se nanašajo nanj
public:
    ChatSession(tcp::socket socket, ChatRoom& room)
        : socket_(std::move(socket)), room_(room) {}

    void start() {
        do_login();
    }

    void deliver(const std::string& msg) override {
        bool write_in_progress = !write_msgs_.empty();
        write_msgs_.push_back(msg);
        if (!write_in_progress) {
            do_write();
        }
    }

    std::string get_username() const override {
        return username_;
    }

private:
    // 1. Faza: Prijava
    void do_login() {
        auto self(shared_from_this()); //je nov shared pointer
        boost::asio::async_read_until(socket_, read_buffer_, "\n",
            [this, self](boost::system::error_code ec, std::size_t length) { //zajame self, števec referenc na
                if (!ec) {      //ChatSession se poveča za 1, objekt ostane živ, dokler se lambda ne zaključi - varnost
                    std::istream is(&read_buffer_);
                    std::getline(is, username_);

                    // Odstrani \r če obstaja (Windows/Telnet) - ta znak pomakne kazalec na začetek bralne vrstice
                    if (!username_.empty() && username_.back() == '\r') username_.pop_back();

                    if (room_.join(self, username_)) {
                        // Uspeh
                        std::string reply = "ACCEPTED\n";
                        boost::asio::async_write(socket_, boost::asio::buffer(reply),
                            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                                if (!ec) {
                                    do_read_loop(); // Začni brati sporočila
                                }
                            });
                    } else {
                        // Napaka - ime zasedeno
                        std::string reply = "REJECTED\n";
                        boost::asio::async_write(socket_, boost::asio::buffer(reply),
                            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                                if (!ec) {
                                    do_login(); // Poskusi znova prebrati ime
                                }
                            });
                    }
                } else {
                    room_.leave(self);
                }
            });
    }

    // 2. Faza: Branje sporočil
    void do_read_loop() {
        auto self(shared_from_this());
        boost::asio::async_read_until(socket_, read_buffer_, "\n",
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) {
                    std::istream is(&read_buffer_);
                    std::string msg;
                    std::getline(is, msg);

                    if (!msg.empty() && msg.back() == '\r') msg.pop_back();

                    // Oblikuj sporočilo: "User: Sporočilo"
                    std::string formatted_msg = username_ + ": " + msg + "\n";
                    room_.broadcast(formatted_msg, self);

                    do_read_loop();
                } else {
                    room_.leave(self);
                }
            });
    }

    void do_write() {
        auto self(shared_from_this());
        boost::asio::async_write(socket_,
            boost::asio::buffer(write_msgs_.front().data(), write_msgs_.front().length()),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) {
                    write_msgs_.pop_front();
                    if (!write_msgs_.empty()) {
                        do_write();
                    }
                } else {
                    room_.leave(self);
                }
            });
    }

    tcp::socket socket_;
    ChatRoom& room_;
    boost::asio::streambuf read_buffer_;
    std::deque<std::string> write_msgs_;
    std::string username_;
};

class ChatServer {
public:
    ChatServer(boost::asio::io_context& io_context, const tcp::endpoint& endpoint)
        : acceptor_(io_context, endpoint) {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<ChatSession>(std::move(socket), room_)->start();
                }
                do_accept();
            });
    }

    tcp::acceptor acceptor_;
    ChatRoom room_;
};

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            std::cerr << "Uporaba: server <port>\n";
            return 1;
        }

        boost::asio::io_context io_context;
        tcp::endpoint endpoint(tcp::v4(), std::atoi(argv[1]));
        ChatServer server(io_context, endpoint);

        // Uporaba niti: Zaženemo io_context v več niti
        std::vector<std::thread> threads;
        // Zaženemo 4 niti (ali poljubno število) za sočasno obdelavo
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back([&io_context]() { io_context.run(); });
        }

        std::cout << "Strežnik teče na portu " << argv[1] << "...\n";

        for (auto& t : threads) {
            t.join();
        }
    } catch (std::exception& e) {
        std::cerr << "Izjema: " << e.what() << "\n";
    }

    return 0;
}