// debug
#include <iostream>
#include <fstream>

#include "helpers.h"
#include "DBusMessage.h"
#include "DBusVariant.h"

namespace dbus
{
    // Init serial counter.
    uint32_t DBusMessage::serialCounter_ = 1U;

    uint32_t DBusMessage::prepareCall(const std::string& name, const std::string& path, const std::string& interface, const std::string& method)
    {
        header_ = {ENDIANNESS::LITTLE, MESSAGE_TYPE::METHOD_CALL, 0, 1, 0, serialCounter_};
        serialCounter_++;

        fields_ =
        {{
            {FIELD::DESTINATION, {name}},
            {FIELD::PATH,        {ObjectPath{path}}},
            {FIELD::INTERFACE,   {interface}},
            {FIELD::MEMBER,      {method}},
        }};

        return serial();
    }

    std::string DBusMessage::dump() const
    {
        std::string dump;
        std::stringstream ss;

        ss << "----- Header -----" << std::endl;
        ss << "Endianess:   " << str(header_.endianness) << std::endl;
        ss << "Type:        " << str(header_.type) << std::endl;
        ss << "Flags:       " << (uint32_t)header_.flags << std::endl;
        ss << "Version:     " << (uint32_t)header_.version << std::endl;
        ss << "Size:        " << header_.size << std::endl;
        ss << "Serial:      " << header_.serial << std::endl;

        for (auto& f : fields_)
        {
            ss << str(f.first) << ": " << f.second << std::endl;
        }
        ss << std::endl;

        ss << "---- header hex (send only) ----" << std::endl;
        ss << hexDump(headerBuffer_);

        ss << "----------- Body hex -----------" << std::endl;
        ss << hexDump(body_);

/*
        std::ofstream myfile;
        myfile.open ("body" + std::to_string(header_.serial), std::ios::out | std::ios::binary);
        myfile.write(reinterpret_cast<const char*>(body_.data()), body_.size());
*/
        return ss.str();
    }


    void DBusMessage::serialize()
    {
        if (not body_.empty())
        {
            // add signature to header fields.
            DBusVariant v{signature_};
            fields_.emplace(FIELD::SIGNATURE, v);
        }

        // prepare buffer.
        headerBuffer_.clear();
        headerBuffer_.reserve(sizeof(struct Header) + header_.size + 256); // 256: preallocate memory for fields even if we dont know the finale size yet.

        // insert header
        header_.size = body_.size();  // Update header body size.
        uint8_t const* header_ptr = reinterpret_cast<uint8_t const*>(&header_);
        headerBuffer_.insert(headerBuffer_.begin(), header_ptr, header_ptr+sizeof(struct Header));

        // insert fields
        uint8_t* const fields_size = &headerBuffer_.back() + 1; // WARNING: at this point, this pointer is out of bound!
        headerBuffer_.resize(headerBuffer_.size() + sizeof(uint32_t));
        for (auto& i : fields_)
        {
            updatePadding(8, headerBuffer_);                           // dict entry aligned on 8 bytes.
            insertValue(DBUS_TYPE::BYTE, &i.first, headerBuffer_);     // key.
            insertValue(DBUS_TYPE::VARIANT, &i.second, headerBuffer_); // value.
        }
        *reinterpret_cast<uint32_t*>(fields_size) = &headerBuffer_.back() - (fields_size + 3); // compute size
        updatePadding(8, headerBuffer_); // header size shall be a multiple of 8.
    }


