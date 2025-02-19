#ifndef DBUS_VARIANT_H
#define DBUS_VARIANT_H

#include "Protocol.h"
#include <vector>
#include <iostream>

namespace dbus
{
    class DBusVariant;
    class DBusVariant
    {
    public:
        DBusVariant(DBUS_TYPE type = DBUS_TYPE::UNKNOWN);
        ~DBusVariant();

        void transform(DBUS_TYPE type);
        DBUS_TYPE type() const { return type_; }
        void const* data() const { return storage_; }

        // copy
        DBusVariant(DBusVariant const& other);
        DBusVariant& operator=(DBusVariant const& other);

        // move
        DBusVariant(DBusVariant&& other);
        DBusVariant& operator=(DBusVariant&& other);

        /// Types constructors / assignments
        template<typename T>
        DBusVariant(T const& value)
        {
            static_assert(dbusType<T>() != DBUS_TYPE::UNKNOWN, "Invalid DBus type");

            transform(dbusType<T>());
            get<T>() = value;
        }

        template<typename T>
        DBusVariant& operator=(T const& value)
        {
            static_assert(dbusType<T>() != DBUS_TYPE::UNKNOWN, "Invalid DBus type");

            transform(dbusType<T>());
            get<T>() = value;
            return *this;
        }

        bool isValid() const { return type_ != DBUS_TYPE::UNKNOWN; }

        template<typename T>
        T& get()
        {
            static_assert(dbusType<T>() != DBUS_TYPE::UNKNOWN, "Invalid DBus type");

            return *static_cast<T*>(storage_);
        }

        template<typename T>
        T const& get() const
        {
            static_assert(dbusType<T>() != DBUS_TYPE::UNKNOWN, "Invalid DBus type");

            return *static_cast<T*>(storage_);
        }

    private:
        void cleanup();
        void copy(DBusVariant const& other);

        DBUS_TYPE type_{DBUS_TYPE::UNKNOWN};
        void* storage_{nullptr};
    };

    std::ostream& operator<<(std::ostream& out, DBusVariant const& v);
}

#endif
