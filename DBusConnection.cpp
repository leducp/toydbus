// C++
#include <regex>
#include <iostream>
#include <fstream>
#include <cstring>

// POSIX
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>

#include "helpers.h"
#include "DBusConnection.h"


namespace dbus
{
    namespace auth
    {
        std::string const AUTH      {"AUTH"};
        std::string const EXTERNAL  {"EXTERNAL"};
        std::string const REJECTED  {"REJECTED"};
        std::string const ENDLINE   {"\r\n"};
        std::string const NEGOCIATE {"NEGOTIATE_UNIX_FD"};
        std::string const BEGIN     {"BEGIN"};
    }

    DBusError DBusConnection::connect(BUS_TYPE bus)
    {
        //-------- connect socket --------//
        DBusError err = initSocket(bus);
        if (err)
        {
            return err;
        }

        //-------- start authentication --------//
        // discover supported mode
        err = writeAuthRequest(auth::AUTH);
        if (err)
        {
            return err;
        }

        std::string reply;
        err = readAuth(reply, 2000ms);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        std::regex re{"\\s+"};
        std::vector<std::string> supported_auth
        {
            std::sregex_token_iterator(reply.begin(), reply.end(), re, -1),
            std::sregex_token_iterator()
        };

        std::cout << "Supported AUTH mode: " << std::endl;
        for (auto i : supported_auth)
        {
            if (i == auth::REJECTED)
            {
                continue;
            }
            std::cout << i << std::endl;
        }

        //TODO select mode

        //-------- EXTERNAL mode --------//

        // create UID string to authenticate
        std::stringstream ss;
        ss << "AUTH EXTERNAL ";
        for (auto i : std::to_string(getuid()))
        {
            ss << std::hex << int(i);
        }
        std::cout << ss.str() << std::endl;

        err = writeAuthRequest(ss.str());
        if (err)
        {
            err += EERROR("");
            return err;
        }

        err = readAuth(reply, 2000ms);
        if (err)
        {
            return err;
        }
        std::cout << reply << std::endl;

        //-------- Nego UNIX FD --------//
        err = writeAuthRequest(auth::NEGOCIATE);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        err = readAuth(reply, 2000ms);
        if (err)
        {
            err += EERROR("");
            return err;
        }
        std::cout << reply << std::endl;

        //-------- We are ready: BEGIN --------//
        err = writeAuthRequest(auth::BEGIN);
        if (err)
        {
            err += EERROR("");
            return err;
        }

        //-------- Send Hello() and get our unique name --------//
        DBusMessage hello;
        hello.prepareCall("org.freedesktop.DBus",
                          "/org/freedesktop/DBus",
                          "org.freedesktop.DBus",
                          "Hello");

        DBusMessage uniqueName;
        err = send(std::move(hello));
        if (err)
        {
            err += EERROR("");
            return err;
        }

        err = recv(uniqueName, 100ms);
        if (err)
        {
            err += EERROR("");
            return err;
        }
        std::cout << uniqueName.dump() << std::endl;
        std::string myName;
        err = uniqueName.extractArgument(myName);
        if (err)
        {
            err += EERROR("");
            return err;
        }
        std::cout << "My name is " << myName << std::endl;
        return ESUCCESS;
    }

    DBusError DBusConnection::recv(DBusMessage& msg, milliseconds timeout)
    {
        // Start with fixed length header part.
        msg.body_.resize(sizeof(struct Header));
        DBusError err = readData(msg.body_.data(), sizeof(struct Header), timeout);
        if (err)
        {
            return err;
        }
        std::memcpy(&msg.header_, msg.body_.data(), sizeof(struct Header));
        msg.body_pos_ = msg.body_.size();

        // Next read header fields.
        msg.body_.resize(msg.body_.size() + sizeof(uint32_t));
        err = readData(msg.body_.data() + msg.body_pos_, sizeof(uint32_t), timeout); // read fields size
        if (err)
        {
            return err;
        }

        uint32_t fields_size = *reinterpret_cast<uint32_t*>(msg.body_.data() + msg.body_pos_);
        msg.body_.resize(msg.body_.size() + fields_size);
        err = readData(msg.body_.data() + msg.body_pos_ + sizeof(uint32_t), fields_size, timeout);
        if (err)
        {
            err += EERROR("");
            return err;
        }
/*
        std::ofstream myfile;
        static int y;
        ++y;
        myfile.open ("yolo_" + std::to_string(y), std::ios::out | std::ios::binary);
        myfile.write(reinterpret_cast<const char*>(msg.body_.data()), msg.body_.size());
*/
        err = msg.extractArgument(msg.fields_);
        if (err)
        {
            return err;
        }

        // copy signature to internal field if any.
        auto signatureIt = msg.fields_.find(FIELD::SIGNATURE);
        if (signatureIt == msg.fields_.end())
        {
            msg.signature_.clear();
        }
        else
        {
            msg.signature_ = signatureIt->second.get<Signature>();
        }

        // Read header padding.
        uint8_t header_padding[7]; // at most 7 bytes of padding.
        uint32_t padding_size = msg.body_.size() % 8;
        if (padding_size)
        {
            err = readData(header_padding, 8 - padding_size, timeout);
            if (err)
            {
                err += EERROR("");
                return err;
            }
        }

        // Read message body.
        msg.body_pos_ = 0;
        msg.body_.clear();
        msg.body_.resize(msg.header_.size);
        return readData(msg.body_.data(), msg.body_.size(), timeout);
    }