    void DBusMessage::insertValue(DBUS_TYPE type, void const* data, std::vector<uint8_t>& buffer)
    {
        auto insertPOD = [this](void const* data, int32_t data_size, std::vector<uint8_t>& buffer)
        {
            updatePadding(data_size, buffer);

            uint8_t const* ptr = reinterpret_cast<uint8_t const*>(data);
            buffer.insert(buffer.end(), ptr, ptr + data_size);
        };

        switch (type)
        {
            case DBUS_TYPE::BYTE:
            {
                insertPOD(data, 1, buffer);
                break;
            }
            case DBUS_TYPE::INT16:
            case DBUS_TYPE::UINT16:
            {
                insertPOD(data, 2, buffer);
                break;
            }
            case DBUS_TYPE::BOOLEAN:
            case DBUS_TYPE::UINT32:
            case DBUS_TYPE::INT32:
            {
                insertPOD(data, 4, buffer);
                break;
            }
            case DBUS_TYPE::INT64:
            case DBUS_TYPE::UINT64:
            case DBUS_TYPE::DOUBLE:
            {
                insertPOD(data, 8, buffer);
                break;
            }
            case DBUS_TYPE::STRING:
            case DBUS_TYPE::PATH:
            case DBUS_TYPE::SIGNATURE:
            {
                std::string const* str = reinterpret_cast<std::string const*>(data);
                if (type == DBUS_TYPE::SIGNATURE) // signature have a size of one byte only.
                {
                    uint8_t str_size = str->size();
                    insertValue(DBUS_TYPE::BYTE, &str_size, buffer);
                }
                else
                {
                    uint32_t str_size = str->size();
                    insertValue(DBUS_TYPE::UINT32, &str_size, buffer);
                }

                // Insert string + trailing null
                buffer.insert(buffer.end(), str->begin(), str->end());
                buffer.push_back('\0');
                break;
            }
            case DBUS_TYPE::VARIANT:
            {
                auto insertVariant = [this](DBUS_TYPE type, void const* data, std::vector<uint8_t>& buffer)
                {
                    Signature s{str(type)};
                    insertValue(DBUS_TYPE::SIGNATURE, &s, buffer);
                    insertValue(type, data, buffer);
                };


                DBusVariant const* v = reinterpret_cast<DBusVariant const*>(data);
                insertVariant(v->type(), v->data(), buffer);
                break;
            }
            case DBUS_TYPE::ARRAY:
            case DBUS_TYPE::UNIX_FD:
            case DBUS_TYPE::STRUCT_BEGIN:
            case DBUS_TYPE::STRUCT_END:
            case DBUS_TYPE::DICT_BEGIN:
            case DBUS_TYPE::DICT_END:
            case DBUS_TYPE::UNKNOWN:
            {
                std::abort();
            }
        }
    }


    DBusError DBusMessage::checkSignature(DBUS_TYPE type)
    {
        DBUS_TYPE next_entry_type = static_cast<DBUS_TYPE>(signature_[sign_pos_]);
        if (next_entry_type != type)
        {
            return EERROR("Wrong signature: expected '" + prettyStr(type) + "', got '" + prettyStr(next_entry_type) + "'");
        }
        sign_pos_++;

        return ESUCCESS;
    }


    DBusError DBusMessage::extractArgument(DBUS_TYPE type, void* data)
    {
        switch (type)
        {
            case DBUS_TYPE::BYTE:
            {
                uint8_t* arg = reinterpret_cast<uint8_t*>(data);
                *arg = *reinterpret_cast<uint8_t*>(body_.data() + body_pos_);
                body_pos_ += sizeof(uint8_t);
                break;
            }
            case DBUS_TYPE::BOOLEAN:
            {
                bool* arg = reinterpret_cast<bool*>(data);
                uint32_t dbus_bool;
                extractArgument(DBUS_TYPE::UINT32, &dbus_bool);
                *arg = dbus_bool;
                break;
            }
            case DBUS_TYPE::INT16:
            case DBUS_TYPE::UINT16:
            {
                align(body_pos_, sizeof(uint16_t));
                uint16_t* arg = reinterpret_cast<uint16_t*>(data);
                *arg = *reinterpret_cast<uint16_t*>(body_.data() + body_pos_);
                body_pos_ += sizeof(uint16_t);
                break;
            }
            case DBUS_TYPE::INT32:
            case DBUS_TYPE::UINT32:
            {
                align(body_pos_, sizeof(uint32_t));
                uint32_t* arg = reinterpret_cast<uint32_t*>(data);
                *arg = *reinterpret_cast<uint32_t*>(body_.data() + body_pos_);
                body_pos_ += sizeof(uint32_t);
                break;
            }
            case DBUS_TYPE::INT64:
            case DBUS_TYPE::UINT64:
            {
                align(body_pos_, sizeof(uint64_t));
                uint64_t* arg = reinterpret_cast<uint64_t*>(data);
                *arg = *reinterpret_cast<uint64_t*>(body_.data() + body_pos_);
                body_pos_ += sizeof(uint64_t);
                break;
            }
            case DBUS_TYPE::DOUBLE:
            {
                align(body_pos_, sizeof(double));
                double* arg = reinterpret_cast<double*>(data);
                *arg = *reinterpret_cast<double*>(body_.data() + body_pos_);
                body_pos_ += sizeof(double);
                break;
            }
            case DBUS_TYPE::STRING:
            case DBUS_TYPE::PATH:
            case DBUS_TYPE::SIGNATURE:
            {
                uint32_t str_size;
                if (type == DBUS_TYPE::SIGNATURE)
                {
                    uint8_t sign_size;
                    extractArgument(DBUS_TYPE::BYTE, &sign_size);
                    str_size = sign_size;
                }
                else
                {
                    extractArgument(DBUS_TYPE::UINT32, &str_size);
                }

                std::string dataBody;
                dataBody.insert(dataBody.begin(), body_.data() + body_pos_, body_.data() + body_pos_ + str_size);
                if (type == DBUS_TYPE::PATH)
                {
                    ObjectPath* arg = reinterpret_cast<ObjectPath*>(data);
                    arg->setData(std::move(dataBody));
                }
                else
                {
                    std::string* arg = reinterpret_cast<std::string*>(data);
                    *arg = std::move(dataBody);
                }

                body_pos_ += str_size + 1; // trailing nul.
                break;
            }
            case DBUS_TYPE::VARIANT:
            {
                DBusVariant* arg = static_cast<DBusVariant*>(data);
                Signature s;
                extractArgument(DBUS_TYPE::SIGNATURE, &s);

                DBUS_TYPE variant_type = static_cast<DBUS_TYPE>(s[0]);

                DBusError err;
                switch (variant_type)
                {
                    case DBUS_TYPE::BYTE:      { err = extractVariantMember<uint8_t>(*arg);      break; }
                    case DBUS_TYPE::BOOLEAN:   { err = extractVariantMember<bool>(*arg);         break; }
                    case DBUS_TYPE::INT16:     { err = extractVariantMember<int16_t>(*arg);      break; }
                    case DBUS_TYPE::UINT16:    { err = extractVariantMember<uint16_t>(*arg);     break; }
                    case DBUS_TYPE::INT32:     { err = extractVariantMember<int32_t>(*arg);      break; }
                    case DBUS_TYPE::UINT32:    { err = extractVariantMember<uint32_t>(*arg);     break; }
                    case DBUS_TYPE::INT64:     { err = extractVariantMember<int64_t>(*arg);      break; }
                    case DBUS_TYPE::UINT64:    { err = extractVariantMember<uint64_t>(*arg);     break; }
                    case DBUS_TYPE::DOUBLE:    { err = extractVariantMember<double>(*arg);       break; }
                    case DBUS_TYPE::STRING:    { err = extractVariantMember<std::string>(*arg);  break; }
                    case DBUS_TYPE::SIGNATURE: { err = extractVariantMember<Signature>(*arg);    break; }
                    case DBUS_TYPE::PATH:      { err = extractVariantMember<ObjectPath>(*arg);   break; }
                    case DBUS_TYPE::ARRAY:
                    {
                        DBusVariant array(DBUS_TYPE::ARRAY);
                        err = extractArray(s, 1, array);
                        *arg = array;
                        break;
                    }
                    default:
                    {
                        return EERROR("Variant: unsupported type '" + prettyStr(variant_type) + "'");
                    }
                }

                return err;
            }
            default:
            {
                return EERROR("Unsupported type '" + prettyStr(type) + "'");
            }
        }

        return ESUCCESS;
    }


