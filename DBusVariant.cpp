#include "DBusVariant.h"


namespace dbus
{
    DBusVariant::DBusVariant(DBUS_TYPE type)
    {
        transform(type);
    }


    DBusVariant::~DBusVariant()
    {
        cleanup();
    }


    DBusVariant::DBusVariant(DBusVariant const& other)
    {
        copy(other);
    }


    DBusVariant& DBusVariant::operator=(DBusVariant const& other)
    {
        copy(other);
        return *this;
    }


    DBusVariant::DBusVariant(DBusVariant&& other)
        : type_(other.type_)
        , storage_(other.storage_)
    {
        other.type_ = DBUS_TYPE::UNKNOWN;
        other.storage_ = nullptr;
    }


    DBusVariant& DBusVariant::operator=(DBusVariant&& other)
    {
        type_ = other.type_;
        storage_ = other.storage_;

        other.type_ = DBUS_TYPE::UNKNOWN;
        other.storage_ = nullptr;

        return *this;
    }

    void DBusVariant::transform(DBUS_TYPE type)
    {
        if (type_ == type)
        {
            return; // nothing to do.
        }

        cleanup();
        type_ = type;

        switch (type_)
        {
            case DBUS_TYPE::BYTE:      { storage_ = new uint8_t{0};                       break; }
            case DBUS_TYPE::INT16:     { storage_ = new int16_t{0};                       break; }
            case DBUS_TYPE::UINT16:    { storage_ = new uint16_t{0U};                     break; }
            case DBUS_TYPE::INT32:     { storage_ = new int32_t{0};                       break; }
            case DBUS_TYPE::UINT32:    { storage_ = new uint32_t{0U};                     break; }
            case DBUS_TYPE::INT64:     { storage_ = new int64_t{0};                       break; }
            case DBUS_TYPE::UINT64:    { storage_ = new uint64_t{0U};                     break; }
            case DBUS_TYPE::DOUBLE:    { storage_ = new double{0.0};                      break; }
            case DBUS_TYPE::BOOLEAN:   { storage_ = new bool{false};                      break; }
            case DBUS_TYPE::STRING:    { storage_ = new std::string{};                    break; }
            case DBUS_TYPE::SIGNATURE: { storage_ = new Signature{};                      break; }
            case DBUS_TYPE::PATH:      { storage_ = new ObjectPath{};                     break; }
            case DBUS_TYPE::ARRAY:     { storage_ = new std::vector<DBusVariant>{};       break; }
            default:
            {
                break;
            }
        }
    }


    void DBusVariant::cleanup()
    {
        switch (type_)
        {
            case DBUS_TYPE::BYTE:      { delete static_cast<uint8_t*>(storage_);                  break; }
            case DBUS_TYPE::INT16:     { delete static_cast<int16_t*>(storage_);                  break; }
            case DBUS_TYPE::UINT16:    { delete static_cast<uint16_t*>(storage_);                 break; }
            case DBUS_TYPE::INT32:     { delete static_cast<int32_t*>(storage_);                  break; }
            case DBUS_TYPE::UINT32:    { delete static_cast<uint32_t*>(storage_);                 break; }
            case DBUS_TYPE::INT64:     { delete static_cast<int64_t*>(storage_);                  break; }
            case DBUS_TYPE::UINT64:    { delete static_cast<uint64_t*>(storage_);                 break; }
            case DBUS_TYPE::DOUBLE:    { delete static_cast<double*>(storage_);                   break; }
            case DBUS_TYPE::BOOLEAN:   { delete static_cast<bool*>(storage_);                     break; }
            case DBUS_TYPE::STRING:    { delete static_cast<std::string*>(storage_);              break; }
            case DBUS_TYPE::SIGNATURE: { delete static_cast<Signature*>(storage_);                break; }
            case DBUS_TYPE::PATH:      { delete static_cast<ObjectPath*>(storage_);               break; }
            case DBUS_TYPE::ARRAY:     { delete static_cast<std::vector<DBusVariant>*>(storage_); break; }
            default:
            {
                break;
            }
        }

        storage_ = nullptr;
        type_ = DBUS_TYPE::UNKNOWN;
    }


