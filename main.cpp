#include <iostream>

#include "DBusConnection.h"
#include "DBusMessage.h"

using namespace dbus;


void printObjects(DBusMessage& answer)
{
    Dict<ObjectPath, Dict<std::string, Dict<std::string, DBusVariant>>> yolo;
    DBusError err = answer.extractArgument(yolo);
    if (err)
    {
        err.what();
    }

    for (auto const& path : yolo)
    {
        std::cout << "path: " << path.first << " - ";
        for (auto const& interface : path.second)
        {
            std::cout << "interface: " << interface.first << std::endl;
            for (auto const& var : interface.second)
            {
                std::stringstream ss;
                ss << var.first << ": " << var.second;
                std::cout << "\t\t" << ss.str() << std::endl;
            }
        }
    }
}


int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    DBusConnection bus;
    DBusError err = bus.connect(DBusConnection::BUS_SYSTEM);
    if (err)
    {
        err.what();
        return 1;
    }

    DBusMessage msg;

    //uint32_t serial = msg.prepareCall("org.freedesktop.UDisks2", "/org/freedesktop/UDisks2", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");


    //uint32_t serial = msg.prepareCall("org.freedesktop.ModemManager1", "/org/freedesktop/ModemManager1", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");


    uint32_t serial = msg.prepareCall("org.freedesktop.NetworkManager", "/org/freedesktop",            "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");


    err = bus.send(std::move(msg));
    if (err)
    {
        err.what();
        return 1;
    }

    for (int i=0 ; i<100; ++i)
    {
        DBusMessage answer;
        err = bus.recv(answer, 100ms);
        if (err)
        {
            err.what();
            return 1;
        }

        if (answer.isError())
        {
            printf("aie: %s\n", answer.errorMessage().c_str());
            break;
        }

        if (answer.isReply())
        {
            if (answer.replySerial() == serial)
            {
                //std::cout <<  answer.dump();
                printObjects(answer);
            }
        }
    }

    return 0;
}