    DBusError DBusMessage::extractArray(Signature const& s, int32_t index, DBusVariant& array)
    {
        std::vector<DBusVariant>& refArray = array.get<std::vector<DBusVariant>>();
        uint32_t array_size;
        DBusError err = extractArgument(DBUS_TYPE::UINT32, &array_size);
        if (err)
        {
            return err;
        }

        uint32_t const endPos = body_pos_ + array_size;
        DBUS_TYPE array_type = static_cast<DBUS_TYPE>(s[index]);

        while (endPos > body_pos_)
        {
            switch (array_type)
            {
                case DBUS_TYPE::BYTE:   { err = extractArrayMember<uint8_t>(refArray);    break; }
                case DBUS_TYPE::INT16:  { err = extractArrayMember<int16_t>(refArray);    break; }
                case DBUS_TYPE::INT32:  { err = extractArrayMember<int32_t>(refArray);    break; }
                case DBUS_TYPE::INT64:  { err = extractArrayMember<int64_t>(refArray);    break; }
                case DBUS_TYPE::UINT16: { err = extractArrayMember<uint16_t>(refArray);   break; }
                case DBUS_TYPE::UINT32: { err = extractArrayMember<uint32_t>(refArray);   break; }
                case DBUS_TYPE::UINT64: { err = extractArrayMember<uint64_t>(refArray);   break; }
                case DBUS_TYPE::DOUBLE: { err = extractArrayMember<double>(refArray);     break; }
                case DBUS_TYPE::STRING: { err = extractArrayMember<std::string>(refArray);break; }
                case DBUS_TYPE::PATH:   { err = extractArrayMember<ObjectPath>(refArray); break; }
                case DBUS_TYPE::ARRAY:
                {
                    DBusVariant nextArray(DBUS_TYPE::ARRAY);
                    err = extractArray(s, index + 1, nextArray);
                    refArray.push_back(nextArray);
                    break;
                }
                default:
                {
                    printf("Warning: skip array of type '%s' at %u (unsupported)\n", str(array_type).c_str(), body_pos_);
                    if (DBUS_TYPE::STRUCT_BEGIN == array_type)
                    {
                        align(body_pos_, 8);
                    }
                    if (DBUS_TYPE::DICT_BEGIN == array_type)
                    {
                        align(body_pos_, 8);
                    }

                    body_pos_ += array_size;
                    break;
                }
            }
        }

        return err;
    }
}
