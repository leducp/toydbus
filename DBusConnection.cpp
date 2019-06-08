// C++
#include <regex>
#include <iostream>

// POSIX
#include <sys/socket.h> 
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>

#include "DBusConnection.h"
#include <iomanip>


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
        std::cout << myName << std::endl;
        return ESUCCESS;
    }
    
    
    DBusError DBusConnection::recv(DBusMessage& msg, milliseconds timeout)
    {
        // Start with fixed length header part.
        DBusError err = readData(&msg.header_, sizeof(struct Header), timeout);
        if (err)
        {
            return err;
        }
        
        // Next read header fields.       
        msg.body_.resize(sizeof(uint32_t));
        err = readData(msg.body_.data(), sizeof(uint32_t), timeout); // read fields size
        if (err)
        {
            return err;
        }
        
        uint32_t fields_size = *reinterpret_cast<uint32_t*>(msg.body_.data());
        msg.body_.resize(fields_size + sizeof(uint32_t));
        err = readData(msg.body_.data() + sizeof(uint32_t), msg.body_.size() - sizeof(uint32_t), timeout);
        if (err)
        {
            return err;
        }
        printf("header size: %d %d\n", fields_size, msg.body_.size());
        
        // Read padding.
        uint32_t padding_size = 8 - (fields_size % 8);
        if (padding_size > 0)
        {
            printf("not here\n");
            std::vector<uint8_t> padding;
            padding.resize(padding_size);
            err = readData(padding.data(), padding.size(), timeout);
            if (err)
            {
                return err;
            }
        }

        err = msg.extractArgument(msg.fields_);
        if (err)
        {
            return err;
        }
        /*
        // Parse header fields.
        uint32_t fpos = 0;
        while (fpos < fields.size())
        {
            // extract field type
            FIELD f = static_cast<FIELD>(fields[fpos]);
            
            // extract signature
            fpos += 1;
            uint8_t signature_size = fields[fpos];
            
            fpos += 1;
            Signature s;
            s.insert(s.begin(), fields.data() + fpos, fields.data() + fpos + signature_size);
            fpos += signature_size + 1; // +1 for trailing null
            
            // extract value
            switch (f)
            {
                case FIELD::ERROR_NAME:
                case FIELD::DESTINATION: 
                case FIELD::MEMBER:
                case FIELD::SENDER:
                case FIELD::INTERFACE:
                {
                    if (s != DBUS_TYPE::STRING)
                    {
                        return EERROR("Wrong signature: should be 's', got '" + s + "'.");
                    }
                    uint32_t str_size = *reinterpret_cast<uint32_t*>(fields.data() + fpos);
                    fpos += 4;
                    std::string str;
                    str.insert(str.begin(), fields.data() + fpos, fields.data() + fpos + str_size);
                    fpos += str_size + 1;
                    msg.fields_.push_back({f, {str}});
                    break;
                }
                case FIELD::SIGNATURE: 
                {
                    if (s != DBUS_TYPE::SIGNATURE)
                    {
                        return EERROR("Wrong signature: should be 'g', got '" + s + "'.");
                    }
                    uint8_t str_size = *reinterpret_cast<uint8_t*>(fields.data() + fpos);
                    fpos += 1;
                    Signature s;
                    s.insert(s.begin(), fields.data() + fpos, fields.data() + fpos + str_size);
                    fpos += str_size + 1;
                    msg.fields_.push_back({f, {s}});
                    msg.signature_ = s;
                    break;
                }
                case FIELD::PATH:
                {
                    if (s != DBUS_TYPE::PATH)
                    {
                        return EERROR("Wrong signature: should be 'o', got '" + s + "'.");
                    }
                    uint32_t str_size = *reinterpret_cast<uint32_t*>(fields.data() + fpos);
                    fpos += 4;
                    ObjectPath path;
                    path.insert(path.begin(), fields.data() + fpos, fields.data() + fpos + str_size);
                    fpos += str_size + 1;
                    msg.fields_.push_back({f, {path}});
                    break;
                }
                case FIELD::REPLY_SERIAL:
                case FIELD::UNIX_FDS: 
                {
                    if (s != DBUS_TYPE::UINT32)
                    {
                        return EERROR("Wrong signature: should be 'u', got '" + s + "'.");
                    }
                    uint32_t val = *reinterpret_cast<uint32_t*>(fields.data() + fpos);
                    fpos += 4;
                    msg.fields_.push_back({f, {val}});
                    break;
                }
                case FIELD::INVALID: 
                default: 
                {
                    return EERROR("Invalid field type.");
                }
            }
            
            // each entry aligned on 8 bytes.
            if (fpos % 8)
            {
                fpos += (8 - fpos % 8);
            }
        }
        */
        
        // Read message body. 
        msg.body_.clear();
        msg.body_.resize(msg.header_.size);
        return readData(msg.body_.data(), msg.body_.size(), timeout);
    }
    
    
    DBusError DBusConnection::send(DBusMessage&& msg)
    {
        if (not name_.empty())
        {
            // Add sender filed with our name if we know it (if not, probably the Hello() message).
            msg.fields_.push_back({FIELD::SENDER, {name_}});
        }
        
        msg.serialize();
        std::cout << msg.dump() << std::endl;

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