    void DBusVariant::copy(DBusVariant const& other)
    {
        if (other.type_ != type_)
        {
            cleanup();
            transform(other.type_);
        }

        switch (type_)
        {
            case DBUS_TYPE::BYTE:    { *static_cast<uint8_t*>(storage_)     = other.get<uint8_t>();     break; }
            case DBUS_TYPE::INT16:   { *static_cast<int16_t*>(storage_)     = other.get<int16_t>();     break; }
            case DBUS_TYPE::UINT16:  { *static_cast<uint16_t*>(storage_)    = other.get<int16_t>();     break; }
            case DBUS_TYPE::INT32:   { *static_cast<int32_t*>(storage_)     = other.get<int16_t>();     break; }
            case DBUS_TYPE::UINT32:  { *static_cast<uint32_t*>(storage_)    = other.get<int16_t>();     break; }
            case DBUS_TYPE::INT64:   { *static_cast<int64_t*>(storage_)     = other.get<int16_t>();     break; }
            case DBUS_TYPE::UINT64:  { *static_cast<uint64_t*>(storage_)    = other.get<int16_t>();     break; }
            case DBUS_TYPE::DOUBLE:  { *static_cast<double*>(storage_)      = other.get<int16_t>();     break; }
            case DBUS_TYPE::BOOLEAN: { *static_cast<bool*>(storage_)        = other.get<int16_t>();     break; }
            case DBUS_TYPE::STRING:  { *static_cast<std::string*>(storage_) = other.get<std::string>(); break; }
            case DBUS_TYPE::SIGNATURE: { *static_cast<Signature*>(storage_) = other.get<Signature>();   break; }
            case DBUS_TYPE::PATH:    { *static_cast<ObjectPath*>(storage_)  = other.get<ObjectPath>();  break; }
            case DBUS_TYPE::ARRAY:   { *static_cast<std::vector<DBusVariant>*>(storage_) = other.get<std::vector<DBusVariant>>(); break; }
            default:
            {
                break;
            }
        }
    }


    std::ostream& operator<<(std::ostream& out, DBusVariant const& v)
    {
        switch (v.type())
        {
            case DBUS_TYPE::BYTE:      { out << v.get<uint8_t>();     break; }
            case DBUS_TYPE::INT16:     { out << v.get<int16_t>();     break; }
            case DBUS_TYPE::UINT16:    { out << v.get<uint16_t>();    break; }
            case DBUS_TYPE::INT32:     { out << v.get<int32_t>();     break; }
            case DBUS_TYPE::UINT32:    { out << v.get<uint32_t>();    break; }
            case DBUS_TYPE::INT64:     { out << v.get<int64_t>();     break; }
            case DBUS_TYPE::UINT64:    { out << v.get<uint64_t>();    break; }
            case DBUS_TYPE::DOUBLE:    { out << v.get<double>();      break; }
            case DBUS_TYPE::BOOLEAN:   { out << v.get<bool>();        break; }
            case DBUS_TYPE::STRING:    { out << v.get<std::string>(); break; }
            case DBUS_TYPE::SIGNATURE: { out << v.get<Signature>();   break; }
            case DBUS_TYPE::PATH:      { out << v.get<ObjectPath>() << " ";  break; }
            case DBUS_TYPE::ARRAY:
            {
                std::vector<DBusVariant> const& array = v.get<std::vector<DBusVariant>>();
                out << "[ ";
                for (auto const& entry : array)
                {
                    out << entry ;
                }
                out << " ]";
                break;
            }
            default:
            {
                out << "Unknown variant type";
                break;
            }
        }

        return out;
    }
}