    DBusError DBusConnection::send(DBusMessage&& msg)
    {
        if (not name_.empty())
        {
            // Add sender filed with our name if we know it (if not, probably the Hello() message).
            msg.fields_.emplace(FIELD::SENDER, name_);
        }

        msg.serialize();

        // Send message.
        return writeData(msg.headerBuffer_.data(), msg.headerBuffer_.size(), 100ms);
    }


    DBusError DBusConnection::initSocket(BUS_TYPE bus)
    {
        fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ < 0)
        {
            return EERROR(strerror(errno));
        }

        int rc = -1;
        switch (bus)
        {
            case BUS_SYSTEM:
            {
                constexpr struct sockaddr_un const SYSTEM_BUS {AF_UNIX, "/var/run/dbus/system_bus_socket" };
                rc = ::connect(fd_, (struct sockaddr*)&SYSTEM_BUS, sizeof(struct sockaddr_un));
                break;
            }

            case BUS_SESSION:
            {
                return EERROR("Not implemented");
            }

            case BUS_USER:
            {
                return EERROR("Not implemented");
            }
        }
        if (rc < 0)
        {
            return EERROR(strerror(errno));
        }

        // set socket non blocking
        int flags = fcntl(fd_, F_GETFL, 0);
        if (flags < 0)
        {
            return EERROR(strerror(errno));
        }

        rc = fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
        if (rc < 0)
        {
            return EERROR(strerror(errno));
        }

        rc = write(fd_, "\0", 1);
        if (rc < 0)
        {
            return EERROR(strerror(errno));
        }

        return ESUCCESS;
    }


    DBusError DBusConnection::writeAuthRequest(std::string const& request)
    {
        std::string req = request + auth::ENDLINE;
        int32_t rc = write(fd_, req.c_str(), req.size());
        if (rc < 0)
        {
            return EERROR(strerror(errno));
        }

        return ESUCCESS;
    }


    DBusError DBusConnection::readAuth(std::string& reply, milliseconds timeout)
    {
        reply.clear();
        auto start = steady_clock::now();

        while (true)
        {
            auto now = steady_clock::now();
            milliseconds spent = duration_cast<milliseconds>(now - start);
            if ((timeout - spent) < 0ms)
            {
                return EERROR("timeout");
            }

            uint8_t buffer[4096];
            int r = read(fd_, buffer, 4096);
            if (r < 0)
            {
                if (errno == EAGAIN)
                {
                    usleep(1000);
                    continue;
                }
                return EERROR(strerror(errno));
            }

            reply.insert(reply.end(), buffer, buffer + r);
            if (std::equal(auth::ENDLINE.rbegin(), auth::ENDLINE.rend(), reply.rbegin()))
            {
                return ESUCCESS;
            }
        }
    }


    DBusError DBusConnection::readData(void* data, uint32_t data_size, milliseconds timeout)
    {
        auto start_timestamp = steady_clock::now();

        uint32_t to_read = data_size;
        uint32_t position = 0;
        uint8_t* buffer = reinterpret_cast<uint8_t*>(data);
        while (to_read > 0)
        {
            auto now = steady_clock::now();
            milliseconds spent = duration_cast<milliseconds>(now - start_timestamp);
            if ((timeout - spent) < 0ms)
            {
                return EERROR("timeout");
            }

            int r = read(fd_, buffer + position, to_read);
            if (r < 0)
            {
                if (errno == EAGAIN)
                {
                    usleep(1000);
                    continue;
                }
                return EERROR(strerror(errno));
            }

            to_read -= r;
            position += r;
        }

        return ESUCCESS;
    }


    DBusError DBusConnection::writeData(void const* data, uint32_t data_size, milliseconds timeout)
    {
        auto start_timestamp = steady_clock::now();

        uint32_t to_write = data_size;
        uint32_t position = 0;
        uint8_t const* buffer = reinterpret_cast<uint8_t const*>(data);
        while (to_write > 0)
        {
            auto now = steady_clock::now();
            milliseconds spent = duration_cast<milliseconds>(now - start_timestamp);
            if ((timeout - spent) < 0ms)
            {
                return EERROR("timeout");
            }

            int r = write(fd_, buffer + position, to_write);
            if (r < 0)
            {
                if (errno == EAGAIN)
                {
                    usleep(1000);
                    continue;
                }
                return EERROR(strerror(errno));
            }

            to_write -= r;
            position += r;
        }

        return ESUCCESS;
    }
}
