namespace dbus
{
    template<typename T>
    void DBusMessage::addArgument(T const& arg)
    {
        signature_ += dbusType<T>();
        insertValue(dbusType<T>(), &arg, body_);
    }


    template<typename K, typename V>
    void DBusMessage::addArgument(Dict<K, V> const& arg)
    {
        signature_ += DBUS_TYPE::ARRAY;
        signature_ += DBUS_TYPE::DICT_BEGIN;
        signature_ += dbusType<K>();
        signature_ += dbusType<V>();
        signature_ += DBUS_TYPE::DICT_END;

        // array size.
        uint8_t* const array_size = &body_.back() + 1; // WARNING: at this point, this pointer is out of bound!
        body_.resize(body_.size() + sizeof(uint32_t));

        for (auto& i : arg.value)
        {
            updatePadding(8, body_); // dict entry aligned on 8 bytes.

            // insert key
            insertValue(dbusType<K>(), &i.first, body_);

            // insert value
            insertValue(dbusType<V>(), &i.second, body_);
        }

        *reinterpret_cast<uint32_t*>(array_size) = &body_.back() - (array_size + 3); // Update size.
    }


    template<typename T>
    DBusError DBusMessage::extractArgument(T& arg)
    {
        DBusError err = checkSignature(dbusType<T>());
        if (err)
        {
            return err;
        }

        return extractArgument(dbusType<T>(), &arg);
    }

    template<typename T>
    DBusError DBusMessage::extractArrayMember(std::vector<DBusVariant>& array)
    {
        T data;
        DBusError err = extractArgument(dbusType<T>(), &data);
        array.push_back(data);
        return err;
    }

    template<typename T>
    DBusError DBusMessage::extractVariantMember(DBusVariant& variant)
    {
        T data;
        DBusError err = extractArgument(dbusType<T>(), &data);
        variant = data;
        return err;
    }


    template<typename K, typename V>
    DBusError DBusMessage::extractArgument(Dict<K, V>& arg)
    {
        uint32_t array_size;
        DBusError err = extractArgument(DBUS_TYPE::UINT32, &array_size);
        if (err)
        {
            return err;
        }

        if (array_size == 0)
        {
            align(body_pos_, 8); // dict entries are aligned on 8 bytes, even if array is empty
            return ESUCCESS;
        }

        uint32_t const start_pos = body_pos_;
        while (body_pos_ < (array_size + start_pos))
        {
            align(body_pos_, 8); // dict entries are aligned on 8 bytes.

            K key;
            err = extractArgument(dbusType<K>(), &key);
            if (err)
            {
                return err;
            }

            V value;
            constexpr DBUS_TYPE valueType = dbusType<V>();
            if (valueType == DBUS_TYPE::UNKNOWN)
            {
                err = extractArgument(value); // not a basic type
            }
            else
            {
                err = extractArgument(dbusType<V>(), &value);
            }
            if (err)
            {
                return err;
            }

            arg.emplace(key, value);
        }

        return err;
    }
}
